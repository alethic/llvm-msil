; Comment-only inline asm barriers (the section 12 rows of 2026-10-26 'M22.b
; extended (Rust-found defects 2 and 4)' item 2 and 'M22.e extended
; (Rust-found defects 1-4, tests)'; ImplementationPlan.md M12.a (3) and
; decision D36 as amended for M12; asm-operands.ll has M12's empty asm).
; Rust's zmij (serde_json's float formatter) writes asm!("/*{0}*/",
; inout(reg) x) as an optimization barrier, IR
;   asm sideeffect alignstack inteldialect "/*${0:k}*/",
;       "=&r,0,~{dirflag},~{fpsr},~{flags},~{memory}"
; An inline asm whose template holds nothing but whitespace and '/* ... */'
; comments is accepted like M12's empty barrier asm: the D36 compiler fence
; (MEMBARRIER, printed as '//MEMBARRIER'), each output tied to an input
; (early clobber or not) becomes that input's value, so the value passes
; through, and memory operations do not move across it; one with no
; operands at all, only clobbers, is the fence too. So comment.ll, whose
; templates are comments ('$' operand references, newlines and tabs
; included), gives exactly the IL of empty.ll, the same functions with
; empty templates, at -O0 and -O2 on both x86_64 MSIL triples, with no
; diagnostic, and the output assembles.
;
; errors.ll: every other inline asm keeps 'inline assembly is not supported
; for MSIL', naming its function, whose body is deleted: an untied output
; (with no input, or with an untied input) and any text besides the
; comments, before them, after them or between two; and, with a tied output
; ('=r,0'), a line comment ('// ${0:k}' or '# ${0:k}') and an unterminated
; block comment ('/* ${0:k}'), which only block comments count as (section
; 12 row of 2026-10-27 'Inline asm acceptance as built': MSILPrepareIR's
; isBlankAsmString accepts whitespace and complete '/* ... */' comments
; only). Nothing crashes, and the IL, which keeps the function after them,
; assembles.

; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/comment.ll \
; RUN:   -o %t/comment.lnx.o2.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/empty.ll \
; RUN:   -o %t/empty.lnx.o2.il 2>&1 | count 0
; RUN: diff %t/empty.lnx.o2.il %t/comment.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/comment.ll \
; RUN:   -o %t/comment.win.o2.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/empty.ll \
; RUN:   -o %t/empty.win.o2.il 2>&1 | count 0
; RUN: diff %t/empty.win.o2.il %t/comment.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/comment.ll \
; RUN:   -o %t/comment.lnx.o0.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/empty.ll \
; RUN:   -o %t/empty.lnx.o0.il 2>&1 | count 0
; RUN: diff %t/empty.lnx.o0.il %t/comment.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/comment.ll \
; RUN:   -o %t/comment.win.o0.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/empty.ll \
; RUN:   -o %t/empty.win.o0.il 2>&1 | count 0
; RUN: diff %t/empty.win.o0.il %t/comment.win.o0.il
; RUN: FileCheck %s --check-prefixes=CHECK,O2 --implicit-check-not="{{^ +call}}" --implicit-check-not=localloc \
; RUN:   --input-file=%t/comment.lnx.o2.il
; RUN: FileCheck %s --check-prefixes=CHECK,O0 --implicit-check-not="{{^ +call}}" --implicit-check-not=localloc \
; RUN:   --input-file=%t/comment.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/comment.lnx.o2.dll %t/comment.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/comment.win.o0.dll %t/comment.win.o0.il %}

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

; The value passes through: Rust's barrier returns its argument.
; CHECK-LABEL: {{^}}.method public static int32 'barrier'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'barrier'{{$}}

; A computed value is kept before the fence and used after it.
; CHECK-LABEL: {{^}}.method public static int32 'barrier_use'(int32) cil managed{{$}}
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

; Comments with whitespace, newlines and tabs around them; a 64-bit value.
; CHECK-LABEL: {{^}}.method public static native int 'barrier_ws'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}

; A pointer.
; CHECK-LABEL: {{^}}.method public static native int 'barrier_ptr'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}

; No operands, only a memory clobber: the stores stay on both sides.
; CHECK-LABEL: {{^}}.method public static void 'barrier_mem'(native int) cil managed{{$}}
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

; Two tied outputs of different types.
; CHECK-LABEL: {{^}}.method public static native int 'barrier_two'(int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}

