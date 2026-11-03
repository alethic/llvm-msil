; Constructors and named sections on msil64-unknown-dotnet (ImplementationPlan.md
; 5.18.5, the dotnet link-rule family, milestone P-1a; texts T-P6 to T-P8):
; the same rules in whole-program text and in CIL objects, at -O0 and -O2.
; - ok.ll compiles without a message: llvm.global_ctors entries and
;   .init_array[.N] slots run from <Module>::.cctor after the guard, sorted
;   by priority as on ELF (a plain slot and priority 65535 share the last
;   key, in input order), and no constructor gets arguments; data in a
;   C-identifier section ('mysec') has __start_/__stop_ symbols (start/stop
;   symbols are defined on dotnet as on ELF, LR1's definesStartStop).
; - errors.ll: each refused construct is reported once, with the module's
;   source file name as <file>:
;   - T-P6, a constructor with parameters (ELF passes argc, argv and envp;
;     dotnet passes nothing): "constructor '<f>' takes arguments, which
;     msil64-unknown-dotnet does not pass";
;   - T-P7, a null .init_array slot: "null constructor slot in '<section>'
;     of '<file>': msil64-unknown-dotnet calls every slot";
;   - T-P8, the sections dotnet never runs: "section '<name>' is not
;     supported for msil64-unknown-dotnet: <reason>", with the reason "the
;     portable target runs no destructor sections" for .fini_array and
;     .dtors, "the portable target has no pre-initialization stage" for
;     .preinit_array, and "the portable target runs constructors only from
;     .init_array" for .ctors and .CRT$XCU; the dotted priority forms
;     (.fini_array.101, .dtors.7, .preinit_array.5, .ctors.65535) are
;     refused with the same reasons as the bare names;
;   eleven errors in all, none of them the host triples' section texts.
; - tls.ll: .tdata and .tbss are not T-P8; they keep Phase 2's text
;   "thread-local storage is not supported yet: <s> in <file>" (until M31).
; llc exits with 1 for errors.ll and tls.ll and prints neither 'LLVM ERROR'
; nor a crash report.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{t} = -mtriple=msil64-unknown-dotnet -verify-machineinstrs
; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=warning: --implicit-check-not="global destructors are not supported" \
; DEFINE:   --implicit-check-not="is not a function address" --implicit-check-not="phase 2 CIL objects"

; ok.ll
; RUN: llc %{t} -asm-verbose=false < %t/ok.ll -o %t/ok.o2.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=OK --input-file=%t/ok.o2.il
; RUN: llc %{t} -O0 -asm-verbose=false < %t/ok.ll -o %t/ok.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=OK --input-file=%t/ok.o0.il
; RUN: llc %{t} -filetype=obj < %t/ok.ll -o %t/ok.o 2>&1 | count 0
; RUN: llc %{t} -O0 -filetype=obj < %t/ok.ll -o %t/ok.o0.o 2>&1 | count 0
; RUN: %if ilasm %{ %ilasm-verify -output=%t/ok.o2.dll %t/ok.o2.il %}

; errors.ll
; RUN: not llc %{t} < %t/errors.ll -o /dev/null 2>&1 | %{diag} --check-prefix=ERR
; RUN: not llc %{t} -O0 < %t/errors.ll -o /dev/null 2>&1 | %{diag} --check-prefix=ERR
; RUN: not llc %{t} -filetype=obj < %t/errors.ll -o %t/errors.o 2>&1 | %{diag} --check-prefix=ERR
; RUN: not llc %{t} -O0 -filetype=obj < %t/errors.ll -o %t/errors.o0.o 2>&1 | %{diag} --check-prefix=ERR
; RUN: not llc %{t} < %t/errors.ll -o /dev/null 2>&1 | grep -c "error: " | FileCheck %s --check-prefix=NERR
; RUN: not llc %{t} -filetype=obj < %t/errors.ll -o %t/errors.o 2>&1 | grep -c "error: " \
; RUN:   | FileCheck %s --check-prefix=NERR
; NERR: {{^}}11{{$}}

; tls.ll
; RUN: not llc %{t} < %t/tls.ll -o /dev/null 2>&1 | %{diag} --check-prefix=TLS \
; RUN:   --implicit-check-not="is not supported for msil64-unknown-dotnet"
; RUN: not llc %{t} -O0 < %t/tls.ll -o /dev/null 2>&1 | %{diag} --check-prefix=TLS \
; RUN:   --implicit-check-not="is not supported for msil64-unknown-dotnet"

;--- ok.ll
source_filename = "portable-sections.c"

@counter = global i32 0, align 4
@slot_plain = internal global ptr @slot_a, section ".init_array", align 8
@slot_101 = internal global ptr @slot_b, section ".init_array.101", align 8
@e1 = global i32 1, section "mysec", align 4
@e2 = global i32 2, section "mysec", align 4
@__start_mysec = external global [0 x i32]
@__stop_mysec = external global [0 x i32]
@llvm.used = appending global [2 x ptr] [ptr @slot_plain, ptr @slot_101], section "llvm.metadata"
@llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @ctor_default, ptr null }, { i32, ptr, ptr } { i32 101, ptr @ctor_101, ptr null }]

