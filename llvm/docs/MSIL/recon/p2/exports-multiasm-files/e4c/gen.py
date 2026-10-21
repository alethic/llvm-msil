# E4c: libraries that the program never calls (constructor side effects only), and
# the order in which the program's module initializer initializes them when it
# references each library's export class with one 'ldtoken'.
HDR = '.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }\n'
def lib(n, deps=()):
    s = HDR + "".join(f".assembly extern '{d}' {{ .ver 1:0:0:0 }}\n" for d in deps)
    s += f".assembly '{n}' {{ .ver 1:0:0:0 }}\n"
    s += f".class public abstract sealed auto ansi '{n.upper()}.Native' extends [mscorlib]System.Object {{ .method public static void 'f'() cil managed {{ .maxstack 1 ret }} }}\n"
    touches = "".join(f"  ldtoken [{d}]{d.upper()}.Native\n  pop\n" for d in deps)
    s += f""".method private hidebysig specialname rtspecialname static void .cctor() cil managed
{{ .maxstack 1
{touches}  ldstr "  {n}: constructors run" call void [mscorlib]System.Console::WriteLine(string)
  ret }}
"""
    return s
open('a.il','w').write(lib('a', ['c']))   # a links c
open('b.il','w').write(lib('b'))
open('c.il','w').write(lib('c'))
def prog(order, touch):
    s = HDR + "".join(f".assembly extern '{d}' {{ .ver 1:0:0:0 }}\n" for d in 'abc') + ".assembly 'prog' { .ver 0:0:0:0 }\n"
    t = "".join(f"  ldtoken [{d}]{d.upper()}.Native\n  pop\n" for d in order) if touch else ""
    s += f""".method private hidebysig specialname rtspecialname static void .cctor() cil managed
{{ .maxstack 1
  ldstr "  prog: module initializer begins" call void [mscorlib]System.Console::WriteLine(string)
{t}  ldstr "  prog: module initializer ends" call void [mscorlib]System.Console::WriteLine(string)
  ret }}
.method public static int32 main() cil managed {{ .entrypoint .maxstack 1
  ldstr "  main entered" call void [mscorlib]System.Console::WriteLine(string)
  ldc.i4.0 ret }}
"""
    return s
open('prog_none.il','w').write(prog('', False))
open('prog_ab.il','w').write(prog('ab', True))
open('prog_ba.il','w').write(prog('ba', True))
