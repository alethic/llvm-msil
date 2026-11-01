; The trailer definition of the compiler barrier (ImplementationPlan.md
; decision D36 as M24 amends it; plan 5.14.4 T1-T6 and 5.15.6). In
; whole-program text (llc -S) each single-thread fence, empty asm barrier and
; signal fence is "call void '__msil_compiler_barrier'()" (fence.ll,
; asm-comment-barrier.ll and asm-operands.ll check the calls), and the module
; defines that method once, after every other method:
;   .method assembly static void '__msil_compiler_barrier'() cil managed noinlining
;   {
;     .maxstack 0
;     ret
;   } // end of method '__msil_compiler_barrier'
; - However many barriers and functions call it, there is one definition
;   (many.ll); a module without a barrier has none (none.ll: a system fence is
;   Interlocked::MemoryBarrier and needs no trailer).
; - A module that defines a function of that name and type calls its own
;   definition, which stays public, and gets no trailer definition (own.ll).
; - A declaration of that name and type that code calls is the same function:
;   the module gets the trailer definition and declares no P/Invoke for it,
;   with or without a barrier (decl.ll, declonly.ll); an unused declaration
;   of another type changes nothing (unused.ll).
; - A definition of another type is the error "library call
;   '__msil_compiler_barrier' does not match the type of the function of that
;   name in this module" in each function with a barrier, whose call is not
;   built, and the module gets no trailer definition (mismatch.ll); a direct
;   call of a declaration of another type is the same error, and that call is
;   not built either (mmcall.ll); a declaration of another type whose address
;   is taken is the module-level error "function '__msil_compiler_barrier' is
;   declared as int32(int32) but the toolchain defines it as void()"
;   (addr.ll).
; - In object form (-msil-object-form text and -filetype=obj objects) the
;   module defines no trailer: each barrier calls the [msil.extern] symbol,
;   which an object lists as an UNDEFINED FUNCTION symbol (5.14.4 T1; msil-ld's
;   '<trailer>' input defines it at link time), while a module that defines
;   the function calls and exports its own method.
; Three triples (x86_64-unknown-linux-gnu-msil, x86_64-pc-windows-msvc-msil,
; aarch64-unknown-linux-gnu-msil), -O2 and -O0; the whole-program outputs
; without an error assemble.

; RUN: split-file %s %t
; DEFINE: %{wp} = llc -asm-verbose=false -verify-machineinstrs
; DEFINE: %{of} = llc -asm-verbose=false -msil-object-form -verify-machineinstrs
; DEFINE: %{obj} = llc -filetype=obj -verify-machineinstrs
; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:

