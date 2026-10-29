// key.il (Linux): pthread key destructors that are native-callable thunks. keyt.exe <del|uco>
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly keyt { .ver 0:0:0:0 }
.module keyt.exe
.corflags 0x00000001
.class private auto ansi sealed beforefieldinit System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute extends [mscorlib]System.Attribute
{ .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Attribute::.ctor() ret } }
.class private auto ansi sealed D_v_p extends [mscorlib]System.MulticastDelegate
{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {}
  .method public hidebysig newslot virtual instance void Invoke(native int v) runtime managed {}
}
.class private auto ansi sealed D_p_p extends [mscorlib]System.MulticastDelegate
{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {}
  .method public hidebysig newslot virtual instance native int Invoke(native int v) runtime managed {}
}
.class private abstract auto ansi sealed Prog extends [mscorlib]System.Object
{
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 key_create(native int d) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 key_set(native int v) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 key_native_thread(native int v) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 run_thread(native int s, native int a, native int r) cil managed preservesig {}
  .field static class [mscorlib]System.Delegate k1
  .field static class [mscorlib]System.Delegate k2
  .method static void dtor(native int v) cil managed
  {
    .maxstack 3
    ldstr "  destructor({0}) on managed thread {1}" ldarg.0 call int64 [mscorlib]System.IntPtr::op_Explicit(native int) box [mscorlib]System.Int64
    call class [mscorlib]System.Threading.Thread [mscorlib]System.Threading.Thread::get_CurrentThread() callvirt instance int32 [mscorlib]System.Threading.Thread::get_ManagedThreadId() box [mscorlib]System.Int32
    call void [mscorlib]System.Console::WriteLine(string, object, object) ret
  }
  .method static void uco_dtor(native int v) cil managed { .custom instance void System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() = ( 01 00 00 00 ) .maxstack 1 ldarg.0 call void Prog::dtor(native int) ret }
  .method static native int start(native int v) cil managed { .maxstack 1 ldarg.0 call int32 Prog::key_set(native int) pop ldc.i4.0 conv.i ret }
  .method static native int uco_start(native int v) cil managed { .custom instance void System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() = ( 01 00 00 00 ) .maxstack 1 ldarg.0 call native int Prog::start(native int) ret }
  .method static int32 Main(string[] args) cil managed
  {
    .entrypoint
    .maxstack 4
    .locals (native int d, native int s, native int r)
    ldarg.0 ldc.i4.0 ldelem.ref ldstr "uco" call bool [mscorlib]System.String::op_Equality(string, string) brtrue.s UCO
    ldnull ldftn void Prog::dtor(native int) newobj instance void D_v_p::.ctor(object, native int) dup stsfld class [mscorlib]System.Delegate Prog::k1
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stloc.0
    ldnull ldftn native int Prog::start(native int) newobj instance void D_p_p::.ctor(object, native int) dup stsfld class [mscorlib]System.Delegate Prog::k2
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stloc.1 br.s GO
UCO: ldftn void Prog::uco_dtor(native int) stloc.0 ldftn native int Prog::uco_start(native int) stloc.1
GO: ldloc.0 call int32 Prog::key_create(native int) pop
    ldstr "native thread that never ran managed code:" call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4 42 conv.i call int32 Prog::key_native_thread(native int) pop
    ldstr "native thread whose start routine is managed:" call void [mscorlib]System.Console::WriteLine(string)
    ldloc.1 ldc.i4 43 conv.i ldloca.s r conv.u call int32 Prog::run_thread(native int, native int, native int) pop
    ldc.i4 200 call void [mscorlib]System.Threading.Thread::Sleep(int32)
    ldstr "done" call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4.0 ret
  }
}
