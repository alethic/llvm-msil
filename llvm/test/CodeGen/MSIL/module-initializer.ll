; The module initializer, checked against the complete example of recon
; asmprinter.md 9 (ImplementationPlan.md M8.c, decision D12, plan 5.1): the
; program of that example (a global array, a table of string pointers, a
; zero-initialized counter, a function pointer to a static function and three
; string literals), with the IR global order the example assumes and with
; main calling sum directly (indirect calls are covered by call-indirect.ll
; and ldftn-calli.ll). The trailer is the example's, with the decisions that
; came after it: [mscorlib] references (D10), IR names for private globals
; ('.str', not '.L.str'; D11), 'ldc.i4' constants in any encoding, and
; function address fields (plan section 12, 'Phase-1 function addresses'). In
; order:
; - the P/Invoke declaration of puts (D14: "libc" on Linux, "ucrtbase" on
;   windows-msvc), after every method;
; - the image of the 72 initialized bytes (table +0, '.str' +20, '.str.1' +25,
;   names +32 at its 16-byte alignment, op +48, '.str.2' +56), with zeros in
;   the relocated slots, then the RVA field;
; - '<msil>data' and one address field per global, in layout order (hits, the
;   only zero-initialized global, is last both in IR order and in the layout),
;   then an empty line;
; - the address field '<msil>fn_sum' of sum, the only function whose address
;   is used (main calls sum and puts directly), then an empty line;
; - the .cctor: AllocHGlobal(76 + 16 - 1), align up to 16, store the base,
;   cpblk the image, initblk the 4 zero bytes at 72, store the address fields,
;   set '<msil>fn_sum' with 'ldftn' of sum, then the three relocations in site
;   order (names[0] = '.str' at 32, names[1] = '.str.1' at 40, op =
;   '<msil>fn_sum' at 48), then ret; 'ldftn' appears nowhere else; the zero
;   tail at 72 and the three sites are 8-aligned, so no line has an
;   'unaligned.' prefix (plan section 12, 'M8.c amended (alignment prefixes
;   in <Module>::.cctor)'; the CHECK-NEXT chain pins that);
; - then the generated entry point '<msil>Main' (M3), which calls main.
; The module assembles. main and sum are only checked for the instructions
; that touch globals and calls.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=pinvokeimpl --implicit-check-not=".field " \
; DEFINE:   --implicit-check-not=int64 --implicit-check-not=DisableRuntimeMarshalling --implicit-check-not="[System." \
; DEFINE:   --implicit-check-not=ldftn
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.lnx.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} --check-prefixes=CHECK,O2,LNX --input-file=%t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.win.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} --check-prefixes=CHECK,O2,WIN --input-file=%t.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O0,LNX --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O0,WIN --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "prog.c"

@table = global [5 x i32] [i32 5, i32 3, i32 9, i32 1, i32 7], align 16
@.str = private unnamed_addr constant [5 x i8] c"zero\00", align 1
@.str.1 = private unnamed_addr constant [4 x i8] c"one\00", align 1
@names = global [2 x ptr] [ptr @.str, ptr @.str.1], align 16
@op = global ptr @sum, align 8
@.str.2 = private unnamed_addr constant [16 x i8] c"hello from msil\00", align 1
@hits = global i32 0, align 4

; CHECK-LABEL: {{^}}.method assembly static int32 'sum'(native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}} // end of method 'sum'{{$}}
define internal i32 @sum(ptr %p, i32 %n) {
entry:
  %cmp4 = icmp sgt i32 %n, 0
  br i1 %cmp4, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:
  %wide.trip.count = zext nneg i32 %n to i64
  br label %for.body

for.cond.cleanup:
  %s.0.lcssa = phi i32 [ 0, %entry ], [ %add, %for.body ]
  ret i32 %s.0.lcssa

for.body:
  %indvars.iv = phi i64 [ 0, %for.body.preheader ], [ %indvars.iv.next, %for.body ]
  %s.05 = phi i32 [ 0, %for.body.preheader ], [ %add, %for.body ]
  %arrayidx = getelementptr inbounds nuw [4 x i8], ptr %p, i64 %indvars.iv
  %0 = load i32, ptr %arrayidx, align 4
  %add = add nsw i32 %0, %s.05
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond.not = icmp eq i64 %indvars.iv.next, %wide.trip.count
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}

