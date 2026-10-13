// e11: identifier quoting. Method/field/local/param names that are dotted, start with a digit,
// contain '$', '-', '@', '?', spaces, non-ASCII, quotes/backslashes (escaped), or collide with
// ILAsm keywords/opcodes. Plus label spellings.
#include "common.il"
.assembly e11b_bom {}
.module e11b_bom.dll

.method static int32 'foo.bar'() cil managed { .maxstack 1 ldc.i4.1 ret }
.method static int32 '1digit'() cil managed { .maxstack 1 ldc.i4.2 ret }
.method static int32 'has$dollar'() cil managed { .maxstack 1 ldc.i4.3 ret }
.method static int32 '.str'() cil managed { .maxstack 1 ldc.i4.4 ret }
.method static int32 'llvm.memcpy.p0.p0.i64'() cil managed { .maxstack 1 ldc.i4.5 ret }
.method static int32 'add'() cil managed { .maxstack 1 ldc.i4.6 ret }          // opcode name
.method static int32 'int32'() cil managed { .maxstack 1 ldc.i4.7 ret }        // type keyword
.method static int32 'value'() cil managed { .maxstack 1 ldc.i4.8 ret }        // keyword
.method static int32 'method'() cil managed { .maxstack 1 ldc.i4 9 ret }
.method static int32 'native'() cil managed { .maxstack 1 ldc.i4 10 ret }
.method static int32 'a-b@c?d'() cil managed { .maxstack 1 ldc.i4 11 ret }
.method static int32 'with space'() cil managed { .maxstack 1 ldc.i4 12 ret }
.method static int32 'quote\'and\\back'() cil managed { .maxstack 1 ldc.i4 13 ret }
.method static int32 'ünïcödé'() cil managed { .maxstack 1 ldc.i4 14 ret }
.method static int32 '_ZN3foo3barEv'() cil managed { .maxstack 1 ldc.i4 15 ret }
.method static int32 'foo.cold.1'() cil managed { .maxstack 1 ldc.i4 16 ret }
.method static int32 '__func__.main'() cil managed { .maxstack 1 ldc.i4 17 ret }
// unquoted plain C identifiers that are legal ILAsm ids
.method static int32 plain_id() cil managed { .maxstack 1 ldc.i4 18 ret }
.method static int32 _leading_underscore() cil managed { .maxstack 1 ldc.i4 19 ret }
.method static int32 'main'() cil managed { .maxstack 1 ldc.i4 20 ret }

.field static int32 'g.field$1'
.field static int32 'int64'

// params/locals with awkward names; labels in several spellings
.method static int32 'labels'(int32 'value', int32 '1p') cil managed
{
  .maxstack 2
  .locals init (int32 'add', int32 'tmp$1')
  ldarg 'value'
  stloc 'add'
  ldarg '1p'
  stloc 'tmp$1'
  br BB0_1
BB0_1:
  br '$BB0_2'
'$BB0_2':
  br '.LBB0_3'
'.LBB0_3':
  br L$4
L$4:
  br 'BB.5'
'BB.5':
  ldloc 'add'
  ldloc 'tmp$1'
  add
  ret
}

.method public static int32 '__msil_start'() cil managed
{
  .entrypoint
  .maxstack 4
  ldstr "foo.bar"      call int32 'foo.bar'()      conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "1digit"       call int32 '1digit'()       conv.i8 ldc.i8 2  call void 'check.i8'(string, int64, int64)
  ldstr "has$dollar"   call int32 'has$dollar'()   conv.i8 ldc.i8 3  call void 'check.i8'(string, int64, int64)
  ldstr ".str"         call int32 '.str'()         conv.i8 ldc.i8 4  call void 'check.i8'(string, int64, int64)
  ldstr "llvm.memcpy"  call int32 'llvm.memcpy.p0.p0.i64'()  conv.i8 ldc.i8 5  call void 'check.i8'(string, int64, int64)
  ldstr "add"          call int32 'add'()          conv.i8 ldc.i8 6  call void 'check.i8'(string, int64, int64)
  ldstr "int32"        call int32 'int32'()        conv.i8 ldc.i8 7  call void 'check.i8'(string, int64, int64)
  ldstr "value"        call int32 'value'()        conv.i8 ldc.i8 8  call void 'check.i8'(string, int64, int64)
  ldstr "method"       call int32 'method'()       conv.i8 ldc.i8 9  call void 'check.i8'(string, int64, int64)
  ldstr "native"       call int32 'native'()       conv.i8 ldc.i8 10 call void 'check.i8'(string, int64, int64)
  ldstr "a-b@c?d"      call int32 'a-b@c?d'()      conv.i8 ldc.i8 11 call void 'check.i8'(string, int64, int64)
  ldstr "with space"   call int32 'with space'()   conv.i8 ldc.i8 12 call void 'check.i8'(string, int64, int64)
  ldstr "quote/back"   call int32 'quote\'and\\back'()  conv.i8 ldc.i8 13 call void 'check.i8'(string, int64, int64)
  ldstr "unicode"      call int32 'ünïcödé'()      conv.i8 ldc.i8 14 call void 'check.i8'(string, int64, int64)
  ldstr "mangled"      call int32 '_ZN3foo3barEv'() conv.i8 ldc.i8 15 call void 'check.i8'(string, int64, int64)
  ldstr "foo.cold.1"   call int32 'foo.cold.1'()   conv.i8 ldc.i8 16 call void 'check.i8'(string, int64, int64)
  ldstr "__func__.main" call int32 '__func__.main'() conv.i8 ldc.i8 17 call void 'check.i8'(string, int64, int64)
  ldstr "plain_id"     call int32 plain_id()       conv.i8 ldc.i8 18 call void 'check.i8'(string, int64, int64)
  ldstr "_leading"     call int32 _leading_underscore() conv.i8 ldc.i8 19 call void 'check.i8'(string, int64, int64)
  ldstr "main"         call int32 'main'()         conv.i8 ldc.i8 20 call void 'check.i8'(string, int64, int64)
  ldc.i4 21  stsfld int32 'g.field$1'
  ldc.i4 22  stsfld int32 'int64'
  ldstr "field g.field$1"  ldsfld int32 'g.field$1'  conv.i8 ldc.i8 21 call void 'check.i8'(string, int64, int64)
  ldstr "field int64"      ldsfld int32 'int64'      conv.i8 ldc.i8 22 call void 'check.i8'(string, int64, int64)
  ldstr "labels/params"    ldc.i4 30  ldc.i4 12  call int32 'labels'(int32, int32)  conv.i8 ldc.i8 42 call void 'check.i8'(string, int64, int64)
  // reflection: what name did the runtime record for the escaped/unicode methods?
  ldstr "reflected name of 'quote\\'and\\\\back' = "  call void [mscorlib]System.Console::Write(string)
  ldftn int32 'quote\'and\\back'()  pop
  call int32 'report'()
  ret
}
