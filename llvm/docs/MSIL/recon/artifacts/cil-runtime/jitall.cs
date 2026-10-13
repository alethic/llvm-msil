// jitall: force-JIT every method body of an assembly (incl. <Module> globals) without running it.
// Reports InvalidProgramException etc. per method. Exit code = number of failing methods.
// Optional 2nd arg: only methods whose name contains this substring.
using System;
using System.Reflection;
using System.Runtime.CompilerServices;
static class JitAll
{
    static int Main(string[] args)
    {
        var asm = Assembly.LoadFrom(args[0]);
        int bad = 0, ok = 0;
        const BindingFlags all = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static |
                                 BindingFlags.Instance | BindingFlags.DeclaredOnly;
        foreach (var mod in asm.GetModules())
        {
            var methods = new System.Collections.Generic.List<MethodBase>(mod.GetMethods(all)); // globals
            foreach (var t in mod.GetTypes())
            {
                methods.AddRange(t.GetMethods(all));
                methods.AddRange(t.GetConstructors(all));
            }
            foreach (var m in methods)
            {
                if (m.GetMethodBody() == null) continue;            // pinvoke / abstract
                if (m.IsConstructor && m.IsStatic) continue;          // don't run .cctors twice
                if (args.Length > 1 && !m.Name.Contains(args[1])) continue;
                try { RuntimeHelpers.PrepareMethod(m.MethodHandle); ok++; }
                catch (Exception e)
                {
                    bad++;
                    Console.WriteLine($"JIT FAIL {m.DeclaringType?.FullName ?? "<Module>"}::{m.Name}: {e.GetType().Name}: {e.Message}");
                }
            }
        }
        Console.WriteLine($"jitall: {ok} ok, {bad} failed");
        return bad;
    }
}
