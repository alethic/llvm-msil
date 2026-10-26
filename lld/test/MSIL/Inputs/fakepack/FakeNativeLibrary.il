// FakeNativeLibrary 8.0.0.0: System.Runtime.InteropServices.NativeLibrary
// with the two members of the data-import helper's NativeLibrary body
// (ImplementationPlan.md 5.10.12; M21's BCLMembers.def entries
// NativeLibrary_Load and NativeLibrary_GetExport). It is a file of its own so
// that a test can give a pack with or without it. The alias
// 'FakeCore as mscorlib': see FakeInterop.il.
//
// gen.py builds variants from this file with '#define'; all are named
// FakeNativeLibrary, and the helper uses NativeLibrary only when both members
// are defined, so none of them qualifies:
//   FakeNativeLibraryLoadOnly    LOADONLY    Load(string) only
//   FakeNativeLibraryExportOnly  EXPORTONLY  GetExport(native int, string)
//                                            only
//   FakeNativeLibraryWrongSig    WRONGSIG    Load(string) and
//                                            GetExport(native int, char*)
//   FakeNativeLibraryPrivExport  PRIVEXPORT  Load(string) and an 'assembly'
//                                            GetExport(native int, string)
.assembly extern FakeCore as mscorlib
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
.assembly FakeNativeLibrary
{
  .custom instance void [mscorlib]System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
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
.module FakeNativeLibrary.dll

.class public abstract auto ansi sealed beforefieldinit System.Runtime.InteropServices.NativeLibrary
       extends [mscorlib]System.Object
{
#ifndef EXPORTONLY
  .method public hidebysig static native int Load(string libraryPath) cil managed { ldnull throw }
#endif
#ifdef WRONGSIG
  .method public hidebysig static native int GetExport(native int handle, char* name) cil managed { ldnull throw }
#else
#ifdef PRIVEXPORT
  .method assembly hidebysig static native int GetExport(native int handle, string name) cil managed { ldnull throw }
#else
#ifndef LOADONLY
  .method public hidebysig static native int GetExport(native int handle, string name) cil managed { ldnull throw }
#endif
#endif
#endif
  .method public hidebysig static void Free(native int handle) cil managed { ldnull throw }
}
