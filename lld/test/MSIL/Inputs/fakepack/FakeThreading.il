// FakeThreading 8.1.2.3: System.Threading.Interlocked with MemoryBarrier(),
// as System.Threading holds it in the .NET 8 pack. Its key is another
// Microsoft key (token 31bf3856ad364e35) and every version field differs, so
// its AssemblyRef shows each column.
//
// gen.py builds variants from this file with '#define' (ilasm's preprocessor
// knows only #ifdef and #ifndef, so PLAIN asks for the plain
// 'void MemoryBarrier()'); all are named FakeThreading except PRIVATE and
// PRIVREF:
//   FakeThreading            PLAIN
//   FakeThreadingNoBarrier   (none)         no MemoryBarrier at all
//   FakeThreadingRet         RET PLAIN      int32 MemoryBarrier() first: a
//                                           return-type overload
//   FakeThreadingOverloads   OVERLOADS      int32 MemoryBarrier() and
//                                           void MemoryBarrier(int32) only
//   FakeThreadingInstance    INSTANCE       instance void MemoryBarrier() only
//   FakeThreadingBase        BASE           MemoryBarrier() only in the base
//                                           class InterlockedBase
//   FakeThreadingObsolete    OBSOLETE       MemoryBarrier() is [Obsolete]
//   FakeThreadingPreview     PREVIEW        MemoryBarrier() is
//                                           [RequiresPreviewFeatures]
//   FakeThreadingTwo         TWO PLAIN      also void modopt(IsVolatile)
//                                           MemoryBarrier(): two matches once
//                                           modifiers are ignored
//   FakeThreadingFr          FR PLAIN       culture "fr"
//   System.Private.FakeLib   PRIVATE PLAIN  named System.Private.FakeLib
//                                           8.0.0.0 and not a reference
//                                           assembly (no [ReferenceAssembly]),
//                                           like a runtime's
//                                           System.Private.CoreLib
//   FakeThreadingGeneric     GENERIC        only the generic
//                                           void MemoryBarrier<T>()
//   FakeThreadingVararg      VARARG         only vararg void MemoryBarrier()
//   FakeThreadingNonPublic   NONPUBLIC      only an 'assembly'
//                                           void MemoryBarrier()
//   FakeThreadingHidden      BASE NONPUBLIC an 'assembly' MemoryBarrier() in
//                                           Interlocked and a public one in
//                                           its base class InterlockedBase:
//                                           the nearer one has the same
//                                           signature, so the runtime would
//                                           resolve to it
//   FakeThreadingHiddenMod   BASE NONPUBMOD an 'assembly' void
//                                           modopt(IsVolatile) MemoryBarrier()
//                                           in Interlocked and a public
//                                           void MemoryBarrier() in
//                                           InterlockedBase: the signatures
//                                           differ in a custom modifier
//   FakeThreadingXBase       XBASE          no MemoryBarrier(): Interlocked
//                                           extends the InterlockedBase of
//                                           another reference,
//                                           FakeBarrierBase.il, which has a
//                                           public one
//   FakeThreadingImpl        NOREFASM PLAIN without [ReferenceAssembly], but
//                                           not named System.Private.*
//   System.Private.FakeLibRef
//                            PRIVREF PLAIN  named System.Private.FakeLib
//                                           8.0.0.0, with [ReferenceAssembly]
//
// The alias 'FakeCore as mscorlib': see FakeInterop.il.
.assembly extern FakeCore as mscorlib
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
#ifdef XBASE
.assembly extern FakeBarrierBase
{
  .ver 1:0:0:0
}
#endif
#ifdef PRIVATE
.assembly System.Private.FakeLib
#else
#ifdef PRIVREF
.assembly System.Private.FakeLib
#else
.assembly FakeThreading
#endif
#endif
{
#ifndef PRIVATE
#ifndef NOREFASM
  .custom instance void [mscorlib]System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
#endif
#endif
  .publickey = ( 00 24 00 00 04 80 00 00 94 00 00 00 06 02 00 00
                 00 24 00 00 52 53 41 31 00 04 00 00 01 00 01 00
                 B5 FC 90 E7 02 7F 67 87 1E 77 3A 8F DE 89 38 C8
                 1D D4 02 BA 65 B9 20 1D 60 59 3E 96 C4 92 65 1E
                 88 9C C1 3F 14 15 EB B5 3F AC 11 31 AE 0B D3 33
                 C5 EE 60 21 67 2D 97 18 EA 31 A8 AE BD 0D A0 07
                 2F 25 D8 7D BA 6F C9 0F FD 59 8E D4 DA 35 E4 4C
                 39 8C 45 43 07 E8 E3 3B 84 26 14 3D AE C9 F5 96
                 83 6F 97 C8 F7 47 50 E5 97 5C 64 E2 18 9F 45 DE
                 F4 6B 2A 2B 12 47 AD C3 65 2B F5 C3 08 05 5D A9 )
#ifdef PRIVATE
  .ver 8:0:0:0
#else
#ifdef PRIVREF
  .ver 8:0:0:0
#else
  .ver 8:1:2:3
#endif
#endif
#ifdef FR
  .locale "fr"
#endif
}
.module FakeThreading.dll

