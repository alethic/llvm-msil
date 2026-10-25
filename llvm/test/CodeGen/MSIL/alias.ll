; Aliases in whole-program mode (ImplementationPlan.md M12.a (1a), decision
; D26 as amended by the section 12 row of 2026-10-21 for M12; recon
; p2/backlog.md 2.1 and judge-c-fidelity.md F5). -S and llc compile one
; module as the whole program, so MSILPrepareIR lowers an alias whose aliasee
; object is a defined function (function value type, offset 0) or a global
; variable (any other value type, any constant offset): every use of the alias
; (code, initializers, llvm.global_ctors, other aliases) becomes its aliasee
; expression, and the alias leaves llvm.used and llvm.compiler.used and is
; erased. A function alias with non-local linkage (external, weak, weak_odr,
; dllexport, hidden) first gets a forwarding method under its own name, with
; the alias's function type, linkage, visibility, DLL storage class and
; unnamed_addr, whose body is one plain call of the aliasee object (never a
; 'jmp', which ILVerify rejects) returning its result; the forwarder is
; public, nothing calls it, and an entry point named main that is an alias
; keeps working ('<msil>Main' calls the forwarder 'main'). A linkonce_odr
; alias's forwarder is linkonce_odr and unreferenced, so M12.a (2) drops it:
; only external, weak and weak_odr aliases (here also hidden and dllexport
; ones) keep a forwarder, and internal, private and linkonce_odr aliases and
; data aliases leave no name in the output.
;
; main.ll: callers call the aliasee, also through a chain of two aliases; the
; address of a function alias is the aliasee's ('<msil>fn_impl', so the two
; addresses compare equal and the compare folds to 1); a call through an alias
; whose type differs from the aliasee's goes to MSILFixFunctionBitcasts'
; wrapper 'one_arg_bitcast', which runs after MSILPrepareIR, and so does the
; forwarder of that alias; data aliases at offsets 0 and 16 into 'table' are
; 'table' and 'table' + 16 in code and in initializers (the relocations of
; <Module>::.cctor); llvm.global_ctors names the aliasee 'ctor_impl', which
; .cctor calls; the no_cfi and dso_local_equivalent wrappers of an alias in
; code (of the external alias 'a1', of a chain 'chain_c' -> 'chain_b' ->
; internal 'chain_a' -> 'impl', and of a chain 'rev_y' -> internal 'rev_x' ->
; 'impl' declared in reverse order) and no_cfi in an initializer ('gref') wrap
; the aliasee object 'impl', never a forwarder: each method loads
; '<msil>fn_impl' and .cctor stores it into 'gref' (dso_local_equivalent is
; used only in code, because an initializer rejects it for every function,
; alias or not); llc reports nothing at -O0 and -O2 on both triples, and the
; output assembles. The FWD prefix checks the forwarders, which may come
; anywhere in the module, each as one multi-line pattern; the IR prefix checks
; the module MSILPrepareIR leaves (forwarder linkage and visibility, the
; rewritten initializers and lists, no alias left).
;
; errors.ll (a split part): every other alias keeps its Phase 1 error 'alias
; @<name> is not supported for MSIL' (a function-typed alias of a variable, an
; alias of an ifunc, a non-function alias of a function, a function alias at
; an offset into a function, and a thread_local alias of a variable that is
; not thread_local, which tlsvar.ll and tlsvar0.ll below check), and an ifunc
; keeps 'ifunc @<name> is not supported for MSIL'; their uses become poison as
; in Phase 1, so their callers compile with an undefined result, nothing
; crashes and the IL on stdout assembles.
;
; tls.ll and tls0.ll (split parts): a thread_local alias of a thread_local
; variable, at offset 4 ('a') and at offset 0 ('b'), which main reads through
; llvm.threadlocal.address. Such an alias is a variable alias like 'd16' and
; 'd0' above, so M12.a (1a) lowers it and rewrites every use, while
; thread_local stays an error (D26): the only diagnostic is the variable's
; 'thread-local variable @tls is not supported for MSIL yet', exit 1, with no
; crash, on both triples at -O0 and -O2 (review-r2: the offset-4 alias left
; the module broken, 'llvm.threadlocal.address first argument must be a
; GlobalValue').
;
; tlsvar.ll and tlsvar0.ll (split parts): a thread_local alias 'a' of the
; ordinary variable 'g', at offset 4 and at offset 0, which main reads through
; llvm.threadlocal.address. clang emits '@a = thread_local alias i32, ptr @g'
; for 'int g[2]; extern __thread int a __attribute__((alias("g")));'.
; Rewriting its uses to 'g' would hand llvm.threadlocal.address a variable
; that is not thread-local (or, at offset 4, a constant expression), which
; the verifier rejects, so M12.a (1a) keeps such an alias an error: the only
; diagnostic is 'alias @a is not supported for MSIL', exit 1, with no crash,
; on both triples at -O0 and -O2. No thread-local variable error appears,
; because 'g' is not thread_local (triage-r3: before M12.a's r2 fix both
; offsets aborted with 'LLVM ERROR: Broken function found').

; DEFINE: %{il} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="'int_alias'" \
; DEFINE:   --implicit-check-not="'priv_alias'" --implicit-check-not="'lodr_alias'" \
; DEFINE:   --implicit-check-not="'chain_a'" --implicit-check-not="'rev_x'" --implicit-check-not="'d0'" \
; DEFINE:   --implicit-check-not="'d16'" --implicit-check-not="<msil>fn_a1" --implicit-check-not="<msil>fn_chain_" \
; DEFINE:   --implicit-check-not="<msil>fn_rev_" \
; DEFINE:   --implicit-check-not="<msil>fn_ext_alias" --implicit-check-not="<msil>fn_weak_alias"
; DEFINE: %{fwd} = FileCheck %s --check-prefix=FWD
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o2.il 2>&1 | count 0
; RUN: %{il} --input-file=%t/main.lnx.o2.il
; RUN: %{fwd} --input-file=%t/main.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o2.il 2>&1 | count 0
; RUN: %{il} --input-file=%t/main.win.o2.il
; RUN: %{fwd} --input-file=%t/main.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o0.il 2>&1 | count 0
; RUN: %{il} --input-file=%t/main.lnx.o0.il
; RUN: %{fwd} --input-file=%t/main.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o0.il 2>&1 | count 0
; RUN: %{il} --input-file=%t/main.win.o0.il
; RUN: %{fwd} --input-file=%t/main.win.o0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/main.ll -o %t/main.prep.ll
; RUN: FileCheck %s --check-prefix=IR --input-file=%t/main.prep.ll \
; RUN:   --implicit-check-not="{{^ *@[^ ]+ = .*alias (i32|void)}}" --implicit-check-not=@int_alias \
; RUN:   --implicit-check-not=@priv_alias --implicit-check-not=@chain_a --implicit-check-not=@rev_x \
; RUN:   --implicit-check-not=@d0 --implicit-check-not=@d16 \
; RUN:   --implicit-check-not="{{(no_cfi|dso_local_equivalent) @(a1|chain_|rev_)}}"
; RUN: FileCheck %s --check-prefix=IRFWD --input-file=%t/main.prep.ll
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.lnx.o2.dll %t/main.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.win.o0.dll %t/main.win.o0.il %}

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/errors.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/errors.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/errors.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/errors.ll \
; RUN:   > %t/errors.lnx.il
; RUN: FileCheck %s --check-prefix=ERR-IL --input-file=%t/errors.lnx.il --implicit-check-not="'fn_of_var'" \
; RUN:   --implicit-check-not="'alias_of_ifunc'" --implicit-check-not="'data_of_fn'" \
; RUN:   --implicit-check-not="'fn_offset'" --implicit-check-not="'ifn'"
; RUN: %if ilasm %{ %ilasm-verify -output=%t/errors.dll %t/errors.lnx.il %}

