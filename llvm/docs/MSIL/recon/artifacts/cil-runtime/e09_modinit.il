// e09: module initializer (<Module>::.cctor) that
//   - allocates the unmanaged data segment (Marshal.AllocHGlobal - works on net48 and net8+),
//   - copies the RVA .data image into it with cpblk from ldsflda of an RVA field,
//   - zero-fills the bss tail with initblk,
//   - applies relocations (pointer to another global, function pointer),
//   - stores the base in a static field.
// Also an export class callable from other assemblies / C#.
//
// Segment layout (SEG_SIZE = 64, init image = 32 bytes, bss = 32 bytes):
//   +0   int32  g_counter = 5
//   +4   int32  pad
//   +8   char*  g_msg     = &g_str        (reloc: base + 24)
//   +16  int(*)(int) g_fp = &sq           (reloc: ldftn)
//   +24  char   g_str[8]  = "seg-ok\0\0"
//   +32  int64  g_bss[4]  (zero)
#include "common.il"
.assembly e09_modinit {}
.module e09_modinit.dll

.field static assembly native int '__msil_data_base'
.field static assembly int32 '__msil_cctor_ran'

.class private explicit ansi sealed '__msil_init_image_t' extends [mscorlib]System.ValueType { .pack 1 .size 32 }
.field static assembly valuetype '__msil_init_image_t' '__msil_init_image' at D_INIT
.data D_INIT = bytearray (
  05 00 00 00  00 00 00 00                        // +0  g_counter=5, pad
  00 00 00 00  00 00 00 00                        // +8  g_msg (relocated)
  00 00 00 00  00 00 00 00                        // +16 g_fp (relocated)
  73 65 67 2D 6F 6B 00 00 )                       // +24 "seg-ok"

.method static int32 'sq'(int32 x) cil managed { .maxstack 2 ldarg.0 ldarg.0 mul ret }

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 4
  // base = Marshal.AllocHGlobal(64)
  ldc.i4 64
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stsfld native int '__msil_data_base'
  // memcpy(base, &init_image, 32)
  ldsfld native int '__msil_data_base'
  ldsflda valuetype '__msil_init_image_t' '__msil_init_image'
  ldc.i4 32
  cpblk
  // memset(base + 32, 0, 32)
  ldsfld native int '__msil_data_base'
  ldc.i4 32
  add
  ldc.i4.0
  ldc.i4 32
  initblk
  // reloc: *(base+8) = base + 24
  ldsfld native int '__msil_data_base'
  ldc.i4.8
  add
  ldsfld native int '__msil_data_base'
  ldc.i4 24
  add
  stind.i
  // reloc: *(base+16) = &sq
  ldsfld native int '__msil_data_base'
  ldc.i4 16
  add
  ldftn int32 'sq'(int32)
  stind.i
  ldc.i4.1
  stsfld int32 '__msil_cctor_ran'
  ret
}

.class public abstract sealed auto ansi Exports extends [mscorlib]System.Object
{
  .method public static int32 'get_counter'() cil managed
  { .maxstack 1  ldsfld native int '__msil_data_base'  ldind.i4  ret }
  .method public static int32 'call_fp'(int32 x) cil managed
  { .maxstack 3  ldarg.0  ldsfld native int '__msil_data_base'  ldc.i4 16  add  ldind.i  calli int32(int32)  ret }
  .method public static native int 'msg'() cil managed
  { .maxstack 2  ldsfld native int '__msil_data_base'  ldc.i4.8  add  ldind.i  ret }
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  ldstr ".cctor ran before entrypoint"  ldsfld int32 '__msil_cctor_ran'  conv.i8  ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "g_counter"  call int32 Exports::'get_counter'()  conv.i8  ldc.i8 5  call void 'check.i8'(string, int64, int64)
  ldstr "g_fp(12)"   ldc.i4 12  call int32 Exports::'call_fp'(int32)  conv.i8  ldc.i8 144  call void 'check.i8'(string, int64, int64)
  ldstr "g_msg == base+24"  call native int Exports::'msg'()  ldsfld native int '__msil_data_base'  sub  conv.i8  ldc.i8 24  call void 'check.i8'(string, int64, int64)
  call native int Exports::'msg'()  call string [mscorlib]System.Runtime.InteropServices.Marshal::PtrToStringAnsi(native int)
  call void [mscorlib]System.Console::WriteLine(string)
  ldstr "bss zero"  ldsfld native int '__msil_data_base'  ldc.i4 56  add  ldind.i8  ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "base 8-aligned"  ldsfld native int '__msil_data_base'  ldc.i4.7  conv.i  and  conv.i8  ldc.i8 0  call void 'check.i8'(string, int64, int64)
  call int32 'report'()
  ret
}