; Many barriers, one definition.
; DEFINE: %{many} = FileCheck %s --check-prefix=MANY --implicit-check-not=__msil_compiler_barrier \
; DEFINE:   --implicit-check-not=pinvokeimpl --implicit-check-not=MEMBARRIER
; RUN: %{wp} -mtriple=x86_64-unknown-linux-gnu-msil < %t/many.ll -o %t/many.lnx.il 2>&1 | count 0
; RUN: %{many} --input-file=%t/many.lnx.il
; RUN: %{wp} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/many.ll -o %t/many.lnx.o0.il 2>&1 | count 0
; RUN: %{many} --input-file=%t/many.lnx.o0.il
; RUN: %{wp} -mtriple=x86_64-pc-windows-msvc-msil < %t/many.ll -o %t/many.win.il 2>&1 | count 0
; RUN: %{many} --input-file=%t/many.win.il
; RUN: %{wp} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/many.ll -o %t/many.win.o0.il 2>&1 | count 0
; RUN: %{many} --input-file=%t/many.win.o0.il
; RUN: %{wp} -mtriple=aarch64-unknown-linux-gnu-msil < %t/many.ll -o %t/many.a64.il 2>&1 | count 0
; RUN: %{many} --input-file=%t/many.a64.il
; RUN: %{wp} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %t/many.ll -o %t/many.a64.o0.il 2>&1 | count 0
; RUN: %{many} --input-file=%t/many.a64.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/many.lnx.dll %t/many.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/many.win.o0.dll %t/many.win.o0.il %}
; Object form: no definition, five calls of the external symbol.
; DEFINE: %{manyof} = FileCheck %s --check-prefix=MANY-OF --implicit-check-not=__msil_compiler_barrier
; RUN: %{of} -mtriple=x86_64-unknown-linux-gnu-msil < %t/many.ll -o %t/many.lnx.of.il 2>&1 | count 0
; RUN: %{manyof} --input-file=%t/many.lnx.of.il
; RUN: %{of} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/many.ll -o %t/many.win.o0.of.il 2>&1 | count 0
; RUN: %{manyof} --input-file=%t/many.win.o0.of.il
; RUN: %{of} -mtriple=aarch64-unknown-linux-gnu-msil < %t/many.ll -o %t/many.a64.of.il 2>&1 | count 0
; RUN: %{manyof} --input-file=%t/many.a64.of.il
; RUN: %{obj} -mtriple=x86_64-unknown-linux-gnu-msil < %t/many.ll -o %t/many.lnx.o 2>&1 | count 0
; RUN: %{obj} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/many.ll -o %t/many.win.o0.o 2>&1 | count 0
; RUN: %{obj} -mtriple=aarch64-unknown-linux-gnu-msil < %t/many.ll -o %t/many.a64.o 2>&1 | count 0
; DEFINE: %{manycil} = FileCheck %s --check-prefix=MANY-CIL --implicit-check-not=__msil_compiler_barrier
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/many.lnx.o | FileCheck %s --check-prefix=MANY-LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/many.win.o0.o | FileCheck %s --check-prefix=MANY-LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/many.a64.o | FileCheck %s --check-prefix=MANY-LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t/many.lnx.o | %{manycil} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t/many.win.o0.o | %{manycil} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t/many.a64.o | %{manycil} %}

; No barrier, no definition (a system fence is not a compiler barrier).
; RUN: %{wp} -mtriple=x86_64-unknown-linux-gnu-msil < %t/none.ll -o %t/none.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONE --implicit-check-not=__msil_compiler_barrier --input-file=%t/none.lnx.il
; RUN: %{wp} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/none.ll -o %t/none.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONE --implicit-check-not=__msil_compiler_barrier --input-file=%t/none.win.o0.il
; RUN: %{wp} -mtriple=aarch64-unknown-linux-gnu-msil < %t/none.ll -o %t/none.a64.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONE --implicit-check-not=__msil_compiler_barrier --input-file=%t/none.a64.il
; RUN: %{obj} -mtriple=x86_64-unknown-linux-gnu-msil < %t/none.ll -o %t/none.lnx.o 2>&1 | count 0
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/none.lnx.o | FileCheck %s --check-prefix=NONE-LINK %}

; The module's own definition.
; DEFINE: %{own} = FileCheck %s --check-prefix=OWN --implicit-check-not=__msil_compiler_barrier \
; DEFINE:   --implicit-check-not=pinvokeimpl
; RUN: %{wp} -mtriple=x86_64-unknown-linux-gnu-msil < %t/own.ll -o %t/own.lnx.il 2>&1 | count 0
; RUN: %{own} --input-file=%t/own.lnx.il
; RUN: %{wp} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/own.ll -o %t/own.win.o0.il 2>&1 | count 0
; RUN: %{own} --input-file=%t/own.win.o0.il
; RUN: %{wp} -mtriple=aarch64-unknown-linux-gnu-msil < %t/own.ll -o %t/own.a64.il 2>&1 | count 0
; RUN: %{own} --input-file=%t/own.a64.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/own.dll %t/own.lnx.il %}
; RUN: %{of} -mtriple=x86_64-unknown-linux-gnu-msil < %t/own.ll -o %t/own.lnx.of.il 2>&1 | count 0
; RUN: %{own} --input-file=%t/own.lnx.of.il
; RUN: %{obj} -mtriple=x86_64-unknown-linux-gnu-msil < %t/own.ll -o %t/own.lnx.o 2>&1 | count 0
; RUN: %{obj} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %t/own.ll -o %t/own.a64.o0.o 2>&1 | count 0
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/own.lnx.o | FileCheck %s --check-prefix=OWN-LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/own.a64.o0.o | FileCheck %s --check-prefix=OWN-LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t/own.lnx.o \
; RUN:   | FileCheck %s --check-prefix=OWN-CIL --implicit-check-not="[msil.extern]" %}

