//===-- jitall.cs - Force-JIT every method of an assembly -------*- C# -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// jitall loads an assembly and asks the runtime to JIT-compile every method
// that has an IL body, including the global methods of <Module>, without
// calling any of them. The JIT rejects malformed bodies that ilasm accepts
// (.maxstack too small, stack underflow, a value left on the stack at ret or
// at localloc, falling off the end of a method), so this catches bad IL before
// a test program runs (llvm/docs/MSIL/recon/cil-runtime.md, experiment e15).
//
// Preparing a method of a type whose static constructor has not run makes the
// runtime run it first, so <Module>::.cctor (the module initializer: data
// segment and C constructors) does run under jitall, and so do the target's
// imports it calls. For a non-host target jitall must therefore run where those
// imports resolve (msil-jitall.py --runner wsl for a Linux triple on Windows).
// A failing initializer makes every method of <Module> fail with a
// TypeInitializationException, which hides the methods' own JIT errors. The
// entry point never runs.
//
// Usage:
//   dotnet exec --runtimeconfig jitall.runtimeconfig.json jitall.dll
//       <assembly> [<method name substring>]
//
// Output: 'JIT FAIL <type>::<method>: <exception>' for every method the JIT
// rejects, then 'jitall: N ok, M failed'. The exit code is M, capped at 255 so
// that a large count never wraps to 0 on Linux. A usage error or an assembly
// that cannot be loaded prints 'jitall: error: ...' and exits with 255 without
// a summary line.
//
// Methods without an IL body (pinvokeimpl, abstract, runtime-implemented) and
// static constructors (the runtime compiles and runs those itself when it
// initializes their type) are skipped.
//
// Bad IL can make the JIT itself run away (the .NET 10 JIT grew without bound
// on a join with different stack depths), so tests run jitall through
// msil/utils/msil-jitall.py, which applies a timeout and a memory cap.
// msil/utils/jitall/build-jitall.py compiles this file offline.
//
//===----------------------------------------------------------------------===//

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;

static class JitAll
{
    const int ToolError = 255;

    const BindingFlags AllDeclared = BindingFlags.Public | BindingFlags.NonPublic |
                                     BindingFlags.Static | BindingFlags.Instance |
                                     BindingFlags.DeclaredOnly;

    static int Main(string[] args)
    {
        // UTF-8 and LF whatever the console code page is: method names are
        // C identifiers and may be non-ASCII.
        Stream stdoutStream = Console.OpenStandardOutput();
        var stdout = new StreamWriter(stdoutStream, new UTF8Encoding(false));
        stdout.NewLine = "\n";
        stdout.AutoFlush = true;
        Console.SetOut(stdout);
        PrepareToReport(stdoutStream);

        if (args.Length < 1 || args.Length > 2)
        {
            Console.WriteLine("jitall: error: usage: jitall <assembly> [<method name substring>]");
            return ToolError;
        }
        string filter = args.Length > 1 ? args[1] : null;

        Assembly asm;
        try
        {
            asm = Load(Path.GetFullPath(args[0]));
        }
        catch (Exception e)
        {
            Console.WriteLine($"jitall: error: cannot load {args[0]}: {Describe(e)}");
            return ToolError;
        }

        try
        {
            return JitAssembly(asm, filter);
        }
        catch (Exception e)
        {
            Console.WriteLine($"jitall: error: {Describe(e)}");
            return ToolError;
        }
    }

    // A module initializer that exhausts the memory cap (RLIMIT_DATA on Linux)
    // leaves no memory for what reporting it needs the first time: console
    // set-up, the resource strings (and ICU) behind the messages of
    // TypeInitializationException and OutOfMemoryException, and the JIT for
    // Describe and Name. The runtime then fails fast without a word, so the
    // cap shows up as a crash instead of the OutOfMemoryException that
    // msil-jitall.py looks for. Do all of that once before the target runs.
    static void PrepareToReport(Stream stdoutStream)
    {
        stdoutStream.Write(ReadOnlySpan<byte>.Empty);
        var e = new TypeInitializationException("<Module>", new OutOfMemoryException());
        GC.KeepAlive($"JIT FAIL {Name(MethodBase.GetCurrentMethod())}: {Describe(e)}");
    }