; ERR: {{^}}error: {{.*}}in function asm_comment_untied {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_comment_untied_input {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_comment_then_text {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_text_then_comment {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_text_between {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_line_comment {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_hash_comment {{.*}}: inline assembly is not supported for MSIL{{$}}
; ERR: {{^}}error: {{.*}}in function asm_unterminated_comment {{.*}}: inline assembly is not supported for MSIL{{$}}

; The diagnosed functions are gone; the function after them stays.
; ERR-IL:      {{^}}.method public static int32 'after_asm'(int32, int32) cil managed{{$}}
; ERR-IL-NEXT: {{^\{$}}
; ERR-IL-NEXT: {{^}}  .maxstack 2{{$}}
; ERR-IL-NEXT: {{^}}  ldarg.0{{$}}
; ERR-IL-NEXT: {{^}}  ldarg.1{{$}}
; ERR-IL-NEXT: {{^}}  add{{$}}
; ERR-IL-NEXT: {{^}}  ret{{$}}

;--- comment.ll
define i32 @barrier(i32 %x) {
  %r = call i32 asm sideeffect alignstack inteldialect "/*${0:k}*/", "=&r,0,~{dirflag},~{fpsr},~{flags},~{memory}"(i32 %x)
  ret i32 %r
}

define i32 @barrier_use(i32 %x) {
  %y = add i32 %x, 5
  %r = call i32 asm sideeffect "/* keep $0 in a register */", "=r,0,~{dirflag},~{fpsr},~{flags}"(i32 %y)
  %z = mul i32 %r, 3
  ret i32 %z
}

define i64 @barrier_ws(i64 %x) {
  %r = call i64 asm " \09/* first */\0A  /*second*/ \0A", "=r,0"(i64 %x)
  ret i64 %r
}

define ptr @barrier_ptr(ptr %p) {
  %r = call ptr asm sideeffect "/*${0}*/", "=r,0,~{memory}"(ptr %p)
  ret ptr %r
}

define void @barrier_mem(ptr %p) {
  store i32 1, ptr %p, align 4
  call void asm sideeffect "/* compiler barrier */", "~{memory}"()
  store i32 2, ptr %p, align 4
  ret void
}

define i64 @barrier_two(i32 %a, i64 %b) {
  %r = call { i32, i64 } asm sideeffect "/* $0 $1 */", "=r,=r,0,1"(i32 %a, i64 %b)
  %r0 = extractvalue { i32, i64 } %r, 0
  %r1 = extractvalue { i32, i64 } %r, 1
  %w = sext i32 %r0 to i64
  %s = add i64 %w, %r1
  ret i64 %s
}

;--- empty.ll
define i32 @barrier(i32 %x) {
  %r = call i32 asm sideeffect alignstack inteldialect "", "=&r,0,~{dirflag},~{fpsr},~{flags},~{memory}"(i32 %x)
  ret i32 %r
}

define i32 @barrier_use(i32 %x) {
  %y = add i32 %x, 5
  %r = call i32 asm sideeffect "", "=r,0,~{dirflag},~{fpsr},~{flags}"(i32 %y)
  %z = mul i32 %r, 3
  ret i32 %z
}

define i64 @barrier_ws(i64 %x) {
  %r = call i64 asm "", "=r,0"(i64 %x)
  ret i64 %r
}

define ptr @barrier_ptr(ptr %p) {
  %r = call ptr asm sideeffect "", "=r,0,~{memory}"(ptr %p)
  ret ptr %r
}

define void @barrier_mem(ptr %p) {
  store i32 1, ptr %p, align 4
  call void asm sideeffect "", "~{memory}"()
  store i32 2, ptr %p, align 4
  ret void
}

define i64 @barrier_two(i32 %a, i64 %b) {
  %r = call { i32, i64 } asm sideeffect "", "=r,=r,0,1"(i32 %a, i64 %b)
  %r0 = extractvalue { i32, i64 } %r, 0
  %r1 = extractvalue { i32, i64 } %r, 1
  %w = sext i32 %r0 to i64
  %s = add i64 %w, %r1
  ret i64 %s
}

;--- errors.ll
define i32 @asm_comment_untied() {
  %r = call i32 asm sideeffect "/*${0:k}*/", "=r"()
  ret i32 %r
}

define i32 @asm_comment_untied_input(i32 %x) {
  %r = call i32 asm sideeffect "/*${0:k}*/", "=r,r"(i32 %x)
  ret i32 %r
}

define i32 @asm_comment_then_text(i32 %x) {
  %r = call i32 asm sideeffect "/* barrier */ nop", "=r,0"(i32 %x)
  ret i32 %r
}

define i32 @asm_text_then_comment(i32 %x) {
  %r = call i32 asm sideeffect "nop /* barrier */", "=r,0"(i32 %x)
  ret i32 %r
}

define void @asm_text_between() {
  call void asm sideeffect "/* a */ pause /* b */", "~{memory}"()
  ret void
}

define i32 @asm_line_comment(i32 %x) {
  %r = call i32 asm sideeffect "// ${0:k}", "=r,0"(i32 %x)
  ret i32 %r
}

define i32 @asm_hash_comment(i32 %x) {
  %r = call i32 asm sideeffect "# ${0:k}", "=r,0"(i32 %x)
  ret i32 %r
}

define i32 @asm_unterminated_comment(i32 %x) {
  %r = call i32 asm sideeffect "/* ${0:k}", "=r,0"(i32 %x)
  ret i32 %r
}

define i32 @after_asm(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}
