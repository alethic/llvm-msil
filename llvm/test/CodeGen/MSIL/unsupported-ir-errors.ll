; IR that Phase 1 cannot compile is a clean error naming the symbol, with exit
; code 1 and no crash after it (ImplementationPlan.md section 2, 'Diagnostics
; leave well-formed output'; decisions D19, D26; M2.a item (5) with the
; amendment-log rows of 2026-10-12 for inline asm, named registers and address
; spaces; M2.d for module asm). llc keeps running after an error, so
; MSILPrepareIR neutralizes what it diagnosed: a diagnosed definition loses its
; body, an alias, ifunc or global in a non-zero address space is replaced by
; poison in all its uses and erased, a thread_local global becomes an ordinary
; global, and llvm.global_dtors is erased. Each case runs the whole pipeline on
; two triples, and once with -stop-after=msil-prepare-ir to check the
; diagnostics of MSILPrepareIR alone and the IR it leaves behind.
;
; Function bodies are implemented in M3 (the M2 lowering hooks are stubs), so
; nothing here may leave a definition for instruction selection: functions that
; use a diagnosed symbol are available_externally, which is never
; code-generated but still has to be rewritten, and the resolver an ifunc needs
; is a variadic definition, which loses its body.

; DEFINE: %{nocrash} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: split-file %s %t

; Alias (D26), used by a function and by a global initializer.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/alias.ll 2>&1 | %{nocrash} --check-prefix=ALIAS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/alias.ll 2>&1 | %{nocrash} --check-prefix=ALIAS
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/alias.ll -o - 2>%t/alias.err \
; RUN:   | FileCheck %s --check-prefix=ALIAS-IR --implicit-check-not=msil_alias_sym
; RUN: FileCheck %s --check-prefix=ALIAS --input-file=%t/alias.err
; ALIAS: error: {{.*}}msil_alias_sym
; ALIAS-IR: @msil_alias_target = global i32 7
; ALIAS-IR: @msil_alias_ref = global ptr poison
; ALIAS-IR: define available_externally i32 @msil_alias_user()
; ALIAS-IR-NEXT: %v = load i32, ptr poison

; IFunc (D26), called by a function.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/ifunc.ll 2>&1 | %{nocrash} --check-prefix=IFUNC
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/ifunc.ll 2>&1 | %{nocrash} --check-prefix=IFUNC
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/ifunc.ll -o - 2>%t/ifunc.err \
; RUN:   | FileCheck %s --check-prefix=IFUNC-IR --implicit-check-not=msil_ifunc_sym
; RUN: FileCheck %s --check-prefix=IFUNC --input-file=%t/ifunc.err
; IFUNC-DAG: error: {{.*}}msil_ifunc_sym
; IFUNC-DAG: error: {{.*(msil_ifunc_resolver.*variadic function definitions are not supported for MSIL yet|variadic function definitions are not supported for MSIL yet.*msil_ifunc_resolver)}}
; IFUNC-IR: declare ptr @msil_ifunc_resolver(...)
; IFUNC-IR: define available_externally i32 @msil_ifunc_user()
; IFUNC-IR-NEXT: %r = call i32 poison()

; thread_local global (D26).
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/tls.ll 2>&1 | %{nocrash} --check-prefix=TLS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/tls.ll 2>&1 | %{nocrash} --check-prefix=TLS
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/tls.ll -o - 2>%t/tls.err \
; RUN:   | FileCheck %s --check-prefix=TLS-IR --implicit-check-not=thread_local
; RUN: FileCheck %s --check-prefix=TLS --input-file=%t/tls.err
; TLS: error: {{.*}}msil_tls_var
; TLS-IR: @msil_tls_var = global i32 1

; Non-empty llvm.global_dtors (D26).
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/dtors.ll 2>&1 | %{nocrash} --check-prefix=DTORS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/dtors.ll 2>&1 | %{nocrash} --check-prefix=DTORS
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/dtors.ll -o - 2>%t/dtors.err \
; RUN:   | FileCheck %s --check-prefix=DTORS-IR --implicit-check-not=llvm.global_dtors
; RUN: FileCheck %s --check-prefix=DTORS --input-file=%t/dtors.err
; DTORS: error: {{.*}}{{llvm\.global_dtors|msil_dtor_fn}}
; DTORS-IR: source_filename = "dtors.c"
; An empty llvm.global_dtors is not an error.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/empty-dtors.ll 2>&1 \
; RUN:   | %{nocrash} --check-prefix=EMPTY-DTORS --implicit-check-not=error:
; EMPTY-DTORS: .assembly 'empty-dtors'