; A declaration of the same type, called directly.
; DEFINE: %{decl} = FileCheck %s --check-prefix=DECL --implicit-check-not=__msil_compiler_barrier \
; DEFINE:   --implicit-check-not=pinvokeimpl
; RUN: %{wp} -mtriple=x86_64-unknown-linux-gnu-msil < %t/decl.ll -o %t/decl.lnx.il 2>&1 | count 0
; RUN: %{decl} --input-file=%t/decl.lnx.il
; RUN: %{wp} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/decl.ll -o %t/decl.win.o0.il 2>&1 | count 0
; RUN: %{decl} --input-file=%t/decl.win.o0.il
; RUN: %{wp} -mtriple=aarch64-unknown-linux-gnu-msil < %t/decl.ll -o %t/decl.a64.il 2>&1 | count 0
; RUN: %{decl} --input-file=%t/decl.a64.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/decl.dll %t/decl.lnx.il %}
; RUN: %{obj} -mtriple=x86_64-pc-windows-msvc-msil < %t/decl.ll -o %t/decl.win.o 2>&1 | count 0
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/decl.win.o | FileCheck %s --check-prefix=DECL-LINK %}
; DEFINE: %{declonly} = FileCheck %s --check-prefix=DECLONLY --implicit-check-not=__msil_compiler_barrier \
; DEFINE:   --implicit-check-not=pinvokeimpl
; RUN: %{wp} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/declonly.ll -o %t/declonly.lnx.o0.il 2>&1 | count 0
; RUN: %{declonly} --input-file=%t/declonly.lnx.o0.il
; RUN: %{wp} -mtriple=aarch64-unknown-linux-gnu-msil < %t/declonly.ll -o %t/declonly.a64.il 2>&1 | count 0
; RUN: %{declonly} --input-file=%t/declonly.a64.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/declonly.dll %t/declonly.a64.il %}
; DEFINE: %{unused} = FileCheck %s --check-prefix=UNUSED --implicit-check-not=__msil_compiler_barrier \
; DEFINE:   --implicit-check-not=pinvokeimpl
; RUN: %{wp} -mtriple=x86_64-pc-windows-msvc-msil < %t/unused.ll -o %t/unused.win.il 2>&1 | count 0
; RUN: %{unused} --input-file=%t/unused.win.il
; RUN: %{wp} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/unused.ll -o %t/unused.lnx.o0.il 2>&1 | count 0
; RUN: %{unused} --input-file=%t/unused.lnx.o0.il

; Another type: errors.
; RUN: not %{wp} -mtriple=x86_64-unknown-linux-gnu-msil < %t/mismatch.ll > %t/mismatch.lnx.il 2> %t/mismatch.err
; RUN: %{diag} --check-prefix=MISMATCH --input-file=%t/mismatch.err
; RUN: FileCheck %s --check-prefix=MISMATCH-IL --implicit-check-not="'__msil_compiler_barrier'()" \
; RUN:   --input-file=%t/mismatch.lnx.il
; RUN: not %{wp} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/mismatch.ll > %t/mismatch.win.o0.il 2> %t/mismatch.err
; RUN: %{diag} --check-prefix=MISMATCH --input-file=%t/mismatch.err
; RUN: FileCheck %s --check-prefix=MISMATCH-IL --implicit-check-not="'__msil_compiler_barrier'()" \
; RUN:   --input-file=%t/mismatch.win.o0.il
; RUN: not %{wp} -mtriple=aarch64-unknown-linux-gnu-msil < %t/mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MISMATCH
; RUN: not %{wp} -mtriple=x86_64-unknown-linux-gnu-msil < %t/mmcall.ll > %t/mmcall.lnx.il 2> %t/mmcall.err
; RUN: %{diag} --check-prefix=MMCALL --input-file=%t/mmcall.err
; RUN: FileCheck %s --check-prefix=MMCALL-IL --implicit-check-not=__msil_compiler_barrier \
; RUN:   --implicit-check-not="{{^ +call}}" --implicit-check-not=pinvokeimpl --input-file=%t/mmcall.lnx.il
; RUN: not %{wp} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/mmcall.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MMCALL
; RUN: not %{wp} -mtriple=aarch64-unknown-linux-gnu-msil < %t/mmcall.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MMCALL
; RUN: not %{wp} -mtriple=x86_64-unknown-linux-gnu-msil < %t/addr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ADDR
; RUN: not %{wp} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/addr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ADDR
; RUN: not %{wp} -mtriple=aarch64-unknown-linux-gnu-msil < %t/addr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=ADDR

