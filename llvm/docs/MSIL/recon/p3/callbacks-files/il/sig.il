// sig.il (Linux): a C signal handler installed with signal() through a native-callable thunk.
// sig.exe <sync|async> <del|uco>
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly sigt { .ver 0:0:0:0 }
.module sigt.exe
.corflags 0x00000001
.class private auto ansi sealed beforefieldinit System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute extends [mscorlib]System.Attribute
{ .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Attribute::.ctor() ret } }
.class private auto ansi sealed D_v_i extends [mscorlib]System.MulticastDelegate
{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {}
  .method public hidebysig newslot virtual instance void Invoke(int32 s) runtime managed {}
}
.class private abstract auto ansi sealed Prog extends [mscorlib]System.Object
{
  .method assembly static pinvokeimpl("libc.so.6" nomangle cdecl) native int signal(int32 s, native int h) cil managed preservesig {}
  .method assembly static pinvokeimpl("libc.so.6" nomangle cdecl) int32 raise(int32 s) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 start_killer(int32 s, int32 ms) cil managed preservesig {}
  .field static int32 hits
  .field static class [mscorlib]System.Delegate keep
  .field static object sink
  .method static void 'handler'(int32 s) cil managed { .maxstack 2 ldsfld int32 Prog::hits ldc.i4.1 add stsfld int32 Prog::hits ret }
  .method static void uco_handler(int32 s) cil managed { .custom instance void System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() = ( 01 00 00 00 ) .maxstack 1 ldarg.0 call void Prog::'handler'(int32) ret }
  .method static int32 Main(string[] args) cil managed
  {
    .entrypoint
    .maxstack 4
    .locals (native int h, int64 t0, int32 i)
    ldarg.0 ldc.i4.1 ldelem.ref ldstr "uco" call bool [mscorlib]System.String::op_Equality(string, string) brtrue.s UCO
    ldnull ldftn void Prog::'handler'(int32) newobj instance void D_v_i::.ctor(object, native int) dup stsfld class [mscorlib]System.Delegate Prog::keep
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stloc.0 br.s GO
UCO: ldftn void Prog::uco_handler(int32) stloc.0
GO: ldc.i4.s 10 ldloc.0 call native int Prog::signal(int32, native int) pop
    ldarg.0 ldc.i4.0 ldelem.ref ldstr "sync" call bool [mscorlib]System.String::op_Equality(string, string) brfalse.s ASYNC
    ldc.i4.s 10 call int32 Prog::raise(int32) pop
    ldstr "sync: handler ran {0} time(s)" ldsfld int32 Prog::hits box [mscorlib]System.Int32 call void [mscorlib]System.Console::WriteLine(string, object)
    ldc.i4.0 ret
ASYNC:
    // The signal arrives while this thread runs managed code (allocating, so it reaches GC safe points).
    ldc.i4.s 10 ldc.i4 200 call int32 Prog::start_killer(int32, int32) pop
    ldc.i4.0 stloc.2
L:  ldc.i4.s 32 newarr [mscorlib]System.Byte stsfld object Prog::sink
    ldloc.2 ldc.i4.1 add dup stloc.2 ldc.i4 100000000 blt.s L
    ldstr "async: handler ran {0} time(s)" ldsfld int32 Prog::hits box [mscorlib]System.Int32 call void [mscorlib]System.Console::WriteLine(string, object)
    ldc.i4.0 ret
  }
}