; DEFINE: %{tls} = FileCheck %s --check-prefix=TLS --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/tls.ll -o /dev/null 2>&1 | %{tls}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/tls.ll -o /dev/null 2>&1 | %{tls}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/tls.ll -o /dev/null 2>&1 \
; RUN:   | %{tls}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/tls.ll -o /dev/null 2>&1 \
; RUN:   | %{tls}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/tls0.ll -o /dev/null 2>&1 | %{tls}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/tls0.ll -o /dev/null 2>&1 | %{tls}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/tls0.ll -o /dev/null 2>&1 \
; RUN:   | %{tls}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/tls0.ll -o /dev/null 2>&1 \
; RUN:   | %{tls}

; DEFINE: %{tlsvar} = FileCheck %s --check-prefix=TLSVAR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/tlsvar.ll -o /dev/null 2>&1 \
; RUN:   | %{tlsvar}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/tlsvar.ll -o /dev/null 2>&1 \
; RUN:   | %{tlsvar}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/tlsvar.ll -o /dev/null 2>&1 \
; RUN:   | %{tlsvar}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/tlsvar.ll -o /dev/null 2>&1 \
; RUN:   | %{tlsvar}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/tlsvar0.ll -o /dev/null 2>&1 \
; RUN:   | %{tlsvar}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/tlsvar0.ll -o /dev/null 2>&1 \
; RUN:   | %{tlsvar}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/tlsvar0.ll -o /dev/null 2>&1 \
; RUN:   | %{tlsvar}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/tlsvar0.ll -o /dev/null 2>&1 \
; RUN:   | %{tlsvar}