;--- many.ll
; Five barriers in three functions: single-thread fences of four orderings
; and an empty asm barrier.
source_filename = "many.c"

; MANY-LABEL:  {{^}}.method public static void 'one'(native int) cil managed{{$}}
; MANY:        {{^}}  call void '__msil_compiler_barrier'(){{$}}
; MANY:        {{^}}  call void '__msil_compiler_barrier'(){{$}}
; MANY:        {{^}}} // end of method 'one'{{$}}
; MANY-LABEL:  {{^}}.method public static void 'two'(native int) cil managed{{$}}
; MANY:        {{^}}  call void '__msil_compiler_barrier'(){{$}}
; MANY:        {{^}}  call void '__msil_compiler_barrier'(){{$}}
; MANY:        {{^}}} // end of method 'two'{{$}}
; MANY-LABEL:  {{^}}.method public static void 'three'() cil managed{{$}}
; MANY:        {{^}}  call void '__msil_compiler_barrier'(){{$}}
; MANY:        {{^}}} // end of method 'three'{{$}}
; MANY-LABEL:  {{^}}.method assembly static void '__msil_compiler_barrier'() cil managed noinlining{{$}}
; MANY-NEXT:   {{^\{$}}
; MANY-NEXT:   {{^}}  .maxstack 0{{$}}
; MANY-NEXT:   {{^}}  ret{{$}}
; MANY-NEXT:   {{^}}} // end of method '__msil_compiler_barrier'{{$}}
; MANY-NOT:    {{.}}

; MANY-OF-COUNT-5: {{^}}  call void [msil.extern]'<msil>Symbols'::'__msil_compiler_barrier'(){{$}}

; MANY-LINK:      {{^}}LinkInfo v1.0 triple={{.*}}
; MANY-LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT 'one' method=0x06000001{{$}}
; MANY-LINK-NEXT: {{^}}Symbol 1 FUNCTION GLOBAL DEFAULT 'two' method=0x06000002{{$}}
; MANY-LINK-NEXT: {{^}}Symbol 2 FUNCTION GLOBAL DEFAULT 'three' method=0x06000003{{$}}
; MANY-LINK-NEXT: {{^}}Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED '__msil_compiler_barrier'{{$}}
; MANY-LINK-NOT:  {{.}}

; MANY-CIL-COUNT-5: {{^ +}}L{{[0-9]+}}: call void() [msil.extern]<msil>Symbols::'__msil_compiler_barrier'{{$}}

define void @one(ptr %p) {
  store volatile i32 1, ptr %p
  fence syncscope("singlethread") seq_cst
  store volatile i32 2, ptr %p
  fence syncscope("singlethread") acquire
  ret void
}

define void @two(ptr %p) {
  fence syncscope("singlethread") release
  store i32 3, ptr %p
  call void asm sideeffect "", "~{memory}"()
  ret void
}

define void @three() {
  fence syncscope("singlethread") acq_rel
  ret void
}

;--- none.ll
source_filename = "none.c"

; NONE-LABEL: {{^}}.method public static void 'sys'() cil managed{{$}}
; NONE:       {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
; NONE:       {{^}}} // end of method 'sys'{{$}}
; NONE-NOT:   {{^}}.method

