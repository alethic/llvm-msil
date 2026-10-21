.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly main { .ver 0:0:0:0 }
.module main.exe
.module extern mod2.netmodule
.file mod2.netmodule
.class extern public Lib { .file mod2.netmodule }
.method private static int32 Main(string[] a) cil managed
{
  .entrypoint
  .maxstack 2
  call int32 [.module mod2.netmodule]Lib::F()
  call int32 [.module mod2.netmodule]::'G'(int32)
  ret
}
