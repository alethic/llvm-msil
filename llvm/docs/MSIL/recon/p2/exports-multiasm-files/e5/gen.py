"""E5: global C library state shared between two assemblies that each P/Invoke a libc.

gen.py <libcrt> <progcrt> <outdir>
  libcrt/progcrt: ucrtbase | msvcrt | libc   (the P/Invoke library of each assembly)
The library exports:
  lib_alloc()          malloc(16) and strcpy "from-lib"
  lib_fopen(path)      fopen(path, "w")
  lib_fclose(FILE*)    fclose
  lib_set_errno()      strtol("99999999999999999999", 0, 10) -> ERANGE
  lib_errno()          *errno location
The program: reads the lib's string, frees it with its own free(), writes to the lib's
FILE* with its own fputs, reads errno after the lib set it, sets errno and asks the lib.
Each step is selected by argv[1] so that a crash in one step does not hide the others.
"""
import os, sys

libcrt, progcrt, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
H = ".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }\n"


def errno_fn(crt):
    return "__errno_location" if crt == "libc" else "_errno"


def imports(crt, prefix):
    e = errno_fn(crt)
    return f"""
.method public static pinvokeimpl("{crt}" as "malloc" cdecl) native int '{prefix}malloc'(native int) cil managed preservesig {{}}
.method public static pinvokeimpl("{crt}" as "free" cdecl) void '{prefix}free'(native int) cil managed preservesig {{}}
.method public static pinvokeimpl("{crt}" as "strcpy" cdecl) native int '{prefix}strcpy'(native int, native int) cil managed preservesig {{}}
.method public static pinvokeimpl("{crt}" as "fopen" cdecl) native int '{prefix}fopen'(native int, native int) cil managed preservesig {{}}
.method public static pinvokeimpl("{crt}" as "fclose" cdecl) int32 '{prefix}fclose'(native int) cil managed preservesig {{}}
.method public static pinvokeimpl("{crt}" as "fputs" cdecl) int32 '{prefix}fputs'(native int, native int) cil managed preservesig {{}}
.method public static pinvokeimpl("{crt}" as "strtol" cdecl) int32 '{prefix}strtol'(native int, native int, int32) cil managed preservesig {{}}
.method public static pinvokeimpl("{crt}" as "{e}" cdecl) native int '{prefix}errno_loc'() cil managed preservesig {{}}
"""


def cstr(s):  # an unmanaged copy of a string literal (never freed)
    return f'ldstr "{s}" call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)'


lib = H + ".assembly 'crtlib' { .ver 1:0:0:0 }\n" + imports(libcrt, "c_") + f"""
.class public abstract sealed auto ansi 'E5.Native' extends [mscorlib]System.Object
{{
  .method public static native int 'lib_alloc'() cil managed
  {{ .maxstack 2 .locals (native int) ldc.i4.s 16 conv.i call native int 'c_malloc'(native int) stloc.0
     ldloc.0 {cstr("from-lib")} call native int 'c_strcpy'(native int, native int) pop ldloc.0 ret }}
  .method public static native int 'lib_fopen'(native int) cil managed
  {{ .maxstack 2 ldarg.0 {cstr("w")} call native int 'c_fopen'(native int, native int) ret }}
  .method public static int32 'lib_fclose'(native int) cil managed {{ .maxstack 1 ldarg.0 call int32 'c_fclose'(native int) ret }}
  .method public static void 'lib_set_errno'() cil managed
  {{ .maxstack 3 call native int 'c_errno_loc'() ldc.i4.0 stind.i4 {cstr("99999999999999999999")} ldc.i4.0 conv.i ldc.i4.s 10 call int32 'c_strtol'(native int, native int, int32) pop ret }}
  .method public static int32 'lib_errno'() cil managed {{ .maxstack 1 call native int 'c_errno_loc'() ldind.i4 ret }}
}}
"""
prog = H + ".assembly extern 'crtlib' { .ver 1:0:0:0 }\n.assembly 'crtprog' { .ver 0:0:0:0 }\n" + imports(progcrt, "p_") + f"""
.method public static void sayi(string, int32) cil managed
{{ .maxstack 2 ldarg.0 ldarg.1 box [mscorlib]System.Int32 call string [mscorlib]System.String::Concat(object, object) call void [mscorlib]System.Console::WriteLine(string) ret }}
.method public static void heap() cil managed noinlining
{{ .maxstack 2 .locals (native int)
  call native int [crtlib]'E5.Native'::'lib_alloc'() stloc.0
  ldstr "  heap: string from the library's malloc: " ldloc.0 call string [mscorlib]System.Runtime.InteropServices.Marshal::PtrToStringAnsi(native int)
  call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
  ldloc.0 call void 'p_free'(native int)
  ldstr "  heap: freed by the program's free(): no crash" call void [mscorlib]System.Console::WriteLine(string) ret }}
.method public static void stdio() cil managed noinlining
{{ .maxstack 3 .locals (native int)
  {cstr("e5-out.txt")} call native int [crtlib]'E5.Native'::'lib_fopen'(native int) stloc.0
  ldstr "  stdio: fputs(FILE* from the library's fopen) = " {cstr("written by the program")} ldloc.0 call int32 'p_fputs'(native int, native int) call void sayi(string, int32)
  ldstr "  stdio: fclose by the library = " ldloc.0 call int32 [crtlib]'E5.Native'::'lib_fclose'(native int) call void sayi(string, int32)
  ldstr "  stdio: file contains: " ldstr "e5-out.txt" call string [mscorlib]System.IO.File::ReadAllText(string)
  call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string) ret }}
.method public static void errno() cil managed noinlining
{{ .maxstack 2 .locals (int32, int32, int32)
  call void [crtlib]'E5.Native'::'lib_set_errno'()
  call native int 'p_errno_loc'() ldind.i4 stloc.0
  call int32 [crtlib]'E5.Native'::'lib_errno'() stloc.1
  call native int 'p_errno_loc'() ldc.i4.7 stind.i4
  call int32 [crtlib]'E5.Native'::'lib_errno'() stloc.2
  ldstr "  errno: library set ERANGE (34); the program reads " ldloc.0 call void sayi(string, int32)
  ldstr "  errno: then the library reads " ldloc.1 call void sayi(string, int32)
  ldstr "  errno: the program stored 7; the library reads " ldloc.2 call void sayi(string, int32)
  ldstr "  errno: after Console output the program reads " call native int 'p_errno_loc'() ldind.i4 call void sayi(string, int32) ret }}
.method public static int32 main(string[] args) cil managed
{{ .entrypoint .maxstack 2
  ldarg.0 ldc.i4.0 ldelem.ref ldstr "errno-warm" call bool [mscorlib]System.String::op_Equality(string, string) brfalse N0
  call native int 'p_errno_loc'() pop call void errno() br D
N0:
  ldarg.0 ldc.i4.0 ldelem.ref ldstr "heap" call bool [mscorlib]System.String::op_Equality(string, string) brfalse N1 call void heap() br D
N1: ldarg.0 ldc.i4.0 ldelem.ref ldstr "stdio" call bool [mscorlib]System.String::op_Equality(string, string) brfalse N2 call void stdio() br D
N2: call void errno()
D: ldc.i4.0 ret }}
"""
open(os.path.join(out, "crtlib.il"), "w").write(lib)
open(os.path.join(out, "crtprog.il"), "w").write(prog)