; NONE-LINK:      {{^}}LinkInfo v1.0 triple={{.*}}
; NONE-LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT 'sys' method=0x06000001{{$}}
; NONE-LINK-NOT:  {{.}}

define void @sys() {
  fence seq_cst
  ret void
}

;--- own.ll
; The module defines the function itself, with the type of the barrier.
source_filename = "own.c"

; OWN:       {{^}}.method public static void '__msil_compiler_barrier'() cil managed{{$}}
; OWN-NEXT:  {{^\{$}}
; OWN-NEXT:  {{^}}  .maxstack 0{{$}}
; OWN-NEXT:  {{^}}  ret{{$}}
; OWN-NEXT:  {{^}}} // end of method '__msil_compiler_barrier'{{$}}
; OWN:       {{^}}.method public static void 'user'(native int) cil managed{{$}}
; OWN:       {{^}}  call void '__msil_compiler_barrier'(){{$}}
; OWN:       {{^}}} // end of method 'user'{{$}}
; OWN-NOT:   {{^}}.method

; OWN-LINK:      {{^}}LinkInfo v1.0 triple={{.*}}
; OWN-LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT '__msil_compiler_barrier' method=0x06000001{{$}}
; OWN-LINK-NEXT: {{^}}Symbol 1 FUNCTION GLOBAL DEFAULT 'user' method=0x06000002{{$}}
; OWN-LINK-NOT:  {{.}}

; OWN-CIL: {{^}}  .method '__msil_compiler_barrier' void() flags=0x0016 impl=0x0000{{$}}
; OWN-CIL: {{^ +}}L{{[0-9]+}}: call void() '<Module>'::'__msil_compiler_barrier'{{$}}

define void @__msil_compiler_barrier() {
  ret void
}

define void @user(ptr %p) {
  store i32 1, ptr %p
  fence syncscope("singlethread") seq_cst
  store i32 2, ptr %p
  ret void
}

;--- decl.ll
; A declaration with the type of the barrier, called directly, and a barrier.
source_filename = "decl.c"

; DECL-LABEL: {{^}}.method public static void 'direct'() cil managed{{$}}
; DECL:       {{^}}  call void '__msil_compiler_barrier'(){{$}}
; DECL-LABEL: {{^}}.method public static void 'fenced'() cil managed{{$}}
; DECL:       {{^}}  call void '__msil_compiler_barrier'(){{$}}
; DECL-LABEL: {{^}}.method assembly static void '__msil_compiler_barrier'() cil managed noinlining{{$}}
; DECL-NEXT:  {{^\{$}}
; DECL-NEXT:  {{^}}  .maxstack 0{{$}}
; DECL-NEXT:  {{^}}  ret{{$}}
; DECL-NEXT:  {{^}}} // end of method '__msil_compiler_barrier'{{$}}
; DECL-NOT:   {{.}}

; The declaration is the first symbol, in IR order.
; DECL-LINK:      {{^}}LinkInfo v1.0 triple={{.*}}
; DECL-LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED '__msil_compiler_barrier'{{$}}
; DECL-LINK-NEXT: {{^}}Symbol 1 FUNCTION GLOBAL DEFAULT 'direct' method=0x06000001{{$}}
; DECL-LINK-NEXT: {{^}}Symbol 2 FUNCTION GLOBAL DEFAULT 'fenced' method=0x06000002{{$}}
; DECL-LINK-NOT:  {{.}}

declare void @__msil_compiler_barrier()

define void @direct() {
  call void @__msil_compiler_barrier()
  ret void
}

define void @fenced() {
  fence syncscope("singlethread") seq_cst
  ret void
}

;--- declonly.ll
; The same declaration, called directly, and no barrier.
source_filename = "declonly.c"

; DECLONLY-LABEL: {{^}}.method public static void 'direct'() cil managed{{$}}
; DECLONLY:       {{^}}  call void '__msil_compiler_barrier'(){{$}}
; DECLONLY-LABEL: {{^}}.method assembly static void '__msil_compiler_barrier'() cil managed noinlining{{$}}
; DECLONLY-NEXT:  {{^\{$}}
; DECLONLY-NEXT:  {{^}}  .maxstack 0{{$}}
; DECLONLY-NEXT:  {{^}}  ret{{$}}
; DECLONLY-NEXT:  {{^}}} // end of method '__msil_compiler_barrier'{{$}}
; DECLONLY-NOT:   {{.}}

