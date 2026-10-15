; llvm.global_ctors (ImplementationPlan.md M8.c, Design.md 4.1, decision D12;
; recon asmprinter.md 7.3): the constructors run at the end of the .cctor,
; after the segment is filled, every address field is stored and every
; relocation is applied, each as 'call void '<ctor>'()', in priority order
; (lower first; constructors of equal priority keep their order in the list,
; as AsmPrinter::preprocessXXStructorList sorts them stably), then 'ret'. The
; third field of an entry (the associated data) does not matter, and
; llvm.global_ctors itself is not data (no field, no bytes). A module whose
; only module initializer work is a constructor still gets a .cctor that calls
; it and does nothing else: no data segment ('<msil>data', AllocHGlobal), no
; image and no field. A constructor that returns a value (C allows
; '__attribute__((constructor)) int f(void)') is called with its own
; signature, and the .cctor pops the result ('call int32 ...' then 'pop').
; A module with an empty llvm.global_ctors and no globals gets no .cctor at
; all. Both triples, -O2 and -O0, and every module assembles.
; (llvm.global_dtors is an error of MSILPrepareIR, unsupported-ir-errors.ll.)
;
; Errors (exit code 1, once, no crash): a constructor with parameters, which
; the .cctor has no values for ("global constructor '<name>' takes arguments,
; which MSIL does not pass"), and an llvm.global_ctors entry that is not a
; function ("a global constructor that is not a function is not supported
; for MSIL").

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".field " --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=global_ctors
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/ctors.ll \
; RUN:   -o %t/ctors.lnx.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/ctors.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/ctors.ll \
; RUN:   -o %t/ctors.win.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/ctors.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/ctors.ll \
; RUN:   -o %t/ctors.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/ctors.lnx.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ctors.lnx.dll %t/ctors.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ctors.win.dll %t/ctors.win.il %}

; DEFINE: %{nodata} = --implicit-check-not="<msil>image" --implicit-check-not="'<msil>data'" \
; DEFINE:   --implicit-check-not=AllocHGlobal --implicit-check-not=".field " --implicit-check-not=cpblk \
; DEFINE:   --implicit-check-not=initblk --implicit-check-not=global_ctors --implicit-check-not=pinvokeimpl
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/ctor-only.ll \
; RUN:   -o %t/ctor-only.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=ONLY %{nodata} --input-file=%t/ctor-only.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/ctor-only.ll \
; RUN:   -o %t/ctor-only.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=ONLY %{nodata} --input-file=%t/ctor-only.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ctor-only.dll %t/ctor-only.win.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/ctor-results.ll \
; RUN:   -o %t/ctor-results.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=RESULTS %{nodata} --input-file=%t/ctor-results.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/ctor-results.ll \
; RUN:   -o %t/ctor-results.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=RESULTS %{nodata} --input-file=%t/ctor-results.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/ctor-results.ll \
; RUN:   -o %t/ctor-results.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=RESULTS %{nodata} --input-file=%t/ctor-results.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ctor-results.lnx.dll %t/ctor-results.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ctor-results.win.dll %t/ctor-results.win.il %}

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/ctor-args.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ARGS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/ctor-args.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ARGS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/ctor-args.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ARGS
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/ctor-not-fn.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=NOTFN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/ctor-not-fn.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=NOTFN

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/no-ctors.ll \
; RUN:   -o %t/no-ctors.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONE --implicit-check-not=.cctor --implicit-check-not=".field " \
; RUN:   --implicit-check-not="<msil>" --input-file=%t/no-ctors.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/no-ctors.dll %t/no-ctors.il %}

;--- ctors.ll
; first +0 and ptr +8 (a relocation to order) are initialized; order +16 is
; zero-initialized.
source_filename = "global-ctors.c"

@first = global i32 7, align 4
@ptr = global ptr @order, align 8
@order = global i32 0, align 4

