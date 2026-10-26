; Named sections in whole-program output (ImplementationPlan.md 5.8.10, 5.9.9,
; 5.9.10, M20; D26 as amended by the section 12 row of 2026-10-21 for M20;
; Q5 and Design.md 4.1: '-S output is a one-object self-link'). Each global
; is a segment named after its section, laid out by cli::layoutImage as a
; one-object link lays it out:
; - groups.ll: .data/.rodata/.bss segments are units by themselves; every
;   other name is a group placed at its first segment's position; on ELF
;   triples 'reg$a' and 'reg$b' are groups of their own, on COFF triples they
;   join 'reg', sorted by full name; a zero-initialized member of a mixed
;   group is zero bytes inside the image. Address fields are stored in
;   image-offset order.
; - ctors-*.ll: every 8-byte slot of an .init_array* section (ELF) or a
;   .CRT$XC* section (COFF) is a constructor, called from .cctor after the
;   relocations together with llvm.global_ctors, in 5.9.9's order: by key
;   (ELF: the priority, .init_array -> 65536 and INIT priority 65535 -> 65536;
;   COFF: the section name clang gives the priority, byte-wise), and for
;   equal keys the slots, in (segment, offset) order, before the INIT
;   entries. ELF's .init_array.N sections are laid out by priority.
; - startstop*.ll: on linux-gnu a referenced external or extern_weak
;   __start_X/__stop_X naming an output section with a C-identifier name is
;   an address field at its start or end; windows-msvc keeps D26's error, as
;   link.exe and lld-link leave such symbols undefined; a __start_ of no
;   section keeps the error on linux-gnu too.
; - texts-*.ll: destructor tables, .ctors/.dtors and other .CRT$X* sections,
;   TLS-named sections and a constructor slot without a function get 5.9.9's
;   texts, with the module's source file name as <file>; M12's 'is not
;   supported for MSIL yet (phase 2 CIL objects)' is gone.
; - preinit.ll: under ELF rules .preinit_array is an unsupported section
;   (section 12 row 'Plan 5.9.9 and M20.a (1) amended (`.preinit_array`)'),
;   so a used slot there on linux-gnu is 5.9.9's 'section '.preinit_array'
;   in <file> is not supported', never a silent drop of its function.
; Every module without an error compiles silently and assembles.

; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: split-file %s %t

; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/groups.ll -o %t/groups.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=GRP,GRP-LNX --input-file=%t/groups.lnx.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/groups.ll -o %t/groups.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=GRP,GRP-WIN --input-file=%t/groups.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/groups.ll -o %t/groups.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=GRP,GRP-WIN --input-file=%t/groups.win.o0.il

; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/ctors-lnx.ll -o %t/ctors.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=CTORS --input-file=%t/ctors.lnx.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/ctors-lnx.ll -o %t/ctors.lnx.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=CTORS --input-file=%t/ctors.lnx.o0.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/ctors-win.ll -o %t/ctors.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=CTORS --input-file=%t/ctors.win.il

; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/startstop.ll -o %t/ss.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=SS --input-file=%t/ss.lnx.il
; RUN: FileCheck %s --check-prefix=SSF --input-file=%t/ss.lnx.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/startstop-weak.ll -o %t/ssw.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=SS --implicit-check-not="<msil>undef_" --input-file=%t/ssw.lnx.il
; RUN: FileCheck %s --check-prefix=SSF --input-file=%t/ssw.lnx.il
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/startstop.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SS-WIN %{nocrash} --implicit-check-not=warning:
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/nosuch.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NOSUCH %{nocrash} --implicit-check-not=warning:

; DEFINE: %{old} = --implicit-check-not="phase 2 CIL objects" --implicit-check-not=warning:
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/texts-lnx.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=TXT-LNX %{nocrash} %{old}
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/texts-lnx.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=TXT-LNX %{nocrash} %{old}
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/texts-win.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=TXT-WIN %{nocrash} %{old}
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/preinit.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PRE %{nocrash} %{old} --implicit-check-not=error:
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/preinit.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PRE %{nocrash} %{old} --implicit-check-not=error:

