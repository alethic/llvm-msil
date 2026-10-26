// FakeCore 8.0.0.0, the core library of lld/test/MSIL's synthetic reference
// sets (ImplementationPlan.md 5.11.3, M21.d): it defines top-level
// System.Object and has no AssemblyRef row. '.mscorlib' tells ilasm that this
// module defines the core types; without it ilasm derives every class from
// [mscorlib]System.Object and adds that AssemblyRef, whatever the assembly is
// called (bcl-binding 7.5), which is how IlasmCore.il gets one.
//
// Like System.Runtime in the .NET 8 pack it holds the core half of
// BCLMembers.def (System.ValueType, Environment, Buffer, Object and
// EntryPointNotFoundException), System.Exception for exception clauses, and
// the attribute and modifier types the rest of the pack uses. Its key is the
// one of the .NET packs (token b03f5f7f11d50a3a). Bodies throw, as in a
// reference assembly.
//
// gen.py builds two variants with '#define', both named FakeCore:
//   FakeCoreNoCtor    NOCTOR    EntryPointNotFoundException has .ctor() only,
//                               while its base System.Exception keeps
//                               .ctor(string): constructors are not inherited
//                               (ImplementationPlan.md section 12, '5.11.4
//                               amended (the base-type member search)')
//   FakeCoreNoInAttr  NOINATTR  no System.Runtime.InteropServices.InAttribute,
//                               the modreq type of FakeInteropMods
.mscorlib
.assembly FakeCore
{
  .custom instance void System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
  .publickey = ( 00 24 00 00 04 80 00 00 94 00 00 00 06 02 00 00
                 00 24 00 00 52 53 41 31 00 04 00 00 01 00 01 00
                 07 D1 FA 57 C4 AE D9 F0 A3 2E 84 AA 0F AE FD 0D
                 E9 E8 FD 6A EC 8F 87 FB 03 76 6C 83 4C 99 92 1E
                 B2 3B E7 9A D9 D5 DC C1 DD 9A D2 36 13 21 02 90
                 0B 72 3C F9 80 95 7F C4 E1 77 10 8F C6 07 77 4F
                 29 E8 32 0E 92 EA 05 EC E4 E8 21 C0 A5 EF E8 F1
                 64 5C 4C 0C 93 C1 AB 99 28 5D 62 2C AA 65 2C 1D
                 FA D6 3D 74 5D 6F 2D E5 F1 7E 5E AF 0F C4 96 3D
                 26 1C 8A 12 43 65 18 20 6D C0 93 34 4D 5A D2 93 )
  .ver 8:0:0:0
}
.module FakeCore.dll

.class public auto ansi beforefieldinit System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
.class public abstract auto ansi beforefieldinit System.ValueType extends System.Object { }
.class public abstract auto ansi beforefieldinit System.Enum extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Void extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Boolean extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Char extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.SByte extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Byte extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Int16 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.UInt16 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Int32 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.UInt32 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Int64 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.UInt64 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Single extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Double extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.IntPtr extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.UIntPtr extends System.ValueType { }
.class public auto ansi sealed beforefieldinit System.String extends System.Object { }
.class public abstract auto ansi beforefieldinit System.Array extends System.Object { }
.class public abstract auto ansi beforefieldinit System.Type extends System.Object { }
.class public abstract auto ansi beforefieldinit System.Attribute extends System.Object
{
  .method family hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}

.class public auto ansi beforefieldinit System.Exception extends System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit System.EntryPointNotFoundException extends System.Exception
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
#ifndef NOCTOR
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
#endif
}
.class public abstract auto ansi sealed beforefieldinit System.Environment extends System.Object
{
  .method public hidebysig static string[] GetCommandLineArgs() cil managed { ldnull throw }
}
.class public abstract auto ansi sealed beforefieldinit System.Buffer extends System.Object
{
  // The unsigned overload first: only the full signature picks the right one.
  .method public hidebysig static void MemoryCopy(void* source, void* destination, uint64 destinationSizeInBytes,
                                                  uint64 sourceBytesToCopy) cil managed { ldnull throw }
  .method public hidebysig static void MemoryCopy(void* source, void* destination, int64 destinationSizeInBytes,
                                                  int64 sourceBytesToCopy) cil managed { ldnull throw }
}

.class public auto ansi sealed beforefieldinit System.ObsoleteAttribute extends System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi sealed beforefieldinit System.Runtime.Versioning.RequiresPreviewFeaturesAttribute
       extends System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
.class public auto ansi sealed beforefieldinit System.Runtime.CompilerServices.ReferenceAssemblyAttribute
       extends System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
#ifndef NOINATTR
.class public auto ansi sealed beforefieldinit System.Runtime.InteropServices.InAttribute extends System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
#endif
.class public abstract auto ansi sealed beforefieldinit System.Runtime.CompilerServices.IsVolatile
       extends System.Object { }
.class public auto ansi beforefieldinit System.Runtime.CompilerServices.CallConvCdecl extends System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