;--- main.ll
source_filename = "alias.c"

@table = global [8 x i32] [i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17], align 16
@d0 = alias i32, ptr @table
@d16 = alias i32, getelementptr (i8, ptr @table, i64 16)
@init_d0 = global ptr @d0, align 8
@init_d16 = global ptr @d16, align 8
@init_fn = global ptr @ext_alias, align 8
@ctor_flag = global i32 0, align 4
@gref = global ptr no_cfi @chain_c, align 8
@llvm.used = appending global [2 x ptr] [ptr @used_alias, ptr @table], section "llvm.metadata"
@llvm.compiler.used = appending global [2 x ptr] [ptr @d16, ptr @ctor_flag], section "llvm.metadata"
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @ctor_alias, ptr null }]

@ext_alias = alias i32 (i32), ptr @impl
@int_alias = internal alias i32 (i32), ptr @impl
@priv_alias = private alias i32 (i32), ptr @impl
@weak_alias = weak alias i32 (i32), ptr @impl
@wodr_alias = weak_odr alias i32 (i32), ptr @impl
@lodr_alias = linkonce_odr alias i32 (i32), ptr @impl
@chain_a = internal alias i32 (i32), ptr @impl
@chain_b = hidden unnamed_addr alias i32 (i32), ptr @chain_a
@chain_c = alias i32 (i32), ptr @chain_b
@rev_y = alias i32 (i32), ptr @rev_x
@rev_x = internal alias i32 (i32), ptr @impl
@a1 = alias i32 (i32), ptr @impl
@used_alias = dllexport alias i32 (i32), ptr @impl
@mm_alias = alias i32 (i32, i32), ptr @one_arg
@ctor_alias = alias void (), ptr @ctor_impl
@main = alias i32 (), ptr @real_main

; IR:     @init_d0 = global ptr @table, align 8
; IR:     @init_d16 = global ptr getelementptr (i8, ptr @table, i64 16), align 8
; IR:     @init_fn = global ptr @impl, align 8
; IR:     @gref = global ptr no_cfi @impl, align 8
; IR:     @llvm.used = appending global [1 x ptr] [ptr @table], section "llvm.metadata"
; IR:     @llvm.compiler.used = appending global [1 x ptr] [ptr @ctor_flag], section "llvm.metadata"
; IR:     @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @ctor_impl, ptr null }]

; CHECK-LABEL: {{^}}.method public static int32 'impl'(int32) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'impl'{{$}}
define i32 @impl(i32 %x) noinline {
  %r = add i32 %x, 1
  ret i32 %r
}