; Variadic definition (D19), called by another definition. A variadic
; declaration alone is not an error.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/variadic.ll 2>&1 | %{nocrash} --check-prefix=VA
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/variadic.ll 2>&1 | %{nocrash} --check-prefix=VA
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/variadic.ll -o - 2>%t/variadic.err \
; RUN:   | FileCheck %s --check-prefix=VA-IR
; RUN: FileCheck %s --check-prefix=VA --input-file=%t/variadic.err --implicit-check-not=msil_va_decl
; VA: error: {{.*(msil_va_def.*variadic function definitions are not supported for MSIL yet|variadic function definitions are not supported for MSIL yet.*msil_va_def)}}
; VA-IR: declare i32 @msil_va_def(i32, ...)
; VA-IR: define available_externally i32 @msil_va_caller()

; Module-level inline asm (D26), diagnosed by the AsmPrinter; it must not
; reach the output either.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/module-asm.ll 2>&1 \
; RUN:   | %{nocrash} --check-prefix=MODASM --implicit-check-not=msil_module_asm_sym
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/module-asm.ll 2>&1 \
; RUN:   | %{nocrash} --check-prefix=MODASM --implicit-check-not=msil_module_asm_sym
; MODASM: error: {{.*}}module-level inline assembly is not supported for MSIL

; A call and an invoke of inline asm, and the named-register intrinsics.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/asm-call.ll 2>&1 | %{nocrash} --check-prefix=ASMCALL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/asm-call.ll 2>&1 | %{nocrash} --check-prefix=ASMCALL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/asm-call.ll -o - 2>%t/asm-call.err \
; RUN:   | FileCheck %s --check-prefix=ASMCALL-IR --implicit-check-not=sideeffect
; RUN: FileCheck %s --check-prefix=ASMCALL --input-file=%t/asm-call.err
; ASMCALL: error: {{.*}}msil_asm_call
; ASMCALL-IR: declare void @msil_asm_call()

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/asm-invoke.ll 2>&1 | %{nocrash} --check-prefix=ASMINVOKE
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/asm-invoke.ll 2>&1 | %{nocrash} --check-prefix=ASMINVOKE
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/asm-invoke.ll -o - 2>%t/asm-invoke.err \
; RUN:   | FileCheck %s --check-prefix=ASMINVOKE-IR --implicit-check-not=sideeffect
; RUN: FileCheck %s --check-prefix=ASMINVOKE --input-file=%t/asm-invoke.err
; ASMINVOKE: error: {{.*}}msil_asm_invoke
; ASMINVOKE-IR: declare void @msil_asm_invoke()

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/read-register.ll 2>&1 | %{nocrash} --check-prefix=READREG
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/read-register.ll 2>&1 | %{nocrash} --check-prefix=READREG
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/read-register.ll -o - 2>%t/read-register.err \
; RUN:   | FileCheck %s --check-prefix=READREG-IR --implicit-check-not="call i64 @llvm.read_register"
; RUN: FileCheck %s --check-prefix=READREG --input-file=%t/read-register.err
; READREG: error: {{.*}}msil_read_reg
; READREG-IR: declare i64 @msil_read_reg()

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/read-volatile-register.ll 2>&1 | %{nocrash} --check-prefix=READVREG
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/read-volatile-register.ll 2>&1 | %{nocrash} --check-prefix=READVREG
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/read-volatile-register.ll -o - 2>%t/read-volatile-register.err \
; RUN:   | FileCheck %s --check-prefix=READVREG-IR --implicit-check-not="call i64 @llvm.read_volatile_register"
; RUN: FileCheck %s --check-prefix=READVREG --input-file=%t/read-volatile-register.err
; READVREG: error: {{.*}}msil_read_volatile_reg
; READVREG-IR: declare i64 @msil_read_volatile_reg()

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/write-register.ll 2>&1 | %{nocrash} --check-prefix=WRITEREG
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/write-register.ll 2>&1 | %{nocrash} --check-prefix=WRITEREG
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/write-register.ll -o - 2>%t/write-register.err \
; RUN:   | FileCheck %s --check-prefix=WRITEREG-IR --implicit-check-not="call void @llvm.write_register"
; RUN: FileCheck %s --check-prefix=WRITEREG --input-file=%t/write-register.err
; WRITEREG: error: {{.*}}msil_write_reg
; WRITEREG-IR: declare void @msil_write_reg(i64)

