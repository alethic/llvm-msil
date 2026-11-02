.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly utf { }
.module utf.exe
.method static int32 conv(uint8* p, int32 n) {
  .maxstack 4
  .locals init (int32 r)
  .try {
    ldc.i4.0
    ldc.i4.1
    newobj instance void [mscorlib]System.Text.UTF8Encoding::.ctor(bool, bool)
    ldarg.0
    ldarg.1
    callvirt instance string [mscorlib]System.Text.Encoding::GetString(uint8*, int32)
    callvirt instance int32 [mscorlib]System.String::get_Length()
    stloc.0
    leave.s done
  } filter {
    isinst [mscorlib]System.Text.DecoderFallbackException
    ldnull
    cgt.un
    endfilter
  } {
    pop
    ldc.i4.m1
    stloc.0
    leave.s done
  }
done:
  ldloc.0
  ret
}
.method static int32 main() {
  .entrypoint
  .maxstack 3
  .locals init (int64 v)
  // valid: C3 A9 C3 A9 = 2 chars
  ldc.i8 0xA9C3A9C3
  stloc.0
  ldloca.s v
  conv.u
  ldc.i4.4
  call int32 conv(uint8*, int32)
  call void [mscorlib]System.Console::WriteLine(int32)
  // invalid: FF FE
  ldc.i8 0xFEFF
  stloc.0
  ldloca.s v
  conv.u
  ldc.i4.2
  call int32 conv(uint8*, int32)
  call void [mscorlib]System.Console::WriteLine(int32)
  ldc.i4.s 42
  ret
}