@llvm.global_ctors = appending global [5 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 200, ptr @c200, ptr null },
  { i32, ptr, ptr } { i32 65535, ptr @cdefault, ptr null },
  { i32, ptr, ptr } { i32 101, ptr @c101, ptr @order },
  { i32, ptr, ptr } { i32 65535, ptr @cdefault2, ptr null },
  { i32, ptr, ptr } { i32 101, ptr @c101b, ptr null }]

; order = order * 10 + 2
; CHECK-LABEL: {{^}}.method assembly static void 'c200'() cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int 'order'{{$}}
; CHECK:       {{^}}  stind.i4{{$}}
; CHECK:       {{^}}} // end of method 'c200'{{$}}
define internal void @c200() {
  %v = load i32, ptr @order, align 4
  %m = mul i32 %v, 10
  %a = add i32 %m, 2
  store i32 %a, ptr @order, align 4
  ret void
}

; CHECK-LABEL: {{^}}.method assembly static void 'cdefault'() cil managed{{$}}
define internal void @cdefault() {
  %v = load i32, ptr @order, align 4
  %m = mul i32 %v, 10
  %a = add i32 %m, 3
  store i32 %a, ptr @order, align 4
  ret void
}

; CHECK-LABEL: {{^}}.method assembly static void 'c101'() cil managed{{$}}
define internal void @c101() {
  %v = load i32, ptr @order, align 4
  %m = mul i32 %v, 10
  %a = add i32 %m, 1
  store i32 %a, ptr @order, align 4
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'cdefault2'() cil managed{{$}}
define void @cdefault2() {
  %v = load i32, ptr @order, align 4
  %m = mul i32 %v, 10
  %a = add i32 %m, 4
  store i32 %a, ptr @order, align 4
  ret void
}

; CHECK-LABEL: {{^}}.method assembly static void 'c101b'() cil managed{{$}}
; CHECK:       {{^}}} // end of method 'c101b'{{$}}
define internal void @c101b() {
  %v = load i32, ptr @order, align 4
  %m = mul i32 %v, 10
  %a = add i32 %m, 5
  store i32 %a, ptr @order, align 4
  ret void
}