; Pointers in non-zero address spaces: an x86 segment load through an
; inttoptr constant (__readgsqword), a 32-bit pointer parameter (__ptr32),
; an addrspacecast, and a global in a segment address space.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/as-inttoptr.ll 2>&1 | %{nocrash} --check-prefix=ASINT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/as-inttoptr.ll 2>&1 | %{nocrash} --check-prefix=ASINT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-prepare-ir < %t/as-inttoptr.ll -o - 2>%t/as-inttoptr.err \
; RUN:   | FileCheck %s --check-prefix=ASINT-IR --implicit-check-not=inttoptr
; RUN: FileCheck %s --check-prefix=ASINT --input-file=%t/as-inttoptr.err
; ASINT: error: {{.*}}msil_gs_load
; ASINT-IR: declare i64 @msil_gs_load()

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/as-param.ll 2>&1 | %{nocrash} --check-prefix=ASPARAM
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/as-param.ll 2>&1 | %{nocrash} --check-prefix=ASPARAM
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-prepare-ir < %t/as-param.ll -o - 2>%t/as-param.err \
; RUN:   | FileCheck %s --check-prefix=ASPARAM-IR
; RUN: FileCheck %s --check-prefix=ASPARAM --input-file=%t/as-param.err
; ASPARAM: error: {{.*}}msil_ptr32_param
; ASPARAM-IR: declare i32 @msil_ptr32_param(ptr addrspace(270){{.*}})

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/as-cast.ll 2>&1 | %{nocrash} --check-prefix=ASCAST
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/as-cast.ll 2>&1 | %{nocrash} --check-prefix=ASCAST
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/as-cast.ll -o - 2>%t/as-cast.err \
; RUN:   | FileCheck %s --check-prefix=ASCAST-IR --implicit-check-not=addrspacecast
; RUN: FileCheck %s --check-prefix=ASCAST --input-file=%t/as-cast.err
; ASCAST: error: {{.*}}msil_as_cast
; ASCAST-IR: declare i64 @msil_as_cast(ptr{{.*}})

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/as-global.ll 2>&1 | %{nocrash} --check-prefix=ASGLOBAL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/as-global.ll 2>&1 | %{nocrash} --check-prefix=ASGLOBAL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/as-global.ll -o - 2>%t/as-global.err \
; RUN:   | FileCheck %s --check-prefix=ASGLOBAL-IR --implicit-check-not=msil_as_global
; RUN: FileCheck %s --check-prefix=ASGLOBAL --input-file=%t/as-global.err
; ASGLOBAL: error: {{.*}}msil_as_global
; ASGLOBAL-IR: source_filename = "as-global.c"

