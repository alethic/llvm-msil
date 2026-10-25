; Empty inline asm with operands (ImplementationPlan.md M12.a (3), decision
; D36 as amended by the section 12 row of 2026-10-21 for M12; recon
; p2/backlog.md 2.3). Rust's black_box and C's DoNotOptimize idioms are calls
; of an InlineAsm whose string is empty: MSILPrepareIR accepts such a call
; (not an invoke or a callbr) when its inputs are of any kind (register,
; memory, indirect memory, alternatives, immediates, 'g'), its outputs are
; indirect ('=*m') or direct and tied to an input, with any clobbers and no
; labels, sideeffect or not. It becomes the D36 compiler fence (the
; MEMBARRIER pseudo, printed as a tab and '//MEMBARRIER', fence.ll), one
; llvm.fake.use of every input that is not a constant and is an integer of at
; most 64 bits, a pointer, a float or a double, and llvm.sideeffect (which
; emits nothing); a tied output becomes its input (two of them an aggregate
; built from the inputs). So no call is printed, the input's computation is
; kept (stored to a local or popped) before the barrier, and loads and stores
; do not move across it: a store before the barrier and one after it are both
; kept, and memory is read again after it. An immediate, an i128 and a vector
; input get no fake use and are accepted. llc reports nothing at -O0 and -O2
; on both triples, and the output assembles.
;
; Tied outputs follow the section 12 row 'D36 row of 2026-10-21 (...)
; clarified' (2026-10-21): clang's acceptance rule is the only one, and
; MSILPrepareIR lowers every tied shape clang emits for it. A direct output is
; tied to the input of its numeric matching constraint ('=r,0') or, for a
; local register variable, to the input whose constraint is the output's own
; '{<reg>}' ('={eax},{eax}', clang's "+r" on register int x asm("eax")). The
; output is that input when the types are equal, the input zero-extended when
; it is an integer narrower than the integer output (_Bool "+r": an i1 input
; tied to an i8 output), and the input converted with inttoptr or ptrtoint
; when one is a pointer and the other an integer of the pointer's width
; ('=r'(p) : '0'(n)). The expected IL of these shapes came from llc before the
; change on the hand-lowered IR (fence, llvm.fake.use, llvm.sideeffect, the
; cast), which gives the same IL whether the cast is placed before the fence
; or after llvm.sideeffect.
;
; Indirect outputs and alternatives follow the section 12 row 'D36 rows of
; 2026-10-21 clarified (review-r2)'. (1) An output that clang emits indirect
; although an input is tied to it by number holds after the statement what
; native x86 leaves in it. With one alternative ('=*rm,0' from "=rm" with
; "0", '=*imr,0' from "=g") native code takes the register alternative, so
; MSILPrepareIR stores the tied input through the output pointer right after
; llvm.sideeffect: as it is when its size is the elementtype's (i32, i64), and
; truncated when it is a wider integer (an i8 output tied to an i32 input gets
; the low byte). With a memory alternative ('=*r|m,0|0' from "=r,m" with
; "0,0", '=*r|m,0' from "+r,m") and for an indirect output tied to an
; indirect input ('=*m,*0' from "=m" with "0") native code takes the memory
; alternative and the memory keeps its contents: nothing is stored. Each
; output's memory holds 5 before the statement and the tied input is an
; argument, so the IL shows which of the two the output holds after it. The
; expected IL of the stores came from llc before the change on the
; hand-lowered IR; after the store, -O2 returns the stored argument (the load
; is forwarded) and -O0 reloads it. (2) A direct output whose tie is written
; with alternatives ('=r|r,0|0' from "=r,r" with "0,0", '=r|r,r|0' from
; "r,0", '=r|r,r|r,r|0' from "r,r" and "r,0") is tied to the input of its
; first alternative that has a matching input and is lowered like a numeric
; tie: the result is that input. Tied asms in a chain (the second's input is
; the first's result, also through a pointer -> i64 -> pointer round trip)
; leave no asm, and the second's input is the first's input (cast). An asm
; with no operands at all, only clobbers, is the fence too (stores on both
; sides stay in order); in Phase 1 it was an error.
;
; errors.ll (a split part): every other inline asm keeps the Phase 1 error
; 'inline assembly is not supported for MSIL', naming its function, whose
; body is deleted: a non-empty string ("nop"), an untied register output
; ('=r,r', and '=r' with no input), an output tied to an input wider than it
; (clang widens the output instead), an output whose register differs from
; its input's ('={eax},{ecx}': not tied), an output tied to an input of
; another kind (float input, i32 output), a callbr, an unwinding invoke, a
; condition-flag output ('={@ccz},0', clang's "+@ccz" and "=@ccz" with "0":
; native code sets it from the flags after the statement; that row's part
; (3)) and an indirect float output tied to a double input ('=*rm,0'; part
; (1)). Nothing crashes, and the IL on stdout assembles.
;
; aarch64-q.ll (a split part): AArch64's 'Q' (memory addressed by one base
; register) is a memory alternative, like x86's 'm'. clang emits '=*r|Q,0|0'
; for "=r,Q" with "0,0" and '=*Q|r,0|0' for "=Q,r" with "0,0" on
; aarch64-*-msil (an int local x = 5 and an int y), and native AArch64 code
; takes the memory alternative at -O0 and -O2 (it stores nothing and returns
; the 5), so MSILPrepareIR stores nothing either, as in tied_out_alt_mem:
; llc reports nothing on aarch64-unknown-linux-gnu-msil and
; aarch64-pc-windows-msvc-msil at -O0 and -O2, the memory keeps its 5, and the
; output assembles as an ARM64 image ('-arm64', as in aarch64-triples.ll). On
; x86 'Q' is a register class (a, b, c and d; clang emits a direct '=r|Q,0|0'
; there), so every alternative of the same IR is a register and the indirect
; tie keeps 'inline assembly is not supported for MSIL', naming each
; function, on both x86_64 triples at -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="{{^ +call}}" \
; DEFINE:   --implicit-check-not=localloc --implicit-check-not=MEMBARRIER
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o2.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t/main.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o2.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t/main.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t/main.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t/main.win.o0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/main.ll -o %t/main.prep.ll
; RUN: FileCheck %s --check-prefix=IR --input-file=%t/main.prep.ll --implicit-check-not=" asm "
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
; RUN: FileCheck %s --check-prefix=ERR-IL --input-file=%t/errors.lnx.il --implicit-check-not=".method "
; RUN: %if ilasm %{ %ilasm-verify -output=%t/errors.dll %t/errors.lnx.il %}

; DEFINE: %{a64} = FileCheck %s --check-prefix=A64 --implicit-check-not=int64 \
; DEFINE:   --implicit-check-not="{{^ +call}}" --implicit-check-not=localloc --implicit-check-not=MEMBARRIER \
; DEFINE:   --implicit-check-not=ldarg.1
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/aarch64-q.ll \
; RUN:   -o %t/a64.lnx.o2.il 2>&1 | count 0
; RUN: %{a64} --input-file=%t/a64.lnx.o2.il
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/aarch64-q.ll \
; RUN:   -o %t/a64.win.o2.il 2>&1 | count 0
; RUN: %{a64} --input-file=%t/a64.win.o2.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/aarch64-q.ll -o %t/a64.lnx.o0.il 2>&1 | count 0
; RUN: %{a64} --input-file=%t/a64.lnx.o0.il
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/aarch64-q.ll -o %t/a64.win.o0.il 2>&1 | count 0
; RUN: %{a64} --input-file=%t/a64.win.o0.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/aarch64-q.ll \
; RUN:   -o %t/a64.prep.ll
; RUN: FileCheck %s --check-prefix=A64-IR --input-file=%t/a64.prep.ll --implicit-check-not=" asm " \
; RUN:   --implicit-check-not="store i32 %y"
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/a64.lnx.o2.dll %t/a64.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/a64.win.o0.dll %t/a64.win.o0.il %}