; RUN: %if ilasm %{ %ilasm-verify -output=%t/groups.lnx.dll %t/groups.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/groups.win.dll %t/groups.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ctors.lnx.dll %t/ctors.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ctors.win.dll %t/ctors.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ss.lnx.dll %t/ss.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ssw.lnx.dll %t/ssw.lnx.il %}

;--- groups.ll
; ELF:  d1 0, reg$b 4, mysec {m1 8, m2 12, mz 16}, reg$a 20, reg 24, d2 28;
; COFF: d1 0, reg {r0 4, ra 8, rb 12}, mysec {m1 16, m2 20, mz 24}, d2 28;
; then z 32 (the zero part). 32 initialized bytes, TotalSize 36.
source_filename = "sections-whole-program.c"

@d1 = global i32 1, align 4
@rb = global i32 11, section "reg$b", align 4
@m1 = global i32 17, section "mysec", align 4
@ra = global i32 10, section "reg$a", align 4
@m2 = global i32 18, section "mysec", align 4
@r0 = global i32 16, section "reg", align 4
@mz = global i32 0, section "mysec", align 4
@d2 = global i32 2, align 4
@z = global i32 0, align 4

define i32 @sum() {
  %a = load i32, ptr @ra, align 4
  %b = load i32, ptr @m2, align 4
  %c = add i32 %a, %b
  ret i32 %c
}

