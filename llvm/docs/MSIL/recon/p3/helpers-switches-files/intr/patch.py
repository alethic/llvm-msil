import re, sys
src = open(sys.argv[1]).read()
modern = sys.argv[3] == 'modern'
SR = '[System.Runtime]'
rep = {
  "call int32 'bcl_popc'(int32)": f"call int32 {SR}System.Numerics.BitOperations::PopCount(uint32)",
  "call int32 'bcl_clz'(int32)": f"call int32 {SR}System.Numerics.BitOperations::LeadingZeroCount(uint32)",
  "call int32 'bcl_rotl'(int32, int32)": f"call uint32 {SR}System.Numerics.BitOperations::RotateLeft(uint32, int32)",
  "call int32 'bcl_bswap'(int32)": "call uint32 [System.Memory]System.Buffers.Binary.BinaryPrimitives::ReverseEndianness(uint32)",
  "call float64 'bcl_fma'(float64, float64, float64)": f"call float64 {SR}System.Math::FusedMultiplyAdd(float64, float64, float64)",
}
safe = {
  "call native int 'bcl_d2u'(float64)": "call int64 [mscorlib]System.BitConverter::DoubleToInt64Bits(float64)\n  conv.i",
  "call float64 'bcl_fabs'(float64)": "call float64 [mscorlib]System.Math::Abs(float64)",
  "call float64 'bcl_floor'(float64)": "call float64 [mscorlib]System.Math::Floor(float64)",
  "call float64 'bcl_sqrt'(float64)": "call float64 [mscorlib]System.Math::Sqrt(float64)",
  "call float64 'bcl_sin'(float64)": "call float64 [mscorlib]System.Math::Sin(float64)",
}
todo = dict(safe);
if modern: todo.update(rep)
for k, v in todo.items():
    n = src.count(k); assert n >= 1, k; src = src.replace(k, v)
if modern:
    src = src.replace('.assembly extern mscorlib', '.assembly extern System.Runtime { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }\n.assembly extern System.Memory { .publickeytoken = (CC 7B 13 FF CD 2D DD 51 ) .ver 8:0:0:0 }\n.assembly extern mscorlib', 1)
open(sys.argv[2], 'w').write(src)
