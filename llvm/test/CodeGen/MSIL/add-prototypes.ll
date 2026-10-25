; Declarations without a prototype get the type of their call sites
; (ImplementationPlan.md M9.b and M1.d; Design.md 2.1; recon isel.md 2.5 and
; passes-frame.md 2), ported from
; llvm/test/CodeGen/WebAssembly/add-prototypes.ll. clang marks every
; declaration without a prototype that has no body with "no-prototype" for
; MSIL triples (M1.d) and gives it the type 'T (...)'. MSILAddMissingPrototypes,
; a copy of WebAssemblyAddMissingPrototypes, replaces each such declaration by
; one with the function type of its first call, keeps its other attributes
; (so "msil-import-module" still makes it an import, D14) and drops
; "no-prototype"; a declaration that is never used becomes 'T ()'. Uses
; that are not calls (stored in data, returned, compared, passed) refer to the
; new declaration. The fixed declaration is an ordinary Phase-1 import: its
; calls use its new signature, its address is its address field
; '<msil>fn_<name>', which <Module>::.cctor sets once with 'ldftn' of it
; (section 12 'Phase-1 function addresses', 2026-10-15, M9), and it gets a
; P/Invoke declaration with that signature (D14: "mylib" from the attribute;
; puts from the C library, "libc" on Linux and "ucrtbase" on Windows).
;
; - sret.ll: an sret parameter before the '...' is allowed and kept; the
;   result needs a value type at the P/Invoke boundary, which Phase 1 reports
;   as an error at the end of the module (no crash).
; - sret-mismatch.ll: a declaration with an sret parameter before the '...'
;   whose first call does not fit it (a call type fits when it has a pointer
;   parameter 0 and returns void): no argument at all, an i32 first argument,
;   and a call that returns i32 (WebAssembly's pass would give the declaration
;   the call type with 'sret' on it, which the IR verifier rejects: 'Attribute
;   after last parameter!', 'Attribute 'sret(...)' applied to incompatible
;   type!', 'Invalid struct return type!'). Each declaration is one error, in
;   module order, however many calls it has: 'call of
;   prototype-less function '<name>' does not match its sret parameter'. The
;   declaration then gets its fixed-parameter type 'void (ptr sret)', so the
;   only later errors are the ones it gets anyway (here the P/Invoke error of
;   sret.ll; without "msil-import-module" it would be the undefined-function
;   error), never a crash.
; - knr-import.ll: a K&R call with a variadic call type
;   ('call i32 (i32, double, ...) @f(...)'). clang emits fixed-arity K&R calls
;   for every MSIL triple since M9's CGCall.cpp change (section 12, 2026-10-15;
;   clang/test/CodeGen/MSIL/no-proto-call.c); this shape comes from IR made by
;   native x86-64 SysV clang (x86_64-unknown-linux-gnu,
;   X86_64TargetCodeGenInfo::isNoProtoCallVariadic), or written by hand, and
;   fed to llc with an MSIL triple. The declaration still becomes a
;   fixed-arity import, called with its fixed signature (recon registry-tm.md
;   6 'Must handle as fixed-arity'; Design.md 2.1), not a variadic callee.
; - conflict.ll: two calls with different types; the first one found wins and
;   the pass warns once 'prototype-less function used with conflicting
;   signatures: <name>'; the other call is a castable mismatch (ptr vs i64)
;   that MSILFixFunctionBitcasts wraps, so the module compiles.
; - addr-only.ll: a declaration that is never called directly but whose
;   address is used (stored to memory, in a global initializer, passed as an
;   argument, called through a pointer) has no call type to take its
;   signature from. WebAssembly's 'T ()' would give a '()' P/Invoke and a '()'
;   ldftn, called by a calli with another signature (garbage at run time), so
;   each such declaration is one error, in module order: 'the signature of
;   prototype-less function '<name>' cannot be derived: its address is used,
;   but it is never called directly; declare it with a prototype'. A
;   declaration with no use at all (func_not_called) and one that is called
;   directly and whose address is also taken (foo) are not errors (main.ll).
; - dead-users.ll: the address of a declaration only in the body of a
;   definition that MSILPrepareIR diagnoses and deletes (a variadic definition
;   and one with inline assembly). Deleting the body leaves the ConstantExprs
;   on the declaration (ptrtoint, getelementptr) as dead constant users, which
;   are not address uses (section 12 'M9.b amended (prototype-less functions
;   whose address is used)': 'dead constant users do not count'). The only
;   errors are MSILPrepareIR's, none 'cannot be derived'.
; - badproto.ll: malformed "no-prototype" declarations, which clang never
;   emits: one that is not variadic, and one with a parameter before the '...'
;   that is not sret. WebAssembly's pass stops at the first with
;   report_fatal_error; the MSIL copy reports each as an error, in module
;   order, and goes on.

