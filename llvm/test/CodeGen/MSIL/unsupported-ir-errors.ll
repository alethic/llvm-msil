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
; diagnostics of MSILPrepareIR alone and the IR it leaves behind. M3 adds the
; identifier length limit (D11 amended), garbage collection strategies and the
; stack map intrinsics (amendment log 2026-10-13, fix-loop rows for fix-5).
;
; Calls are implemented in M4 and global variables in M8, so no definition that
; is compiled here may call a function or use a global: functions that use a
; diagnosed symbol are available_externally, which is never code-generated but
; still has to be rewritten, and the resolver an ifunc needs is a variadic
; definition, which loses its body. Leaf definitions compile since M3.

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

; IL identifiers are at most 1023 bytes (D11 and plan 5.1 amended): Framework
; and NuGet ilasm reject a name of 1024 or more UTF-8 bytes ('name too long'),
; counted after they decode the \' and \134 escapes. genname.py takes
; KIND:BYTES arguments and prints, for each, a definition returning 1 whose
; name has BYTES bytes: 'a' (ascii), U+00E9 and a final 'a' for an odd count
; (e-acute), or a quote, a backslash and then 'a' (quote-backslash); or a
; declaration named with 'b', called by an available_externally function
; (used-decl); or a declaration named with 'c' and never used (unused-decl).
; The well-formed '@ok' comes last. At 1023 bytes everything compiles silently
; and assembles, and the declarations are not reported.
; RUN: %python %t/genname.py ascii:1023 e-acute:1023 quote-backslash:1023 used-decl:1023 unused-decl:1024 \
; RUN:   > %t/name-1023.ll
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/name-1023.ll \
; RUN:   -o %t/name-1023.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NAME-OK --input-file=%t/name-1023.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/name-1023.ll \
; RUN:   -o %t/name-1023.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NAME-OK --input-file=%t/name-1023.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/name-1023.lnx.dll %t/name-1023.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/name-1023.win.dll %t/name-1023.win.il %}

; NAME-OK:      {{^}}.method public static int32 '{{a+}}'() cil managed{{$}}
; NAME-OK-NEXT: {{^\{$}}
; NAME-OK-NEXT: {{^}}  .maxstack 1{{$}}
; NAME-OK-NEXT: {{^}}  ldc.i4.1{{$}}
; NAME-OK-NEXT: {{^}}  ret{{$}}
; NAME-OK-NEXT: {{^}}} // end of method '{{a+}}'{{$}}
; NAME-OK:      {{^}}.method public static int32 '{{[^a']+}}a'() cil managed{{$}}
; NAME-OK:      {{^}}.method public static int32 '\'\134{{a+}}'() cil managed{{$}}
; NAME-OK:      {{^}}.method public static int32 'ok'() cil managed{{$}}

; At 1024 bytes each definition and the used declaration is an error naming
; the function in full, the definitions lose their bodies, and '@ok' still
; compiles to IL that assembles. llc deletes its -o file after an error, so
; that IL is taken from stdout.
; RUN: %python %t/genname.py ascii:1024 e-acute:1024 quote-backslash:1024 used-decl:1024 > %t/name-1024.ll
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/name-1024.ll -o /dev/null 2>&1 \
; RUN:   | %{nocrash} --check-prefix=NAME-LONG --implicit-check-not=error:
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/name-1024.ll -o /dev/null 2>&1 \
; RUN:   | %{nocrash} --check-prefix=NAME-LONG --implicit-check-not=error:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/name-1024.ll -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=NAME-LONG-IR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/name-1024.ll \
; RUN:   > %t/name-1024.win.il
; RUN: FileCheck %s --check-prefix=NAME-LONG-IL --input-file=%t/name-1024.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/name-1024.dll %t/name-1024.win.il %}

