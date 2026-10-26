// FakeInterop 8.0.0.0: System.Runtime.InteropServices.Marshal with the
// BCLMembers.def methods msil-ld's trailer calls (AllocHGlobal(native int),
// StringToHGlobalAnsi, StringToHGlobalUni), as System.Runtime.InteropServices
// holds them in the .NET 8 pack. AllocHGlobal(int32) comes first, so only a
// match on the full signature binds the native int overload.
//
// gen.py builds variants from this file with '#define':
//   FakeInteropMods  MODS  AllocHGlobal(native int) returns
//                          native int modreq([FakeCore]InAttribute) and takes
//                          native int modopt([FakeCore]IsVolatile)
//                          (bcl-binding 3.4: the definition's modifiers)
//   FakeInteropV9    V9    version 9.0.0.0 (same name and key)
//   FakeInteropK2    K2    FakeThreading.il's key (token 31bf3856ad364e35)
//   FakeInteropExtra EXTRA the same Assembly row (so the same identity) with
//                          one more type, System.Runtime.InteropServices.Extra:
//                          other contents
//
// ilasm derives a class without its own base from [mscorlib]System.Object;
// the alias 'FakeCore as mscorlib' makes that FakeCore's, so every
// '[mscorlib]' below names FakeCore.
.assembly extern FakeCore as mscorlib
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
.assembly FakeInterop
{
  .custom instance void [mscorlib]System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
#ifdef K2
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
#else
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
#endif
#ifdef V9
  .ver 9:0:0:0
#else
  .ver 8:0:0:0
#endif
}
.module FakeInterop.dll

.class public abstract auto ansi sealed beforefieldinit System.Runtime.InteropServices.Marshal
       extends [mscorlib]System.Object
{
  .method public hidebysig static native int AllocHGlobal(int32 cb) cil managed { ldnull throw }
#ifdef MODS
  .method public hidebysig static native int modreq([mscorlib]System.Runtime.InteropServices.InAttribute)
          AllocHGlobal(native int modopt([mscorlib]System.Runtime.CompilerServices.IsVolatile) cb) cil managed
  {
    ldnull
    throw
  }
#else
  .method public hidebysig static native int AllocHGlobal(native int cb) cil managed { ldnull throw }
#endif
  .method public hidebysig static native int StringToHGlobalAnsi(string s) cil managed { ldnull throw }
  .method public hidebysig static native int StringToHGlobalUni(string s) cil managed { ldnull throw }
}
#ifdef EXTRA
.class public abstract auto ansi sealed beforefieldinit System.Runtime.InteropServices.Extra
       extends [mscorlib]System.Object
{
}
#endif
