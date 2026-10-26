// field-obj-*.o: CIL objects (ImplementationPlan.md 5.8.2-5.8.4) in
// object-form text whose one function, 'void named()', reads BCL fields,
// which llc never requests (BCLMembers.def has no field entry). They are
// Field requests (5.11.5) on [msil.bcl] TypeRefs to FakeFields.il's types:
//   field-obj-s     ldsfld Fake.FieldDerived::S (only Fake.FieldBase has S)
//   field-obj-i     ldfld  Fake.FieldDerived::I (only Fake.FieldBase has I)
//   field-obj-p     ldsfld Fake.FieldDerived::P (its own P is private)
//   field-obj-q     ldfld  Fake.FieldDerived::Q (its own Q is private and
//                          has a modreq)
//   field-obj-m     ldsfld Fake.FieldDerived::M (two non-public M of its own)
//   field-obj-base  ldsfld Fake.FieldBase::S, then ldfld Fake.FieldBase::I
// gen.py builds each from this file with '#define FIELD_<suffix>', the
// suffix in upper case (FIELD_S, ..., FIELD_BASE). The record is
// named-obj.il's, the one llc -msil-object-form writes for
// 'define void @named()' on x86_64-pc-windows-msvc-msil (one FUNCTION
// symbol, MethodDef 0x06000001); BCL references have no symbol. gen.py
// assembles it with '-dll -pe64 -x64', as %ilasm-verify does.
.assembly extern 'msil.extern' { .ver 0:0:0:0 }
.assembly extern 'msil.bcl' { .ver 0:0:0:0 }
.assembly extern 'msil.link' { .ver 0:0:0:0 }

.method public static void 'named'() cil managed
{
  .maxstack 1
#ifdef FIELD_S
  ldsfld int32 [msil.bcl]Fake.FieldDerived::S
  pop
#endif
#ifdef FIELD_I
  ldnull
  ldfld int32 [msil.bcl]Fake.FieldDerived::I
  pop
#endif
#ifdef FIELD_P
  ldsfld int32 [msil.bcl]Fake.FieldDerived::P
  pop
#endif
#ifdef FIELD_Q
  ldnull
  ldfld int32 [msil.bcl]Fake.FieldDerived::Q
  pop
#endif
#ifdef FIELD_M
  ldsfld int32 [msil.bcl]Fake.FieldDerived::M
  pop
#endif
#ifdef FIELD_BASE
  ldsfld int32 [msil.bcl]Fake.FieldBase::S
  pop
  ldnull
  ldfld int32 [msil.bcl]Fake.FieldBase::I
  pop
#endif
  ret
} // end of method 'named'

.module 'named.ll'
.custom instance void [msil.link]'<msil>LinkInfo'::.ctor(uint8[]) = ( 01 00 39 00 00 00 4D 53 4C 4B 01 00 00 1B 78 38 36 5F 36 34 2D 70 63 2D 77 69 6E 64 6F 77 73 2D 6D 73 76 63 2D 6D 73 69 6C 01 00 00 08 04 02 08 01 0D 01 00 00 05 6E 61 6D 65 64 01 00 00 06 00 00 )