    static int JitAssembly(Assembly asm, string filter)
    {
        int ok = 0, bad = 0;
        foreach (Module mod in asm.GetModules())
        {
            var methods = new List<MethodBase>();
            try
            {
                methods.AddRange(mod.GetMethods(AllDeclared)); // <Module> globals
            }
            catch (Exception e)
            {
                bad++;
                Console.WriteLine($"JIT FAIL <Module>: {Describe(e)}");
            }
            foreach (Type t in GetTypes(mod, ref bad))
            {
                try
                {
                    methods.AddRange(t.GetMethods(AllDeclared));
                    methods.AddRange(t.GetConstructors(AllDeclared));
                }
                catch (Exception e)
                {
                    bad++;
                    Console.WriteLine($"JIT FAIL {t.FullName}: {Describe(e)}");
                }
            }

            foreach (MethodBase m in methods)
            {
                if (filter != null && !m.Name.Contains(filter))
                    continue;
                if (m.IsConstructor && m.IsStatic)
                    continue; // the runtime JITs and runs it with its type
                try
                {
                    if (!HasILBody(m))
                        continue;
                    if (m.ContainsGenericParameters)
                    {
                        Console.WriteLine($"jitall: skipped {Name(m)} (open generic)");
                        continue;
                    }
                    RuntimeHelpers.PrepareMethod(m.MethodHandle);
                    ok++;
                }
                catch (Exception e)
                {
                    bad++;
                    Console.WriteLine($"JIT FAIL {Name(m)}: {Describe(e)}");
                }
            }
        }

        Console.WriteLine($"jitall: {ok} ok, {bad} failed");
        return Math.Min(bad, 255);
    }

    // Assembly.LoadFrom, unless an assembly with the same simple name is
    // already loaded (jitall itself, or a framework assembly): LoadFrom would
    // then fail or hand back that other assembly, so the file is loaded into a
    // context of its own instead.
    static Assembly Load(string path)
    {
        Assembly asm = null;
        try
        {
            asm = Assembly.LoadFrom(path);
        }
        catch (FileLoadException)
        {
        }
        if (asm != null && SamePath(asm.Location, path))
            return asm;

        asm = new TargetLoadContext(path).LoadFromAssemblyPath(path);
        if (!SamePath(asm.Location, path))
            throw new FileLoadException($"the runtime loaded {asm.Location} instead");
        return asm;
    }

    static bool SamePath(string a, string b)
    {
        if (string.IsNullOrEmpty(a))
            return false;
        return string.Equals(Path.GetFullPath(a), Path.GetFullPath(b),
                             OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase
                                                         : StringComparison.Ordinal);
    }

    // Module.GetTypes throws if any type fails to load; report those and keep
    // the types that did load.
    static IEnumerable<Type> GetTypes(Module mod, ref int bad)
    {
        try
        {
            return mod.GetTypes();
        }
        catch (ReflectionTypeLoadException e)
        {
            foreach (Exception le in e.LoaderExceptions)
            {
                if (le == null)
                    continue;
                bad++;
                Console.WriteLine($"JIT FAIL {mod.Name}: {Describe(le)}");
            }
            var loaded = new List<Type>();
            foreach (Type t in e.Types)
                if (t != null)
                    loaded.Add(t);
            return loaded;
        }
    }

    static bool HasILBody(MethodBase m)
    {
        if ((m.Attributes & (MethodAttributes.Abstract | MethodAttributes.PinvokeImpl)) != 0)
            return false;
        MethodImplAttributes impl = m.MethodImplementationFlags;
        if ((impl & MethodImplAttributes.CodeTypeMask) != MethodImplAttributes.IL ||
            (impl & MethodImplAttributes.InternalCall) != 0)
            return false;
        return m.GetMethodBody() != null;
    }

    static string Name(MethodBase m) => $"{m.DeclaringType?.FullName ?? "<Module>"}::{m.Name}";

    // One line: exception type and message, then the inner exceptions.
    static string Describe(Exception e)
    {
        var sb = new StringBuilder();
        for (Exception x = e; x != null; x = x.InnerException)
        {
            if (x != e)
                sb.Append(" ---> ");
            sb.Append(x.GetType().Name).Append(": ").Append(x.Message);
        }
        return sb.ToString().Replace("\r", " ").Replace("\n", " ");
    }

    sealed class TargetLoadContext : AssemblyLoadContext
    {
        readonly string dir;

        public TargetLoadContext(string path) : base("jitall-target")
        {
            dir = Path.GetDirectoryName(path);
        }

        // References to assemblies next to the target resolve there, as with
        // LoadFrom; everything else (mscorlib) falls back to the default context.
        protected override Assembly Load(AssemblyName name)
        {
            string candidate = Path.Combine(dir, name.Name + ".dll");
            return File.Exists(candidate) ? LoadFromAssemblyPath(candidate) : null;
        }
    }
}
