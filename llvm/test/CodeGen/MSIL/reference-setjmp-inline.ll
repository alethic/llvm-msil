; The setjmp fence of .NET references on hand-written IR (ImplementationPlan.md
; 5.21.7 RB11 rule 8 and section 12's rows '5.21.7 RB11 rule 8 amended' of
; 2026-11-21, '5.21.7 RB11 rule 8 amended (inlining)' and '5.21.7 RB11 rule
; 8's inlining fence as built' of 2026-11-22, milestone R-a). P1's rule 8
; refuses a function that calls setjmp and holds a reference (T-RB3), and it
; runs after LLVM's inliners, so the fence, MSILTTIImpl::areInlineCompatible,
; keeps them from putting a function that holds a reference into one that
; calls setjmp, and a function that calls setjmp into one that holds a
; reference. getAttributeBasedInliningDecision asks it before it honors
; always_inline or flatten. clang/test/CodeGen/MSIL/ref-setjmp-inline.c pins
; the fence on the IR that clang emits; this file pins the IR attribute
; 'flatten', which clang never emits, and the call-site form of a setjmp
; call, which the fence does not see:
; - flatten.ll: the AlwaysInliner's own worklist for the functions that have
;   the IR attribute 'flatten', which asks the fence too. Clang never reaches
;   it, since it marks each call in a flatten function always_inline instead
;   (CGCall.cpp), so ref-setjmp-inline.c pins flatten only in that form; IR
;   from other front ends, or written by hand, reaches the worklist. The
;   flatten function @f, which holds a reference (a call result), still
;   calls @g, which calls '_setjmp3' declared without returns_twice (rule 8
;   counts such a call by its name); the flatten function @k, which calls a
;   returns_twice 'setjmp', still calls @h, which holds a reference; and the
;   control, the flatten function @q, which calls 'setjmp', has @p, which
;   holds no reference, inlined, which shows that flatten ran. On the x86_64
;   Windows and Linux triples, msil64-unknown-dotnet and aarch64. With
;   -ignore-tti-inline-compatible, LLVM's switch that skips the hook, @g and
;   @h are inlined (NOHOOK), so the other runs fail if the fence stops
;   answering.
; - gap.ll: the call-site form, which the fence does not see and leaves to
;   T-RB3 (the last row above). The fence finds setjmp calls among the uses
;   of the functions named setjmp, _setjmp, _setjmpex, _setjmp3, sigsetjmp,
;   __sigsetjmp, vfork, getcontext and savectx, while rule 8 also counts a
;   call with returns_twice on the call site alone, of a function of another
;   name declared without it; clang emits one after a redeclaration that adds
;   the attribute to a function already called. So flatten inlines
;   @gap_helper, which holds a reference, into @gap_caller, which makes such
;   a call (GAP), and llc then stops with T-RB3 for @gap_caller in object
;   mode (TRB3, pinned whole as in reference-errors.ll): the miss is loud.
;   The other direction stays apart by LLVM's own rule: in @gap_holder, which
;   holds a reference, the call of @gap_jumper, which makes the same call,
;   stays a call (isInlineViable refuses a callee that exposes
;   returns_twice), and P1 accepts both functions. On the x86_64 Windows and
;   Linux triples and msil64-unknown-dotnet.
; opt only warns when it cannot create the MSIL TargetMachine, so stderr is
; checked to be empty after each opt run.

; RUN: rm -rf %t && split-file %s %t

