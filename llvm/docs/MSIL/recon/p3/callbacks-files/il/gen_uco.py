# gen_uco.py: writes uco.il (binds UnmanagedCallersOnlyAttribute from System.Runtime.InteropServices 8.0),
# uconame.il (a private attribute class of the same full name, mscorlib only) and ucomscorlib.il
# ([mscorlib]System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute through the facade).
import os
HERE = os.path.dirname(os.path.abspath(__file__))
UCO_BLOB = '01 00 00 00'
CDECL_BLOB = open(os.path.join(HERE, 'uco-blob.txt')).read().splitlines()[1].strip()


def ucoattr(asm, blob):
    scope = f'[{asm}]' if asm else ''
    return f'.custom instance void {scope}System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() = ( {blob} )'


def gen(name, header, attr_asm, extra_types='', cdecl_ok=False):
    A = lambda: ucoattr(attr_asm, UCO_BLOB)
    AC = ucoattr(attr_asm, CDECL_BLOB) if cdecl_ok else A()
    il = f'''// {name}.il: [UnmanagedCallersOnly] thunks (modern .NET only).
.assembly extern mscorlib {{ .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }}
.assembly extern System {{ .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }}
{header}
.assembly {name} {{ .ver 0:0:0:0 }}
.module {name}.exe
.corflags 0x00000001
{extra_types}
.class private abstract auto ansi sealed beforefieldinit Prog extends [mscorlib]System.Object
{{
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 call_cb(native int f, int32 x) cil managed preservesig {{}}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 call_cb_twice(native int f, int32 x) cil managed preservesig {{}}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int64 loop_cb(native int f, int32 n) cil managed preservesig {{}}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) native int get_native_fn() cil managed preservesig {{}}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) void do_qsort(native int b, native int n, native int sz, native int cmp) cil managed preservesig {{}}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 run_thread(native int s, native int a, native int r) cil managed preservesig {{}}
  .field static int32 tsv
  .custom instance void [mscorlib]System.ThreadStaticAttribute::.ctor() = ( 01 00 00 00 )
  .field static native int fp_early

  // The C functions (ordinary managed methods; direct calls and the managed fast path use these).
  .method static int32 add1(int32 x) cil managed {{ .maxstack 2 ldarg.0 ldc.i4.1 add ret }}
  .method static int32 add1_throw(int32 x) cil managed
  {{
    .maxstack 2
    ldarg.0 ldc.i4.5 blt.s OK
    ldstr "thrown in callback" newobj instance void [mscorlib]System.InvalidOperationException::.ctor(string) throw
OK: ldarg.0 ldc.i4.1 add ret
  }}
  .method static int32 cmp_int(native int a, native int b) cil managed
  {{ .maxstack 3 ldarg.0 ldind.i4 ldarg.1 ldind.i4 cgt ldarg.0 ldind.i4 ldarg.1 ldind.i4 clt sub ret }}
  .method static native int thread_start(native int arg) cil managed
  {{
    .maxstack 4
    ldstr "  in thread: tsv before={{0}} managedId={{1}} isBackground={{2}}"
    ldsfld int32 Prog::tsv box [mscorlib]System.Int32
    call class [mscorlib]System.Threading.Thread [mscorlib]System.Threading.Thread::get_CurrentThread()
    callvirt instance int32 [mscorlib]System.Threading.Thread::get_ManagedThreadId() box [mscorlib]System.Int32
    call class [mscorlib]System.Threading.Thread [mscorlib]System.Threading.Thread::get_CurrentThread()
    callvirt instance bool [mscorlib]System.Threading.Thread::get_IsBackground() box [mscorlib]System.Boolean
    call void [mscorlib]System.Console::WriteLine(string, object, object, object)
    ldc.i4 77 stsfld int32 Prog::tsv
    ldarg.0 ldc.i4.1 add ret
  }}

  // The native-callable thunks the switch would emit: one per address-taken function.
  .method assembly static int32 '<msil>uco_add1'(int32 x) cil managed
  {{ {A()}
     .maxstack 1 ldarg.0 call int32 Prog::add1(int32) ret }}
  .method assembly static int32 '<msil>uco_add1_cdecl'(int32 x) cil managed
  {{ {AC}
     .maxstack 1 ldarg.0 call int32 Prog::add1(int32) ret }}
  .method assembly static int32 '<msil>uco_add1_cold'(int32 x) cil managed
  {{ {A()}
     .maxstack 1 ldarg.0 call int32 Prog::add1(int32) ret }}
  .method assembly static int32 '<msil>uco_add1_throw'(int32 x) cil managed
  {{ {A()}
     .maxstack 1 ldarg.0 call int32 Prog::add1_throw(int32) ret }}
  .method assembly static int32 '<msil>uco_cmp_int'(native int a, native int b) cil managed
  {{ {A()}
     .maxstack 2 ldarg.0 ldarg.1 call int32 Prog::cmp_int(native int, native int) ret }}
  .method assembly static native int '<msil>uco_thread_start'(native int a) cil managed
  {{ {A()}
     .maxstack 1 ldarg.0 call native int Prog::thread_start(native int) ret }}

  .method static bool is(string a, string b) cil managed {{ .maxstack 2 ldarg.0 ldarg.1 call bool [mscorlib]System.String::op_Equality(string, string) ret }}
  .method static void say(int64 v, string s) cil managed
  {{
    .maxstack 3
    ldarg.1 ldstr ": " ldarg.0 box [mscorlib]System.Int64 call string [mscorlib]System.String::Concat(object, object, object)
    call void [mscorlib]System.Console::WriteLine(string) ret
  }}
  .method static native int late_ldftn() cil managed noinlining
  {{ .maxstack 1 ldftn int32 Prog::'<msil>uco_add1'(int32) ret }}
  .method static int32 Main(string[] args) cil managed
  {{
    .entrypoint
    .maxstack 8
    .locals (string t, native int p, int64 s, int32 n, int32 i, native int q, native int mem)
    ldarg.0 ldc.i4.0 ldelem.ref stloc.0
    ldc.i4 1000000 stloc.3
    ldarg.0 ldlen conv.i4 ldc.i4.2 blt.s NODEF
    ldarg.0 ldc.i4.1 ldelem.ref call int32 [mscorlib]System.Int32::Parse(string) stloc.3
NODEF:
    ldstr "runtime" call class [mscorlib]System.Version [mscorlib]System.Environment::get_Version() callvirt instance string [mscorlib]System.Object::ToString()
    call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
    ldloc.0 ldstr "uco" call bool Prog::is(string, string) brtrue T_UCO
    ldloc.0 ldstr "uco-cdecl" call bool Prog::is(string, string) brtrue T_UCO_CDECL
    ldloc.0 ldstr "uco-cold" call bool Prog::is(string, string) brtrue T_UCO_COLD
    ldloc.0 ldstr "uco-identity" call bool Prog::is(string, string) brtrue T_UCO_ID
    ldloc.0 ldstr "uco-direct" call bool Prog::is(string, string) brtrue T_UCO_DIRECT
    ldloc.0 ldstr "uco-managed-calli" call bool Prog::is(string, string) brtrue T_UCO_MCALLI
    ldloc.0 ldstr "uco-calli" call bool Prog::is(string, string) brtrue T_UCO_CALLI
    ldloc.0 ldstr "uco-thread" call bool Prog::is(string, string) brtrue T_UCO_THREAD
    ldloc.0 ldstr "uco-throw" call bool Prog::is(string, string) brtrue T_UCO_THROW
    ldloc.0 ldstr "uco-qsort" call bool Prog::is(string, string) brtrue T_UCO_QSORT
    ldstr "unknown test" call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4 99 ret
T_UCO:
    ldftn int32 Prog::'<msil>uco_add1'(int32) ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of UCO thunk" call void Prog::say(int64, string)
    ldftn int32 Prog::'<msil>uco_add1'(int32) ldloc.3 call int64 Prog::loop_cb(native int, int32)
    ldstr "native loop over UCO thunk" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_UCO_CDECL:
    ldftn int32 Prog::'<msil>uco_add1_cdecl'(int32) ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of UCO thunk with CallConvs=Cdecl" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_UCO_COLD:
    ldftn int32 Prog::'<msil>uco_add1_cold'(int32) ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of never-run UCO thunk" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_UCO_ID:
    ldftn int32 Prog::'<msil>uco_add1'(int32) stsfld native int Prog::fp_early
    ldsfld native int Prog::fp_early ldloc.3 call int64 Prog::loop_cb(native int, int32) pop
    call void [mscorlib]System.GC::Collect()
    ldc.i4 200 call void [mscorlib]System.Threading.Thread::Sleep(int32)
    ldsfld native int Prog::fp_early ldloc.3 call int64 Prog::loop_cb(native int, int32) pop
    call native int Prog::late_ldftn() ldsfld native int Prog::fp_early ceq conv.i8
    ldstr "ldftn of UCO thunk before and after 2n native calls and a sleep equal" call void Prog::say(int64, string)
    ldsfld native int Prog::fp_early ldftn int32 Prog::add1(int32) ceq conv.i8
    ldstr "UCO thunk equals ldftn of the C function" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_UCO_DIRECT:
    ldc.i4 41 call int32 Prog::'<msil>uco_add1'(int32) conv.i8
    ldstr "direct managed call of UCO thunk" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_UCO_MCALLI:
    ldc.i4 41 ldftn int32 Prog::'<msil>uco_add1'(int32) calli int32(int32) conv.i8
    ldstr "managed calli of UCO pointer" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_UCO_CALLI:
    ldc.i4 41 ldftn int32 Prog::'<msil>uco_add1'(int32) calli unmanaged cdecl int32(int32) conv.i8
    ldstr "calli unmanaged cdecl of UCO pointer" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_UCO_THREAD:
    ldc.i4 5 stsfld int32 Prog::tsv
    ldftn native int Prog::'<msil>uco_thread_start'(native int) ldc.i4 41 conv.i ldloca.s q conv.u call int32 Prog::run_thread(native int, native int, native int) conv.i8
    ldstr "run_thread status" call void Prog::say(int64, string)
    ldloc.s q conv.i8 ldstr "native thread on UCO start routine returned" call void Prog::say(int64, string)
    ldftn native int Prog::'<msil>uco_thread_start'(native int) ldc.i4 50 conv.i ldloca.s q conv.u call int32 Prog::run_thread(native int, native int, native int) pop
    ldloc.s q conv.i8 ldstr "second native thread returned" call void Prog::say(int64, string)
    ldsfld int32 Prog::tsv conv.i8 ldstr "main thread tsv after" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_UCO_THROW:
    .try {{
      ldftn int32 Prog::'<msil>uco_add1_throw'(int32) ldc.i4 41 call int32 Prog::call_cb_twice(native int, int32) conv.i8
      ldstr "no exception?" call void Prog::say(int64, string)
      leave.s THDONE
    }} catch [mscorlib]System.InvalidOperationException {{
      callvirt instance string [mscorlib]System.Exception::get_Message() ldstr "caught across native frames: " call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
      leave.s THDONE
    }}
THDONE:
    ldc.i4.0 ret
T_UCO_QSORT:
    ldc.i4.s 40 call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(int32) stloc.s mem
    ldc.i4.0 stloc.s i
QF: ldloc.s mem ldloc.s i ldc.i4.4 mul add ldc.i4.s 10 ldloc.s i sub stind.i4
    ldloc.s i ldc.i4.1 add dup stloc.s i ldc.i4.s 10 blt.s QF
    ldloc.s mem ldc.i4.s 10 conv.i ldc.i4.4 conv.i ldftn int32 Prog::'<msil>uco_cmp_int'(native int, native int) call void Prog::do_qsort(native int, native int, native int, native int)
    ldc.i4.0 stloc.s i ldc.i4.0 conv.i8 stloc.2
QP: ldloc.2 ldc.i4.s 10 conv.i8 mul ldloc.s mem ldloc.s i ldc.i4.4 mul add ldind.i4 conv.i8 add stloc.2
    ldloc.s i ldc.i4.1 add dup stloc.s i ldc.i4.s 10 blt.s QP
    ldloc.2 ldstr "qsort result digits" call void Prog::say(int64, string)
    ldc.i4.0 ret
  }}
}}
'''
    with open(os.path.join(HERE, f'{name}.il'), 'w') as f:
        f.write(il)


gen('uco', '.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }',
    'System.Runtime.InteropServices', cdecl_ok=True)
gen('uconame', '', '', '''.class private auto ansi sealed beforefieldinit System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute extends [mscorlib]System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Attribute::.ctor() ret }
}''')
gen('ucomscorlib', '', 'mscorlib')
