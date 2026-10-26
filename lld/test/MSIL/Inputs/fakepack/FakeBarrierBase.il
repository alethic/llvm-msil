// FakeBarrierBase 1.0.0.0, without a public key: the base class of
// FakeThreading.il's variant XBASE (FakeThreadingXBase), whose
// System.Threading.Interlocked defines no MemoryBarrier() and extends
// [FakeBarrierBase]System.Threading.InterlockedBase. A method (other than a
// constructor) found only in a base type that another reference defines
// binds on the requested type (ImplementationPlan.md 5.11.4), so the output
// references Interlocked's assembly and not this one. The alias 'FakeCore as mscorlib': see
// FakeInterop.il.
//
// gen.py's variant FakeBarrierBaseCopy (#define COPY) is the same assembly
// named FakeBarrierBaseCopy: next to FakeBarrierBase, InterlockedBase has two
// definitions.
.assembly extern FakeCore as mscorlib
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
#ifdef COPY
.assembly FakeBarrierBaseCopy
#else
.assembly FakeBarrierBase
#endif
{
  .custom instance void [mscorlib]System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
  .ver 1:0:0:0
}
#ifdef COPY
.module FakeBarrierBaseCopy.dll
#else
.module FakeBarrierBase.dll
#endif

.class public abstract auto ansi beforefieldinit System.Threading.InterlockedBase extends [mscorlib]System.Object
{
  .method public hidebysig static void MemoryBarrier() cil managed { ldnull throw }
  .method family hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