; RUN: opt -mtriple=x86_64-pc-windows-msvc-msil -passes=always-inline -S < %t/flatten.ll 2>%t/flatten.win.err \
; RUN:   | FileCheck %t/flatten.ll
; RUN: count 0 < %t/flatten.win.err
; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -passes=always-inline -S < %t/flatten.ll 2>%t/flatten.lnx.err \
; RUN:   | FileCheck %t/flatten.ll
; RUN: count 0 < %t/flatten.lnx.err
; RUN: opt -mtriple=msil64-unknown-dotnet -passes=always-inline -S < %t/flatten.ll 2>%t/flatten.p64.err \
; RUN:   | FileCheck %t/flatten.ll
; RUN: count 0 < %t/flatten.p64.err
; RUN: opt -mtriple=aarch64-pc-windows-msvc-msil -passes=always-inline -S < %t/flatten.ll 2>%t/flatten.a64.err \
; RUN:   | FileCheck %t/flatten.ll
; RUN: count 0 < %t/flatten.a64.err
; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -passes=always-inline -ignore-tti-inline-compatible -S \
; RUN:   < %t/flatten.ll 2>%t/flatten.nohook.err | FileCheck %t/flatten.ll --check-prefix=NOHOOK
; RUN: count 0 < %t/flatten.nohook.err

; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="Stack dump"

; RUN: opt -mtriple=x86_64-pc-windows-msvc-msil -passes=always-inline -S < %t/gap.ll -o %t/gap.opt.win.ll \
; RUN:   2>%t/gap.win.err
; RUN: count 0 < %t/gap.win.err
; RUN: FileCheck %t/gap.ll --check-prefix=GAP --implicit-check-not="call i32 @gap_helper" \
; RUN:   --input-file=%t/gap.opt.win.ll
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj < %t/gap.opt.win.ll -o %t/gap.win.o 2>&1 \
; RUN:   | FileCheck %t/gap.ll --check-prefix=TRB3 %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:
; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -passes=always-inline -S < %t/gap.ll -o %t/gap.opt.lnx.ll \
; RUN:   2>%t/gap.lnx.err
; RUN: count 0 < %t/gap.lnx.err
; RUN: FileCheck %t/gap.ll --check-prefix=GAP --implicit-check-not="call i32 @gap_helper" \
; RUN:   --input-file=%t/gap.opt.lnx.ll
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj < %t/gap.opt.lnx.ll -o %t/gap.lnx.o 2>&1 \
; RUN:   | FileCheck %t/gap.ll --check-prefix=TRB3 %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:
; RUN: opt -mtriple=msil64-unknown-dotnet -passes=always-inline -S < %t/gap.ll -o %t/gap.opt.p64.ll \
; RUN:   2>%t/gap.p64.err
; RUN: count 0 < %t/gap.p64.err
; RUN: FileCheck %t/gap.ll --check-prefix=GAP --implicit-check-not="call i32 @gap_helper" \
; RUN:   --input-file=%t/gap.opt.p64.ll
; RUN: not llc -mtriple=msil64-unknown-dotnet -filetype=obj < %t/gap.opt.p64.ll -o %t/gap.p64.o 2>&1 \
; RUN:   | FileCheck %t/gap.ll --check-prefix=TRB3 %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:

;--- flatten.ll
; With the fence: @f and @k keep their calls, @q's call of @p is inlined.
; CHECK-LABEL: define i32 @f()
; CHECK-NEXT:    %o = call target("msil.ref") @mk()
; CHECK-NEXT:    %c = call i32 @g()
; CHECK-NEXT:    ret i32 %c
; CHECK-LABEL: define i32 @k()
; CHECK-NEXT:    %s = call i32 @setjmp(ptr @buf2)
; CHECK-NEXT:    %c = call i32 @h()
; CHECK-NEXT:    %t = add i32 %s, %c
; CHECK-NEXT:    ret i32 %t
; CHECK-LABEL: define i32 @q(i32 %x)
; CHECK-NEXT:    %s = call i32 @setjmp(ptr @buf2)
; CHECK-NEXT:    %y.i = mul i32 %x, 5
; CHECK-NEXT:    %t = add i32 %s, %y.i
; CHECK-NEXT:    ret i32 %t

; Without it, both calls are inlined. The flatten worklist gives each call
; it inlines !inline_history metadata, which these lines leave out.
; NOHOOK-LABEL: define i32 @f()
; NOHOOK-NEXT:    %o = call target("msil.ref") @mk()
; NOHOOK-NEXT:    %r.i = call i32 @_setjmp3(ptr @buf, i32 0)
; NOHOOK-NEXT:    ret i32 %r.i
; NOHOOK-LABEL: define i32 @k()
; NOHOOK-NEXT:    %s = call i32 @setjmp(ptr @buf2)
; NOHOOK-NEXT:    %o.i = call target("msil.ref") @mk()
; NOHOOK-NEXT:    %t = add i32 %s, 1
; NOHOOK-NEXT:    ret i32 %t