; ++hits reads and writes through the field of hits (the address may be
; loaded twice or kept in a local). names[argc & 1] adds the scaled index to
; the field of names; the order of the index computation and the 'names' base
; is not pinned (the add is commutative), only that their sum is loaded and
; passed to puts. At -O0 the addresses may be loaded early and kept in locals,
; so only their presence is checked there.
; CHECK-LABEL: {{^}}.method public static int32 'main'(int32, native int) cil managed{{$}}
; O2:          {{^}}  ldsfld native int 'hits'{{$}}
; O2:          {{^}}  ldind.i4{{$}}
; O2:          {{^}}  stind.i4{{$}}
; O2-NEXT:     {{^}}  ldsfld native int '.str.2'{{$}}
; O2-NEXT:     {{^}}  call int32 'puts'(native int){{$}}
; O2-NEXT:     {{^}}  pop{{$}}
; O2:          {{^}}  ldsfld native int 'names'{{$}}
; O2:          {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldind.i{{$}}
; O2-NEXT:     {{^}}  call int32 'puts'(native int){{$}}
; O2-NEXT:     {{^}}  pop{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'table'{{$}}
; O2-NEXT:     {{^}}  ldc.i4.5{{$}}
; O2-NEXT:     {{^}}  call int32 'sum'(native int, int32){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  ldsfld native int 'hits'{{$}}
; O0-DAG:      {{^}}  ldsfld native int '.str.2'{{$}}
; O0-DAG:      {{^}}  ldsfld native int 'names'{{$}}
; O0-DAG:      {{^}}  ldsfld native int 'table'{{$}}
; O0-DAG:      {{^}}  call int32 'sum'(native int, int32){{$}}
; CHECK:       {{^}}} // end of method 'main'{{$}}
define i32 @main(i32 %argc, ptr %argv) {
entry:
  %0 = load i32, ptr @hits, align 4
  %inc = add nsw i32 %0, 1
  store i32 %inc, ptr @hits, align 4
  %call = tail call i32 @puts(ptr @.str.2)
  %and = and i32 %argc, 1
  %idxprom = zext nneg i32 %and to i64
  %arrayidx = getelementptr inbounds nuw [8 x i8], ptr @names, i64 %idxprom
  %1 = load ptr, ptr %arrayidx, align 8
  %call1 = tail call i32 @puts(ptr %1)
  %call2 = tail call i32 @sum(ptr @table, i32 5)
  ret i32 %call2
}

declare i32 @puts(ptr)

; CHECK-NOT:     {{^}}.method {{(public|assembly) static}}
; LNX:           {{^}}.method public hidebysig static pinvokeimpl("libc" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; WIN:           {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK-NOT:     {{^}}.method
; CHECK:         {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; CHECK:         {{^ *}}.pack 1{{( |$)}}
; CHECK:         {{^ *}}.size 72{{( |$)}}
; CHECK:         {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; CHECK-NEXT:    {{^}}  05 00 00 00 03 00 00 00 09 00 00 00 01 00 00 00{{( |$)}}
; CHECK-NEXT:    {{^}}  07 00 00 00 7A 65 72 6F 00 6F 6E 65 00 00 00 00{{( |$)}}
; CHECK-NEXT:    {{^}}  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00{{( |$)}}
; CHECK-NEXT:    {{^}}  00 00 00 00 00 00 00 00 68 65 6C 6C 6F 20 66 72{{( |$)}}
; CHECK-NEXT:    {{^}}  6F 6D 20 6D 73 69 6C 00 ){{( |$)}}
; CHECK:         {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CHECK:         {{^}}.field private static initonly native int '<msil>data'{{$}}
; CHECK:         {{^}}.field public static initonly native int 'table'{{( |$)}}
; CHECK:         {{^}}.field assembly static initonly native int '.str'{{( |$)}}
; CHECK:         {{^}}.field assembly static initonly native int '.str.1'{{( |$)}}
; CHECK:         {{^}}.field public static initonly native int 'names'{{( |$)}}
; CHECK:         {{^}}.field public static initonly native int 'op'{{( |$)}}
; CHECK:         {{^}}.field assembly static initonly native int '.str.2'{{( |$)}}
; CHECK:         {{^}}.field public static initonly native int 'hits'{{( |$)}}
; CHECK-EMPTY:
; CHECK-NEXT:    {{^}}.field private static initonly native int '<msil>fn_sum'{{$}}
; CHECK-EMPTY:
; CHECK-NEXT:    {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:    {{^\{$}}
; CHECK-NEXT:    {{^}}  .maxstack {{[3-9]}}{{$}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 91( |$)}}
; CHECK-NEXT:    {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:    {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; CHECK-NEXT:    {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; CHECK-NEXT:    {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:    {{^}}  and{{( |$)}}
; CHECK-NEXT:    {{^}}  stsfld native int '<msil>data'{{( |$)}}
;   copy the initialized image
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 72( |$)}}
; CHECK-NEXT:    {{^}}  cpblk{{( |$)}}
;   zero-fill [72, 76)
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 72( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; CHECK-NEXT:    {{^}}  initblk{{( |$)}}
;   the address fields
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  stsfld native int 'table'{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 20( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  stsfld native int '.str'{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 25( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  stsfld native int '.str.1'{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  stsfld native int 'names'{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  stsfld native int 'op'{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 56( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  stsfld native int '.str.2'{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 72( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  stsfld native int 'hits'{{( |$)}}
;   the function address field of sum
; CHECK-NEXT:    {{^}}  ldftn int32 'sum'(native int, int32){{( |$)}}
; CHECK-NEXT:    {{^}}  stsfld native int '<msil>fn_sum'{{( |$)}}
;   reloc @32: names[0] = .str
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsfld native int '.str'{{( |$)}}
; CHECK-NEXT:    {{^}}  stind.i{{( |$)}}
;   reloc @40: names[1] = .str.1
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 40( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsfld native int '.str.1'{{( |$)}}
; CHECK-NEXT:    {{^}}  stind.i{{( |$)}}
;   reloc @48: op = sum
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:    {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; CHECK-NEXT:    {{^}}  add{{( |$)}}
; CHECK-NEXT:    {{^}}  ldsfld native int '<msil>fn_sum'{{( |$)}}
; CHECK-NEXT:    {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:    {{^}}  ret{{( |$)}}
; CHECK-NEXT:    {{^}}} // end of method '.cctor'{{$}}
; CHECK:         {{^}}.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed{{$}}
; CHECK-NEXT:    {{^\{$}}
; CHECK-NEXT:    {{^}}  .entrypoint{{$}}
; CHECK:         {{^}}  call int32 'main'(int32, native int){{$}}
; CHECK:         {{^}}} // end of method '<msil>Main'{{$}}
; CHECK-NOT:     {{^}}.method