; OK-LABEL: {{^}}.method public static native int 'mysec_bytes'() cil managed{{$}}
; OK-DAG:   {{^}}  ldsfld native int '__stop_mysec'{{$}}
; OK-DAG:   {{^}}  ldsfld native int '__start_mysec'{{$}}
; OK:       {{^}}} // end of method 'mysec_bytes'{{$}}
; OK-DAG:   {{^}}.field {{.*}}static {{.*}}native int '__start_mysec'{{$}}
; OK-DAG:   {{^}}.field {{.*}}static {{.*}}native int '__stop_mysec'{{$}}
; OK-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; OK:       {{^}}  call uint8[] [mscorlib]System.BitConverter::GetBytes(int32){{$}}
; OK:       {{^}}  call void 'slot_b'(){{$}}
; OK-NEXT:  {{^}}  call void 'ctor_101'(){{$}}
; OK-NEXT:  {{^}}  call void 'slot_a'(){{$}}
; OK-NEXT:  {{^}}  call void 'ctor_default'(){{$}}
; OK-NEXT:  {{^}}  ret{{$}}
; OK:       {{^}}} // end of method '.cctor'{{$}}
define i64 @mysec_bytes() {
  %b = ptrtoint ptr @__stop_mysec to i64
  %a = ptrtoint ptr @__start_mysec to i64
  %d = sub i64 %b, %a
  ret i64 %d
}

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

define internal void @ctor_default() {
  call void @step(i32 3)
  ret void
}

define internal void @ctor_101() {
  call void @step(i32 4)
  ret void
}

define i32 @main() {
  %c = load i32, ptr @counter, align 4
  ret i32 %c
}

;--- errors.ll
source_filename = "portable-sections.c"

@counter = global i32 0, align 4
@good_slot = internal global ptr @init_fn, section ".init_array", align 8
@null_slot = internal global ptr null, section ".init_array", align 8
@fini_slot = internal global ptr @init_fn, section ".fini_array", align 8
@dtors_slot = internal global ptr @init_fn, section ".dtors", align 8
@preinit_slot = internal global ptr @init_fn, section ".preinit_array", align 8
@ctors_slot = internal global ptr @init_fn, section ".ctors", align 8
@crt_slot = internal global ptr @init_fn, section ".CRT$XCU", align 8
@fini_101_slot = internal global ptr @init_fn, section ".fini_array.101", align 8
@dtors_7_slot = internal global ptr @init_fn, section ".dtors.7", align 8
@preinit_5_slot = internal global ptr @init_fn, section ".preinit_array.5", align 8
@ctors_65535_slot = internal global ptr @init_fn, section ".ctors.65535", align 8
@llvm.used = appending global [11 x ptr] [ptr @good_slot, ptr @null_slot, ptr @fini_slot, ptr @dtors_slot, ptr @preinit_slot, ptr @ctors_slot, ptr @crt_slot, ptr @fini_101_slot, ptr @dtors_7_slot, ptr @preinit_5_slot, ptr @ctors_65535_slot], section "llvm.metadata"
@llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init_fn, ptr null }, { i32, ptr, ptr } { i32 101, ptr @ctor_args, ptr null }]

; ERR-DAG: {{^}}{{.*}}error: {{.*}}constructor 'ctor_args' takes arguments, which msil64-unknown-dotnet does not pass{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}null constructor slot in '.init_array' of 'portable-sections.c': msil64-unknown-dotnet calls every slot{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}section '.fini_array' is not supported for msil64-unknown-dotnet: the portable target runs no destructor sections{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}section '.dtors' is not supported for msil64-unknown-dotnet: the portable target runs no destructor sections{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}section '.preinit_array' is not supported for msil64-unknown-dotnet: the portable target has no pre-initialization stage{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}section '.ctors' is not supported for msil64-unknown-dotnet: the portable target runs constructors only from .init_array{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}section '.CRT$XCU' is not supported for msil64-unknown-dotnet: the portable target runs constructors only from .init_array{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}section '.fini_array.101' is not supported for msil64-unknown-dotnet: the portable target runs no destructor sections{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}section '.dtors.7' is not supported for msil64-unknown-dotnet: the portable target runs no destructor sections{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}section '.preinit_array.5' is not supported for msil64-unknown-dotnet: the portable target has no pre-initialization stage{{$}}
; ERR-DAG: {{^}}{{.*}}error: {{.*}}section '.ctors.65535' is not supported for msil64-unknown-dotnet: the portable target runs constructors only from .init_array{{$}}

define internal void @init_fn() {
  store i32 1, ptr @counter, align 4
  ret void
}

define internal void @ctor_args(i32 %argc, ptr %argv, ptr %envp) {
  store i32 %argc, ptr @counter, align 4
  ret void
}

define i32 @main() {
  %c = load i32, ptr @counter, align 4
  ret i32 %c
}

;--- tls.ll
source_filename = "portable-tls.c"

@tls_data = global i32 3, section ".tdata", align 4
@tls_bss = global i32 0, section ".tbss", align 4

; TLS-DAG: {{^}}{{.*}}error: {{.*}}thread-local storage is not supported yet: {{.+}} in portable-tls.c{{$}}
; TLS-DAG: {{^}}{{.*}}error: {{.*}}thread-local storage is not supported yet: {{.+}} in portable-tls.c{{$}}

define i32 @main() {
  %a = load i32, ptr @tls_data, align 4
  %b = load i32, ptr @tls_bss, align 4
  %s = add i32 %a, %b
  ret i32 %s
}