@buf = internal global [32 x i64] zeroinitializer
@buf2 = internal global [16 x i64] zeroinitializer

declare target("msil.ref") @mk()

declare i32 @_setjmp3(ptr, i32)

declare i32 @setjmp(ptr) returns_twice

; The callee calls '_setjmp3', a setjmp call by its name alone; the caller
; holds a reference.
define internal i32 @g() {
  %r = call i32 @_setjmp3(ptr @buf, i32 0)
  ret i32 %r
}

define i32 @f() flatten {
  %o = call target("msil.ref") @mk()
  %c = call i32 @g()
  ret i32 %c
}

; The callee holds a reference; the caller calls setjmp.
define internal i32 @h() {
  %o = call target("msil.ref") @mk()
  ret i32 1
}

define i32 @k() flatten {
  %s = call i32 @setjmp(ptr @buf2)
  %c = call i32 @h()
  %t = add i32 %s, %c
  ret i32 %t
}

; The control: a callee without references, into a setjmp caller.
define internal i32 @p(i32 %x) {
  %y = mul i32 %x, 5
  ret i32 %y
}

define i32 @q(i32 %x) flatten {
  %s = call i32 @setjmp(ptr @buf2)
  %c = call i32 @p(i32 %x)
  %t = add i32 %s, %c
  ret i32 %t
}

;--- gap.ll
; @myjmp is none of the nine functions that the fence looks up by name, and
; it is declared without returns_twice; each call of it has the attribute on
; the call site alone (#0), as clang emits it after a redeclaration that
; adds the attribute. The
; references are stack slots, as in clang's -O0 code, not calls of an
; internal function that returns one, which flatten would inline too, taking
; the reference away; and no declaration has a reference in its type, which
; P1 would refuse with T-RB2.
; GAP:         declare i32 @myjmp(ptr){{$}}
; GAP-LABEL:   define i32 @gap_caller()
; GAP-NEXT:      %slot.i = alloca target("msil.ref"), align 8
; GAP-NEXT:      %r = call i32 @myjmp(ptr @buf) #[[RT:[0-9]+]]
; GAP:           store target("msil.ref") zeroinitializer, ptr %slot.i, align 8
; GAP-LABEL:   define i32 @gap_holder()
; GAP-NEXT:      %slot = alloca target("msil.ref"), align 8
; GAP-NEXT:      store target("msil.ref") zeroinitializer, ptr %slot, align 8
; GAP-NEXT:      %c = call i32 @gap_jumper()
; GAP:         attributes #[[RT]] = { returns_twice }

; TRB3: {{^}}error: <unknown>:0:0: in function gap_caller i32 (): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}

@buf = internal global [64 x i8] zeroinitializer, align 16

declare i32 @myjmp(ptr)

; The callee holds a reference; the caller makes the call-site form.
define internal i32 @gap_helper() {
  %slot = alloca target("msil.ref"), align 8
  store target("msil.ref") zeroinitializer, ptr %slot, align 8
  ret i32 1
}

define i32 @gap_caller() flatten {
  %r = call i32 @myjmp(ptr @buf) #0
  %c = call i32 @gap_helper()
  %t = add i32 %r, %c
  ret i32 %t
}

; The other direction: the callee makes the call-site form; the caller holds
; a reference.
define internal i32 @gap_jumper() {
  %r = call i32 @myjmp(ptr @buf) #0
  ret i32 %r
}

define i32 @gap_holder() flatten {
  %slot = alloca target("msil.ref"), align 8
  store target("msil.ref") zeroinitializer, ptr %slot, align 8
  %c = call i32 @gap_jumper()
  ret i32 %c
}

attributes #0 = { returns_twice }