; DEFINE: %{il} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning: --implicit-check-not="{{^ +ldftn }}"
; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error:
; RUN: split-file %s %t

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-add-missing-prototypes < %t/main.ll -o - \
; RUN:   | FileCheck %s --check-prefix=IR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=msil-add-missing-prototypes < %t/main.ll -o - \
; RUN:   | FileCheck %s --check-prefix=IR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{il} --check-prefixes=CHECK,LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{il} --check-prefixes=CHECK,WIN
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{il} --check-prefixes=CHECK,LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll 2>&1 \
; RUN:   | %{il} --check-prefixes=CHECK,WIN
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/main.ll -o %t/main.o2.il \
; RUN:   && %ilasm-verify -output=%t/main.o2.dll %t/main.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/main.ll -o %t/main.o0.il \
; RUN:   && %ilasm-verify -output=%t/main.o0.dll %t/main.o0.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-add-missing-prototypes < %t/sret.ll -o - \
; RUN:   | FileCheck %s --check-prefix=SRET-IR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/sret.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SRET --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/sret.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SRET --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"

; DEFINE: %{srdiag} = FileCheck %s --check-prefix=SRETMIS --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=Broken \
; DEFINE:   --implicit-check-not="does not match its sret parameter"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/sret-mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{srdiag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/sret-mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{srdiag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/sret-mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{srdiag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/sret-mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{srdiag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-add-missing-prototypes < %t/sret-mismatch.ll \
; RUN:   -o - 2>/dev/null | FileCheck %s --check-prefix=SRETMIS-IR

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/knr-import.ll 2>&1 \
; RUN:   | %{il} --check-prefix=KNR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/knr-import.ll 2>&1 \
; RUN:   | %{il} --check-prefix=KNR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/knr-import.ll 2>&1 \
; RUN:   | %{il} --check-prefix=KNR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/knr-import.ll -o - \
; RUN:   | FileCheck %s --check-prefix=KNR-IR
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/knr-import.ll -o %t/knr-import.il \
; RUN:   && %ilasm-verify -output=%t/knr-import.dll %t/knr-import.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/conflict.ll \
; RUN:   -o %t/conflict.lnx.il 2> %t/conflict.lnx.err
; RUN: FileCheck %s --check-prefix=CONFLICT-WARN --input-file=%t/conflict.lnx.err \
; RUN:   --implicit-check-not=warning: --implicit-check-not=error:
; RUN: FileCheck %s --check-prefix=CONFLICT --input-file=%t/conflict.lnx.il --implicit-check-not=int64
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/conflict.ll \
; RUN:   -o %t/conflict.win.il 2> %t/conflict.win.err
; RUN: FileCheck %s --check-prefix=CONFLICT-WARN --input-file=%t/conflict.win.err \
; RUN:   --implicit-check-not=warning: --implicit-check-not=error:
; RUN: FileCheck %s --check-prefix=CONFLICT --input-file=%t/conflict.win.il --implicit-check-not=int64
; RUN: %if ilasm %{ %ilasm-verify -output=%t/conflict.dll %t/conflict.lnx.il %}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/addr-only.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ADDR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/addr-only.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ADDR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/addr-only.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ADDR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/addr-only.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ADDR

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/dead-users.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DEADCU
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/dead-users.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DEADCU
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/dead-users.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DEADCU
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/dead-users.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DEADCU

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/badproto.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BADPROTO
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/badproto.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BADPROTO
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/badproto.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BADPROTO
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/badproto.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BADPROTO

;--- main.ll
source_filename = "add-prototypes.c"

; IR: @foo_addr = global ptr @foo, align 8
@foo_addr = global ptr @foo, align 8

; IR-LABEL: define void @call_foo(
; IR-NEXT:    %call = call i64 @foo(i32 42)
; CHECK-LABEL: .method {{.*}}'call_foo'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 42{{$}}
; CHECK-NEXT:  {{^}}  call native int 'foo'(int32){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_foo(i32 %a) {
  %call = call i64 @foo(i32 42)
  ret void
}

; IR-LABEL: define i64 @call_foo_ptr(
; IR-NEXT:    %call = call i64 @foo(i32 43)
; CHECK-LABEL: .method {{.*}}'call_foo_ptr'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 43{{$}}
; CHECK-NEXT:  {{^}}  call native int 'foo'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_foo_ptr(i32 %a) {
  %call = call i64 (i32) @foo(i32 43)
  ret i64 %call
}

; IR-LABEL: define ptr @to_intptr_inst()
; IR-NEXT:    ret ptr @foo
; CHECK-LABEL: .method {{.*}}'to_intptr_inst'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_foo'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @to_intptr_inst() {
  ret ptr @foo
}

; IR-LABEL: define i8 @null_compare()
; IR:         %cmp = icmp eq ptr @foo, null
; IR:         br i1 %cmp, label %if.then, label %if.end
; CHECK-LABEL: .method {{.*}}'null_compare'(
; CHECK:       {{^}}  ldsfld native int '<msil>fn_foo'{{$}}
; CHECK:       {{^}}} // end of method 'null_compare'{{$}}
define i8 @null_compare() {
  %cmp = icmp eq ptr @foo, null
  br i1 %cmp, label %if.then, label %if.end
if.then:
  ret i8 0
if.end:
  ret i8 1
}

; IR-LABEL: define void @as_paramater()
; IR-NEXT:    call void @func_param(ptr @foo)
; CHECK-LABEL: .method {{.*}}'as_paramater'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_foo'{{$}}
; CHECK-NEXT:  {{^}}  call void 'func_param'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @as_paramater() {
  call void @func_param(ptr @foo)
  ret void
}

; puts without a prototype, called with the fixed-arity call type that clang
; emits for every MSIL triple (knr-import.ll has the variadic K&R shape).
; IR-LABEL: define i32 @call_puts(
; IR-NEXT:    %r = call i32 @puts(ptr %s)
; CHECK-LABEL: .method {{.*}}'call_puts'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_puts(ptr %s) {
  %r = call i32 @puts(ptr %s)
  ret i32 %r
}

define void @func_param(ptr %p) {
  ret void
}

; The replaced declarations, at the end of the function list in their
; original order, with "msil-import-module" kept and "no-prototype" gone.
; IR:     declare void @func_not_called() #[[IMPORT:[0-9]+]]
; IR:     declare i64 @foo(i32) #[[IMPORT]]
; IR:     declare i32 @puts(ptr){{$}}
; IR:     attributes #[[IMPORT]] = { "msil-import-module"="mylib" }
; IR-NOT: no-prototype
declare void @func_not_called(...) #1
declare i64 @foo(...) #1
declare i32 @puts(...) #2

attributes #1 = { "no-prototype" "msil-import-module"="mylib" }
attributes #2 = { "no-prototype" }

; P/Invoke declarations with the fixed signatures; the declaration that is
; never called and never used gets none.
; CHECK:      {{^}}// ---- undefined external functions: P/Invoke ----{{$}}
; CHECK-DAG:  {{^}}.method public hidebysig static pinvokeimpl("mylib" nomangle cdecl) native int 'foo'(int32) cil managed preservesig {}{{$}}
; LNX-DAG:    {{^}}.method public hidebysig static pinvokeimpl("libc" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; WIN-DAG:    {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK-NOT:  func_not_called
; The fixed import has one address field, after the data segment's fields;
; the .cctor sets it with the ldftn of the fixed signature, after foo_addr's
; field and before the relocation, which stores it into foo_addr.
; CHECK:      {{^}}.field public static initonly native int 'foo_addr'{{$}}
; CHECK-EMPTY:
; CHECK-NEXT: {{^}}.field private static initonly native int '<msil>fn_foo'{{$}}
; CHECK-EMPTY:
; CHECK-NEXT: {{^}}.method {{.*}} .cctor() cil managed{{$}}
; CHECK:      {{^}}  stsfld native int 'foo_addr'{{$}}
; CHECK-NEXT: {{^}}  ldftn native int 'foo'(int32){{$}}
; CHECK-NEXT: {{^}}  stsfld native int '<msil>fn_foo'{{$}}
; CHECK-NEXT: {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT: {{^}}  ldsfld native int '<msil>fn_foo'{{$}}
; CHECK-NEXT: {{^}}  stind.i{{$}}
; CHECK-NEXT: {{^}}  ret{{$}}
; CHECK-NOT:  func_not_called

;--- sret.ll
; An sret parameter in a declaration without a prototype is kept.
source_filename = "sret.c"

%struct.foo = type { i32, i32 }

; SRET-IR-LABEL: define void @sret_param()
; SRET-IR:         call void @make_struct_foo(ptr sret(%struct.foo) %foo)
; SRET-IR:       declare void @make_struct_foo(ptr sret(%struct.foo)) #{{[0-9]+}}
; SRET-IR-NOT:   no-prototype
; SRET:          error: {{.*}}make_struct_foo
define void @sret_param() {
  %foo = alloca %struct.foo, align 4
  call void @make_struct_foo(ptr sret(%struct.foo) %foo)
  ret void
}

declare void @make_struct_foo(ptr sret(%struct.foo), ...) #1

attributes #1 = { "no-prototype" "msil-import-module"="mylib" }

;--- sret-mismatch.ll
; Each declaration has an sret parameter before the '...' and a first call
; that does not fit it: f1 is called with no argument (twice: still one
; error), f2 with an i32, f3 with a pointer but an i32 result. One error per
; declaration, in module order (the pass runs before the passes that report
; the later errors); then each declaration is 'void (ptr sret)' (SRETMIS-IR).
; The later errors of these imports (P/Invoke, as in sret.ll) are not checked.
; SRETMIS:    {{^}}error: call of prototype-less function 'f1' does not match its sret parameter{{$}}
; SRETMIS:    {{^}}error: call of prototype-less function 'f2' does not match its sret parameter{{$}}
; SRETMIS:    {{^}}error: call of prototype-less function 'f3' does not match its sret parameter{{$}}
; SRETMIS-IR: declare void @f1(ptr sret(%struct.foo)){{( #[0-9]+)?$}}
; SRETMIS-IR: declare void @f2(ptr sret(%struct.foo)){{( #[0-9]+)?$}}
; SRETMIS-IR: declare void @f3(ptr sret(%struct.foo)){{( #[0-9]+)?$}}
source_filename = "sret-mismatch.c"

%struct.foo = type { i32, i32 }

declare void @f1(ptr sret(%struct.foo), ...) #0
declare void @f2(ptr sret(%struct.foo), ...) #0
declare void @f3(ptr sret(%struct.foo), ...) #0

define void @call_f1() {
  call void @f1()
  ret void
}

define void @call_f2() {
  call void @f2(i32 1)
  ret void
}

define i32 @call_f3(ptr %p) {
  %r = call i32 @f3(ptr %p)
  ret i32 %r
}

define void @call_f1_again() {
  call void @f1()
  ret void
}

attributes #0 = { "no-prototype" "msil-import-module"="mylib" }

;--- knr-import.ll
; K&R calls of imports without a prototype, with the variadic call types that
; native x86-64 SysV clang emits (IR made by clang for x86_64-unknown-linux-gnu,
; or written by hand, compiled with an MSIL triple; clang for MSIL triples
; emits fixed-arity calls): fixed-arity imports, no 'variadic' diagnostic, and
; calls (here or through a wrapper) with the fixed signature. KNR-IR checks
; the declarations once both IR fix-up passes have run.
source_filename = "knr-import.c"

; KNR-IR:     declare i32 @knr_import(i32, double) #{{[0-9]+}}
; KNR-IR:     declare i32 @puts(ptr){{$}}
; KNR-LABEL: .method {{.*}}'use_knr_import'(
; KNR-SAME:  int32, float64) cil managed{{$}}
; KNR:       {{^}}  ldarg.0{{$}}
; KNR-NEXT:  {{^}}  ldarg.1{{$}}
; KNR-NEXT:  {{^}}  call int32 'knr_import{{(_bitcast(\.[0-9]+)?)?}}'(int32, float64){{$}}
; KNR-NEXT:  {{^}}  ret{{$}}
define i32 @use_knr_import(i32 %a, double %b) {
  %r = call i32 (i32, double, ...) @knr_import(i32 %a, double %b)
  ret i32 %r
}

; KNR-LABEL: .method {{.*}}'use_knr_puts'(
; KNR-SAME:  native int) cil managed{{$}}
; KNR:       {{^}}  ldarg.0{{$}}
; KNR-NEXT:  {{^}}  call int32 'puts{{(_bitcast(\.[0-9]+)?)?}}'(native int){{$}}
; KNR-NEXT:  {{^}}  ret{{$}}
define i32 @use_knr_puts(ptr %s) {
  %r = call i32 (ptr, ...) @puts(ptr %s)
  ret i32 %r
}

; KNR:       {{^}}// ---- undefined external functions: P/Invoke ----{{$}}
; KNR-DAG:   {{^}}.method public hidebysig static pinvokeimpl("mylib" nomangle cdecl) int32 'knr_import'(int32, float64) cil managed preservesig {}{{$}}
; KNR-DAG:   {{^}}.method public hidebysig static pinvokeimpl("{{libc|ucrtbase}}" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
declare i32 @knr_import(...) #1
declare i32 @puts(...) #2

attributes #1 = { "no-prototype" "msil-import-module"="mylib" }
attributes #2 = { "no-prototype" }

;--- conflict.ll
; Two call types for one declaration without a prototype: one warning, and
; both calls compile (the pointer and the i64 argument are both native int).
source_filename = "conflict.c"

; CONFLICT-WARN: warning: {{.*}}prototype-less function used with conflicting signatures{{.*}}conflict

; CONFLICT-LABEL: .method {{.*}}'with_ptr'(
; CONFLICT:       {{^}}  ldarg.0{{$}}
; CONFLICT-NEXT:  {{^}}  call int32 'conflict{{(_bitcast(\.[0-9]+)?)?}}'(native int){{$}}
; CONFLICT-NEXT:  {{^}}  ret{{$}}
define i32 @with_ptr(ptr %p) {
  %r = call i32 @conflict(ptr %p)
  ret i32 %r
}

; CONFLICT-LABEL: .method {{.*}}'with_i64'(
; CONFLICT:       {{^}}  ldarg.0{{$}}
; CONFLICT-NEXT:  {{^}}  call int32 'conflict{{(_bitcast(\.[0-9]+)?)?}}'(native int){{$}}
; CONFLICT-NEXT:  {{^}}  ret{{$}}
define i32 @with_i64(i64 %x) {
  %r = call i32 @conflict(i64 %x)
  ret i32 %r
}

; CONFLICT: {{^}}.method public hidebysig static pinvokeimpl("mylib" nomangle cdecl) int32 'conflict'(native int) cil managed preservesig {}{{$}}
declare i32 @conflict(...) #1

attributes #1 = { "no-prototype" "msil-import-module"="mylib" }

;--- addr-only.ll
; putchar's address is stored to memory, loaded back and called through the
; pointer; puts's address is only in a global initializer. Neither is called
; directly, so neither has a call type to take its signature from: one error
; for each, in module order, and nothing else.
; ADDR:      {{^}}error: the signature of prototype-less function 'putchar' cannot be derived: its address is used, but it is never called directly; declare it with a prototype{{$}}
; ADDR:      {{^}}error: the signature of prototype-less function 'puts' cannot be derived: its address is used, but it is never called directly; declare it with a prototype{{$}}
source_filename = "addr-only.c"

@tbl = global ptr @puts, align 8

define i32 @main() {
  %slot = alloca ptr, align 8
  store volatile ptr @putchar, ptr %slot, align 8
  %f = load volatile ptr, ptr %slot, align 8
  %r = call i32 %f(i32 111)
  ret i32 0
}

declare i32 @putchar(...) #0
declare i32 @puts(...) #0

attributes #0 = { "no-prototype" }

;--- dead-users.ll
; puts's address is only in the body of the variadic vdef, putchar's only in
; the operand of asmdef's inline assembly. MSILPrepareIR reports both
; definitions and deletes their bodies, which leaves the ptrtoint and
; getelementptr ConstantExprs on puts and putchar with no users. Those dead
; constant users must not count as address uses: the only errors are
; MSILPrepareIR's two, and neither declaration gets 'the signature of
; prototype-less function ... cannot be derived'.
; DEADCU:    {{^}}error: {{.*}}in function vdef i64 (i32, ...): variadic function definitions are not supported for MSIL yet{{$}}
; DEADCU:    {{^}}error: {{.*}}in function asmdef i64 (i32): inline assembly is not supported for MSIL{{$}}
source_filename = "dead-users.c"

define i64 @vdef(i32 %n, ...) {
  ret i64 ptrtoint (ptr @puts to i64)
}

define i64 @asmdef(i32 %n) {
  %v = call i64 asm sideeffect "", "=r,r"(i64 ptrtoint (ptr getelementptr (i8, ptr @putchar, i64 1) to i64))
  ret i64 %v
}

define i32 @main() {
  ret i32 0
}

declare i32 @puts(...) #0
declare i32 @putchar(...) #0

attributes #0 = { "no-prototype" }

;--- badproto.ll
; Hand-written "no-prototype" declarations that clang never emits: np1 is not
; variadic, np2 has a parameter before the '...' that is not sret. Each is one
; error, in module order. np2 is never called: a call of it would add
; LowerCall's 'calls to variadic functions' error.
; BADPROTO:  {{^}}error: functions with 'no-prototype' attribute must take varargs: np1{{$}}
; BADPROTO:  {{^}}error: functions with 'no-prototype' attribute should not have params: np2{{$}}
source_filename = "badproto.c"

declare i32 @np1(i32) #0
declare i32 @np2(i32, ...) #0

define i32 @use(i32 %x) {
  %a = call i32 @np1(i32 %x)
  ret i32 %a
}

attributes #0 = { "no-prototype" "msil-import-module"="m" }