; Every alias, the chain through the internal 'chain_a' included, calls the
; aliasee.
; IR-LABEL: define i32 @call_all(
; IR-COUNT-9: {{^}} %{{.*}} = call i32 @impl(i32 %{{.*}})
; IR-NEXT:    ret i32
; CHECK-LABEL: {{^}}.method public static int32 'call_all'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-COUNT-9: {{^}}  call int32 'impl'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_all'{{$}}
define i32 @call_all(i32 %x) {
  %a = call i32 @ext_alias(i32 %x)
  %b = call i32 @int_alias(i32 %a)
  %c = call i32 @priv_alias(i32 %b)
  %d = call i32 @weak_alias(i32 %c)
  %e = call i32 @wodr_alias(i32 %d)
  %f = call i32 @lodr_alias(i32 %e)
  %g = call i32 @chain_a(i32 %f)
  %h = call i32 @chain_b(i32 %g)
  %i = call i32 @used_alias(i32 %h)
  ret i32 %i
}

; &alias == &aliasee: both are 'impl'.
; CHECK-LABEL: {{^}}.method public static int32 'same_address'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i1 @same_address() {
  %c = icmp eq ptr @ext_alias, @impl
  ret i1 %c
}

; CHECK-LABEL: {{^}}.method public static native int 'address_of_alias'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @address_of_alias() {
  ret ptr @weak_alias
}

; no_cfi and dso_local_equivalent wrappers of an alias wrap its aliasee
; object, directly and through the chains.
; IR-LABEL: define ptr @nocfi_a1(
; IR-NEXT:  ret ptr no_cfi @impl
; IR-LABEL: define ptr @dsole_a1(
; IR-NEXT:  ret ptr dso_local_equivalent @impl
; IR-LABEL: define ptr @nocfi_chain_b(
; IR-NEXT:  ret ptr no_cfi @impl
; IR-LABEL: define ptr @dsole_chain_b(
; IR-NEXT:  ret ptr dso_local_equivalent @impl
; IR-LABEL: define ptr @dsole_chain_c(
; IR-NEXT:  ret ptr dso_local_equivalent @impl
; IR-LABEL: define ptr @nocfi_rev_y(
; IR-NEXT:  ret ptr no_cfi @impl
; IR-LABEL: define ptr @dsole_rev_y(
; IR-NEXT:  ret ptr dso_local_equivalent @impl
; CHECK-LABEL: {{^}}.method public static native int 'nocfi_a1'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @nocfi_a1() {
  ret ptr no_cfi @a1
}

; CHECK-LABEL: {{^}}.method public static native int 'dsole_a1'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @dsole_a1() {
  ret ptr dso_local_equivalent @a1
}

; CHECK-LABEL: {{^}}.method public static native int 'nocfi_chain_b'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @nocfi_chain_b() {
  ret ptr no_cfi @chain_b
}

; CHECK-LABEL: {{^}}.method public static native int 'dsole_chain_b'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @dsole_chain_b() {
  ret ptr dso_local_equivalent @chain_b
}

; CHECK-LABEL: {{^}}.method public static native int 'dsole_chain_c'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @dsole_chain_c() {
  ret ptr dso_local_equivalent @chain_c
}

; CHECK-LABEL: {{^}}.method public static native int 'nocfi_rev_y'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @nocfi_rev_y() {
  ret ptr no_cfi @rev_y
}

; CHECK-LABEL: {{^}}.method public static native int 'dsole_rev_y'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @dsole_rev_y() {
  ret ptr dso_local_equivalent @rev_y
}

; CHECK-LABEL: {{^}}.method public static int32 'one_arg'(int32) cil managed{{$}}
define i32 @one_arg(i32 %x) noinline {
  %r = mul i32 %x, 3
  ret i32 %r
}

; A call through an alias of another function type: the wrapper drops the
; extra argument.
; CHECK-LABEL: {{^}}.method public static int32 'call_mismatched'(int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'one_arg_bitcast'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_mismatched(i32 %x, i32 %y) {
  %r = call i32 @mm_alias(i32 %x, i32 %y)
  ret i32 %r
}

; Data aliases are their aliasee expressions.
; CHECK-LABEL: {{^}}.method public static int32 'read_d0'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'table'{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @read_d0() {
  %v = load i32, ptr @d0, align 4
  ret i32 %v
}

; CHECK-LABEL: {{^}}.method public static int32 'read_d16'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'table'{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @read_d16() {
  %v = load i32, ptr @d16, align 4
  ret i32 %v
}

; CHECK-LABEL: {{^}}.method public static void 'ctor_impl'() cil managed{{$}}
define void @ctor_impl() {
  store i32 42, ptr @ctor_flag, align 4
  ret void
}

; CHECK-LABEL: {{^}}.method assembly static int32 'real_main'() cil managed{{$}}
define internal i32 @real_main() {
  %v = load i32, ptr @ctor_flag, align 4
  %r = sub i32 %v, 42
  ret i32 %r
}

; The data segment: table at +0, init_d0 at +32, init_d16 at +40, init_fn at
; +48, gref at +56; .cctor stores 'table', 'table' + 16 and twice the address
; of 'impl' into them, then runs the constructor through its aliasee.
; CHECK:       {{^}}.field public static initonly native int 'table'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'init_d0'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'init_d16'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'init_fn'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'gref'{{$}}
; CHECK-NEXT:  {{^}}.field public static initonly native int 'ctor_flag'{{$}}
; CHECK:       {{^}}.field private static initonly native int '<msil>fn_impl'{{$}}
; CHECK:       {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK:       {{^}}  ldftn int32 'impl'(int32){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'table'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 40{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'table'{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 48{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>data'{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 56{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_impl'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  call void 'ctor_impl'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '.cctor'{{$}}
; CHECK:       {{^}}.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .entrypoint{{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'main'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '<msil>Main'{{$}}

; One forwarder per external, weak, weak_odr, hidden and dllexport function
; alias, wherever MSILPrepareIR put it; the chains' forwarders call the
; aliasee object, not the next alias.
; FWD-DAG: {{^}}.method public static int32 'ext_alias'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 'impl'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'ext_alias'$}}
; FWD-DAG: {{^}}.method public static int32 'weak_alias'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 'impl'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'weak_alias'$}}
; FWD-DAG: {{^}}.method public static int32 'wodr_alias'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 'impl'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'wodr_alias'$}}
; FWD-DAG: {{^}}.method public static int32 'chain_b'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 'impl'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'chain_b'$}}
; FWD-DAG: {{^}}.method public static int32 'chain_c'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 'impl'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'chain_c'$}}
; FWD-DAG: {{^}}.method public static int32 'rev_y'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 'impl'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'rev_y'$}}
; FWD-DAG: {{^}}.method public static int32 'a1'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 'impl'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'a1'$}}
; FWD-DAG: {{^}}.method public static int32 'used_alias'(int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 'impl'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'used_alias'$}}
; FWD-DAG: {{^}}.method public static int32 'mm_alias'(int32, int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 2[[:space:]]+ldarg\.0[[:space:]]+ldarg\.1[[:space:]]+call int32 'one_arg_bitcast'\(int32, int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'mm_alias'$}}
; FWD-DAG: {{^}}.method public static void 'ctor_alias'() cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 0[[:space:]]+call void 'ctor_impl'\(\)[[:space:]]+ret[[:space:]]+\} // end of method 'ctor_alias'$}}
; FWD-DAG: {{^}}.method public static int32 'main'() cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+call int32 'real_main'\(\)[[:space:]]+ret[[:space:]]+\} // end of method 'main'$}}
; FWD-DAG: {{^}}.method assembly static int32 'one_arg_bitcast'(int32, int32) cil managed{{$[[:space:]]+\{[[:space:]]+\.maxstack 1[[:space:]]+ldarg\.0[[:space:]]+call int32 'one_arg'\(int32\)[[:space:]]+ret[[:space:]]+\} // end of method 'one_arg_bitcast'$}}

; The forwarders keep the alias's linkage, visibility, DLL storage class and
; unnamed_addr (a FileCheck run of its own: they may come anywhere).
; IRFWD-DAG: define {{(dso_local )?}}i32 @ext_alias(i32 {{.*}}{{$}}
; IRFWD-DAG: define weak {{(dso_local )?}}i32 @weak_alias(i32 {{.*}}{{$}}
; IRFWD-DAG: define weak_odr {{(dso_local )?}}i32 @wodr_alias(i32 {{.*}}{{$}}
; IRFWD-DAG: define hidden {{(dso_local )?}}i32 @chain_b(i32 {{[^)]*}}) unnamed_addr {{.*}}{{$}}
; IRFWD-DAG: define {{(dso_local )?}}i32 @chain_c(i32 {{.*}}{{$}}
; IRFWD-DAG: define {{(dso_local )?}}i32 @rev_y(i32 {{.*}}{{$}}
; IRFWD-DAG: define {{(dso_local )?}}i32 @a1(i32 {{.*}}{{$}}
; IRFWD-DAG: define dllexport {{(dso_local )?}}i32 @used_alias(i32 {{.*}}{{$}}
; IRFWD-DAG: define {{(dso_local )?}}i32 @mm_alias(i32 {{[^,]*}}, i32 {{.*}}{{$}}
; IRFWD-DAG: define {{(dso_local )?}}void @ctor_alias() {{.*}}{{$}}
; IRFWD-DAG: define {{(dso_local )?}}i32 @main() {{.*}}{{$}}

;--- errors.ll
source_filename = "alias-errors.c"

; ERR-DAG: {{^}}error: {{.*}}alias @fn_of_var is not supported for MSIL{{$}}
; ERR-DAG: {{^}}error: {{.*}}alias @alias_of_ifunc is not supported for MSIL{{$}}
; ERR-DAG: {{^}}error: {{.*}}alias @data_of_fn is not supported for MSIL{{$}}
; ERR-DAG: {{^}}error: {{.*}}alias @fn_offset is not supported for MSIL{{$}}
; ERR-DAG: {{^}}error: {{.*}}ifunc @ifn is not supported for MSIL{{$}}

@var = global i32 1, align 4
@fn_of_var = alias void (), ptr @var
@ifn = ifunc i32 (i32), ptr @resolver
@alias_of_ifunc = alias i32 (i32), ptr @ifn
@data_of_fn = alias i32, ptr @target
@fn_offset = alias i32 (i32), getelementptr (i8, ptr @target, i64 4)

; ERR-IL-LABEL: {{^}}.method public static native int 'resolver'() cil managed{{$}}
define ptr @resolver() {
  ret ptr null
}

; ERR-IL-LABEL: {{^}}.method public static int32 'target'(int32) cil managed{{$}}
define i32 @target(i32 %x) noinline {
  ret i32 %x
}

; ERR-IL-LABEL: {{^}}.method public static int32 'calls_ifunc'(int32) cil managed{{$}}
; ERR-IL-NOT:   {{^}}  call
; ERR-IL:       {{^}}  ret{{$}}
; ERR-IL-NEXT:  {{^}}} // end of method 'calls_ifunc'{{$}}
define i32 @calls_ifunc(i32 %x) {
  %r = call i32 @ifn(i32 %x)
  ret i32 %r
}

; ERR-IL-LABEL: {{^}}.method public static int32 'calls_alias_of_ifunc'(int32) cil managed{{$}}
; ERR-IL-NOT:   {{^}}  call
; ERR-IL:       {{^}}  ret{{$}}
; ERR-IL-NEXT:  {{^}}} // end of method 'calls_alias_of_ifunc'{{$}}
define i32 @calls_alias_of_ifunc(i32 %x) {
  %r = call i32 @alias_of_ifunc(i32 %x)
  ret i32 %r
}

; ERR-IL-LABEL: {{^}}.method public static int32 'calls_offset'(int32) cil managed{{$}}
; ERR-IL-NOT:   {{^}}  call
; ERR-IL:       {{^}}  ret{{$}}
; ERR-IL-NEXT:  {{^}}} // end of method 'calls_offset'{{$}}
define i32 @calls_offset(i32 %x) {
  %r = call i32 @fn_offset(i32 %x)
  ret i32 %r
}

; ERR-IL-LABEL: {{^}}.method public static int32 'after_alias_errors'(int32, int32) cil managed{{$}}
; ERR-IL-NEXT:  {{^\{$}}
; ERR-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; ERR-IL-NEXT:  {{^}}  ldarg.0{{$}}
; ERR-IL-NEXT:  {{^}}  ldarg.1{{$}}
; ERR-IL-NEXT:  {{^}}  add{{$}}
; ERR-IL-NEXT:  {{^}}  ret{{$}}
define i32 @after_alias_errors(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- tls.ll
source_filename = "alias-tls.c"

; TLS: {{^}}error: thread-local variable @tls is not supported for MSIL yet{{$}}

@tls = thread_local global [2 x i32] zeroinitializer
@a = thread_local alias i32, getelementptr (i8, ptr @tls, i64 4)

define i32 @main() {
  %p = call ptr @llvm.threadlocal.address.p0(ptr @a)
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

declare ptr @llvm.threadlocal.address.p0(ptr)

;--- tls0.ll
source_filename = "alias-tls0.c"

@tls = thread_local global [2 x i32] zeroinitializer
@b = thread_local alias i32, ptr @tls

define i32 @main() {
  %p = call ptr @llvm.threadlocal.address.p0(ptr @b)
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

declare ptr @llvm.threadlocal.address.p0(ptr)

;--- tlsvar.ll
source_filename = "alias-tlsvar.c"

; TLSVAR: {{^}}error: alias @a is not supported for MSIL{{$}}

@g = global [2 x i32] zeroinitializer
@a = thread_local alias i32, getelementptr (i8, ptr @g, i64 4)

define i32 @main() {
  %p = call ptr @llvm.threadlocal.address.p0(ptr @a)
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

declare ptr @llvm.threadlocal.address.p0(ptr)

;--- tlsvar0.ll
source_filename = "alias-tlsvar0.c"

@g = global [2 x i32] zeroinitializer
@a = thread_local alias i32, ptr @g

define i32 @main() {
  %p = call ptr @llvm.threadlocal.address.p0(ptr @a)
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

declare ptr @llvm.threadlocal.address.p0(ptr)
