// FakeTypesB 1.0.0.0, without a public key: the second half of the lookup
// tests of ImplementationPlan.md 5.11.4 (see FakeTypesA.il):
//   Fake.Widget      not public: ignored, so FakeTypesA's binds
//   Fake.Dup         public, as in FakeTypesA: ambiguous in either order
//   Fake.Outer       not public, with a nested-public Inner: ignored, since
//                    an enclosing type is not public
//   Fake.Box         public, with a nested-private Inner (ignored) and a
//                    nested-public Lid; with no key an assembly-qualified
//                    name says PublicKeyToken=null
// gen.py's variant FakeTypesC (#define TYPES_C) is the same assembly named
// FakeTypesC: a third definition of Fake.Dup.
// The alias 'FakeCore as mscorlib': see FakeInterop.il.
.assembly extern FakeCore as mscorlib
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
#ifdef TYPES_C
.assembly FakeTypesC
#else
.assembly FakeTypesB
#endif
{
  .custom instance void [mscorlib]System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
  .ver 1:0:0:0
}
#ifdef TYPES_C
.module FakeTypesC.dll
#else
.module FakeTypesB.dll
#endif

.class private auto ansi beforefieldinit Fake.Widget extends [mscorlib]System.Object { }
.class public auto ansi beforefieldinit Fake.Dup extends [mscorlib]System.Object { }
.class private auto ansi beforefieldinit Fake.Outer extends [mscorlib]System.Object
{
  .class nested public auto ansi beforefieldinit Inner extends [mscorlib]System.Object
  {
    .method public hidebysig static void Ping() cil managed { ldnull throw }
  }
}
.class public auto ansi beforefieldinit Fake.Box extends [mscorlib]System.Object
{
  .class nested private auto ansi beforefieldinit Inner extends [mscorlib]System.Object { }
  .class nested public auto ansi beforefieldinit Lid extends [mscorlib]System.Object { }
}
