// reg.il: a process-wide slot shared by every assembly without a shared type: AppDomain data.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly regt { .ver 0:0:0:0 }
.module regt.exe
.corflags 0x00000001
.class private abstract auto ansi sealed Prog extends [mscorlib]System.Object
{
  .method static int32 Main() cil managed
  {
    .entrypoint
    .maxstack 4
    .locals (object o, object lk)
    ldstr "<msil>fntables" call string [mscorlib]System.String::Intern(string) stloc.1
    ldloc.1 call void [mscorlib]System.Threading.Monitor::Enter(object)
    call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain() ldstr "<msil>fntables" callvirt instance object [mscorlib]System.AppDomain::GetData(string) stloc.0
    ldloc.0 brtrue.s HAVE
    call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain() ldstr "<msil>fntables"
    ldc.i4 4096 call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(int32) box [mscorlib]System.IntPtr
    callvirt instance void [mscorlib]System.AppDomain::SetData(string, object)
HAVE:
    ldloc.1 call void [mscorlib]System.Threading.Monitor::Exit(object)
    ldstr "registry slot: {0}" call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain() ldstr "<msil>fntables" callvirt instance object [mscorlib]System.AppDomain::GetData(string)
    call void [mscorlib]System.Console::WriteLine(string, object)
    ldc.i4.0 ret
  }
}
