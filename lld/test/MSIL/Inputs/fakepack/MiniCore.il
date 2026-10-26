// A minimal core library: '.mscorlib' (see FakeCore.il), no AssemblyRef and
// no public key, defining top-level System.Object and System.ValueType only.
// gen.py builds each variant with one '#define' (ImplementationPlan.md
// 5.11.3, 5.11.9; M21.d):
//   CoreA, CoreB, CoreC  CORE_A, CORE_B, CORE_C  three core libraries at once
//                                                are 'the references mix 3
//                                                core libraries: ...'
//                                                (section 12, '5.11.9
//                                                clarified (three or more
//                                                candidates)')
//   BaseCore             BASETYPE                System.Object extends the
//                                                class Fake.Root, so it is no
//                                                core library: '(defines
//                                                System.Object with a base
//                                                type)' (section 12, '5.11.9
//                                                clarified (the core-library
//                                                error's <list> ...)')
// Under '.mscorlib' ilasm writes no base type for System.Object whatever the
// source says, and makes Fake.Root extend System.Object, so for BaseCore
// gen.py patches the TypeDef table of the assembled image: System.Object's
// Extends becomes Fake.Root and Fake.Root's becomes 0 (no base type).
.mscorlib
#ifdef CORE_A
.assembly CoreA
#endif
#ifdef CORE_B
.assembly CoreB
#endif
#ifdef CORE_C
.assembly CoreC
#endif
#ifdef BASETYPE
.assembly BaseCore
#endif
{
  .ver 1:0:0:0
}
#ifdef CORE_A
.module CoreA.dll
#endif
#ifdef CORE_B
.module CoreB.dll
#endif
#ifdef CORE_C
.module CoreC.dll
#endif
#ifdef BASETYPE
.module BaseCore.dll

.class public abstract auto ansi beforefieldinit Fake.Root { }
#endif

.class public auto ansi beforefieldinit System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
.class public abstract auto ansi beforefieldinit System.ValueType extends System.Object { }