; The diagnosed globals above are replaced by poison in every use, including
; uses through the 'no_cfi' and 'dso_local_equivalent' constants
; (__builtin_function_start and relative vtables), which can wrap only a
; GlobalValue and must therefore be replaced as a whole. The aliasees are
; variables, because a function definition would reach the M2 lowering stubs.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/nocfi-alias.ll 2>&1 | %{nocrash} --check-prefix=NOCFI-ALIAS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/nocfi-alias.ll 2>&1 | %{nocrash} --check-prefix=NOCFI-ALIAS
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/nocfi-alias.ll -o - 2>%t/nocfi-alias.err \
; RUN:   | FileCheck %s --check-prefix=NOCFI-ALIAS-IR --implicit-check-not=msil_nocfi_alias_sym
; RUN: FileCheck %s --check-prefix=NOCFI-ALIAS --input-file=%t/nocfi-alias.err
; NOCFI-ALIAS: error: {{.*}}msil_nocfi_alias_sym
; NOCFI-ALIAS-IR: @msil_nocfi_alias_target = global i32 7
; NOCFI-ALIAS-IR: @msil_nocfi_alias_ref = global ptr poison

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/nocfi-ifunc.ll 2>&1 | %{nocrash} --check-prefix=NOCFI-IFUNC
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/nocfi-ifunc.ll 2>&1 | %{nocrash} --check-prefix=NOCFI-IFUNC
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/nocfi-ifunc.ll -o - 2>%t/nocfi-ifunc.err \
; RUN:   | FileCheck %s --check-prefix=NOCFI-IFUNC-IR --implicit-check-not=msil_nocfi_ifunc_sym
; RUN: FileCheck %s --check-prefix=NOCFI-IFUNC --input-file=%t/nocfi-ifunc.err
; NOCFI-IFUNC-DAG: error: {{.*}}msil_nocfi_ifunc_sym
; NOCFI-IFUNC-DAG: error: {{.*(msil_nocfi_ifunc_resolver.*variadic function definitions are not supported for MSIL yet|variadic function definitions are not supported for MSIL yet.*msil_nocfi_ifunc_resolver)}}
; NOCFI-IFUNC-IR: @msil_nocfi_ifunc_ref = global ptr poison
; NOCFI-IFUNC-IR: declare ptr @msil_nocfi_ifunc_resolver(...)

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/dsole-global.ll 2>&1 | %{nocrash} --check-prefix=DSOLE-GLOBAL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/dsole-global.ll 2>&1 | %{nocrash} --check-prefix=DSOLE-GLOBAL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/dsole-global.ll -o - 2>%t/dsole-global.err \
; RUN:   | FileCheck %s --check-prefix=DSOLE-GLOBAL-IR --implicit-check-not=msil_dsole_alias_sym
; RUN: FileCheck %s --check-prefix=DSOLE-GLOBAL --input-file=%t/dsole-global.err
; DSOLE-GLOBAL: error: {{.*}}msil_dsole_alias_sym
; DSOLE-GLOBAL-IR: @msil_dsole_alias_target = global i32 7
; DSOLE-GLOBAL-IR: @msil_dsole_alias_ref = global ptr poison

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/dsole-ret.ll 2>&1 | %{nocrash} --check-prefix=DSOLE-RET
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/dsole-ret.ll 2>&1 | %{nocrash} --check-prefix=DSOLE-RET
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/dsole-ret.ll -o - 2>%t/dsole-ret.err \
; RUN:   | FileCheck %s --check-prefix=DSOLE-RET-IR --implicit-check-not=msil_dsole_ret_sym
; RUN: FileCheck %s --check-prefix=DSOLE-RET --input-file=%t/dsole-ret.err
; DSOLE-RET: error: {{.*}}msil_dsole_ret_sym
; DSOLE-RET-IR: define available_externally ptr @msil_dsole_ret_user()
; DSOLE-RET-IR-NEXT: ret ptr poison

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/nocfi-as1.ll 2>&1 | %{nocrash} --check-prefix=NOCFI-AS1
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/nocfi-as1.ll 2>&1 | %{nocrash} --check-prefix=NOCFI-AS1
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/nocfi-as1.ll -o - 2>%t/nocfi-as1.err \
; RUN:   | FileCheck %s --check-prefix=NOCFI-AS1-IR --implicit-check-not=msil_nocfi_as1_global
; RUN: FileCheck %s --check-prefix=NOCFI-AS1 --input-file=%t/nocfi-as1.err
; NOCFI-AS1: error: {{.*}}global variable @msil_nocfi_as1_global in address space 1 is not supported for MSIL
; NOCFI-AS1-IR: @msil_nocfi_as1_ref = global ptr addrspace(1) poison