; DEFINE: %{x86q} = FileCheck %s --check-prefix=X86Q --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/aarch64-q.ll -o /dev/null 2>&1 \
; RUN:   | %{x86q}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/aarch64-q.ll -o /dev/null 2>&1 \
; RUN:   | %{x86q}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/aarch64-q.ll -o /dev/null \
; RUN:   2>&1 | %{x86q}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/aarch64-q.ll -o /dev/null \
; RUN:   2>&1 | %{x86q}

;--- main.ll
source_filename = "asm-operands.c"

; "r" on an integer: the multiplication survives, then the barrier.
; IR-LABEL: define i32 @keep_r(
; IR:       %m = mul i32 %x, 7
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR-NEXT:  call void (...) @llvm.fake.use(i32 %m)
; IR-NEXT:  call void @llvm.sideeffect()
; IR-NEXT:  ret i32 0
; CHECK-LABEL: {{^}}.method public static int32 'keep_r'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  starg.s 0{{$}}
; O0-NEXT:     {{^}}  pop{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'keep_r'{{$}}
define i32 @keep_r(i32 %x) {
  %m = mul i32 %x, 7
  call void asm sideeffect "", "r"(i32 %m)
  ret i32 0
}

; rustc's black_box: "r,~{memory}" on a pointer. Both stores stay, in order.
; CHECK-LABEL: {{^}}.method public static void 'keep_ptr_memory'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @keep_ptr_memory(ptr %p) {
  store i32 1, ptr %p, align 4
  call void asm sideeffect "", "r,~{memory}"(ptr %p)
  store i32 2, ptr %p, align 4
  ret void
}

; An indirect memory input ("*m", as clang writes "m" on an lvalue), with
; clang's x86 clobbers: memory is read after the barrier.
; CHECK-LABEL: {{^}}.method public static int32 'keep_mem'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @keep_mem(ptr %p) {
  call void asm sideeffect "", "*m,~{memory},~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %p)
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

; Alternatives ("r|m", from C's "r,m"), an immediate and "g".
; IR-LABEL: define void @keep_imm(
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR-NEXT:  call void @llvm.sideeffect()
; IR-NEXT:  ret void
; CHECK-LABEL: {{^}}.method public static void 'keep_rm'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @keep_rm(i64 %x) {
  call void asm sideeffect "", "r|m,~{memory},~{dirflag},~{fpsr},~{flags}"(i64 %x)
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'keep_imm'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @keep_imm() {
  call void asm sideeffect "", "i"(i32 42)
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'keep_g'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @keep_g(i32 %x) {
  call void asm sideeffect "", "g"(i32 %x)
  ret void
}

; A tied output ("=r,0", clang's "+r") is its input.
; IR-LABEL: define i32 @tied(
; IR:       %y = add i32 %x, 5
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR-NEXT:  call void (...) @llvm.fake.use(i32 %y)
; IR-NEXT:  call void @llvm.sideeffect()
; IR-NEXT:  %z = mul i32 %y, 3
; CHECK-LABEL: {{^}}.method public static int32 'tied'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; O2-NEXT:     {{^}}  starg.s 0{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @tied(i32 %x) {
  %y = add i32 %x, 5
  %r = call i32 asm sideeffect "", "=r,0,~{dirflag},~{fpsr},~{flags}"(i32 %y)
  %z = mul i32 %r, 3
  ret i32 %z
}

; The asm's result returned as it is: the argument.
; CHECK-LABEL: {{^}}.method public static int32 'tied_ret'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_ret'{{$}}
define i32 @tied_ret(i32 %x) {
  %r = call i32 asm sideeffect "", "=r,0"(i32 %x)
  ret i32 %r
}

; Two tied outputs of different types: the struct result is built from the
; inputs.
; CHECK-LABEL: {{^}}.method public static native int 'tied_two'(int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @tied_two(i32 %a, i64 %b) {
  %r = call { i32, i64 } asm sideeffect "", "=r,=r,0,1"(i32 %a, i64 %b)
  %r0 = extractvalue { i32, i64 } %r, 0
  %r1 = extractvalue { i32, i64 } %r, 1
  %e = sext i32 %r0 to i64
  %s = add i64 %e, %r1
  ret i64 %s
}

; _Bool "+r": clang ties an i8 output to the i1 input. The output is the
; input zero-extended (a sign extension would give 255 for true).
; IR-LABEL: define i32 @tied_bool(
; IR-NEXT:  %b = icmp slt i32 %x, 10
; IR-DAG:   fence syncscope("singlethread") seq_cst
; IR-DAG:   call void (...) @llvm.fake.use(i1 %b)
; IR-DAG:   call void @llvm.sideeffect()
; IR-DAG:   [[BOOL:%[^ ]+]] = zext i1 %b to i8
; IR:       %c = zext i8 [[BOOL]] to i32
; IR-NEXT:  ret i32 %c
; CHECK-LABEL: {{^}}.method public static int32 'tied_bool'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 10{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  starg.s 0{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_bool'{{$}}
define i32 @tied_bool(i32 %x) {
  %b = icmp slt i32 %x, 10
  %r = call i8 asm sideeffect "", "=r,0,~{dirflag},~{fpsr},~{flags}"(i1 %b)
  %c = zext i8 %r to i32
  ret i32 %c
}

; A pointer output tied to an integer input of the pointer's width
; ("=r"(p) : "0"(n), Linux's RELOC_HIDE): inttoptr of the input.
; IR-LABEL: define ptr @tied_inttoptr(
; IR-NEXT:  %m = add i64 %n, 16
; IR-DAG:   fence syncscope("singlethread") seq_cst
; IR-DAG:   call void (...) @llvm.fake.use(i64 %m)
; IR-DAG:   call void @llvm.sideeffect()
; IR-DAG:   [[I2P:%[^ ]+]] = inttoptr i64 %m to ptr
; IR:       ret ptr [[I2P]]
; CHECK-LABEL: {{^}}.method public static native int 'tied_inttoptr'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; O2-NEXT:     {{^}}  starg.s 0{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_inttoptr'{{$}}
define ptr @tied_inttoptr(i64 %n) {
  %m = add i64 %n, 16
  %r = call ptr asm sideeffect "", "=r,0,~{dirflag},~{fpsr},~{flags}"(i64 %m)
  ret ptr %r
}

; An integer output of the pointer's width tied to a pointer input: ptrtoint
; of the input, stored.
; IR-LABEL: define void @tied_ptrtoint(
; IR-NEXT:  %q = getelementptr i8, ptr %p, i64 16
; IR-DAG:   fence syncscope("singlethread") seq_cst
; IR-DAG:   call void (...) @llvm.fake.use(ptr %q)
; IR-DAG:   call void @llvm.sideeffect()
; IR-DAG:   [[P2I:%[^ ]+]] = ptrtoint ptr %q to i64
; IR:       store i64 [[P2I]], ptr %out, align 8
; IR-NEXT:  ret void
; CHECK-LABEL: {{^}}.method public static void 'tied_ptrtoint'(native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; O2-NEXT:     {{^}}  starg.s 0{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_ptrtoint'{{$}}
define void @tied_ptrtoint(ptr %p, ptr %out) {
  %q = getelementptr i8, ptr %p, i64 16
  %r = call i64 asm sideeffect "", "=r,0,~{dirflag},~{fpsr},~{flags}"(ptr %q)
  store i64 %r, ptr %out, align 8
  ret void
}

; A local register variable's "+r" ties the output to the input with the
; same '{<reg>}' constraint: the output is the input.
; IR-LABEL: define i32 @tied_regvar(
; IR-NEXT:  %z = mul i32 %y, 3
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR-NEXT:  call void (...) @llvm.fake.use(i32 %z)
; IR-NEXT:  call void @llvm.sideeffect()
; IR-NEXT:  ret i32 %z
; CHECK-LABEL: {{^}}.method public static int32 'tied_regvar'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  starg.s 0{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_regvar'{{$}}
define i32 @tied_regvar(i32 %y) {
  %z = mul i32 %y, 3
  %r = call i32 asm sideeffect "", "={eax},{eax},~{dirflag},~{fpsr},~{flags}"(i32 %z)
  ret i32 %r
}

; An indirect output ("=*m"): the store before it is kept and memory is read
; again after it.
; CHECK-LABEL: {{^}}.method public static int32 'out_mem'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @out_mem(ptr %p) {
  store i32 5, ptr %p, align 4
  call void asm sideeffect "", "=*m"(ptr elementtype(i32) %p)
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

; Without 'sideeffect' the asm is lowered the same way: fence.ll's
; MEMBARRIER line, the input's computation kept, no call.
; CHECK-LABEL: {{^}}.method public static int32 'non_sideeffect'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  starg.s 0{{$}}
; O0-NEXT:     {{^}}  pop{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @non_sideeffect(i32 %x) {
  %m = mul i32 %x, 7
  call void asm "", "r"(i32 %m)
  ret i32 0
}

; Floating-point inputs get a fake use; i128 and vector inputs do not.
; IR-LABEL: define void @keep_fp(
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR-DAG:   call void (...) @llvm.fake.use(double %d)
; IR-DAG:   call void (...) @llvm.fake.use(float %f)
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  ret void
; CHECK-LABEL: {{^}}.method public static void 'keep_fp'(float64, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @keep_fp(double %d, float %f) {
  call void asm sideeffect "", "x,x"(double %d, float %f)
  ret void
}

; IR-LABEL: define void @keep_wide(
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR-NEXT:  call void @llvm.sideeffect()
; IR-NEXT:  ret void
; CHECK-LABEL: {{^}}.method public static void 'keep_wide'(native int, native int, int32, int32, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @keep_wide(i128 %v, <4 x i32> %w) {
  call void asm sideeffect "", "r,x"(i128 %v, <4 x i32> %w)
  ret void
}

; An indirect output tied by number with one alternative ("=rm" with "0"):
; the input is stored through the output pointer after llvm.sideeffect, so
; the function returns %y, not the 5 stored before.
; IR-LABEL: define i32 @tied_out_rm(
; IR-NEXT:  store i32 5, ptr %x, align 4
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  store i32 %y, ptr %x{{(, align [[:digit:]]+)?$}}
; IR-NEXT:  %v = load i32, ptr %x, align 4
; CHECK-LABEL: {{^}}.method public static int32 'tied_out_rm'(native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^  (unaligned\. [124][[:space:]]+)?stind\.i4$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_out_rm'{{$}}
define i32 @tied_out_rm(ptr %x, i32 %y) {
  store i32 5, ptr %x, align 4
  call void asm sideeffect "", "=*rm,0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %x, i32 %y)
  %v = load i32, ptr %x, align 4
  ret i32 %v
}

; "=g" with "0" (clang's '=*imr'; not sideeffect): the same store.
; IR-LABEL: define i32 @tied_out_g(
; IR-NEXT:  store i32 5, ptr %x, align 4
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  store i32 %y, ptr %x{{(, align [[:digit:]]+)?$}}
; IR-NEXT:  %v = load i32, ptr %x, align 4
; CHECK-LABEL: {{^}}.method public static int32 'tied_out_g'(native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^  (unaligned\. [124][[:space:]]+)?stind\.i4$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_out_g'{{$}}
define i32 @tied_out_g(ptr %x, i32 %y) {
  store i32 5, ptr %x, align 4
  call void asm "", "=*imr,0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %x, i32 %y)
  %v = load i32, ptr %x, align 4
  ret i32 %v
}

; An i64 output tied to an i64 input: the same store, of the native int.
; IR-LABEL: define i64 @tied_out_rm_i64(
; IR-NEXT:  store i64 5, ptr %x, align 8
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  store i64 %y, ptr %x{{(, align [[:digit:]]+)?$}}
; IR-NEXT:  %v = load i64, ptr %x, align 8
; CHECK-LABEL: {{^}}.method public static native int 'tied_out_rm_i64'(native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^  (unaligned\. [124][[:space:]]+)?stind\.i$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_out_rm_i64'{{$}}
define i64 @tied_out_rm_i64(ptr %x, i64 %y) {
  store i64 5, ptr %x, align 8
  call void asm sideeffect "", "=*rm,0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i64) %x, i64 %y)
  %v = load i64, ptr %x, align 8
  ret i64 %v
}

; A char output tied to an int input: the input's low byte is stored.
; IR-LABEL: define i32 @tied_out_rm_char(
; IR-NEXT:  store i8 5, ptr %x, align 1
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  [[LOW:%[^ ]+]] = trunc i32 %y to i8
; IR-NEXT:  store i8 [[LOW]], ptr %x{{(, align [[:digit:]]+)?$}}
; IR-NEXT:  %v = load i8, ptr %x, align 1
; CHECK-LABEL: {{^}}.method public static int32 'tied_out_rm_char'(native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  stind.i1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i1{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  conv.i1{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_out_rm_char'{{$}}
define i32 @tied_out_rm_char(ptr %x, i32 %y) {
  store i8 5, ptr %x, align 1
  call void asm sideeffect "", "=*rm,0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i8) %x, i32 %y)
  %v = load i8, ptr %x, align 1
  %e = sext i8 %v to i32
  ret i32 %e
}

; A memory alternative ("=r,m" with "0,0"): the memory keeps its 5.
; IR-LABEL: define i32 @tied_out_alt_mem(
; IR-NEXT:  store i32 5, ptr %x, align 4
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  %v = load i32, ptr %x, align 4
; CHECK-LABEL: {{^}}.method public static int32 'tied_out_alt_mem'(native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_out_alt_mem'{{$}}
define i32 @tied_out_alt_mem(ptr %x, i32 %y) {
  store i32 5, ptr %x, align 4
  call void asm sideeffect "", "=*r|m,0|0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %x, i32 %y)
  %v = load i32, ptr %x, align 4
  ret i32 %v
}

; The "+r,m" shape: no store either.
; IR-LABEL: define i32 @tied_out_rw_alt_mem(
; IR-NEXT:  store i32 5, ptr %x, align 4
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  %v = load i32, ptr %x, align 4
; CHECK-LABEL: {{^}}.method public static int32 'tied_out_rw_alt_mem'(native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_out_rw_alt_mem'{{$}}
define i32 @tied_out_rw_alt_mem(ptr %x, i32 %y) {
  store i32 5, ptr %x, align 4
  call void asm sideeffect "", "=*r|m,0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %x, i32 %y)
  %v = load i32, ptr %x, align 4
  ret i32 %v
}

; An indirect output tied to an indirect input ("=m" with "0"): no store.
; IR-LABEL: define i32 @tied_out_mem_mem(
; IR-NEXT:  store i32 5, ptr %x, align 4
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  %v = load i32, ptr %x, align 4
; CHECK-LABEL: {{^}}.method public static int32 'tied_out_mem_mem'(native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_out_mem_mem'{{$}}
define i32 @tied_out_mem_mem(ptr %x, ptr %y) {
  store i32 5, ptr %x, align 4
  call void asm "", "=*m,*0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(i32) %x, ptr elementtype(i32) %y)
  %v = load i32, ptr %x, align 4
  ret i32 %v
}

; A direct output tied with alternatives ("=r,r" with "0,0"): the input.
; IR-LABEL: define i32 @tied_alt(
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  ret i32 %y
; CHECK-LABEL: {{^}}.method public static int32 'tied_alt'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_alt'{{$}}
define i32 @tied_alt(i32 %y) {
  %r = call i32 asm sideeffect "", "=r|r,0|0"(i32 %y)
  ret i32 %r
}

; "r,0": only the second alternative matches; it names the one input.
; IR-LABEL: define i32 @tied_alt_r0(
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  ret i32 %y
; CHECK-LABEL: {{^}}.method public static int32 'tied_alt_r0'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_alt_r0'{{$}}
define i32 @tied_alt_r0(i32 %y) {
  %r = call i32 asm sideeffect "", "=r|r,r|0"(i32 %y)
  ret i32 %r
}

; Two inputs, "r,r"(a) and "r,0"(b): the first alternative with a match is
; the second, whose matching input is %b.
; IR-LABEL: define i32 @tied_alt_second(
; IR:       call void @llvm.sideeffect()
; IR-NEXT:  ret i32 %b
; CHECK-LABEL: {{^}}.method public static int32 'tied_alt_second'(int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_alt_second'{{$}}
define i32 @tied_alt_second(i32 %a, i32 %b) {
  %r = call i32 asm sideeffect "", "=r|r,r|r,r|0"(i32 %a, i32 %b)
  ret i32 %r
}

; Chained tied asms: the second's input becomes the first's input.
; IR-LABEL: define i32 @tied_chain(
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR-NEXT:  call void (...) @llvm.fake.use(i32 %x)
; IR-NEXT:  call void @llvm.sideeffect()
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR-NEXT:  call void (...) @llvm.fake.use(i32 %x)
; IR-NEXT:  call void @llvm.sideeffect()
; IR-NEXT:  ret i32 %x
; CHECK-LABEL: {{^}}.method public static int32 'tied_chain'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_chain'{{$}}
define i32 @tied_chain(i32 %x) {
  %a = call i32 asm sideeffect "", "=r,0"(i32 %x)
  %b = call i32 asm sideeffect "", "=r,0"(i32 %a)
  ret i32 %b
}

; A pointer -> i64 -> pointer round trip of two tied asms: the second's input
; is the ptrtoint of the first's input, and the result its inttoptr.
; IR-LABEL: define ptr @tied_roundtrip(
; IR-DAG:   call void (...) @llvm.fake.use(ptr %p)
; IR-DAG:   [[P2I:%[^ ]+]] = ptrtoint ptr %p to i64
; IR-DAG:   call void (...) @llvm.fake.use(i64 [[P2I]])
; IR-DAG:   [[I2P:%[^ ]+]] = inttoptr i64 [[P2I]] to ptr
; IR:       ret ptr [[I2P]]
; CHECK-LABEL: {{^}}.method public static native int 'tied_roundtrip'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'tied_roundtrip'{{$}}
define ptr @tied_roundtrip(ptr %p) {
  %i = call i64 asm "", "=r,0,~{dirflag},~{fpsr},~{flags}"(ptr %p)
  %q = call ptr asm "", "=r,0,~{dirflag},~{fpsr},~{flags}"(i64 %i)
  ret ptr %q
}

; An asm with no operands, only clobbers, between two stores.
; IR-LABEL: define void @no_operands(
; IR-NEXT:  store i32 1, ptr %p, align 4
; IR-NEXT:  fence syncscope("singlethread") seq_cst
; IR-NEXT:  call void @llvm.sideeffect()
; IR-NEXT:  store i32 2, ptr %q, align 4
; CHECK-LABEL: {{^}}.method public static void 'no_operands'(native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'no_operands'{{$}}
define void @no_operands(ptr %p, ptr %q) {
  store i32 1, ptr %p, align 4
  call void asm sideeffect "", "~{memory},~{dirflag},~{fpsr},~{flags}"()
  store i32 2, ptr %q, align 4
  ret void
}

;--- errors.ll
source_filename = "asm-errors.c"

; ERR: {{^}}error: {{.*}}in function asm_nop {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_untied {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_output_only {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_tied_other_type {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_tied_other_reg {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_tied_float {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_callbr {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_invoke {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_flag_output {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_tied_out_float {{.*}}: inline assembly is not supported for MSIL{{$}}

declare i32 @__gxx_personality_v0(...)

define i32 @asm_nop(i32 %x) {
  call void asm sideeffect "nop", "r"(i32 %x)
  ret i32 %x
}

define i32 @asm_untied(i32 %x) {
  %r = call i32 asm sideeffect "", "=r,r"(i32 %x)
  ret i32 %r
}

define i32 @asm_output_only() {
  %r = call i32 asm sideeffect "", "=r"()
  ret i32 %r
}

; The input is wider than the output (clang widens the output instead).
define i32 @asm_tied_other_type(i64 %x) {
  %r = call i32 asm sideeffect "", "=r,0"(i64 %x)
  ret i32 %r
}

; Different registers: the output is not tied to the input.
define i32 @asm_tied_other_reg(i32 %x) {
  %r = call i32 asm sideeffect "", "={eax},{ecx}"(i32 %x)
  ret i32 %r
}

; A float input tied to an i32 output (and not sideeffect): no conversion.
define i32 @asm_tied_float(float %f) {
  %r = call i32 asm "", "=r,0"(float %f)
  ret i32 %r
}

define i32 @asm_callbr(i32 %x) {
entry:
  callbr void asm sideeffect "", "r,!i"(i32 %x)
          to label %normal [label %indirect]
normal:
  ret i32 0
indirect:
  ret i32 1
}

define i32 @asm_invoke(i32 %x) personality ptr @__gxx_personality_v0 {
entry:
  invoke void asm sideeffect unwind "", "r"(i32 %x)
          to label %ok unwind label %lp
ok:
  ret i32 0
lp:
  %l = landingpad { ptr, i32 } cleanup
  ret i32 1
}

; A condition-flag output ("+@ccz", and "=@ccz" with "0"): never kept.
define i32 @asm_flag_output(i32 %x) {
  %r = call i32 asm sideeffect "", "={@ccz},0,~{dirflag},~{fpsr},~{flags}"(i32 %x)
  ret i32 %r
}

; An indirect float output tied to a double input (native x86 instruction
; selection cannot handle it either).
define i32 @asm_tied_out_float(ptr %f, double %d) {
  call void asm sideeffect "", "=*rm,0,~{dirflag},~{fpsr},~{flags}"(ptr elementtype(float) %f, double %d)
  ret i32 0
}

; ERR-IL:      {{^}}.method public static int32 'after_asm'(int32, int32) cil managed{{$}}
; ERR-IL-NEXT: {{^\{$}}
; ERR-IL-NEXT: {{^}}  .maxstack 2{{$}}
; ERR-IL-NEXT: {{^}}  ldarg.0{{$}}
; ERR-IL-NEXT: {{^}}  ldarg.1{{$}}
; ERR-IL-NEXT: {{^}}  add{{$}}
; ERR-IL-NEXT: {{^}}  ret{{$}}
define i32 @after_asm(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- aarch64-q.ll
source_filename = "asm-aarch64-q.c"

; X86Q: {{^}}error: {{.*}}in function q_alt_mem {{.*}}: inline assembly is not supported for MSIL{{$}}
; X86Q: {{^}}error: {{.*}}in function q_first_alt_mem {{.*}}: inline assembly is not supported for MSIL{{$}}

; "=r,Q" with "0,0": the memory keeps its 5.
; A64-IR-LABEL: define i32 @q_alt_mem(
; A64-IR-NEXT:  store i32 5, ptr %x, align 4
; A64-IR-NEXT:  fence syncscope("singlethread") seq_cst
; A64-IR:       call void @llvm.sideeffect()
; A64-IR-NEXT:  %v = load i32, ptr %x, align 4
; A64-LABEL: {{^}}.method public static int32 'q_alt_mem'(native int, int32) cil managed{{$}}
; A64-NEXT:  {{^\{$}}
; A64-NEXT:  {{^}}  .maxstack 2{{$}}
; A64-NEXT:  {{^}}  ldarg.0{{$}}
; A64-NEXT:  {{^}}  ldc.i4.5{{$}}
; A64-NEXT:  {{^}}  stind.i4{{$}}
; A64-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; A64-NEXT:  {{^}}  ldarg.0{{$}}
; A64-NEXT:  {{^}}  ldind.i4{{$}}
; A64-NEXT:  {{^}}  ret{{$}}
; A64-NEXT:  {{^}}} // end of method 'q_alt_mem'{{$}}
define i32 @q_alt_mem(ptr %x, i32 %y) {
  store i32 5, ptr %x, align 4
  call void asm "", "=*r|Q,0|0"(ptr elementtype(i32) %x, i32 %y)
  %v = load i32, ptr %x, align 4
  ret i32 %v
}

; "=Q,r" with "0,0": the same.
; A64-IR-LABEL: define i32 @q_first_alt_mem(
; A64-IR-NEXT:  store i32 5, ptr %x, align 4
; A64-IR-NEXT:  fence syncscope("singlethread") seq_cst
; A64-IR:       call void @llvm.sideeffect()
; A64-IR-NEXT:  %v = load i32, ptr %x, align 4
; A64-LABEL: {{^}}.method public static int32 'q_first_alt_mem'(native int, int32) cil managed{{$}}
; A64-NEXT:  {{^\{$}}
; A64-NEXT:  {{^}}  .maxstack 2{{$}}
; A64-NEXT:  {{^}}  ldarg.0{{$}}
; A64-NEXT:  {{^}}  ldc.i4.5{{$}}
; A64-NEXT:  {{^}}  stind.i4{{$}}
; A64-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; A64-NEXT:  {{^}}  ldarg.0{{$}}
; A64-NEXT:  {{^}}  ldind.i4{{$}}
; A64-NEXT:  {{^}}  ret{{$}}
; A64-NEXT:  {{^}}} // end of method 'q_first_alt_mem'{{$}}
define i32 @q_first_alt_mem(ptr %x, i32 %y) {
  store i32 5, ptr %x, align 4
  call void asm "", "=*Q|r,0|0"(ptr elementtype(i32) %x, i32 %y)
  %v = load i32, ptr %x, align 4
  ret i32 %v
}
