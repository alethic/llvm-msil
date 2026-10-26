// FakeFields 1.0.0.0, without a public key: fields for the field lookup of
// ImplementationPlan.md 5.11.4 (section 12 row 2026-10-26, '5.11.4 amended
// (field requests search the requested type only; ...)'), requested by
// field-obj.il's objects. The runtime does not resolve a field MemberRef
// through base types (a field named on Fake.FieldDerived that only
// Fake.FieldBase defines throws MissingFieldException on .NET Framework 4.8,
// .NET 8 and .NET 10), so a field request searches the requested type only,
// as a constructor request does:
//   Fake.FieldBase     public static int32 S, public int32 I and the public
//                      fields P, Q and M that Fake.FieldDerived also names;
//   Fake.FieldDerived  extends Fake.FieldBase and defines none of S and I;
//                      its own fields of the other names are non-public:
//                        P  private static int32 P (Fake.FieldBase's type);
//                        Q  private int32 modreq(IsVolatile) Q (Fake.FieldBase's
//                           type but for a custom modifier);
//                        M  both kinds: private static int32 M and
//                           assembly static int32 modreq(IsVolatile) M.
// The alias 'FakeCore as mscorlib': see FakeInterop.il.
.assembly extern FakeCore as mscorlib
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
.assembly FakeFields
{
  .custom instance void [mscorlib]System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
  .ver 1:0:0:0
}
.module FakeFields.dll

.class public auto ansi beforefieldinit Fake.FieldBase extends [mscorlib]System.Object
{
  .field public static int32 S
  .field public int32 I
  .field public static int32 P
  .field public int32 Q
  .field public static int32 M
  .method family hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit Fake.FieldDerived extends Fake.FieldBase
{
  .field private static int32 P
  .field private int32 modreq([mscorlib]System.Runtime.CompilerServices.IsVolatile) Q
  .field private static int32 M
  .field assembly static int32 modreq([mscorlib]System.Runtime.CompilerServices.IsVolatile) M
  .method family hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
