// Input of roundtrip-data-labels.test: two RVA fields, g1 and g2, whose
// 4-byte data blocks differ (01 02 03 04 and 05 06 07 08), so ildasm prints
// two data labels I_<RVA> ('.field ... at I_<RVA>', '.data cil I_<RVA> =
// bytearray ('). The block D0 before them is data that no field uses:
// ildasm does not print it, so ilasm puts g1's and g2's data 4 bytes lower
// when it reassembles the listing, and the labels of the two listings differ
// the way an image of writePEImage (plan 5.7.7) and its reassembly do. The
// static field and the method named I_0000ABCD look like such a label but
// are names, which msil-verify.py --roundtrip must compare as they are. The
// stand-in ilasm fake-ilasm.py edits the listing before the real ilasm
// reassembles it. Nothing runs it.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'roundtrip-rva' { .ver 0:0:0:0 }

.class private explicit ansi sealed 'S4' extends [mscorlib]System.ValueType
{
  .pack 1
  .size 4
} // end of class 'S4'

.field public static valuetype 'S4' 'g1' at D1
.field public static valuetype 'S4' 'g2' at D2
.field public static int32 'I_0000ABCD'
.data cil D0 = bytearray (AA BB CC DD)
.data cil D1 = bytearray (01 02 03 04)
.data cil D2 = bytearray (05 06 07 08)

.method public static int32 'I_0000ABCD'() cil managed
{
  .maxstack 1
  ldsfld int32 'I_0000ABCD'
  ret
} // end of method 'I_0000ABCD'

.method public static int32 'main'() cil managed
{
  .maxstack 1
  call int32 'I_0000ABCD'()
  ret
} // end of method 'main'