#ifdef BASE
.class public abstract auto ansi beforefieldinit System.Threading.InterlockedBase extends [mscorlib]System.Object
{
  .method public hidebysig static void MemoryBarrier() cil managed { ldnull throw }
  .method family hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
.class public abstract auto ansi beforefieldinit System.Threading.Interlocked
       extends System.Threading.InterlockedBase
#else
#ifdef XBASE
.class public abstract auto ansi beforefieldinit System.Threading.Interlocked
       extends [FakeBarrierBase]System.Threading.InterlockedBase
#else
.class public abstract auto ansi sealed beforefieldinit System.Threading.Interlocked extends [mscorlib]System.Object
#endif
#endif
{
  .method public hidebysig static void MemoryBarrierProcessWide() cil managed { ldnull throw }
  .method public hidebysig static int32 Exchange(int32& location1, int32 'value') cil managed { ldnull throw }
#ifdef RET
  .method public hidebysig static int32 MemoryBarrier() cil managed { ldnull throw }
#endif
#ifdef OVERLOADS
  .method public hidebysig static int32 MemoryBarrier() cil managed { ldnull throw }
  .method public hidebysig static void MemoryBarrier(int32 kind) cil managed { ldnull throw }
#endif
#ifdef INSTANCE
  .method public hidebysig instance void MemoryBarrier() cil managed { ldnull throw }
#endif
#ifdef GENERIC
  .method public hidebysig static void MemoryBarrier<T>() cil managed { ldnull throw }
#endif
#ifdef VARARG
  .method public hidebysig static vararg void MemoryBarrier() cil managed { ldnull throw }
#endif
#ifdef NONPUBLIC
  .method assembly hidebysig static void MemoryBarrier() cil managed { ldnull throw }
#endif
#ifdef NONPUBMOD
  .method assembly hidebysig static void modopt([mscorlib]System.Runtime.CompilerServices.IsVolatile)
          MemoryBarrier() cil managed { ldnull throw }
#endif
#ifdef TWO
  .method public hidebysig static void modopt([mscorlib]System.Runtime.CompilerServices.IsVolatile)
          MemoryBarrier() cil managed { ldnull throw }
#endif
#ifdef OBSOLETE
  .method public hidebysig static void MemoryBarrier() cil managed
  {
    // [Obsolete("Retired")]
    .custom instance void [mscorlib]System.ObsoleteAttribute::.ctor(string) = ( 01 00 07 52 65 74 69 72 65 64 00 00 )
    ldnull
    throw
  }
#endif
#ifdef PREVIEW
  .method public hidebysig static void MemoryBarrier() cil managed
  {
    .custom instance void [mscorlib]System.Runtime.Versioning.RequiresPreviewFeaturesAttribute::.ctor() = ( 01 00 00 00 )
    ldnull
    throw
  }
#endif
#ifdef PLAIN
  .method public hidebysig static void MemoryBarrier() cil managed { ldnull throw }
#endif
}