; GRP:          {{^ *}}.size 32{{( |$)}}
; GRP:          {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; GRP-LNX-NEXT: {{^}}  01 00 00 00 0B 00 00 00 11 00 00 00 12 00 00 00{{( |$)}}
; GRP-LNX-NEXT: {{^}}  00 00 00 00 0A 00 00 00 10 00 00 00 02 00 00 00 ){{( |$)}}
; GRP-WIN-NEXT: {{^}}  01 00 00 00 10 00 00 00 0A 00 00 00 0B 00 00 00{{( |$)}}
; GRP-WIN-NEXT: {{^}}  11 00 00 00 12 00 00 00 00 00 00 00 02 00 00 00 ){{( |$)}}
; GRP-DAG:      {{^}}.field public static initonly native int 'mz'{{$}}
; GRP-DAG:      {{^}}.field public static initonly native int 'rb'{{$}}
; GRP-LABEL:    {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; GRP:          {{^}}  {{ldc\.i4(\.s)? 51( |$)}}
; GRP:          {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; GRP-NEXT:     {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; GRP-NEXT:     {{^}}  cpblk{{( |$)}}
; GRP:          {{^}}  initblk{{( |$)}}
; GRP-NEXT:     {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-NEXT:     {{^}}  stsfld native int 'd1'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; GRP-LNX-NEXT: {{^}}  add{{( |$)}}
; GRP-LNX-NEXT: {{^}}  stsfld native int 'rb'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; GRP-LNX-NEXT: {{^}}  add{{( |$)}}
; GRP-LNX-NEXT: {{^}}  stsfld native int 'm1'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  {{ldc\.i4(\.s)? 12( |$)}}
; GRP-LNX-NEXT: {{^}}  add{{( |$)}}
; GRP-LNX-NEXT: {{^}}  stsfld native int 'm2'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; GRP-LNX-NEXT: {{^}}  add{{( |$)}}
; GRP-LNX-NEXT: {{^}}  stsfld native int 'mz'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  {{ldc\.i4(\.s)? 20( |$)}}
; GRP-LNX-NEXT: {{^}}  add{{( |$)}}
; GRP-LNX-NEXT: {{^}}  stsfld native int 'ra'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-LNX-NEXT: {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; GRP-LNX-NEXT: {{^}}  add{{( |$)}}
; GRP-LNX-NEXT: {{^}}  stsfld native int 'r0'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; GRP-WIN-NEXT: {{^}}  add{{( |$)}}
; GRP-WIN-NEXT: {{^}}  stsfld native int 'r0'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; GRP-WIN-NEXT: {{^}}  add{{( |$)}}
; GRP-WIN-NEXT: {{^}}  stsfld native int 'ra'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  {{ldc\.i4(\.s)? 12( |$)}}
; GRP-WIN-NEXT: {{^}}  add{{( |$)}}
; GRP-WIN-NEXT: {{^}}  stsfld native int 'rb'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; GRP-WIN-NEXT: {{^}}  add{{( |$)}}
; GRP-WIN-NEXT: {{^}}  stsfld native int 'm1'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  {{ldc\.i4(\.s)? 20( |$)}}
; GRP-WIN-NEXT: {{^}}  add{{( |$)}}
; GRP-WIN-NEXT: {{^}}  stsfld native int 'm2'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-WIN-NEXT: {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; GRP-WIN-NEXT: {{^}}  add{{( |$)}}
; GRP-WIN-NEXT: {{^}}  stsfld native int 'mz'{{( |$)}}
; GRP-NEXT:     {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-NEXT:     {{^}}  {{ldc\.i4(\.s)? 28( |$)}}
; GRP-NEXT:     {{^}}  add{{( |$)}}
; GRP-NEXT:     {{^}}  stsfld native int 'd2'{{( |$)}}
; GRP-NEXT:     {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; GRP-NEXT:     {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; GRP-NEXT:     {{^}}  add{{( |$)}}
; GRP-NEXT:     {{^}}  stsfld native int 'z'{{( |$)}}
; GRP-NEXT:     {{^}}  ret{{( |$)}}

;--- ctors-lnx.ll
; Layout: the .init_array group by priority: slot_101 0, slot_pair 8 (two
; slots), slot_plain 24; counter 32 (zero part). Run order: slot_b (key
; 101, a slot), ctor_101 (101), ctor_200 (200, its result popped), slot_c
; and slot_d (300), slot_a (65536, a slot) and ctor_default (65535 -> 65536).
source_filename = "sections-whole-program.c"

@counter = global i32 0, align 4
@slot_plain = internal global ptr @slot_a, section ".init_array", align 8
@slot_101 = internal global ptr @slot_b, section ".init_array.101", align 8
@slot_pair = internal global [2 x ptr] [ptr @slot_c, ptr @slot_d], section ".init_array.300", align 8
@llvm.used = appending global [3 x ptr] [ptr @slot_plain, ptr @slot_101, ptr @slot_pair], section "llvm.metadata"
@llvm.global_ctors = appending global [3 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @ctor_default, ptr null }, { i32, ptr, ptr } { i32 101, ptr @ctor_101, ptr null }, { i32, ptr, ptr } { i32 200, ptr @ctor_200, ptr null }]

define internal void @step(i32 %d) {
  %c = load i32, ptr @counter, align 4
  %m = mul i32 %c, 10
  %n = add i32 %m, %d
  store i32 %n, ptr @counter, align 4
  ret void
}

define internal void @slot_a() {
  call void @step(i32 1)
  ret void
}

define internal void @slot_b() {
  call void @step(i32 2)
  ret void
}

define internal void @slot_c() {
  call void @step(i32 3)
  ret void
}

define internal void @slot_d() {
  call void @step(i32 4)
  ret void
}

define internal void @ctor_default() {
  call void @step(i32 5)
  ret void
}

define internal void @ctor_101() {
  call void @step(i32 6)
  ret void
}

define internal i32 @ctor_200() {
  call void @step(i32 7)
  ret i32 0
}

define i32 @main() {
  %c = load i32, ptr @counter, align 4
  ret i32 %c
}

; CTORS-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CTORS:       {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; CTORS-NEXT:  {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CTORS-NEXT:  {{^}}  cpblk{{( |$)}}
; CTORS:       {{^}}  initblk{{( |$)}}
; CTORS-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CTORS-NEXT:  {{^}}  stsfld native int 'slot_101'{{( |$)}}
; CTORS-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CTORS-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; CTORS-NEXT:  {{^}}  add{{( |$)}}
; CTORS-NEXT:  {{^}}  stsfld native int 'slot_pair'{{( |$)}}
; CTORS-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CTORS-NEXT:  {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; CTORS-NEXT:  {{^}}  add{{( |$)}}
; CTORS-NEXT:  {{^}}  stsfld native int '{{slot_plain|slot_u}}'{{( |$)}}
; CTORS-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; CTORS-NEXT:  {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; CTORS-NEXT:  {{^}}  add{{( |$)}}
; CTORS-NEXT:  {{^}}  stsfld native int 'counter'{{( |$)}}
; The slots hold the function addresses (relocations), then the calls.
; CTORS:       {{^}}  ldsfld native int '<msil>fn_slot_d'{{( |$)}}
; CTORS-NEXT:  {{^}}  stind.i{{( |$)}}
; CTORS:       {{^}}  call void 'slot_b'(){{( |$)}}
; CTORS-NEXT:  {{^}}  call void 'ctor_101'(){{( |$)}}
; CTORS-NEXT:  {{^}}  call int32 'ctor_200'(){{( |$)}}
; CTORS-NEXT:  {{^}}  pop{{( |$)}}
; CTORS-NEXT:  {{^}}  call void 'slot_c'(){{( |$)}}
; CTORS-NEXT:  {{^}}  call void 'slot_d'(){{( |$)}}
; CTORS-NEXT:  {{^}}  call void 'slot_a'(){{( |$)}}
; CTORS-NEXT:  {{^}}  call void 'ctor_default'(){{( |$)}}
; CTORS-NEXT:  {{^}}  ret{{( |$)}}
; CTORS-NEXT:  {{^}}} // end of method '.cctor'{{$}}

;--- ctors-win.ll
; The same program with MSVC's sections: .CRT$XCU (slot_a), .CRT$XCA00101
; (slot_b), .CRT$XCC00300 (slot_c, slot_d); the INIT keys are .CRT$XCU
; (65535), .CRT$XCA00101 (101) and .CRT$XCC (200). The group .CRT is sorted
; by full name, so the layout and the run order equal ctors-lnx.ll's.
source_filename = "sections-whole-program.c"

@counter = global i32 0, align 4
@slot_u = internal constant ptr @slot_a, section ".CRT$XCU", align 8
@slot_101 = internal constant ptr @slot_b, section ".CRT$XCA00101", align 8
@slot_pair = internal constant [2 x ptr] [ptr @slot_c, ptr @slot_d], section ".CRT$XCC00300", align 8
@llvm.used = appending global [3 x ptr] [ptr @slot_u, ptr @slot_101, ptr @slot_pair], section "llvm.metadata"
@llvm.global_ctors = appending global [3 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @ctor_default, ptr null }, { i32, ptr, ptr } { i32 101, ptr @ctor_101, ptr null }, { i32, ptr, ptr } { i32 200, ptr @ctor_200, ptr null }]

define internal void @step(i32 %d) {
  %c = load i32, ptr @counter, align 4
  %m = mul i32 %c, 10
  %n = add i32 %m, %d
  store i32 %n, ptr @counter, align 4
  ret void
}

define internal void @slot_a() {
  call void @step(i32 1)
  ret void
}

define internal void @slot_b() {
  call void @step(i32 2)
  ret void
}

define internal void @slot_c() {
  call void @step(i32 3)
  ret void
}

define internal void @slot_d() {
  call void @step(i32 4)
  ret void
}

define internal void @ctor_default() {
  call void @step(i32 5)
  ret void
}

define internal void @ctor_101() {
  call void @step(i32 6)
  ret void
}

define internal i32 @ctor_200() {
  call void @step(i32 7)
  ret i32 0
}

define i32 @main() {
  %c = load i32, ptr @counter, align 4
  ret i32 %c
}

;--- startstop.ll
; before 0, mysec {e1 4, e2 8}; 12 bytes. __start_mysec is data + 4,
; __stop_mysec data + 12.
source_filename = "sections-whole-program.c"

@before = global i32 7, align 4
@e1 = global i32 1, section "mysec", align 4
@e2 = global i32 2, section "mysec", align 4
@__start_mysec = external global [0 x i32]
@__stop_mysec = external global [0 x i32]

; SSF-LABEL: {{^}}.method public static native int 'mysec_bytes'() cil managed{{$}}
; SSF-DAG:   {{^}}  ldsfld native int '__stop_mysec'{{$}}
; SSF-DAG:   {{^}}  ldsfld native int '__start_mysec'{{$}}
; SSF:       {{^}}} // end of method 'mysec_bytes'{{$}}
; SSF-DAG:   {{^}}.field {{.*}}static {{.*}}native int '__start_mysec'{{$}}
; SSF-DAG:   {{^}}.field {{.*}}static {{.*}}native int '__stop_mysec'{{$}}
; SSF-DAG:   {{^}}  stsfld native int 'e1'{{( |$)}}
; SSF-DAG:   {{^}}  stsfld native int '__start_mysec'{{( |$)}}
define i64 @mysec_bytes() {
  %b = ptrtoint ptr @__stop_mysec to i64
  %a = ptrtoint ptr @__start_mysec to i64
  %d = sub i64 %b, %a
  ret i64 %d
}

; The address fields in image-offset order: e1 and __start_mysec at 4 (in
; either order), e2 at 8, __stop_mysec at 12.
; SS-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; SS:       {{^}}  stsfld native int 'before'{{( |$)}}
; SS-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; SS-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; SS-NEXT:  {{^}}  add{{( |$)}}
; SS-NEXT:  {{^}}  stsfld native int '{{e1|__start_mysec}}'{{( |$)}}
; SS-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; SS-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )4( |$)}}
; SS-NEXT:  {{^}}  add{{( |$)}}
; SS-NEXT:  {{^}}  stsfld native int '{{e1|__start_mysec}}'{{( |$)}}
; SS-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; SS-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; SS-NEXT:  {{^}}  add{{( |$)}}
; SS-NEXT:  {{^}}  stsfld native int 'e2'{{( |$)}}
; SS-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; SS-NEXT:  {{^}}  {{ldc\.i4(\.s)? 12( |$)}}
; SS-NEXT:  {{^}}  add{{( |$)}}
; SS-NEXT:  {{^}}  stsfld native int '__stop_mysec'{{( |$)}}
; SS-NEXT:  {{^}}  ret{{( |$)}}

; SS-WIN-DAG: {{^}}error: external global variable '__start_mysec' is not supported (phase 2 linker){{$}}
; SS-WIN-DAG: {{^}}error: external global variable '__stop_mysec' is not supported (phase 2 linker){{$}}

;--- startstop-weak.ll
; The same with extern_weak declarations (C: extern char __start_mysec[]
; __attribute__((weak))): defined by the section, so not M12's null.
source_filename = "sections-whole-program.c"

@before = global i32 7, align 4
@e1 = global i32 1, section "mysec", align 4
@e2 = global i32 2, section "mysec", align 4
@__start_mysec = extern_weak global [0 x i32]
@__stop_mysec = extern_weak global [0 x i32]

define i64 @mysec_bytes() {
  %b = ptrtoint ptr @__stop_mysec to i64
  %a = ptrtoint ptr @__start_mysec to i64
  %d = sub i64 %b, %a
  ret i64 %d
}

;--- nosuch.ll
; No section 'nosuch': D26's error on linux-gnu too.
source_filename = "sections-whole-program.c"

@e1 = global i32 1, section "mysec", align 4
@__start_nosuch = external global [0 x i32]

define ptr @first() {
  ret ptr @__start_nosuch
}

; NOSUCH: {{^}}error: external global variable '__start_nosuch' is not supported (phase 2 linker){{$}}

;--- texts-lnx.ll
source_filename = "sections-whole-program.c"

@counter = global i32 1, align 4
@fini = internal global ptr @fin, section ".fini_array", align 8
@fini_prio = internal global ptr @fin, section ".fini_array.101", align 8
@old_ctor = internal global ptr @fin, section ".ctors", align 8
@old_dtor = internal global ptr @fin, section ".dtors.5", align 8
@bad_slot = internal global [2 x ptr] [ptr @fin, ptr @counter], section ".init_array", align 8
@tls_named = global i32 1, section ".tdata", align 4
@llvm.used = appending global [5 x ptr] [ptr @fini, ptr @fini_prio, ptr @old_ctor, ptr @old_dtor, ptr @bad_slot], section "llvm.metadata"

define internal void @fin() {
  ret void
}

define i32 @main() {
  %v = load i32, ptr @tls_named, align 4
  ret i32 %v
}

; TXT-LNX-DAG: {{^}}error: {{.*}}global destructors are not supported yet: {{.+}} in sections-whole-program.c{{$}}
; TXT-LNX-DAG: {{^}}error: {{.*}}global destructors are not supported yet: {{.+}} in sections-whole-program.c{{$}}
; TXT-LNX-DAG: {{^}}error: {{.*}}section '.ctors' in sections-whole-program.c is not supported{{$}}
; TXT-LNX-DAG: {{^}}error: {{.*}}section '.dtors.5' in sections-whole-program.c is not supported{{$}}
; TXT-LNX-DAG: {{^}}error: {{.*}}constructor table entry at .init_array+8 in sections-whole-program.c is not a function address{{$}}
; TXT-LNX-DAG: {{^}}error: {{.*}}thread-local storage is not supported yet: {{.+}} in sections-whole-program.c{{$}}

;--- texts-win.ll
source_filename = "sections-whole-program.c"

@counter = global i32 1, align 4
@term = internal constant ptr @fin, section ".CRT$XTU", align 8
@crt_init = internal constant ptr @fin, section ".CRT$XIA", align 8
@crt_lib = internal constant ptr @fin, section ".CRT$XLB", align 8
@crt_pre = internal constant ptr @fin, section ".CRT$XPA", align 8
@bad_slot = internal constant [2 x ptr] [ptr @fin, ptr @counter], section ".CRT$XCU", align 8
@tls_named = global i32 1, section ".tdata", align 4
@llvm.used = appending global [5 x ptr] [ptr @term, ptr @crt_init, ptr @crt_lib, ptr @crt_pre, ptr @bad_slot], section "llvm.metadata"

define internal void @fin() {
  ret void
}

define i32 @main() {
  %v = load i32, ptr @tls_named, align 4
  ret i32 %v
}

; TXT-WIN-DAG: {{^}}error: {{.*}}global destructors are not supported yet: {{.+}} in sections-whole-program.c{{$}}
; TXT-WIN-DAG: {{^}}error: {{.*}}section '.CRT$XIA' in sections-whole-program.c is not supported{{$}}
; TXT-WIN-DAG: {{^}}error: {{.*}}section '.CRT$XLB' in sections-whole-program.c is not supported{{$}}
; TXT-WIN-DAG: {{^}}error: {{.*}}section '.CRT$XPA' in sections-whole-program.c is not supported{{$}}
; TXT-WIN-DAG: {{^}}error: {{.*}}constructor table entry at .CRT$XCU+8 in sections-whole-program.c is not a function address{{$}}
; TXT-WIN-DAG: {{^}}error: {{.*}}thread-local storage is not supported yet: {{.+}} in sections-whole-program.c{{$}}

;--- preinit.ll
; A function registered in .preinit_array (C: __attribute__((section(
; ".preinit_array"), used)) static void (*pre)(void) = early;), which ld.so
; would run before every .init_array entry.
source_filename = "sections-whole-program.c"

@counter = global i32 0, align 4
@pre = internal global ptr @early, section ".preinit_array", align 8
@llvm.used = appending global [1 x ptr] [ptr @pre], section "llvm.metadata"

define internal void @early() {
  store i32 42, ptr @counter, align 4
  ret void
}

define i32 @main() {
  %c = load i32, ptr @counter, align 4
  ret i32 %c
}

; PRE: {{^}}error: {{.*}}section '.preinit_array' in sections-whole-program.c is not supported{{$}}