; MIR input (functions shaped like the plan 5.4 sample): machineFunctionInfo
; params and results accept only i32, i64, f32 and f64. Any other entry is a
; MIR parse error at its location with exit code 1, not a crash in the MIR
; printer (amendment log 2026-10-13, M2.f item (5) extended).
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -run-pass=none %t/mfi-bad-param.mir -o /dev/null 2>&1 \
; RUN:   | %{nocrash} --check-prefix=MFI-PARAM
; MFI-PARAM: error: {{.*}}:{{[0-9]+}}:{{[0-9]+}}: unknown MSIL value type 'bogus' in machineFunctionInfo params
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -run-pass=none %t/mfi-bad-result.mir -o /dev/null 2>&1 \
; RUN:   | %{nocrash} --check-prefix=MFI-RESULT
; MFI-RESULT: error: {{.*}}:{{[0-9]+}}:{{[0-9]+}}: unknown MSIL value type 'ptr' in machineFunctionInfo results
; The four value types are accepted and printed back.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -run-pass=none -verify-machineinstrs %t/mfi-valid.mir -o - 2>&1 \
; RUN:   | %{nocrash} --check-prefix=MFI-VALID --implicit-check-not=error: --implicit-check-not=warning:
; MFI-VALID-LABEL: name: f
; MFI-VALID:       machineFunctionInfo:
; MFI-VALID-NEXT:    params: [ i32, i64 ]
; MFI-VALID-NEXT:    results: [ i32 ]
; MFI-VALID-LABEL: name: g
; MFI-VALID:       machineFunctionInfo:
; MFI-VALID-NEXT:    params: [ f32, f64 ]
; MFI-VALID-NEXT:    results: [ f64 ]

;--- alias.ll
source_filename = "alias.c"

@msil_alias_target = global i32 7
@msil_alias_sym = alias i32, ptr @msil_alias_target
@msil_alias_ref = global ptr @msil_alias_sym

define available_externally i32 @msil_alias_user() {
  %v = load i32, ptr @msil_alias_sym
  ret i32 %v
}

;--- ifunc.ll
source_filename = "ifunc.c"

@msil_ifunc_sym = ifunc i32 (), ptr @msil_ifunc_resolver

define ptr @msil_ifunc_resolver(...) {
  ret ptr null
}

define available_externally i32 @msil_ifunc_user() {
  %r = call i32 @msil_ifunc_sym()
  ret i32 %r
}

;--- tls.ll
source_filename = "tls.c"

@msil_tls_var = thread_local global i32 1

;--- dtors.ll
source_filename = "dtors.c"

@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @msil_dtor_fn, ptr null }]

declare void @msil_dtor_fn()

;--- empty-dtors.ll
source_filename = "empty-dtors.c"

@llvm.global_dtors = appending global [0 x { i32, ptr, ptr }] zeroinitializer

;--- variadic.ll
source_filename = "variadic.c"

define i32 @msil_va_def(i32 %n, ...) {
  ret i32 %n
}

define available_externally i32 @msil_va_caller() {
  %r = call i32 (i32, ...) @msil_va_def(i32 1, i32 2)
  ret i32 %r
}

declare i32 @msil_va_decl(ptr, ...)

;--- module-asm.ll
source_filename = "module-asm.c"

module asm ".globl msil_module_asm_sym"
module asm "msil_module_asm_sym:"

;--- asm-call.ll
source_filename = "asm-call.c"

define void @msil_asm_call() {
  call void asm sideeffect "nop", "~{dirflag},~{fpsr},~{flags}"()
  ret void
}

;--- asm-invoke.ll
source_filename = "asm-invoke.c"

declare i32 @__gxx_personality_v0(...)

define void @msil_asm_invoke() personality ptr @__gxx_personality_v0 {
entry:
  invoke void asm sideeffect unwind "nop", "~{dirflag},~{fpsr},~{flags}"()
          to label %ok unwind label %lp

ok:
  ret void

lp:
  %l = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %l
}

;--- read-register.ll
source_filename = "read-register.c"

define i64 @msil_read_reg() {
  %v = call i64 @llvm.read_register.i64(metadata !0)
  ret i64 %v
}

declare i64 @llvm.read_register.i64(metadata)

!0 = !{!"rsp"}

;--- read-volatile-register.ll
source_filename = "read-volatile-register.c"

define i64 @msil_read_volatile_reg() {
  %v = call i64 @llvm.read_volatile_register.i64(metadata !0)
  ret i64 %v
}

declare i64 @llvm.read_volatile_register.i64(metadata)

!0 = !{!"rsp"}

;--- write-register.ll
source_filename = "write-register.c"

define void @msil_write_reg(i64 %v) {
  call void @llvm.write_register.i64(metadata !0, i64 %v)
  ret void
}

declare void @llvm.write_register.i64(metadata, i64)

!0 = !{!"rsp"}

