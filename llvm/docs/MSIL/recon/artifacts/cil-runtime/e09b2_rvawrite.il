// e09b: what the OLD backend did (and why we don't):
//   1. '&(label)' inside .data  -> an absolute VA that needs a PE base relocation.
//      Does the runtime apply it?  Compare the stored value with the real ldsflda address.
//   2. Writing through ldsflda of an RVA field (is image data writable?).
#include "common.il"
.assembly e09b2_rvawrite {}
.module e09b2_rvawrite.dll

.class private explicit ansi sealed 'b8' extends [mscorlib]System.ValueType { .pack 1 .size 8 }
.field static assembly valuetype 'b8' 'target' at D_TARGET
.field static assembly valuetype 'b8' 'ptr_to_target' at D_PTR
.data D_TARGET = bytearray (11 22 33 44 55 66 77 88)
.data D_PTR = int64 (0)   // (no &(label) reloc in this variant)
.data D_PAD = int32 (0)

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  .locals init (int64 stored, int64 actual)
  ldsflda valuetype 'b8' 'ptr_to_target'  ldind.i8  stloc.0
  ldsflda valuetype 'b8' 'target'  conv.u  conv.u8  stloc.1
  ldstr "OBSERVE &(D_TARGET) stored value  = 0x"  call void [mscorlib]System.Console::Write(string)
  ldloca.s stored  ldstr "X16"  call instance string [mscorlib]System.Int64::ToString(string)  call void [mscorlib]System.Console::WriteLine(string)
  ldstr "OBSERVE ldsflda target (actual)   = 0x"  call void [mscorlib]System.Console::Write(string)
  ldloca.s actual  ldstr "X16"  call instance string [mscorlib]System.Int64::ToString(string)  call void [mscorlib]System.Console::WriteLine(string)
  ldstr "OBSERVE stored == actual ? "  call void [mscorlib]System.Console::Write(string)
  ldloc.0  ldloc.1  ceq  call void [mscorlib]System.Console::WriteLine(int32)
  // 2. write into RVA field memory
  ldstr "OBSERVE writing into RVA field..."  call void [mscorlib]System.Console::WriteLine(string)
  ldsflda valuetype 'b8' 'target'  ldc.i4 0x7F  stind.i1
  ldstr "OBSERVE write OK, byte now = "  call void [mscorlib]System.Console::Write(string)
  ldsflda valuetype 'b8' 'target'  ldind.u1  call void [mscorlib]System.Console::WriteLine(int32)
  ldc.i4.0
  ret
}