declare void @__msil_compiler_barrier()

define void @direct() {
  call void @__msil_compiler_barrier()
  ret void
}

;--- unused.ll
; An unused declaration of another type and a barrier.
source_filename = "unused.c"

; UNUSED-LABEL: {{^}}.method public static void 'user'() cil managed{{$}}
; UNUSED:       {{^}}  call void '__msil_compiler_barrier'(){{$}}
; UNUSED-LABEL: {{^}}.method assembly static void '__msil_compiler_barrier'() cil managed noinlining{{$}}
; UNUSED-NEXT:  {{^\{$}}
; UNUSED-NEXT:  {{^}}  .maxstack 0{{$}}
; UNUSED-NEXT:  {{^}}  ret{{$}}
; UNUSED-NEXT:  {{^}}} // end of method '__msil_compiler_barrier'{{$}}
; UNUSED-NOT:   {{.}}

declare i32 @__msil_compiler_barrier(i32)

define void @user() {
  fence syncscope("singlethread") seq_cst
  ret void
}

;--- mismatch.ll
; A definition of another type and two functions with a barrier.
source_filename = "mismatch.c"

; MISMATCH:      {{^}}error: {{.*}}in function user void (ptr): library call '__msil_compiler_barrier' does not match the type of the function of that name in this module{{$}}
; MISMATCH:      {{^}}error: {{.*}}in function user2 void (): library call '__msil_compiler_barrier' does not match the type of the function of that name in this module{{$}}

; MISMATCH-IL:       {{^}}.method public static int32 '__msil_compiler_barrier'(int32) cil managed{{$}}
; MISMATCH-IL-LABEL: {{^}}.method public static void 'user'(native int) cil managed{{$}}
; MISMATCH-IL:       {{^}}} // end of method 'user'{{$}}
; MISMATCH-IL-LABEL: {{^}}.method public static void 'user2'() cil managed{{$}}
; MISMATCH-IL-NEXT:  {{^\{$}}
; MISMATCH-IL-NEXT:  {{^}}  .maxstack 0{{$}}
; MISMATCH-IL-NEXT:  {{^}}  ret{{$}}
; MISMATCH-IL-NEXT:  {{^}}} // end of method 'user2'{{$}}
; MISMATCH-IL-NOT:   {{^}}.method

define i32 @__msil_compiler_barrier(i32 %x) {
  ret i32 %x
}

define void @user(ptr %p) {
  store i32 1, ptr %p
  fence syncscope("singlethread") seq_cst
  store i32 2, ptr %p
  ret void
}

define void @user2() {
  call void asm sideeffect "", "~{memory}"()
  ret void
}

;--- mmcall.ll
; A direct call of a declaration of another type.
source_filename = "mmcall.c"

; MMCALL: {{^}}error: {{.*}}in function user i32 (i32): library call '__msil_compiler_barrier' does not match the type of the function of that name in this module{{$}}

; MMCALL-IL-LABEL: {{^}}.method public static int32 'user'(int32) cil managed{{$}}
; MMCALL-IL:       {{^}}} // end of method 'user'{{$}}
; MMCALL-IL-NOT:   {{^}}.method

declare i32 @__msil_compiler_barrier(i32)

define i32 @user(i32 %x) {
  %r = call i32 @__msil_compiler_barrier(i32 %x)
  ret i32 %r
}

;--- addr.ll
; The address of a declaration of another type, and a barrier.
source_filename = "addr.c"

; ADDR: {{^}}error: function '__msil_compiler_barrier' is declared as int32(int32) but the toolchain defines it as void(){{$}}

declare i32 @__msil_compiler_barrier(i32)

@fp = global ptr @__msil_compiler_barrier

define void @user() {
  fence syncscope("singlethread") seq_cst
  ret void
}
