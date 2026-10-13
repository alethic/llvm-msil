// common.il: test harness shared by experiments e03+ (pulled in with #include).
// Global (<Module>-level) field + methods; names quoted because they contain '.'.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }

.field static int32 'fails'
.field static int32 'passes'

.method static void 'check.i8'(string name, int64 got, int64 expected) cil managed
{
  .maxstack 4
  ldarg.1
  ldarg.2
  beq.s OK
  ldsfld int32 'fails'
  ldc.i4.1
  add
  stsfld int32 'fails'
  ldstr "FAIL "
  ldarg.0
  call string [mscorlib]System.String::Concat(string, string)
  call void [mscorlib]System.Console::Write(string)
  ldstr " got=0x"
  call void [mscorlib]System.Console::Write(string)
  ldarga.s got
  ldstr "X16"
  call instance string [mscorlib]System.Int64::ToString(string)
  call void [mscorlib]System.Console::Write(string)
  ldstr " expected=0x"
  call void [mscorlib]System.Console::Write(string)
  ldarga.s expected
  ldstr "X16"
  call instance string [mscorlib]System.Int64::ToString(string)
  call void [mscorlib]System.Console::WriteLine(string)
  ret
OK:
  ldsfld int32 'passes'
  ldc.i4.1
  add
  stsfld int32 'passes'
  ret
}

// bitwise float64 compare (so NaN payloads / -0.0 are checked exactly)
.method static void 'check.r8'(string name, float64 got, float64 expected) cil managed
{
  .maxstack 4
  ldarg.0
  ldarg.1
  call int64 [mscorlib]System.BitConverter::DoubleToInt64Bits(float64)
  ldarg.2
  call int64 [mscorlib]System.BitConverter::DoubleToInt64Bits(float64)
  call void 'check.i8'(string, int64, int64)
  ret
}

.method static int32 'report'() cil managed
{
  .maxstack 4
  ldstr "passes="
  call void [mscorlib]System.Console::Write(string)
  ldsfld int32 'passes'
  call void [mscorlib]System.Console::Write(int32)
  ldstr " fails="
  call void [mscorlib]System.Console::Write(string)
  ldsfld int32 'fails'
  call void [mscorlib]System.Console::WriteLine(int32)
  ldsfld int32 'fails'
  ret
}