; CHECK-NOT:     {{^}}.method
; CHECK:        {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; CHECK:        {{^ *}}.pack 1{{( |$)}}
; CHECK:        {{^ *}}.size 16{{( |$)}}
; CHECK:        {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; CHECK-NEXT:   {{^}}  07 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ){{( |$)}}
; CHECK:        {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; CHECK:        {{^}}.field private static initonly native int '<msil>data'{{$}}
; CHECK:        {{^}}.field public static initonly native int 'first'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'ptr'{{( |$)}}
; CHECK:        {{^}}.field public static initonly native int 'order'{{( |$)}}
; CHECK:        {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:   {{^\{$}}
; CHECK-NEXT:   {{^}}  .maxstack {{[3-9]}}{{$}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 35( |$)}}
; CHECK-NEXT:   {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:   {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; CHECK-NEXT:   {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; CHECK-NEXT:   {{^}}  conv.i{{( |$)}}
; CHECK-NEXT:   {{^}}  and{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  cpblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; CHECK-NEXT:   {{^}}  initblk{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'first'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'ptr'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  stsfld native int 'order'{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CHECK-NEXT:   {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; CHECK-NEXT:   {{^}}  add{{( |$)}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'order'{{( |$)}}
; CHECK-NEXT:   {{^}}  stind.i{{( |$)}}
; CHECK-NEXT:   {{^}}  call void 'c101'(){{( |$)}}
; CHECK-NEXT:   {{^}}  call void 'c101b'(){{( |$)}}
; CHECK-NEXT:   {{^}}  call void 'c200'(){{( |$)}}
; CHECK-NEXT:   {{^}}  call void 'cdefault'(){{( |$)}}
; CHECK-NEXT:   {{^}}  call void 'cdefault2'(){{( |$)}}
; CHECK-NEXT:   {{^}}  ret{{( |$)}}
; CHECK-NEXT:   {{^}}} // end of method '.cctor'{{$}}
; CHECK-NOT:     {{^}}.method

;--- ctor-only.ll
; No global variable: the .cctor only calls the constructor.
source_filename = "ctor-only.c"

@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init, ptr null }]

; ONLY:      {{^}}.method assembly static void 'init'() cil managed{{$}}
; ONLY:      {{^}}} // end of method 'init'{{$}}
; ONLY-NOT:  {{^}}.method
; ONLY:      {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; ONLY-NEXT: {{^\{$}}
; ONLY-NEXT: {{^}}  .maxstack 0{{$}}
; ONLY-NEXT: {{^}}  call void 'init'(){{( |$)}}
; ONLY-NEXT: {{^}}  ret{{( |$)}}
; ONLY-NEXT: {{^}}} // end of method '.cctor'{{$}}
; ONLY-NOT:  {{^}}.method
define internal void @init() {
  ret void
}

;--- no-ctors.ll
; An empty constructor list and no global variable: nothing to initialize.
source_filename = "no-ctors.c"

@llvm.global_ctors = appending global [0 x { i32, ptr, ptr }] zeroinitializer

; NONE:      {{^}}.method public static int32 'f'() cil managed{{$}}
; NONE:      {{^}}} // end of method 'f'{{$}}
; NONE-NOT:  {{^}}.method
define i32 @f() {
  ret i32 1
}

;--- ctor-results.ll
; int c1(void) (priority 65535), static double c2(void) (200) and static void
; c3(void) (300) as constructors, and no global variable: the .cctor calls
; c2, c3 and c1 and pops the two results.
source_filename = "ctor-results.c"

@llvm.global_ctors = appending global [3 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 65535, ptr @c1, ptr null },
  { i32, ptr, ptr } { i32 200, ptr @c2, ptr null },
  { i32, ptr, ptr } { i32 300, ptr @c3, ptr null }]

; RESULTS-LABEL: {{^}}.method public static int32 'c1'() cil managed{{$}}
; RESULTS-LABEL: {{^}}.method assembly static float64 'c2'() cil managed{{$}}
; RESULTS-LABEL: {{^}}.method assembly static void 'c3'() cil managed{{$}}
; RESULTS:       {{^}}} // end of method 'c3'{{$}}
; RESULTS-NOT:   {{^}}.method
; RESULTS:       {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; RESULTS-NEXT:  {{^\{$}}
; RESULTS-NEXT:  {{^}}  .maxstack 1{{$}}
; RESULTS-NEXT:  {{^}}  call float64 'c2'(){{( |$)}}
; RESULTS-NEXT:  {{^}}  pop{{( |$)}}
; RESULTS-NEXT:  {{^}}  call void 'c3'(){{( |$)}}
; RESULTS-NEXT:  {{^}}  call int32 'c1'(){{( |$)}}
; RESULTS-NEXT:  {{^}}  pop{{( |$)}}
; RESULTS-NEXT:  {{^}}  ret{{( |$)}}
; RESULTS-NEXT:  {{^}}} // end of method '.cctor'{{$}}
; RESULTS-NOT:   {{^}}.method
define i32 @c1() {
  ret i32 42
}

define internal double @c2() {
  ret double 1.500000e+00
}

define internal void @c3() {
  ret void
}

;--- ctor-args.ll
; __attribute__((constructor)) static void init(int argc, char **argv)
source_filename = "ctor-args.c"

; ARGS: {{^}}error: {{.*}}global constructor 'init' takes arguments, which MSIL does not pass{{$}}

@seen = global i32 0, align 4
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init, ptr null }]

define internal void @init(i32 %argc, ptr %argv) {
  store i32 %argc, ptr @seen, align 4
  ret void
}

;--- ctor-not-fn.ll
; A constructor entry that is the address of a global variable.
source_filename = "ctor-not-fn.c"

; NOTFN: {{^}}error: {{.*}}a global constructor that is not a function is not supported for MSIL{{$}}

@g = global i32 1, align 4
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @g, ptr null }]
