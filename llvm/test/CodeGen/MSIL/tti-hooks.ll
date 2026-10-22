; The IR optimizer queries the MSIL TargetTransformInfo and TargetLowering
; long before the code they describe exists, so the query hooks are final from
; M2 on and never stubs (decisions D8, D33):
; - no lookup tables: SimplifyCFG keeps a switch of constants instead of
;   building a @switch.table global (which needs M7 loads and M8 globals);
; - no relative lookup tables: RelLookupTableConverter keeps the GEP and load
;   of a source-level table of string pointers instead of a table of 32-bit
;   offsets read with llvm.load.relative, even with -relocation-model=pic.
;   Because the TargetMachine forces Reloc::Static, BasicTTIImpl would answer
;   false too, so this catches an override of shouldBuildRelLookupTables()
;   that returns true, not a deleted one;
; - zero vector registers: LoopVectorize and SLPVectorizer produce no vector
;   types even when options force a vector width or register size, and the
;   fixed-width vector register width of 0 bits reaches Loop Access Analysis,
;   which then accepts a backward dependence known only by a lower bound
;   without run-time checks;
; - no partial or runtime unrolling, even when -partial-unrolling-threshold
;   makes BasicTTIImpl ask for both;
; - isLegalAddressingMode accepts only [reg] (CIL ldind/stind have no offset),
;   and it is reached through TTI getGEPCost at -O2, LoopStrengthReduce and
;   CodeGenPrepare, which therefore sinks no address computation into the
;   blocks that use them;
; - areTypesABICompatible rejects half and bfloat, and the vectors, arrays and
;   structs that contain them: ArgumentPromotion keeps the pointer parameter
;   of an internal function that only loads such a value through it, because
;   a half or bfloat passed by value needs a compiler-rt helper until phase 3
;   (D14), and LTO internalizes functions that take one by pointer (b2i and
;   b2u of msil/test/M1/02-fp/i128-fp-conv.c).
; The vectorizer and unroller checks need the forcing options: at plain -O2,
; BasicTTIImpl enables partial and runtime unrolling only for a scheduling
; model with a loop buffer, which MSIL lacks, and its default 32-bit vector
; register width already rules out <4 x i32>, so the -O2 runs below would pass
; without the MSIL overrides.
; The MSIL TargetMachine must be created for each run: opt only warns when it
; cannot create one, so stderr is checked to be empty. Where a native x86
; backend is built, the same IR shows each transformation that MSIL disables.

