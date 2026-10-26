// FakeFacade 4.0.0.0: a facade, like mscorlib in the .NET 8 pack. It defines
// no type and only forwards (ExportedType rows with TypeAttributes.Forwarder
// 0x00200000), each to an assembly it references with version 0.0.0.0, as
// that facade does (bcl-binding 2.3). It forwards System.Object, so it is
// the facade that a core-library error names, and it never qualifies as a
// core library itself (it has AssemblyRef rows).
//
// Fake.Outer/Inner is a nested forwarder: an ExportedType without the
// forwarder bit whose Implementation is Fake.Outer's ExportedType
// (ImplementationPlan.md 5.11.4).
.assembly extern FakeCore
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 0:0:0:0
}
.assembly extern FakeInterop
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 0:0:0:0
}
.assembly extern FakeThreading
{
  .publickeytoken = ( 31 BF 38 56 AD 36 4E 35 )
  .ver 0:0:0:0
}
.assembly extern FakeTypesA
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 0:0:0:0
}
.assembly FakeFacade
{
  .publickey = ( 00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00 )
  .ver 4:0:0:0
}
.module FakeFacade.dll

.class extern forwarder System.Object { .assembly extern FakeCore }
.class extern forwarder System.ValueType { .assembly extern FakeCore }
.class extern forwarder System.Environment { .assembly extern FakeCore }
.class extern forwarder System.Buffer { .assembly extern FakeCore }
.class extern forwarder System.Exception { .assembly extern FakeCore }
.class extern forwarder System.EntryPointNotFoundException { .assembly extern FakeCore }
.class extern forwarder System.Runtime.InteropServices.Marshal { .assembly extern FakeInterop }
.class extern forwarder System.Threading.Interlocked { .assembly extern FakeThreading }
.class extern forwarder Fake.Outer { .assembly extern FakeTypesA }
.class extern nested public Inner { .class extern Fake.Outer }
