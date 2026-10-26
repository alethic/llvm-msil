// FakeTypesA 1.2.3.4: public types for the lookup tests of
// ImplementationPlan.md 5.11.4, reached through msil-ld's hidden
// --print-bcl-binding (type:, typename:) and through named-obj.il:
//   Fake.Widget      defined here only (FakeTypesB has a non-public one)
//   Fake.Dup         defined here and in FakeTypesB (ambiguous)
//   Fake.Outer/Inner a nested-public type with a static method Ping()
//   Fake.Gen`1       a generic type: the metadata name carries the arity
//   Fake.Color       an enum over int16, for the custom-attribute blobs of
//                    --print-bcl-binding=attr: (its values take two bytes)
// The alias 'FakeCore as mscorlib': see FakeInterop.il.
.assembly extern FakeCore as mscorlib
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
.assembly FakeTypesA
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
  .ver 1:2:3:4
}
.module FakeTypesA.dll

.class public auto ansi beforefieldinit Fake.Widget extends [mscorlib]System.Object { }
.class public auto ansi beforefieldinit Fake.Dup extends [mscorlib]System.Object { }
.class public auto ansi beforefieldinit Fake.Outer extends [mscorlib]System.Object
{
  .class nested public auto ansi beforefieldinit Inner extends [mscorlib]System.Object
  {
    .method public hidebysig static void Ping() cil managed { ldnull throw }
  }
}
.class public auto ansi beforefieldinit Fake.'Gen`1'<T> extends [mscorlib]System.Object { }
.class public auto ansi sealed Fake.Color extends [mscorlib]System.Enum
{
  .field public specialname rtspecialname int16 value__
  .field public static literal valuetype Fake.Color Red = int16(1)
  .field public static literal valuetype Fake.Color Blue = int16(2)
}
