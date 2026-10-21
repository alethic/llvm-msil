// A Phase-3/4-shaped CIL object (netmodule), written by hand from Design.md 4.2, 4.4, 4.5, 4.6, 2.3 and 3.2:
// the metadata an object must carry beyond Phase 1's MethodDef/Field/StandAloneSig/ImplMap set.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'System.Runtime.Intrinsics' { .ver 8:0:0:0 }
.assembly extern 'msil.extern' { .ver 0:0:0:0 }
.module 'p3shape.o'
.module extern 'msil.unresolved'

// 2.3: a by-value struct in a P/Invoke signature needs an explicit-layout value type (TypeDef + ClassLayout + FieldLayout)
.class private explicit ansi sealed '<msil>s24' extends [mscorlib]System.ValueType
{ .pack 1 .size 24
  .field [0] public int64 a
  .field [8] public int64 b
  .field [16] public int64 c }
// 4.5 (Framework default): one delegate type per address-taken signature, kept alive, GetFunctionPointerForDelegate
.class private auto ansi sealed '<msil>d_i_i' extends [mscorlib]System.MulticastDelegate
{ .method public hidebysig specialname rtspecialname instance void .ctor(object o, native int m) runtime managed {}
  .method public hidebysig newslot virtual instance int32 Invoke(int32 x) runtime managed {} }

// 4.2/4.4: [ThreadStatic] shadow-stack pointer and TLS block (CustomAttribute on a Field)
.field assembly static native int '<msil>sp'
.custom instance void [mscorlib]System.ThreadStaticAttribute::.ctor() = ( 01 00 00 00 )
.field assembly static class [mscorlib]System.Delegate[] '<msil>keepalive'
.field public static initonly native int 'counter'

.method public static int32 'cb'(int32 x) cil managed { .maxstack 8 ldarg.0 ldc.i4.1 add ret }

// 4.5 (modern switch): UnmanagedCallersOnly thunk (CustomAttribute on a MethodDef, attribute in a switch-gated reference)
.method public static int32 '<msil>uco_cb'(int32 x) cil managed
{ .custom instance void [mscorlib]System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() = ( 01 00 00 00 )
  .maxstack 8 ldarg.0 call int32 'cb'(int32) ret }

// 2.3: P/Invoke with a by-value struct (TypeDefOrRef coded index inside the signature blob)
.method public static pinvokeimpl("msil.unresolved" cdecl) int64 'sum'(valuetype '<msil>s24' s) cil managed preservesig {}

.method public static native int 'thunk_cb'() cil managed
{ .maxstack 8
  ldnull
  ldftn int32 'cb'(int32)
  newobj instance void '<msil>d_i_i'::.ctor(object, native int)
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate)
  ret }

// 4.6: setjmp/longjmp: throw a BCL exception and catch it with a filter that tests the jmp_buf (EH clause class token in catch)
.method public static int32 'sj'(native int jb) cil managed
{ .maxstack 8 .locals init (int32 r)
  .try { ldarg.0 newobj instance void [mscorlib]System.Exception::.ctor() throw }
  filter { isinst [mscorlib]System.Exception ldnull cgt.un endfilter }
  { pop ldc.i4.1 stloc.0 leave.s DONE }
  .try { ldc.i4.0 stloc.0 leave.s DONE } catch [mscorlib]System.InvalidOperationException { pop leave.s DONE }
DONE: ldloc.0 ret }

// 3.2/P4: SIMD through Vector128<T> (TypeSpec for the generic instance, MethodSpec for a generic method)
.method public static void 'vadd'(native int p) cil managed
{ .maxstack 8
  ldarg.0 ldobj valuetype [System.Runtime.Intrinsics]System.Runtime.Intrinsics.Vector128`1<float32>
  ldarg.0 ldobj valuetype [System.Runtime.Intrinsics]System.Runtime.Intrinsics.Vector128`1<float32>
  call valuetype [System.Runtime.Intrinsics]System.Runtime.Intrinsics.Vector128`1<!!0> [System.Runtime.Intrinsics]System.Runtime.Intrinsics.Vector128::Add<float32>(valuetype [System.Runtime.Intrinsics]System.Runtime.Intrinsics.Vector128`1<!!0>, valuetype [System.Runtime.Intrinsics]System.Runtime.Intrinsics.Vector128`1<!!0>)
  stobj valuetype [System.Runtime.Intrinsics]System.Runtime.Intrinsics.Vector128`1<float32>
  ret }

// Phase-2 binding shapes: undefined data and function address through placeholder MemberRefs
.method public static native int 'use'() cil managed
{ .maxstack 8 ldsfld native int [msil.extern]'<msil>Symbols'::'extdata' ldsfld native int [msil.extern]'<msil>FnAddr'::'extfn' add ret }
