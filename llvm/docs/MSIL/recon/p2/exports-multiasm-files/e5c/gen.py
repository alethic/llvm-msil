"""E5c: destructor/atexit order across assemblies.

v1 'separate': each assembly subscribes its own AppDomain.ProcessExit handler in its
   module initializer (the library first, because the program's initializer
   references it).
v2 'shared': each assembly pushes its finalizer onto one process-wide LIFO stack kept
   in an AppDomain data slot; the first assembly to create the slot subscribes the
   one ProcessExit handler that pops and runs them. Creation is serialized with a lock
   on an interned string literal (the same object in every assembly).
Native order for a program P that depends on library L: P's destructors, then L's.
"""
import os, sys
H = ".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }\n"
SAY = "call void [mscorlib]System.Console::WriteLine(string)"

def register(v, me):
    if v == "separate":
        return f"""  call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain()
  ldnull ldftn void '<msil>fini_handler'(object, class [mscorlib]System.EventArgs)
  newobj instance void [mscorlib]System.EventHandler::.ctor(object, native int)
  callvirt instance void [mscorlib]System.AppDomain::add_ProcessExit(class [mscorlib]System.EventHandler)
"""
    return """  ldnull ldftn void '<msil>fini'()
  newobj instance void [mscorlib]System.Action::.ctor(object, native int)
  call void '<msil>atexit_push'(class [mscorlib]System.Action)
"""

COMMON = f""".method assembly static void '<msil>fini_handler'(object, class [mscorlib]System.EventArgs) cil managed
{{ .maxstack 1 call void '<msil>fini'() ret }}
// the shared, process-wide LIFO of finalizers (variant 'shared')
.method assembly static void '<msil>atexit_push'(class [mscorlib]System.Action) cil managed
{{
  .maxstack 3
  .locals (object, bool, class [mscorlib]System.Collections.Stack)
  ldstr "msil.atexit" stloc.0 ldc.i4.0 stloc.1
  .try {{
    ldloc.0 ldloca.s 1 call void [mscorlib]System.Threading.Monitor::Enter(object, bool&)
    call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain() ldstr "msil.atexit"
    callvirt instance object [mscorlib]System.AppDomain::GetData(string) castclass [mscorlib]System.Collections.Stack stloc.2
    ldloc.2 brtrue HAVE
    newobj instance void [mscorlib]System.Collections.Stack::.ctor() stloc.2
    call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain() ldstr "msil.atexit" ldloc.2
    callvirt instance void [mscorlib]System.AppDomain::SetData(string, object)
    call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain()
    ldnull ldftn void '<msil>atexit_run'(object, class [mscorlib]System.EventArgs)
    newobj instance void [mscorlib]System.EventHandler::.ctor(object, native int)
    callvirt instance void [mscorlib]System.AppDomain::add_ProcessExit(class [mscorlib]System.EventHandler)
  HAVE:
    ldloc.2 ldarg.0 callvirt instance void [mscorlib]System.Collections.Stack::Push(object)
    leave DONE
  }} finally {{
    ldloc.1 brfalse NOLOCK ldloc.0 call void [mscorlib]System.Threading.Monitor::Exit(object)
  NOLOCK: endfinally
  }}
DONE: ret
}}
.method assembly static void '<msil>atexit_run'(object, class [mscorlib]System.EventArgs) cil managed
{{
  .maxstack 2
  .locals (class [mscorlib]System.Collections.Stack)
  call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain() ldstr "msil.atexit"
  callvirt instance object [mscorlib]System.AppDomain::GetData(string) castclass [mscorlib]System.Collections.Stack stloc.0
LOOP: ldloc.0 callvirt instance int32 [mscorlib]System.Collections.Stack::get_Count() brfalse END
  ldloc.0 callvirt instance object [mscorlib]System.Collections.Stack::Pop() castclass [mscorlib]System.Action
  callvirt instance void [mscorlib]System.Action::Invoke() br LOOP
END: ret
}}
"""

for v in ("separate", "shared"):
    os.makedirs(v, exist_ok=True)
    lib = H + ".assembly 'dlib' { .ver 1:0:0:0 }\n" + COMMON + f"""
.class public abstract sealed auto ansi 'D.Native' extends [mscorlib]System.Object
{{ .method public static void 'f'() cil managed {{ .maxstack 1 ret }} }}
.method assembly static void '<msil>fini'() cil managed {{ .maxstack 1 ldstr "  dlib: destructors" {SAY} ret }}
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{{ .maxstack 4
  ldstr "  dlib: constructors" {SAY}
{register(v, 'dlib')}  ret }}
"""
    prog = H + ".assembly extern 'dlib' { .ver 1:0:0:0 }\n.assembly 'dprog' { .ver 0:0:0:0 }\n" + COMMON + f"""
.method assembly static void '<msil>fini'() cil managed {{ .maxstack 1 ldstr "  dprog: destructors" {SAY} ret }}
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{{ .maxstack 4
  ldtoken [dlib]'D.Native' pop
  ldstr "  dprog: constructors" {SAY}
{register(v, 'dprog')}  ret }}
.method public static int32 main() cil managed {{ .entrypoint .maxstack 1 ldstr "  main" {SAY} ldc.i4.0 ret }}
"""
    open(f"{v}/dlib.il", "w").write(lib)
    open(f"{v}/dprog.il", "w").write(prog)