;--- as-inttoptr.ll
source_filename = "segment-load.c"

define i64 @msil_gs_load() {
  %v = load i64, ptr addrspace(256) inttoptr (i64 48 to ptr addrspace(256)), align 8
  ret i64 %v
}

;--- as-param.ll
source_filename = "as-param.c"

define i32 @msil_ptr32_param(ptr addrspace(270) %p) {
  ret i32 0
}

;--- as-cast.ll
source_filename = "as-cast.c"

define i64 @msil_as_cast(ptr %p) {
  %q = addrspacecast ptr %p to ptr addrspace(271)
  %i = ptrtoint ptr addrspace(271) %q to i64
  ret i64 %i
}

;--- as-global.ll
source_filename = "as-global.c"

@msil_as_global = addrspace(256) global i32 5

;--- nocfi-alias.ll
source_filename = "nocfi-alias.c"

@msil_nocfi_alias_target = global i32 7
@msil_nocfi_alias_sym = alias void (), ptr @msil_nocfi_alias_target
@msil_nocfi_alias_ref = global ptr no_cfi @msil_nocfi_alias_sym

;--- nocfi-ifunc.ll
source_filename = "nocfi-ifunc.c"

@msil_nocfi_ifunc_sym = ifunc void (), ptr @msil_nocfi_ifunc_resolver
@msil_nocfi_ifunc_ref = global ptr no_cfi @msil_nocfi_ifunc_sym

define ptr @msil_nocfi_ifunc_resolver(...) {
  ret ptr null
}

;--- dsole-global.ll
source_filename = "dsole-global.c"

@msil_dsole_alias_target = global i32 7
@msil_dsole_alias_sym = alias void (), ptr @msil_dsole_alias_target
@msil_dsole_alias_ref = global ptr dso_local_equivalent @msil_dsole_alias_sym

;--- dsole-ret.ll
source_filename = "dsole-ret.c"

@msil_dsole_ret_target = global i32 7
@msil_dsole_ret_sym = alias void (), ptr @msil_dsole_ret_target

define available_externally ptr @msil_dsole_ret_user() {
  ret ptr dso_local_equivalent @msil_dsole_ret_sym
}

;--- nocfi-as1.ll
source_filename = "nocfi-as1.c"

@msil_nocfi_as1_global = addrspace(1) global i32 1
@msil_nocfi_as1_ref = global ptr addrspace(1) no_cfi @msil_nocfi_as1_global

;--- mfi-bad-param.mir
---
name:            f
tracksRegLiveness: true
machineFunctionInfo:
  params:  [ bogus, i64 ]
  results: [ i32 ]
body: |
  bb.0:
    liveins: $arguments
    %0:i32 = ARGUMENT_I32 0, implicit $arguments
    %1:i64 = ARGUMENT_I64 1, implicit $arguments
    dead %2:i32 = CONST_I32 7, implicit-def dead $arguments
    RETURN %0, implicit-def dead $arguments
...

;--- mfi-bad-result.mir
---
name:            f
tracksRegLiveness: true
machineFunctionInfo:
  params:  [ i32, i64 ]
  results: [ ptr ]
body: |
  bb.0:
    liveins: $arguments
    %0:i32 = ARGUMENT_I32 0, implicit $arguments
    %1:i64 = ARGUMENT_I64 1, implicit $arguments
    dead %2:i32 = CONST_I32 7, implicit-def dead $arguments
    RETURN %0, implicit-def dead $arguments
...

;--- mfi-valid.mir
---
name:            f
tracksRegLiveness: true
machineFunctionInfo:
  params:  [ i32, i64 ]
  results: [ i32 ]
body: |
  bb.0:
    liveins: $arguments
    %0:i32 = ARGUMENT_I32 0, implicit $arguments
    %1:i64 = ARGUMENT_I64 1, implicit $arguments
    dead %2:i32 = CONST_I32 7, implicit-def dead $arguments
    RETURN %0, implicit-def dead $arguments
...
---
name:            g
tracksRegLiveness: true
machineFunctionInfo:
  params:  [ f32, f64 ]
  results: [ f64 ]
body: |
  bb.0:
    liveins: $arguments
    %0:f64 = ARGUMENT_F64 1, implicit $arguments
    RETURN %0, implicit-def dead $arguments
...