; NAME-LONG: error: {{.*}}in function {{a+}} i32 (): identifier of 1024 bytes is longer than the 1023 bytes ilasm accepts
; NAME-LONG: error: {{.*}}in function {{[^a' ]+}} i32 (): identifier of 1024 bytes is longer than the 1023 bytes ilasm accepts
; NAME-LONG: error: {{.*}}in function '\{{a+}} i32 (): identifier of 1024 bytes is longer than the 1023 bytes ilasm accepts
; NAME-LONG: error: {{.*}}in function {{b+}} i32 (): identifier of 1024 bytes is longer than the 1023 bytes ilasm accepts

; NAME-LONG-IR: declare i32 @{{a+}}(){{$}}
; NAME-LONG-IR: declare i32 @"{{(\\C3\\A9)+}}"(){{$}}
; NAME-LONG-IR: declare i32 @"'\\{{a+}}"(){{$}}
; NAME-LONG-IR: declare i32 @{{b+}}(){{$}}
; NAME-LONG-IR: define i32 @ok()

; NAME-LONG-IL-NOT: .method
; NAME-LONG-IL:     {{^}}.method public static int32 'ok'() cil managed{{$}}
; NAME-LONG-IL-NOT: .method

; Garbage collection strategies: MSIL emits no stack maps or GC frame tables,
; so a definition with any gc is an error. It loses its body and its strategy
; before GC lowering instantiates the strategy; otherwise the AsmPrinter would
; print native frame tables ("erlang") or crash ("ocaml"), and the shadow stack
; lowering would add a global ("shadow-stack"). An available_externally
; definition is never compiled and only loses its strategy. The definition
; without a gc still compiles, and the IL has no frame tables.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/gc.ll -o /dev/null 2>&1 \
; RUN:   | %{nocrash} --check-prefix=GC --implicit-check-not=error:
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/gc.ll -o /dev/null 2>&1 \
; RUN:   | %{nocrash} --check-prefix=GC --implicit-check-not=error:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/gc.ll -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=GC-IR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/gc.ll > %t/gc.lnx.il
; RUN: FileCheck %s --check-prefix=GC-IL --input-file=%t/gc.lnx.il --implicit-check-not=.short --implicit-check-not=.p2align
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/gc.ll > %t/gc.win.il
; RUN: FileCheck %s --check-prefix=GC-IL --input-file=%t/gc.win.il --implicit-check-not=.short --implicit-check-not=.p2align
; RUN: %if ilasm %{ %ilasm-verify -output=%t/gc.dll %t/gc.win.il %}

; GC: error: {{.*}}in function msil_gc_erlang i32 (): garbage collection strategy 'erlang' is not supported for MSIL
; GC: error: {{.*}}in function msil_gc_ocaml i32 (): garbage collection strategy 'ocaml' is not supported for MSIL
; GC: error: {{.*}}in function msil_gc_shadow_stack i32 (): garbage collection strategy 'shadow-stack' is not supported for MSIL
; GC: error: {{.*}}in function msil_gc_statepoint_example i32 (): garbage collection strategy 'statepoint-example' is not supported for MSIL
; GC: error: {{.*}}in function msil_gc_coreclr i32 (): garbage collection strategy 'coreclr' is not supported for MSIL

; GC-IR: declare i32 @msil_gc_erlang(){{$}}
; GC-IR: declare i32 @msil_gc_ocaml(){{$}}
; GC-IR: declare i32 @msil_gc_shadow_stack(){{$}}
; GC-IR: declare i32 @msil_gc_statepoint_example(){{$}}
; GC-IR: declare i32 @msil_gc_coreclr(){{$}}
; GC-IR: define available_externally i32 @msil_gc_avail() {{\{$}}
; GC-IR: define i32 @msil_gc_free() {{\{$}}

; GC-IL-NOT: .method
; GC-IL:     {{^}}.method public static int32 'msil_gc_free'() cil managed{{$}}
; GC-IL-NOT: .method

; Stack map intrinsics: llvm.experimental.stackmap and both forms of
; llvm.experimental.patchpoint describe native frames to a runtime that reads
; or patches machine code. Each call is an error and its function loses its
; body. A statepoint needs a gc on its function (the IR verifier checks it), so
; it is reported as a garbage collection strategy, before the others. The
; definition without them still compiles.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/stackmap.ll -o /dev/null 2>&1 \
; RUN:   | %{nocrash} --check-prefix=STACKMAP --implicit-check-not=error:
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/stackmap.ll -o /dev/null 2>&1 \
; RUN:   | %{nocrash} --check-prefix=STACKMAP --implicit-check-not=error:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/stackmap.ll -o /dev/null 2>&1 \
; RUN:   | %{nocrash} --check-prefix=STACKMAP --implicit-check-not=error:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/stackmap.ll -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=STACKMAP-IR --implicit-check-not="{{ call }}"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/stackmap.ll \
; RUN:   > %t/stackmap.il
; RUN: FileCheck %s --check-prefix=STACKMAP-IL --input-file=%t/stackmap.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/stackmap.dll %t/stackmap.il %}

; STACKMAP: error: {{.*}}in function msil_statepoint void (): garbage collection strategy 'statepoint-example' is not supported for MSIL
; STACKMAP: error: {{.*}}in function msil_stackmap i32 (i32): stack maps (llvm.experimental.stackmap) are not supported for MSIL
; STACKMAP: error: {{.*}}in function msil_patchpoint_void void (): stack maps (llvm.experimental.patchpoint.void) are not supported for MSIL
; STACKMAP: error: {{.*}}in function msil_patchpoint i64 (i64): stack maps (llvm.experimental.patchpoint) are not supported for MSIL

; STACKMAP-IR: declare void @msil_statepoint(){{$}}
; STACKMAP-IR: declare i32 @msil_stackmap(i32){{$}}
; STACKMAP-IR: declare void @msil_patchpoint_void(){{$}}
; STACKMAP-IR: declare i64 @msil_patchpoint(i64){{$}}
; STACKMAP-IR: define i32 @msil_stackmap_free() {{\{$}}

; STACKMAP-IL-NOT: .method
; STACKMAP-IL:     {{^}}.method public static int32 'msil_stackmap_free'() cil managed{{$}}
; STACKMAP-IL-NOT: .method

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

;--- genname.py
import sys

print('source_filename = "name.c"')
for arg in sys.argv[1:]:
    kind, size = arg.split(":")
    size = int(size)
    print()
    if kind == "used-decl":
        name = "b" * size
        print('declare i32 @"%s"()' % name)
        print()
        print("define available_externally i32 @used_decl_user() {")
        print('  %%r = call i32 @"%s"()' % name)
        print("  ret i32 %r")
        print("}")
        continue
    if kind == "unused-decl":
        print('declare i32 @"%s"()' % ("c" * size))
        continue
    if kind == "ascii":
        name = "a" * size
    elif kind == "e-acute":
        name = "\\C3\\A9" * (size // 2) + "a" * (size % 2)
    elif kind == "quote-backslash":
        name = "'\\5C" + "a" * (size - 2)
    else:
        sys.exit("unknown kind " + kind)
    print('define i32 @"%s"() {' % name)
    print("  ret i32 1")
    print("}")
print()
print("define i32 @ok() {")
print("  ret i32 3")
print("}")

;--- gc.ll
source_filename = "gc-strategy.c"

define i32 @msil_gc_erlang() gc "erlang" {
  ret i32 1
}

define i32 @msil_gc_ocaml() gc "ocaml" {
  ret i32 2
}

define i32 @msil_gc_shadow_stack() gc "shadow-stack" {
  ret i32 3
}

define i32 @msil_gc_statepoint_example() gc "statepoint-example" {
  ret i32 4
}

define i32 @msil_gc_coreclr() gc "coreclr" {
  ret i32 5
}

define available_externally i32 @msil_gc_avail() gc "erlang" {
  ret i32 6
}

define i32 @msil_gc_free() {
  ret i32 7
}

;--- stackmap.ll
source_filename = "stackmap.c"

define void @msil_statepoint() gc "statepoint-example" {
  %t = call token (i64, i32, ptr, i32, i32, ...) @llvm.experimental.gc.statepoint.p0(i64 1, i32 0, ptr elementtype(void ()) @msil_statepoint_target, i32 0, i32 0, i32 0, i32 0)
  ret void
}

define i32 @msil_stackmap(i32 %x) {
  call void (i64, i32, ...) @llvm.experimental.stackmap(i64 2, i32 0, i32 %x)
  ret i32 %x
}

define void @msil_patchpoint_void() {
  call void (i64, i32, ptr, i32, ...) @llvm.experimental.patchpoint.void(i64 3, i32 16, ptr null, i32 0)
  ret void
}

define i64 @msil_patchpoint(i64 %x) {
  %r = call i64 (i64, i32, ptr, i32, ...) @llvm.experimental.patchpoint.i64(i64 4, i32 16, ptr null, i32 0, i64 %x)
  ret i64 %r
}

define i32 @msil_stackmap_free() {
  ret i32 5
}

declare void @msil_statepoint_target()
declare token @llvm.experimental.gc.statepoint.p0(i64, i32, ptr, i32, i32, ...)
declare void @llvm.experimental.stackmap(i64, i32, ...)
declare void @llvm.experimental.patchpoint.void(i64, i32, ptr, i32, ...)
declare i64 @llvm.experimental.patchpoint.i64(i64, i32, ptr, i32, ...)

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