; RUN: opt -mtriple=x86_64-pc-windows-msvc-msil -passes="simplifycfg<switch-to-lookup>" -S < %s 2>%t.switch.err \
; RUN:   | FileCheck %s --check-prefix=SWITCH --implicit-check-not=@switch.table
; RUN: count 0 < %t.switch.err
; SWITCH-LABEL: define i32 @msil_switch(
; SWITCH:         switch i32 %x
; SWITCH-LABEL: define ptr @msil_switch_str(
; SWITCH:         switch i32 %x

; -O2 completes and its output is scalar (on its own this does not show that
; the vector and unrolling overrides work; see the forced runs below).
; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -relocation-model=pic -O2 -S < %s 2>%t.o2.err \
; RUN:   | FileCheck %s --check-prefix=O2 --implicit-check-not=@switch.table \
; RUN:       --implicit-check-not=llvm.load.relative --implicit-check-not="{{<[0-9]+ x }}" \
; RUN:       --implicit-check-not=vector.body --implicit-check-not=.epil --implicit-check-not=.prol
; RUN: count 0 < %t.o2.err
; RUN: opt -mtriple=x86_64-pc-windows-msvc-msil -O2 -S < %s 2>%t.o2win.err \
; RUN:   | FileCheck %s --check-prefix=O2 --implicit-check-not=@switch.table \
; RUN:       --implicit-check-not=llvm.load.relative --implicit-check-not="{{<[0-9]+ x }}" \
; RUN:       --implicit-check-not=vector.body --implicit-check-not=.epil --implicit-check-not=.prol
; RUN: count 0 < %t.o2win.err
; O2: define {{.*}}i32 @msil_switch(
; O2: define {{.*}}ptr @msil_switch_str(
; O2: define {{.*}}void @msil_gep_loop(
; O2: define {{.*}}i32 @msil_sum(
; O2: define {{.*}}i32 @msil_sum_1024(
; O2: define {{.*}}i32 @msil_cgp_sink(
; O2: define {{.*}}void @msil_slp(
; O2: define {{.*}}ptr @msil_rel_table(
; O2: getelementptr {{.*}}ptr @msil_names, i64 %idx
; O2-NEXT: load ptr, ptr

; Unrolling. -partial-unrolling-threshold makes BasicTTIImpl set UP.Partial
; and UP.Runtime for any target, so only the MSIL getUnrollingPreferences
; override keeps the loops whole: no runtime unrolling of the loops with an
; unknown trip count (unroll_iter, a .epil or .prol remainder loop) and no
; partial unrolling of the loop that runs 1024 times (an unrolled copy of a
; value %x is named %x.<k>).
; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -passes="loop-unroll<O2>" -partial-unrolling-threshold=200 \
; RUN:   -S < %s 2>%t.unroll.err | FileCheck %s --check-prefix=UNROLL --implicit-check-not=unroll_iter \
; RUN:   --implicit-check-not=.epil --implicit-check-not=.prol --implicit-check-not="{{\.[0-9]+ = }}"
; RUN: count 0 < %t.unroll.err
; UNROLL-LABEL: define void @msil_gep_loop(
; UNROLL:       loop:
; UNROLL:         store i32 %r, ptr %pbi, align 4
; UNROLL-NEXT:    %i.next = add nuw nsw i64 %i, 1
; UNROLL-LABEL: define i32 @msil_sum(
; UNROLL:       loop:
; UNROLL:         %acc.next = add i32 %acc, %v
; UNROLL-NEXT:    %i.next = add nuw nsw i64 %i, 1
; UNROLL-LABEL: define i32 @msil_sum_1024(
; UNROLL:       loop:
; UNROLL:         %acc.next = add i32 %acc, %v
; UNROLL-NEXT:    %i.next = add nuw nsw i64 %i, 1

; Vectorization. -force-vector-width=4 makes LoopVectorize use four lanes
; whatever the cost model says, and -slp-max-reg-size=128 with a negative
; -slp-threshold makes SLPVectorizer build <4 x i32> operations; both passes
; still give up first when the target has no vector registers.
; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -passes=loop-vectorize -force-vector-width=4 -S < %s 2>%t.lv.err \
; RUN:   | FileCheck %s --check-prefix=LV --implicit-check-not=vector.body --implicit-check-not="{{<[0-9]+ x }}"
; RUN: count 0 < %t.lv.err
; LV-LABEL: define void @msil_gep_loop(
; LV:       loop:
; LV:         store i32 %r, ptr %pbi, align 4
; LV-LABEL: define i32 @msil_sum(
; LV:       loop:
; LV:         %acc.next = add i32 %acc, %v
; RUN: opt -mtriple=x86_64-pc-windows-msvc-msil -passes=slp-vectorizer -slp-max-reg-size=128 -slp-threshold=-1000 \
; RUN:   -S < %s 2>%t.slp.err | FileCheck %s --check-prefix=SLP --implicit-check-not="{{<[0-9]+ x }}"
; RUN: count 0 < %t.slp.err
; SLP-LABEL: define void @msil_slp(
; SLP:         store i32 %s0, ptr %b, align 4
; SLP:         store i32 %s3, ptr %pb3, align 4

; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -relocation-model=pic -passes=rel-lookup-table-converter \
; RUN:   -S < %s 2>%t.rlt.err | FileCheck %s --check-prefix=RLT --implicit-check-not=llvm.load.relative \
; RUN:   --implicit-check-not=@msil_names.rel
; RUN: count 0 < %t.rlt.err
; RLT: @msil_names = internal unnamed_addr constant [4 x ptr]
; RLT-LABEL: define ptr @msil_rel_table(
; RLT:         %p = getelementptr inbounds nuw [8 x i8], ptr @msil_names, i64 %idx
; RLT-NEXT:    %v = load ptr, ptr %p, align 8

; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -passes=loop-reduce -S < %s 2>%t.lsr.err \
; RUN:   | FileCheck %s --check-prefix=LSR
; RUN: count 0 < %t.lsr.err
; LSR: define void @msil_gep_loop(
; LSR: define i32 @msil_sum(

; RUN: opt -mtriple=x86_64-pc-windows-msvc-msil -passes="require<profile-summary>,function(codegenprepare)" \
; RUN:   -S < %s 2>%t.cgp.err | FileCheck %s --check-prefix=CGP --implicit-check-not=sunkaddr
; RUN: count 0 < %t.cgp.err
; CGP-LABEL: define i32 @msil_cgp_sink(
; CGP:       then:
; CGP-NEXT:    %v = load i32, ptr %p
; CGP:       else:
; CGP-NEXT:    %w = load i32, ptr %q

; ArgumentPromotion. The internal functions that load only a bfloat, a half or
; a struct holding an array of bfloat through their pointer parameter keep the
; pointer; the one that loads an i32 gets the i32, which shows that the pass
; ran.
; RUN: opt -mtriple=x86_64-pc-windows-msvc-msil -passes=argpromotion -S < %s 2>%t.apwin.err \
; RUN:   | FileCheck %s --check-prefix=AP
; RUN: count 0 < %t.apwin.err
; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -passes=argpromotion -S < %s 2>%t.ap.err \
; RUN:   | FileCheck %s --check-prefix=AP
; RUN: count 0 < %t.ap.err
; AP-LABEL: define internal i32 @msil_ap_bf16(ptr %p)
; AP-LABEL: define internal i32 @msil_ap_half(ptr %p)
; AP-LABEL: define internal i32 @msil_ap_agg(ptr %p)
; AP-LABEL: define internal i32 @msil_ap_i32(i32 %p.0.val)
; AP-LABEL: define i32 @msil_ap_caller(
; AP:         %rb = call i32 @msil_ap_bf16(ptr %b)
; AP-NEXT:    %rh = call i32 @msil_ap_half(ptr %h)
; AP-NEXT:    %ra = call i32 @msil_ap_agg(ptr %a)
; AP-NEXT:    %i.val = load i32, ptr %i, align 4
; AP-NEXT:    %ri = call i32 @msil_ap_i32(i32 %i.val)

; Loop Access Analysis. For a dependence distance known only by a lower bound
; (@msil_laa_dist: at least 2 bytes, so at most two i8 lanes, 16 bits), it
; reports a backward-vectorizable dependence only when those 16 bits are at
; least twice the fixed-width vector register width
; (MaxTargetVectorWidthInBits in LoopAccessAnalysis.cpp); otherwise the
; dependence is unknown and the pointers are compared at run time (a Check 0
; group). The MSIL width is 0 (D8). The 32-bit default of
; TargetTransformInfoImplBase and BasicTTIImpl, like the native x86 widths,
; gives the run-time checks. The analysis prints to stderr, so stderr cannot be
; checked to be empty here, but without the MSIL TargetMachine opt uses the
; default width and these checks fail.
; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -passes="print<access-info>" -disable-output < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=LAA
; RUN: opt -mtriple=x86_64-pc-windows-msvc-msil -passes="print<access-info>" -disable-output < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=LAA
; LAA-LABEL: Printing analysis 'Loop Access Analysis' for function 'msil_laa_dist':
; LAA-NEXT:    loop:
; LAA-NEXT:      Memory dependences are safe with a maximum safe vector width of 16 bits
; LAA-NEXT:      Dependences:
; LAA-NEXT:        BackwardVectorizable:
; LAA:           Run-time memory checks:
; LAA-NEXT:      Grouped accesses:
; LAA-NOT:       Check {{[0-9]+}}:

; The native x86 backend builds every one of these.
; RUN: %if x86-registered-target %{ opt -mtriple=x86_64-pc-windows-msvc -passes="simplifycfg<switch-to-lookup>" -S < %s \
; RUN:   | FileCheck %s --check-prefix=X86-SWITCH %}
; X86-SWITCH: @switch.table.msil_switch =
; RUN: %if x86-registered-target %{ opt -mtriple=x86_64-unknown-linux-gnu -relocation-model=pic -O2 -S < %s \
; RUN:   | FileCheck %s --check-prefix=X86-O2 %}
; X86-O2-DAG: @switch.table.msil_switch =
; X86-O2-DAG: call ptr @llvm.load.relative
; X86-O2-DAG: vector.body:
; X86-O2-DAG: store <4 x i32>
; X86-O2-DAG: @msil_names.rel =
; RUN: %if x86-registered-target %{ opt -mtriple=x86_64-unknown-linux-gnu -relocation-model=pic \
; RUN:   -passes=rel-lookup-table-converter -S < %s | FileCheck %s --check-prefix=X86-RLT %}
; X86-RLT: @msil_names.rel = internal unnamed_addr constant [4 x i32]
; X86-RLT-LABEL: define ptr @msil_rel_table(
; X86-RLT:         call ptr @llvm.load.relative.i64(ptr @msil_names.rel,
; RUN: %if x86-registered-target %{ opt -mtriple=x86_64-pc-windows-msvc -passes="require<profile-summary>,function(codegenprepare)" \
; RUN:   -S < %s | FileCheck %s --check-prefix=X86-CGP %}
; X86-CGP: %sunkaddr
; RUN: %if x86-registered-target %{ opt -mtriple=x86_64-unknown-linux-gnu -passes="loop-unroll<O2>" \
; RUN:   -partial-unrolling-threshold=200 -S < %s | FileCheck %s --check-prefix=X86-UNROLL %}
; X86-UNROLL-LABEL: define void @msil_gep_loop(
; X86-UNROLL:         %unroll_iter =
; X86-UNROLL:       loop.epil:
; X86-UNROLL-LABEL: define i32 @msil_sum(
; X86-UNROLL:         %unroll_iter =
; X86-UNROLL:       loop.epil:
; X86-UNROLL-LABEL: define i32 @msil_sum_1024(
; X86-UNROLL:         %acc.next.1 = add i32 %acc.next, %v.1
; RUN: %if x86-registered-target %{ opt -mtriple=x86_64-unknown-linux-gnu -passes=loop-vectorize -force-vector-width=4 \
; RUN:   -S < %s | FileCheck %s --check-prefix=X86-LV %}
; X86-LV-LABEL: define void @msil_gep_loop(
; X86-LV:       vector.body:
; X86-LV-LABEL: define i32 @msil_sum(
; X86-LV:       vector.body:
; RUN: %if x86-registered-target %{ opt -mtriple=x86_64-pc-windows-msvc -passes=slp-vectorizer -slp-max-reg-size=128 \
; RUN:   -slp-threshold=-1000 -S < %s | FileCheck %s --check-prefix=X86-SLP %}
; X86-SLP-LABEL: define void @msil_slp(
; X86-SLP:         store <4 x i32>
; It also needs run-time checks for the dependence of @msil_laa_dist.
; RUN: %if x86-registered-target %{ opt -mtriple=x86_64-unknown-linux-gnu -passes="print<access-info>" \
; RUN:   -disable-output < %s 2>&1 | FileCheck %s --check-prefix=X86-LAA %}
; X86-LAA-LABEL: Printing analysis 'Loop Access Analysis' for function 'msil_laa_dist':
; X86-LAA-NEXT:    loop:
; X86-LAA-NEXT:      Memory dependences are safe with run-time checks
; X86-LAA:           Run-time memory checks:
; X86-LAA-NEXT:      Check 0:
; It passes half and bfloat by value, so ArgumentPromotion promotes every
; pointer parameter.
; RUN: %if x86-registered-target %{ opt -mtriple=x86_64-unknown-linux-gnu -passes=argpromotion -S < %s \
; RUN:   | FileCheck %s --check-prefix=X86-AP %}
; X86-AP-LABEL: define internal i32 @msil_ap_bf16(bfloat %p.0.val)
; X86-AP-LABEL: define internal i32 @msil_ap_half(half %p.0.val)
; X86-AP-LABEL: define internal i32 @msil_ap_agg({ i32, [2 x bfloat] } %p.0.val)
; X86-AP-LABEL: define internal i32 @msil_ap_i32(i32 %p.0.val)

@.str.a = private unnamed_addr constant [6 x i8] c"alpha\00"
@.str.b = private unnamed_addr constant [5 x i8] c"beta\00"
@.str.c = private unnamed_addr constant [6 x i8] c"gamma\00"
@.str.d = private unnamed_addr constant [6 x i8] c"delta\00"
@.str.z = private unnamed_addr constant [1 x i8] zeroinitializer

; A switch returning constants that are neither a linear map nor a bitmap:
; only an array lookup table could replace it.
define i32 @msil_switch(i32 %x) #0 {
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
  ]

c0:
  br label %ret

c1:
  br label %ret

c2:
  br label %ret

c3:
  br label %ret

c4:
  br label %ret

default:
  br label %ret

ret:
  %r = phi i32 [ 17, %c0 ], [ 42, %c1 ], [ 9, %c2 ], [ 1001, %c3 ], [ 7, %c4 ], [ -1, %default ]
  ret i32 %r
}

; A switch returning string addresses: a pointer table, which a PIC x86_64
; target turns into a relative lookup table.
define ptr @msil_switch_str(i32 %x) #0 {
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
  ]

c0:
  br label %ret

c1:
  br label %ret

c2:
  br label %ret

c3:
  br label %ret

default:
  br label %ret

ret:
  %r = phi ptr [ @.str.a, %c0 ], [ @.str.b, %c1 ], [ @.str.c, %c2 ], [ @.str.d, %c3 ], [ @.str.z, %default ]
  ret ptr %r
}

%struct.pair = type { i32, i32, [4 x i32] }

; b[i + 4] = a[i + 1] + 2 * a[2 * i] + p[i].f[2] for 0 <= i < n: scaled
; indices, constant offsets and a struct field, so the address modes have a
; base, a scale and an offset.
define void @msil_gep_loop(ptr noalias %a, ptr noalias %b, ptr noalias %p, i64 %n) #0 {
entry:
  %empty = icmp sle i64 %n, 0
  br i1 %empty, label %exit, label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %i1 = add nuw nsw i64 %i, 1
  %pa1 = getelementptr inbounds i32, ptr %a, i64 %i1
  %va1 = load i32, ptr %pa1, align 4
  %i2 = shl nuw nsw i64 %i, 1
  %pa2 = getelementptr inbounds i32, ptr %a, i64 %i2
  %va2 = load i32, ptr %pa2, align 4
  %pf = getelementptr inbounds %struct.pair, ptr %p, i64 %i, i32 2, i64 2
  %vf = load i32, ptr %pf, align 4
  %pb = getelementptr inbounds i8, ptr %b, i64 16
  %pbi = getelementptr inbounds i32, ptr %pb, i64 %i
  %t = shl i32 %va2, 1
  %s = add i32 %va1, %t
  %r = add i32 %s, %vf
  store i32 %r, ptr %pbi, align 4
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; The sum of a[0..n), with a runtime trip count.
define i32 @msil_sum(ptr %a, i64 %n) #0 {
entry:
  %empty = icmp sle i64 %n, 0
  br i1 %empty, label %exit, label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %pa = getelementptr inbounds i32, ptr %a, i64 %i
  %v = load i32, ptr %pa, align 4
  %acc.next = add i32 %acc, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %res = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %res
}

; The sum of a[0..1024): a constant trip count too large for full unrolling,
; so only partial unrolling could replicate the body.
define i32 @msil_sum_1024(ptr %a) #0 {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %pa = getelementptr inbounds i32, ptr %a, i64 %i
  %v = load i32, ptr %pa, align 4
  %acc.next = add i32 %acc, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 1024
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %acc.next
}

; Addresses computed in one block and used in others: CodeGenPrepare sinks
; them into the users only where the target can fold [reg + 16] and
; [reg + 4 * reg].
define i32 @msil_cgp_sink(ptr %b, i64 %i, i1 %c) #0 {
entry:
  %p = getelementptr inbounds i8, ptr %b, i64 16
  %q = getelementptr inbounds i32, ptr %b, i64 %i
  br i1 %c, label %then, label %else

then:
  %v = load i32, ptr %p, align 4
  ret i32 %v

else:
  %w = load i32, ptr %q, align 4
  ret i32 %w
}

; Straight-line code that SLP turns into <4 x i32> operations.
define void @msil_slp(ptr noalias %a, ptr noalias %c, ptr noalias %b) #0 {
entry:
  %a0 = load i32, ptr %a, align 4
  %c0 = load i32, ptr %c, align 4
  %s0 = add i32 %a0, %c0
  store i32 %s0, ptr %b, align 4
  %pa1 = getelementptr inbounds i32, ptr %a, i64 1
  %pc1 = getelementptr inbounds i32, ptr %c, i64 1
  %pb1 = getelementptr inbounds i32, ptr %b, i64 1
  %a1 = load i32, ptr %pa1, align 4
  %c1 = load i32, ptr %pc1, align 4
  %s1 = add i32 %a1, %c1
  store i32 %s1, ptr %pb1, align 4
  %pa2 = getelementptr inbounds i32, ptr %a, i64 2
  %pc2 = getelementptr inbounds i32, ptr %c, i64 2
  %pb2 = getelementptr inbounds i32, ptr %b, i64 2
  %a2 = load i32, ptr %pa2, align 4
  %c2 = load i32, ptr %pc2, align 4
  %s2 = add i32 %a2, %c2
  store i32 %s2, ptr %pb2, align 4
  %pa3 = getelementptr inbounds i32, ptr %a, i64 3
  %pc3 = getelementptr inbounds i32, ptr %c, i64 3
  %pb3 = getelementptr inbounds i32, ptr %b, i64 3
  %a3 = load i32, ptr %pa3, align 4
  %c3 = load i32, ptr %pc3, align 4
  %s3 = add i32 %a3, %c3
  store i32 %s3, ptr %pb3, align 4
  ret void
}

; What clang emits at -O2 for
;   static const char *const names[] = {"north", "east", "south", "west"};
;   const char *name(unsigned i) { return names[i & 3]; }
; an internal constant table of pointers to private strings, indexed by an
; argument: a candidate for a relative lookup table. Its strings differ from
; those of @msil_switch_str, so that at -O2 the native control does not merge
; it with that function's switch table, which would give it a second use.
@.str.n = private unnamed_addr constant [6 x i8] c"north\00"
@.str.e = private unnamed_addr constant [5 x i8] c"east\00"
@.str.s = private unnamed_addr constant [6 x i8] c"south\00"
@.str.w = private unnamed_addr constant [5 x i8] c"west\00"
@msil_names = internal unnamed_addr constant [4 x ptr] [ptr @.str.n, ptr @.str.e, ptr @.str.s, ptr @.str.w], align 16

define ptr @msil_rel_table(i32 %i) #0 {
entry:
  %and = and i32 %i, 3
  %idx = zext nneg i32 %and to i64
  %p = getelementptr inbounds nuw [8 x i8], ptr @msil_names, i64 %idx
  %v = load ptr, ptr %p, align 8
  ret ptr %v
}

; Internal noinline functions that read their argument through a pointer, as
; b2i and b2u of i128-fp-conv.c do once LTO internalizes them.
define internal i32 @msil_ap_bf16(ptr %p) #1 {
entry:
  %v = load bfloat, ptr %p, align 2
  %r = fptosi bfloat %v to i32
  ret i32 %r
}

define internal i32 @msil_ap_half(ptr %p) #1 {
entry:
  %v = load half, ptr %p, align 2
  %r = fptosi half %v to i32
  ret i32 %r
}

define internal i32 @msil_ap_agg(ptr %p) #1 {
entry:
  %v = load { i32, [2 x bfloat] }, ptr %p, align 4
  %e = extractvalue { i32, [2 x bfloat] } %v, 1, 1
  %r = fptosi bfloat %e to i32
  ret i32 %r
}

define internal i32 @msil_ap_i32(ptr %p) #1 {
entry:
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

define i32 @msil_ap_caller(ptr %b, ptr %h, ptr %a, ptr %i) #0 {
entry:
  %rb = call i32 @msil_ap_bf16(ptr %b)
  %rh = call i32 @msil_ap_half(ptr %h)
  %ra = call i32 @msil_ap_agg(ptr %a)
  %ri = call i32 @msil_ap_i32(ptr %i)
  %s0 = add i32 %rb, %rh
  %s1 = add i32 %s0, %ra
  %s2 = add i32 %s1, %ri
  ret i32 %s2
}

; a[i + k] = a[i] + 1 for 0 <= i < n with k = x + 2 for a byte x: a backward
; dependence whose distance is 2 to 257 bytes, known at compile time only by
; its lower bound. It is the last function, so that the ordered O2 checks
; above end before it.
define void @msil_laa_dist(ptr %a, i8 %x, i64 %n) #0 {
entry:
  %xz = zext i8 %x to i64
  %k = add nuw nsw i64 %xz, 2
  %empty = icmp sle i64 %n, 0
  br i1 %empty, label %exit, label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %pa = getelementptr inbounds i8, ptr %a, i64 %i
  %v = load i8, ptr %pa, align 1
  %ik = add nuw nsw i64 %i, %k
  %pb = getelementptr inbounds i8, ptr %a, i64 %ik
  %w = add i8 %v, 1
  store i8 %w, ptr %pb, align 1
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; What clang attaches to every x86_64 function, and to a noinline one.
attributes #0 = { nounwind "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { noinline nounwind "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
