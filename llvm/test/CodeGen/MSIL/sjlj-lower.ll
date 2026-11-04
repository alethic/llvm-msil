; MSILLowerSjLj (ImplementationPlan.md 5.15.4 'Names recognized', 'The jmp_buf
; prefix' and 'The transformation', 5.14.6 I1, M27.b; recon p3/sjlj.md 3.2-3.5)
; at slot P8 (5.13.6), checked after the pass with -stop-after=msil-lower-sjlj:
; - main.ll: every setjmp site (here '_setjmp' with one argument, 'setjmp',
;   '__sigsetjmp' and 'sigsetjmp') stores the prefix into its buffer (the
;   magic 'MSJ1' = 826954573 at offset 0, the site label 1..N at 4, and at 8
;   the address of the function's one status block 'alloca { i32, i32 }',
;   label 0 at entry) and saves llvm.stacksave; its result becomes 'phi i32
;   [ 0, <site> ], [ <val>, <restore> ]'. Each call reachable from a site whose
;   callee may longjmp calls a wrapper '__msil_sjlj_invoke.<n>' (direct) or
;   '__msil_sjlj_invoke_ind.<n>' (indirect, the callee pointer first) with the
;   status block last, then loads the label and branches on it. One dispatch
;   block per function loads val, disarms (stores label 0) and switches to one
;   restore block per site (llvm.stackrestore of that site's save). On the
;   Linux and msil64-unknown-dotnet triples only (P-1r R1: 'TT.isOSLinux() ||
;   TT.isOSDotNet()'), a sigsetjmp site writes the mask flag at offset 64
;   (savemask != 0) and, for a mask that is not the constant 0, saves the mask
;   with 'sigprocmask(0, null, buf + 72)', and its restore path calls
;   'sigprocmask(2, buf + 72, null)'; on the Windows triple nothing touches
;   offsets 64 and 72 and nothing calls sigprocmask. Not wrapped: a call
;   before the site that no path from it reaches, a willreturn callee, strlen
;   (a TargetLibraryInfo function without a function pointer), puts, and
;   printf and snprintf, whose variadic prototypes ExpandVariadics (P7) has
;   already rewritten (TargetLibraryInfo knows them by name; section 12 row
;   '5.15.4 'Names recognized' and M27.b extended (variadic setjmp-family
;   declarations)'). Wrapped: a call
;   in the loop back to the site, calls after it, qsort (it takes a function
;   pointer), an indirect call, a direct longjmp in the setjmp caller (a
;   call of '__msil_longjmp', which loses its noreturn), and the cases that
;   exemption must not reach (review r2 item 6): an external variadic
;   declaration that TargetLibraryInfo does not know (with the va_list that
;   P7 gave the call), 'nobuiltin' calls of printf and strlen, and a TLI name
;   defined in the module. A site with no
;   wrapped call keeps its prefix stores and gives 0. SSA is rebuilt (a 'phi
;   ptr' with an undef or poison operand, for the save that reaches the
;   dispatch through the loop before the site ran), and the module still
;   compiles (object-form text, which allows undefined functions; the IR
;   verifier runs after the IR passes). Every call and every other use of
;   'longjmp', '_longjmp', 'siglongjmp' and '__longjmp_chk' (calls in a
;   function without a site, global initializers, a stored address) becomes
;   '__msil_longjmp'; no call of a setjmp name and no use of a longjmp name is
;   left (%{gone}).
; - names.ll: wrapper identity and names. One direct wrapper per (callee,
;   call-site function type), one indirect wrapper per call-site type, two
;   counters from 0 in first-use order, shared by the module's functions;
;   parameters are the call's arguments with their attributes (shifted by one
;   for indirect wrappers), then the ptr status; 'define internal' with
;   exactly 'noinline "msil-sjlj-invoke"="<index of the status parameter>"';
;   the inner call keeps the call-site type and every call-site attribute. Two
;   fixed-arity calls of one explicit import with two types (P6 gave both
;   "msil-fixed-arity"="1") get two wrappers whose inner calls keep the
;   attribute, and the outer calls do not have it, so the object still has
;   FIXED_ARITY only on the import's two signatures; 'noreturn' stays on the
;   inner call and leaves the outer one.
; - wrapkey.ll: the wrapper key (section 12 row 'M27.b's **Wrapper identity**
;   ... amended (wrapper key)'): calls of one call-site type that differ in
;   the attributes the wrapper copies get distinct wrappers. Under opaque
;   pointers a 'byval(%struct.S)' call and a plain-pointer call are both
;   'void(ptr)': two indirect wrappers, the byval one with byval on its formal
;   and its inner call, the plain one with none; byval calls of a 24-byte and
;   a 64-byte struct get two more; repeated calls reuse their wrappers.
; - the helpers (5.14.6 I1, K4), in main.ll and names.ll: 'define linkonce_odr
;   hidden void @__msil_longjmp(ptr, i32)' in its COMDAT, noreturn nounwind,
;   without unnamed_addr (it replaces &longjmp, which compares equal across
;   objects): a buffer with the magic stores 'val == 0 ? 1 : val' into the
;   status block's val, then the buffer's label into its label, then calls
;   '__msil_sjlj_throw(status)'; any other buffer calls
;   '__msil_sjlj_throw(null)'; 'define
;   linkonce_odr hidden void @__msil_sjlj_throw(ptr) unnamed_addr' in its
;   COMDAT, noreturn "msil-sjlj-throw", whose IR body is 'unreachable'.
; - win.ll: the MSVC forms, '_setjmp(buf, llvm.frameaddress(0))' and the
;   aarch64 '_setjmpex(buf, llvm.sponentry())': the second argument's call is
;   erased (ISel would reject the frame intrinsic), on both Windows triples,
;   and the module compiles. clang's MSVC x86 form '_setjmp3(buf, 0, ...)' is
;   a variadic declaration whose buffer is a fixed parameter, so it stays a
;   site. kept.ll: a frameaddress with another use stays.
; - untouched.ll: a module without setjmp-family declarations (a defined
;   'setjmp' and a defined 'longjmp' are not intercepted) leaves the pass
;   exactly as ExpandVariadics (P7) left it; -msil-disable-sjlj-lowering
;   (M27.f's N4 test) leaves main.ll and win.ll unchanged too.
; IR value names, block order around the dispatch and the exact GEP spelling
; are not pinned (an i8 GEP of the buffer with an i32 or i64 offset is), nor
; the order of the prefix stores.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{gone} = --implicit-check-not="{{(call .*|ptr )@(_setjmp|_setjmp3|setjmp|__sigsetjmp|sigsetjmp|_setjmpex|longjmp|_longjmp|siglongjmp|__longjmp_chk)([^_a-zA-Z0-9.]|$)}}"
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{f} =
; DEFINE: %{mir} = %t/%{f}.%{t}%{o}.ll
; DEFINE: %{stop} = llc -mtriple=%{t} %{o} -stop-after=msil-lower-sjlj < %t/%{f}.ll -o %{mir}
; DEFINE: %{compile} = llc -mtriple=%{t} %{o} -msil-object-form -verify-machineinstrs < %t/%{f}.ll \
; DEFINE:   -o /dev/null 2>&1 | count 0
; DEFINE: %{lnx} = %{stop} && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,LNX %{gone} --input-file=%{mir} && \
; DEFINE:   FileCheck %s --check-prefixes=RST,LNX-RST --input-file=%{mir} && \
; DEFINE:   FileCheck %s --check-prefix=SIGZ --input-file=%{mir} && \
; DEFINE:   FileCheck %s --check-prefix=HLJ --input-file=%{mir} && \
; DEFINE:   FileCheck %s --check-prefix=HTH --input-file=%{mir} && %{compile}
; DEFINE: %{win} = %{stop} && \
; DEFINE:   FileCheck %s --check-prefix=CHECK %{gone} --input-file=%{mir} \
; DEFINE:     --implicit-check-not=sigprocmask --implicit-check-not="{{, i(32|64) (64|72)$}}" && \
; DEFINE:   FileCheck %s --check-prefix=RST --input-file=%{mir} && \
; DEFINE:   FileCheck %s --check-prefix=HLJ --input-file=%{mir} && \
; DEFINE:   FileCheck %s --check-prefix=HTH --input-file=%{mir} && %{compile}
; DEFINE: %{names} = %{stop} && \
; DEFINE:   FileCheck %s --check-prefix=NAMES %{gone} --input-file=%{mir} && \
; DEFINE:   FileCheck %s --check-prefix=NAMES-W --input-file=%{mir} && \
; DEFINE:   FileCheck %s --check-prefix=HLJ --input-file=%{mir} && \
; DEFINE:   FileCheck %s --check-prefix=HTH --input-file=%{mir} && %{compile}
; DEFINE: %{winf} = %{stop} && \
; DEFINE:   FileCheck %s --check-prefix=WINF %{gone} --input-file=%{mir} \
; DEFINE:     --implicit-check-not="call ptr @llvm.frameaddress" --implicit-check-not="call ptr @llvm.sponentry" && \
; DEFINE:   %{compile}

; REDEFINE: %{f} = main
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{lnx}
; REDEFINE: %{o} = -O0
; RUN: %{lnx}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{win}
; REDEFINE: %{o} = -O2
; RUN: %{win}
; The *-unknown-dotnet half of P-1r R1's mask condition: the Linux checks
; (no execution, no C library).
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{stop} && \
; RUN:   FileCheck %s --check-prefixes=CHECK,LNX %{gone} --input-file=%{mir} && \
; RUN:   FileCheck %s --check-prefixes=RST,LNX-RST --input-file=%{mir} && \
; RUN:   FileCheck %s --check-prefix=SIGZ --input-file=%{mir}

; REDEFINE: %{f} = names
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{names}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{names}
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs \
; RUN:   < %t/names.ll -o %t/names.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/names.o \
; RUN:   | FileCheck %s --check-prefix=NAMES-OBJ --implicit-check-not=FIXED_ARITY %}

; REDEFINE: %{f} = wrapkey
; RUN: %{stop} && FileCheck %s --check-prefix=KEY %{gone} --input-file=%{mir} && %{compile}
; REDEFINE: %{o} = -O2
; RUN: %{stop} && FileCheck %s --check-prefix=KEY %{gone} --input-file=%{mir} && %{compile}

; REDEFINE: %{f} = win
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %{winf}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{winf}

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-lower-sjlj < %t/kept.ll -o %t/kept.out.ll
; RUN: FileCheck %s --check-prefix=KEPT %{gone} --input-file=%t/kept.out.ll

; Unchanged modules: the output after P8 is the output after P7.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-after=expand-variadics < %t/untouched.ll \
; RUN:   -o %t/untouched.lnx.p7.ll
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-after=msil-lower-sjlj < %t/untouched.ll \
; RUN:   -o %t/untouched.lnx.p8.ll
; RUN: diff %t/untouched.lnx.p7.ll %t/untouched.lnx.p8.ll
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=expand-variadics < %t/untouched.ll \
; RUN:   -o %t/untouched.win.p7.ll
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-lower-sjlj < %t/untouched.ll \
; RUN:   -o %t/untouched.win.p8.ll
; RUN: diff %t/untouched.win.p7.ll %t/untouched.win.p8.ll
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=expand-variadics < %t/main.ll \
; RUN:   -o %t/main.p7.ll
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-disable-sjlj-lowering -stop-after=msil-lower-sjlj \
; RUN:   < %t/main.ll -o %t/main.disabled.ll
; RUN: diff %t/main.p7.ll %t/main.disabled.ll
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=expand-variadics < %t/win.ll \
; RUN:   -o %t/win.p7.ll
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-disable-sjlj-lowering \
; RUN:   -stop-after=msil-lower-sjlj < %t/win.ll -o %t/win.disabled.ll
; RUN: diff %t/win.p7.ll %t/win.disabled.ll

;--- main.ll
source_filename = "sjlj-lower.c"

; Addresses of longjmp names in initializers.
; CHECK: @handler = global ptr @__msil_longjmp
; CHECK: @table = global [2 x ptr] [ptr @__msil_longjmp, ptr @__msil_longjmp]
@handler = global ptr @longjmp
@table = global [2 x ptr] [ptr @_longjmp, ptr @siglongjmp]

declare i32 @_setjmp(ptr) #0
declare i32 @setjmp(ptr) #0
declare i32 @__sigsetjmp(ptr, i32) #0
declare i32 @sigsetjmp(ptr, i32) #0
declare void @longjmp(ptr, i32) #1
declare void @_longjmp(ptr, i32) #1
declare void @siglongjmp(ptr, i32) #1
declare void @__longjmp_chk(ptr, i32) #1
declare void @ext(i32)
declare void @before()
declare void @inloop(i32)
declare void @after_loop()
declare i32 @pure(i32) #4
declare i64 @strlen(ptr)
declare void @qsort(ptr, i64, i64, ptr)
declare i32 @cmp(ptr, ptr)
declare i32 @puts(ptr)
declare i32 @printf(ptr, ...)
declare i32 @snprintf(ptr, i64, ptr, ...)
declare void @die(ptr, ...)

; One site and one wrapped call: the status block, the prefix, the save, the
; result phi, the label test after the wrapped call, and the dispatch.
; CHECK-LABEL: define i32 @basic(ptr %b) {
; CHECK:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; CHECK:       store i32 0, ptr %[[ST]]
; CHECK-DAG:   store i32 826954573, ptr %b
; CHECK-DAG:   %[[L:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 4{{$}}
; CHECK-DAG:   store i32 1, ptr %[[L]]
; CHECK-DAG:   %[[S:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 8{{$}}
; CHECK-DAG:   store ptr %[[ST]], ptr %[[S]]
; CHECK-DAG:   call ptr @llvm.stacksave.p0()
; CHECK:       %[[R:[^ ]+]] = phi i32 [ 0, %{{[^ ]+}} ], [ %[[VAL:[^ ]+]], %[[RES:[^ ]+]] ]
; CHECK:       icmp eq i32 %[[R]], 0
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 7, ptr %[[ST]]){{$}}
; CHECK-NEXT:  %[[LBL:[^ ]+]] = load i32, ptr %[[ST]]
; CHECK-NEXT:  %[[J:[^ ]+]] = icmp {{ne|eq}} i32 %[[LBL]], 0
; CHECK-NEXT:  br i1 %[[J]], label %{{[^ ]+}}, label %{{[^ ]+$}}
; CHECK:       %[[VAL]] = load i32, ptr %{{[^ ]+}}
; CHECK-NEXT:  store i32 0, ptr %[[ST]]
; CHECK-NEXT:  switch i32 %{{[^ ]+}}, label %{{[^ ]+}} [
; CHECK-NEXT:    i32 1, label %[[RES]]
; CHECK-NEXT:  ]
; CHECK-NOT:   switch
; CHECK-NOT:   alloca { i32, i32 }
; CHECK-NOT:   __msil_sjlj_invoke
; RST-LABEL:   define i32 @basic(ptr %b) {
; RST:         %[[SSP:[^ ]+]] = call ptr @llvm.stacksave.p0()
; RST:         call void @llvm.stackrestore.p0(ptr %[[SSP]])
; RST-NEXT:    br label %
define i32 @basic(ptr %b) {
entry:
  %r = call i32 @_setjmp(ptr %b) #2
  %c = icmp eq i32 %r, 0
  br i1 %c, label %then, label %done
then:
  call void @ext(i32 7)
  br label %done
done:
  ret i32 %r
}

; Two sites: labels 1 and 2, one status block, one dispatch with two cases,
; and a restore of each site's own save. Site 1's save is in the entry block
; and dominates the dispatch, so its restore uses it directly. Site 2's save
; does not dominate the dispatch, which the path through site 1 alone also
; reaches, so the rebuilt SSA (5.15.4 step 5) carries it to site 2's restore
; through phis: one where site 2's result is merged (incoming: the save) and
; one in the dispatch block (incoming: that phi), whose other operands come
; only from paths on which site 2 never ran (undef there; label 2 cannot be
; dispatched on them).
; CHECK-LABEL: define i32 @two_sites(ptr %b1, ptr %b2) {
; CHECK:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; CHECK-DAG:   store i32 826954573, ptr %b1
; CHECK-DAG:   %[[L1:[^ ]+]] = getelementptr {{.*}}i8, ptr %b1, i{{32|64}} 4{{$}}
; CHECK-DAG:   store i32 1, ptr %[[L1]]
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 1, ptr %[[ST]])
; CHECK-DAG:   store i32 826954573, ptr %b2
; CHECK-DAG:   %[[L2:[^ ]+]] = getelementptr {{.*}}i8, ptr %b2, i{{32|64}} 4{{$}}
; CHECK-DAG:   store i32 2, ptr %[[L2]]
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 2, ptr %[[ST]])
; CHECK:       store i32 0, ptr %[[ST]]
; CHECK-NEXT:  switch i32 %{{[^ ]+}}, label %{{[^ ]+}} [
; CHECK-NEXT:    i32 1, label %{{[^ ]+}}
; CHECK-NEXT:    i32 2, label %{{[^ ]+}}
; CHECK-NEXT:  ]
; CHECK-NOT:   switch
; CHECK-NOT:   alloca { i32, i32 }
; RST-LABEL:   define i32 @two_sites(ptr %b1, ptr %b2) {
; RST:         %[[SSP1:[^ ]+]] = call ptr @llvm.stacksave.p0()
; RST:         %[[SSP2:[^ ]+]] = call ptr @llvm.stacksave.p0()
; RST:         %[[P2:[^ ]+]] = phi ptr {{.*}}[ %[[SSP2]], %{{[^ ]+}} ]
; RST-DAG:     %[[PD:[^ ]+]] = phi ptr {{.*}}[ %[[P2]], %{{[^ ]+}} ]
; RST-DAG:     call void @llvm.stackrestore.p0(ptr %[[SSP1]])
; RST-DAG:     call void @llvm.stackrestore.p0(ptr %[[PD]])
define i32 @two_sites(ptr %b1, ptr %b2) {
entry:
  %r1 = call i32 @setjmp(ptr %b1) #2
  %c1 = icmp eq i32 %r1, 0
  br i1 %c1, label %first, label %out
first:
  call void @ext(i32 1)
  %r2 = call i32 @_setjmp(ptr %b2) #2
  %c2 = icmp eq i32 %r2, 0
  br i1 %c2, label %second, label %out
second:
  call void @ext(i32 2)
  br label %out
out:
  %r = phi i32 [ %r1, %entry ], [ %r2, %first ], [ 0, %second ]
  ret i32 %r
}

; sigsetjmp with the constant mask 1, the constant mask 0 and a variable mask.
; CHECK-LABEL: define i32 @sig_const(ptr %b) {
; CHECK-DAG:   store i32 826954573, ptr %b
; LNX-DAG:     %[[F:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 64{{$}}
; LNX-DAG:     store i32 1, ptr %[[F]]
; LNX-DAG:     %[[M:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 72{{$}}
; LNX-DAG:     call i32 @sigprocmask(i32 0, ptr null, ptr %[[M]])
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 3, ptr %{{[^ ]+}})
; LNX-RST-LABEL: define i32 @sig_const(ptr %b) {
; LNX-RST:     call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 3, ptr %{{[^ ]+}})
; LNX-RST:     call i32 @sigprocmask(i32 2, ptr %{{[^ ]+}}, ptr null)
define i32 @sig_const(ptr %b) {
entry:
  %r = call i32 @__sigsetjmp(ptr %b, i32 1) #2
  %c = icmp eq i32 %r, 0
  br i1 %c, label %then, label %done
then:
  call void @ext(i32 3)
  br label %done
done:
  ret i32 %r
}

; CHECK-LABEL: define i32 @sig_zero(ptr %b) {
; CHECK-DAG:   store i32 826954573, ptr %b
; LNX-DAG:     %[[F:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 64{{$}}
; LNX-DAG:     store i32 0, ptr %[[F]]
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 4, ptr %{{[^ ]+}})
; SIGZ-LABEL:  define i32 @sig_zero(ptr %b) {
; SIGZ-NOT:    @sigprocmask(i32 0,
; SIGZ:        {{^ *}}}{{$}}
define i32 @sig_zero(ptr %b) {
entry:
  %r = call i32 @__sigsetjmp(ptr %b, i32 0) #2
  %c = icmp eq i32 %r, 0
  br i1 %c, label %then, label %done
then:
  call void @ext(i32 4)
  br label %done
done:
  ret i32 %r
}

; CHECK-LABEL: define i32 @sig_var(ptr %b, i32 %m) {
; CHECK-DAG:   store i32 826954573, ptr %b
; LNX-DAG:     %[[F:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 64{{$}}
; LNX-DAG:     store i32 %{{[^ ]+}}, ptr %[[F]]
; LNX-DAG:     %[[M:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 72{{$}}
; LNX-DAG:     call i32 @sigprocmask(i32 0, ptr null, ptr %[[M]])
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 5, ptr %{{[^ ]+}})
; LNX-RST-LABEL: define i32 @sig_var(ptr %b, i32 %m) {
; LNX-RST:     call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 5, ptr %{{[^ ]+}})
; LNX-RST:     call i32 @sigprocmask(i32 2, ptr %{{[^ ]+}}, ptr null)
define i32 @sig_var(ptr %b, i32 %m) {
entry:
  %r = call i32 @sigsetjmp(ptr %b, i32 %m) #2
  %c = icmp eq i32 %r, 0
  br i1 %c, label %then, label %done
then:
  call void @ext(i32 5)
  br label %done
done:
  ret i32 %r
}

; longjmp names in a function without a site: plain calls of __msil_longjmp.
; CHECK-LABEL: define void @jumps(ptr %b, i32 %v, i32 %k) {
; CHECK-NOT:   alloca { i32, i32 }
; CHECK:       call void @__msil_longjmp(ptr %b, i32 %v)
; CHECK:       call void @__msil_longjmp(ptr %b, i32 %v)
; CHECK:       call void @__msil_longjmp(ptr %b, i32 %v)
; CHECK-NOT:   __msil_sjlj_invoke
define void @jumps(ptr %b, i32 %v, i32 %k) {
entry:
  switch i32 %k, label %chk [
    i32 1, label %plain
    i32 2, label %sig
  ]
plain:
  call void @_longjmp(ptr %b, i32 %v) #3
  unreachable
sig:
  call void @siglongjmp(ptr %b, i32 %v) #3
  unreachable
chk:
  call void @__longjmp_chk(ptr %b, i32 %v) #3
  unreachable
}

; A longjmp in the setjmp caller after the site is wrapped (the wrapper calls
; __msil_longjmp), and the outer call is not noreturn; the 'unreachable'
; after it stays on the path where the label is 0.
; CHECK-LABEL: define void @self_jump(ptr %b) {
; CHECK:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(ptr %b, i32 1, ptr %[[ST]]){{$}}
; CHECK-NEXT:  load i32, ptr %[[ST]]
; CHECK:       unreachable
define void @self_jump(ptr %b) {
entry:
  %r = call i32 @_setjmp(ptr %b) #2
  %c = icmp eq i32 %r, 0
  br i1 %c, label %jump, label %out
jump:
  call void @longjmp(ptr %b, i32 1) #3
  unreachable
out:
  ret void
}

; Reachability: 'before' runs before the loop and no path from the site
; reaches it; 'inloop' precedes the site in the loop, but the latch leads back
; to it; 'ext' and 'after_loop' follow the site. The site's save reaches the
; dispatch from 'inloop' too, where on the first iteration it is undefined:
; a rebuilt phi.
; CHECK-LABEL: define void @reach(ptr %b, i32 %n) {
; CHECK:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; CHECK:       call void @before(){{$}}
; CHECK:       phi ptr {{.*}}{{undef|poison}}
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 %i, ptr %[[ST]])
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 %{{[^ ]+}}, ptr %[[ST]])
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(ptr %[[ST]])
; The restore reaches the save through the phi at the site and the phi in the
; dispatch block, as in @two_sites.
; RST-LABEL:   define void @reach(ptr %b, i32 %n) {
; RST:         %[[SSP:[^ ]+]] = call ptr @llvm.stacksave.p0()
; RST:         %[[P:[^ ]+]] = phi ptr {{.*}}[ %[[SSP]], %{{[^ ]+}} ]
; RST:         %[[PD:[^ ]+]] = phi ptr {{.*}}[ %[[P]], %{{[^ ]+}} ]
; RST:         call void @llvm.stackrestore.p0(ptr %[[PD]])
; RST-NEXT:    br label %
define void @reach(ptr %b, i32 %n) {
entry:
  call void @before()
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  call void @inloop(i32 %i)
  br label %site
site:
  %r = call i32 @_setjmp(ptr %b) #2
  br label %latch
latch:
  call void @ext(i32 %r)
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  call void @after_loop()
  ret void
}

; Which callees may longjmp: not a willreturn function, not strlen; qsort
; (it takes a function pointer) and an indirect call are wrapped.
; CHECK-LABEL: define i32 @filters(ptr %b, ptr %fp) {
; CHECK:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; CHECK:       call i32 @pure(i32 %
; CHECK-NEXT:  call i64 @strlen(ptr %b)
; CHECK-NEXT:  call void @__msil_sjlj_invoke.{{[0-9]+}}(ptr %b, i64 %{{[^ ]+}}, i64 4, ptr @cmp, ptr %[[ST]])
; CHECK:       call i32 @__msil_sjlj_invoke_ind.{{[0-9]+}}(ptr %fp, i32 %{{[^ ]+}}, ptr %[[ST]])
define i32 @filters(ptr %b, ptr %fp) {
entry:
  %r = call i32 @_setjmp(ptr %b) #2
  %p = call i32 @pure(i32 %r)
  %l = call i64 @strlen(ptr %b)
  call void @qsort(ptr %b, i64 %l, i64 4, ptr @cmp)
  %x = call i32 %fp(i32 %p)
  ret i32 %x
}

; No wrapped call after the site: the prefix is still stored, the result is 0.
; CHECK-LABEL: define i32 @nullified(ptr %b) {
; CHECK:       alloca { i32, i32 }
; CHECK-DAG:   store i32 826954573, ptr %b
; CHECK-DAG:   %[[L:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 4{{$}}
; CHECK-DAG:   store i32 1, ptr %[[L]]
; CHECK-DAG:   %[[S:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 8{{$}}
; CHECK-DAG:   store ptr %{{[^ ]+}}, ptr %[[S]]
; CHECK:       call i64 @strlen(ptr %b)
; CHECK-NEXT:  ret i32 0
; CHECK-NOT:   {{switch|__msil_sjlj_invoke}}
define i32 @nullified(ptr %b) {
entry:
  %r = call i32 @_setjmp(ptr %b) #2
  %l = call i64 @strlen(ptr %b)
  ret i32 %r
}

; A stored address of a longjmp name.
; CHECK-LABEL: define void @addr(ptr %p) {
; CHECK-NEXT:  store ptr @__msil_longjmp, ptr %p
define void @addr(ptr %p) {
  store ptr @__longjmp_chk, ptr %p
  ret void
}

; C library calls after the site: puts, and printf and snprintf, whose calls
; ExpandVariadics (P7) has already given a va_list argument, are not wrapped;
; 'ext' is, so the site is not nullified.
; CHECK-LABEL: define i32 @libc_calls(ptr %b, ptr %f, ptr %d) {
; CHECK:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; CHECK-NOT:   __msil_sjlj_invoke
; CHECK:       call i32 @puts(ptr %f)
; CHECK-NOT:   __msil_sjlj_invoke
; CHECK:       call i32 @printf(ptr %f, ptr %{{[^ ,)]+}})
; CHECK-NOT:   __msil_sjlj_invoke
; CHECK:       call i32 @snprintf(ptr %d, i64 16, ptr %f, ptr %{{[^ ,)]+}})
; CHECK-NOT:   __msil_sjlj_invoke
; CHECK:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 10, ptr %[[ST]]){{$}}
define i32 @libc_calls(ptr %b, ptr %f, ptr %d) {
entry:
  %r = call i32 @_setjmp(ptr %b) #2
  %a = call i32 @puts(ptr %f)
  %p = call i32 (ptr, ...) @printf(ptr %f, i32 %r)
  %s = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %d, i64 16, ptr %f, i32 %r)
  call void @ext(i32 10)
  ret i32 %p
}

; The other side of that exemption (review r2 item 6): after the site, a call
; of an external variadic declaration that TargetLibraryInfo does not know
; ('die') goes through a wrapper with the va_list argument that P7 gave it;
; 'nobuiltin' call sites of printf and strlen are wrapped (TargetLibraryInfo
; does not vouch for them); a plain printf stays unwrapped; and 'atoi',
; defined in this module, is wrapped although TargetLibraryInfo knows its name.
; P7 expands these calls on every triple here (the va_list is '%va_argument'
; on the Linux triple and '%vararg_buffer' on the others).
; CHECK-LABEL: define i32 @va_user(ptr %b, ptr %f) {
; CHECK:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; CHECK:       call void @[[DIE:__msil_sjlj_invoke\.[0-9]+]](ptr %f, ptr %{{[^ ,)]+}}, ptr %[[ST]]){{$}}
; CHECK-NEXT:  load i32, ptr %[[ST]]
; CHECK:       %p = call i32 @[[PNB:__msil_sjlj_invoke\.[0-9]+]](ptr %f, ptr %{{[^ ,)]+}}, ptr %[[ST]]){{$}}
; CHECK-NEXT:  load i32, ptr %[[ST]]
; CHECK:       %l = call i64 @[[SNB:__msil_sjlj_invoke\.[0-9]+]](ptr %f, ptr %[[ST]]){{$}}
; CHECK-NEXT:  load i32, ptr %[[ST]]
; CHECK-NOT:   __msil_sjlj_invoke
; CHECK:       %q = call i32 @printf(ptr %f, ptr %{{[^ ,)]+}}){{$}}
; CHECK-NOT:   __msil_sjlj_invoke
; CHECK:       %u = call i32 @[[ATOI:__msil_sjlj_invoke\.[0-9]+]](ptr %f, ptr %[[ST]]){{$}}
; CHECK-NEXT:  load i32, ptr %[[ST]]
; The wrappers' inner calls: 'die' with the va_list, printf and strlen still
; 'nobuiltin', the module's atoi.
; CHECK:       define internal void @[[DIE]](ptr %[[A0:[^ ,]+]], ptr %[[A1:[^ ,]+]], ptr %{{[^ )]+}}) #{{[0-9]+}} {
; CHECK:       call void @die(ptr %[[A0]], ptr %[[A1]]){{$}}
; CHECK:       define internal i32 @[[PNB]](ptr %[[A0:[^ ,]+]], ptr %[[A1:[^ ,]+]], ptr %{{[^ )]+}}) #{{[0-9]+}} {
; CHECK:       call i32 @printf(ptr %[[A0]], ptr %[[A1]]) #[[NB:[0-9]+]]{{$}}
; CHECK:       define internal i64 @[[SNB]](ptr %[[A0:[^ ,]+]], ptr %{{[^ )]+}}) #{{[0-9]+}} {
; CHECK:       call i64 @strlen(ptr %[[A0]]) #[[NB]]{{$}}
; CHECK:       define internal i32 @[[ATOI]](ptr %[[A0:[^ ,]+]], ptr %{{[^ )]+}}) #{{[0-9]+}} {
; CHECK:       call i32 @atoi(ptr %[[A0]]){{$}}
; CHECK:       attributes #[[NB]] = { nobuiltin }
define i32 @atoi(ptr %s) {
entry:
  ret i32 0
}

define i32 @va_user(ptr %b, ptr %f) {
entry:
  %r = call i32 @_setjmp(ptr %b) #2
  call void (ptr, ...) @die(ptr %f, i32 %r)
  %p = call i32 (ptr, ...) @printf(ptr %f, i32 %r) #5
  %l = call i64 @strlen(ptr %f) #5
  %q = call i32 (ptr, ...) @printf(ptr %f, i32 %r)
  %u = call i32 @atoi(ptr %f)
  %s = add i32 %q, %u
  ret i32 %s
}

; The helpers (main.ll and names.ll).
; HLJ:      $__msil_longjmp = comdat any
; HLJ:      define linkonce_odr hidden void @__msil_longjmp(ptr {{[^,]*}}%[[B:[^ ,]+]], i32 {{[^%)]*}}%[[V:[^ )]+]]) #[[LJ:[0-9]+]] comdat {
; HLJ:      %[[MG:[^ ]+]] = load i32, ptr %[[B]]
; HLJ-NEXT: %[[OK:[^ ]+]] = icmp eq i32 %[[MG]], 826954573
; HLJ-NEXT: br i1 %[[OK]], label %[[ARM:[^ ,]+]], label %[[BAD:[^ ,]+]]{{$}}
; The arm path: val (1 for 0) into the status block's val, then the label,
; then the throw with the status block.
; HLJ:      {{^ *}}[[ARM]]:
; HLJ:      %[[SP:[^ ]+]] = getelementptr {{.*}}i8, ptr %[[B]], i{{32|64}} 8{{$}}
; HLJ:      %[[ST:[^ ]+]] = load ptr, ptr %[[SP]]
; HLJ:      %[[Z:[^ ]+]] = icmp eq i32 %[[V]], 0
; HLJ-NEXT: %[[V1:[^ ]+]] = select i1 %[[Z]], i32 1, i32 %[[V]]
; HLJ:      %[[VP:[^ ]+]] = getelementptr {{.*}}i8, ptr %[[ST]], i{{32|64}} 4{{$}}
; HLJ-NEXT: store i32 %[[V1]], ptr %[[VP]]
; HLJ:      %[[LP:[^ ]+]] = getelementptr {{.*}}i8, ptr %[[B]], i{{32|64}} 4{{$}}
; HLJ:      %[[LBL:[^ ]+]] = load i32, ptr %[[LP]]
; HLJ:      store i32 %[[LBL]], ptr %[[ST]]
; HLJ-NEXT: call void @__msil_sjlj_throw(ptr %[[ST]])
; HLJ-NEXT: unreachable
; Any other buffer: the throw with a null status (HResult 0, no filter
; accepts it).
; HLJ:      {{^ *}}[[BAD]]:
; HLJ-NEXT: call void @__msil_sjlj_throw(ptr null)
; HLJ-NEXT: unreachable
; HLJ-NEXT: {{^ *}}}{{$}}
; HLJ:      attributes #[[LJ]] = { {{[^"]*}}noreturn {{[^"]*}}nounwind{{[^"]*}} }
; HTH:      $__msil_sjlj_throw = comdat any
; HTH:      define linkonce_odr hidden void @__msil_sjlj_throw(ptr {{[^)]*}}) unnamed_addr #[[TH:[0-9]+]] comdat {
; HTH-NOT:  {{call|store|load|ret }}
; HTH:      unreachable
; HTH-NEXT: {{^ *}}}{{$}}
; HTH:      attributes #[[TH]] = { {{.*}}noreturn {{.*}}"msil-sjlj-throw"{{.*}} }

attributes #0 = { nounwind returns_twice }
attributes #1 = { noreturn nounwind }
attributes #2 = { returns_twice }
attributes #3 = { noreturn }
attributes #4 = { willreturn }
attributes #5 = { nobuiltin }

;--- names.ll
source_filename = "sjlj-names.c"

declare i32 @_setjmp(ptr) #0
declare void @longjmp(ptr, i32) #1
declare void @a(i32)
declare i32 @b(ptr)
declare void @attrs(ptr, i32)
declare void @fatal(i32) #1
declare i32 @myprintf(ptr, ...) #4

; The calls in first-use order: direct a and b; indirect void(i32) and
; i32(ptr, i32); a and void(i32) again (through another pointer); myprintf
; with two call-site types (MSILPrepareVariadics made both fixed-arity calls);
; attrs with parameter attributes; the noreturn fatal. f2 shares a's wrapper,
; and its longjmp (a call of __msil_longjmp) gets the next direct wrapper.
; NAMES-LABEL: define void @f1(ptr %buf, ptr %fp, ptr %fq) {
; NAMES:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; NAMES:       call void @__msil_sjlj_invoke.0(i32 1, ptr %[[ST]])
; NAMES:       call i32 @__msil_sjlj_invoke.1(ptr %buf, ptr %[[ST]])
; NAMES:       call void @__msil_sjlj_invoke_ind.0(ptr %fp, i32 2, ptr %[[ST]])
; NAMES:       call i32 @__msil_sjlj_invoke_ind.1(ptr %fq, ptr %buf, i32 3, ptr %[[ST]])
; NAMES:       call void @__msil_sjlj_invoke.0(i32 4, ptr %[[ST]])
; NAMES:       call void @__msil_sjlj_invoke_ind.0(ptr %fq, i32 5, ptr %[[ST]])
; NAMES:       call i32 @__msil_sjlj_invoke.2(ptr %buf, i32 6, ptr %[[ST]]){{$}}
; NAMES:       call i32 @__msil_sjlj_invoke.3(ptr %buf, ptr %fp, ptr %[[ST]]){{$}}
; NAMES:       call void @__msil_sjlj_invoke.4(ptr {{.*}}%buf, i32 {{.*}}7, ptr %[[ST]])
; NAMES:       call void @__msil_sjlj_invoke.5(i32 8, ptr %[[ST]]){{$}}
define void @f1(ptr %buf, ptr %fp, ptr %fq) {
entry:
  %r = call i32 @_setjmp(ptr %buf) #2
  call void @a(i32 1)
  %x = call i32 @b(ptr %buf)
  call void %fp(i32 2)
  %y = call i32 %fq(ptr %buf, i32 3)
  call void @a(i32 4)
  call void %fq(i32 5)
  %p1 = call i32 (ptr, ...) @myprintf(ptr %buf, i32 6)
  %p2 = call i32 (ptr, ...) @myprintf(ptr %buf, ptr %fp)
  call void @attrs(ptr noundef %buf, i32 signext 7)
  call void @fatal(i32 8) #3
  unreachable
}

; NAMES-LABEL: define void @f2(ptr %buf) {
; NAMES:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; NAMES:       call void @__msil_sjlj_invoke.0(i32 9, ptr %[[ST]])
; NAMES:       call void @__msil_sjlj_invoke.6(ptr %buf, i32 0, ptr %[[ST]]){{$}}
define void @f2(ptr %buf) {
entry:
  %r = call i32 @_setjmp(ptr %buf) #2
  %c = icmp eq i32 %r, 0
  br i1 %c, label %go, label %out
go:
  call void @a(i32 9)
  call void @longjmp(ptr %buf, i32 0) #3
  unreachable
out:
  ret void
}

; The wrappers, in the order they were made.
; NAMES-W:      define internal void @__msil_sjlj_invoke.0(i32 [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W1:[0-9]+]] {
; NAMES-W:      call void @a(i32 [[A]])
; NAMES-W-NEXT: ret void
; NAMES-W:      define internal i32 @__msil_sjlj_invoke.1(ptr [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W1]] {
; NAMES-W:      [[R:%[^ ]+]] = call i32 @b(ptr [[A]])
; NAMES-W-NEXT: ret i32 [[R]]
; NAMES-W:      define internal void @__msil_sjlj_invoke_ind.0(ptr [[FN:%[^ ,]+]], i32 [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W2:[0-9]+]] {
; NAMES-W:      call void [[FN]](i32 [[A]])
; NAMES-W-NEXT: ret void
; NAMES-W:      define internal i32 @__msil_sjlj_invoke_ind.1(ptr [[FN:%[^ ,]+]], ptr [[A:%[^ ,]+]], i32 [[B:%[^ ,]+]], ptr {{[^)]*}}) #[[W3:[0-9]+]] {
; NAMES-W:      [[R:%[^ ]+]] = call i32 [[FN]](ptr [[A]], i32 [[B]])
; NAMES-W-NEXT: ret i32 [[R]]
; NAMES-W:      define internal i32 @__msil_sjlj_invoke.2(ptr [[A:%[^ ,]+]], i32 [[B:%[^ ,]+]], ptr {{[^)]*}}) #[[W2]] {
; NAMES-W:      [[R:%[^ ]+]] = call i32 @myprintf(ptr [[A]], i32 [[B]]) #[[FA:[0-9]+]]
; NAMES-W-NEXT: ret i32 [[R]]
; NAMES-W:      define internal i32 @__msil_sjlj_invoke.3(ptr [[A:%[^ ,]+]], ptr [[B:%[^ ,]+]], ptr {{[^)]*}}) #[[W2]] {
; NAMES-W:      [[R:%[^ ]+]] = call i32 @myprintf(ptr [[A]], ptr [[B]]) #[[FA]]
; NAMES-W-NEXT: ret i32 [[R]]
; NAMES-W:      define internal void @__msil_sjlj_invoke.4(ptr noundef [[A:%[^ ,]+]], i32 signext [[B:%[^ ,]+]], ptr {{[^)]*}}) #[[W2]] {
; NAMES-W:      call void @attrs(ptr noundef [[A]], i32 signext [[B]])
; NAMES-W-NEXT: ret void
; NAMES-W:      define internal void @__msil_sjlj_invoke.5(i32 [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W1]] {
; NAMES-W:      call void @fatal(i32 [[A]]) #[[NR:[0-9]+]]
; NAMES-W:      define internal void @__msil_sjlj_invoke.6(ptr [[A:%[^ ,]+]], i32 [[B:%[^ ,]+]], ptr {{[^)]*}}) #[[W2]] {
; NAMES-W:      call void @__msil_longjmp(ptr [[A]], i32 [[B]])
; NAMES-W-DAG:  attributes #[[W1]] = { noinline "msil-sjlj-invoke"="1" }
; NAMES-W-DAG:  attributes #[[W2]] = { noinline "msil-sjlj-invoke"="2" }
; NAMES-W-DAG:  attributes #[[W3]] = { noinline "msil-sjlj-invoke"="3" }
; NAMES-W-DAG:  attributes #[[FA]] = { "msil-fixed-arity"="1" }
; NAMES-W-DAG:  attributes #[[NR]] = { noreturn }

; The object: FIXED_ARITY only on the import's two signatures.
; NAMES-OBJ:         LinkInfo v1.1 triple=x86_64-unknown-linux-gnu-msil
; NAMES-OBJ-COUNT-2: Signature 0x{{0[6A]}}{{[0-9A-F]+}} 'myprintf' FIXED_ARITY fixed=1

attributes #0 = { nounwind returns_twice }
attributes #1 = { noreturn nounwind }
attributes #2 = { returns_twice }
attributes #3 = { noreturn }
attributes #4 = { "msil-import-module"="libc" }

;--- wrapkey.ll
source_filename = "sjlj-wrapkey.c"

; 32, 24 and 64 bytes.
%struct.S = type { [8 x i32] }
%struct.M = type { [6 x i32] }
%struct.L = type { [16 x i32] }

declare i32 @_setjmp(ptr) #0

; Six indirect 'void(ptr)' calls: byval S, a plain pointer, byval M, byval L,
; then byval S and the plain pointer again.
; KEY-LABEL: define void @keys(ptr %b, ptr %fp, ptr %s, ptr %m, ptr %l) {
; KEY:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; KEY:       call void @__msil_sjlj_invoke_ind.0(ptr %fp, ptr byval(%struct.S) align 8 %s, ptr %[[ST]]){{$}}
; KEY:       call void @__msil_sjlj_invoke_ind.1(ptr %fp, ptr %s, ptr %[[ST]]){{$}}
; KEY:       call void @__msil_sjlj_invoke_ind.2(ptr %fp, ptr byval(%struct.M) align 4 %m, ptr %[[ST]]){{$}}
; KEY:       call void @__msil_sjlj_invoke_ind.3(ptr %fp, ptr byval(%struct.L) align 4 %l, ptr %[[ST]]){{$}}
; KEY:       call void @__msil_sjlj_invoke_ind.0(ptr %fp, ptr byval(%struct.S) align 8 %s, ptr %[[ST]]){{$}}
; KEY:       call void @__msil_sjlj_invoke_ind.1(ptr %fp, ptr %s, ptr %[[ST]]){{$}}
; KEY-NOT:   __msil_sjlj_invoke
; KEY:       ret void
define void @keys(ptr %b, ptr %fp, ptr %s, ptr %m, ptr %l) {
entry:
  %r = call i32 @_setjmp(ptr %b) #1
  call void %fp(ptr byval(%struct.S) align 8 %s)
  call void %fp(ptr %s)
  call void %fp(ptr byval(%struct.M) align 4 %m)
  call void %fp(ptr byval(%struct.L) align 4 %l)
  call void %fp(ptr byval(%struct.S) align 8 %s)
  call void %fp(ptr %s)
  ret void
}

; The four wrappers: each formal and inner call carries exactly its own
; call's attributes, and the plain-pointer wrapper has none.
; KEY:      define internal void @__msil_sjlj_invoke_ind.0(ptr [[FN:%[^ ,]+]], ptr byval(%struct.S) align 8 [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W:[0-9]+]] {
; KEY:      call void [[FN]](ptr byval(%struct.S) align 8 [[A]]){{$}}
; KEY-NEXT: ret void
; KEY:      define internal void @__msil_sjlj_invoke_ind.1(ptr [[FN:%[^ ,]+]], ptr [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W]] {
; KEY:      call void [[FN]](ptr [[A]]){{$}}
; KEY-NEXT: ret void
; KEY:      define internal void @__msil_sjlj_invoke_ind.2(ptr [[FN:%[^ ,]+]], ptr byval(%struct.M) align 4 [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W]] {
; KEY:      call void [[FN]](ptr byval(%struct.M) align 4 [[A]]){{$}}
; KEY-NEXT: ret void
; KEY:      define internal void @__msil_sjlj_invoke_ind.3(ptr [[FN:%[^ ,]+]], ptr byval(%struct.L) align 4 [[A:%[^ ,]+]], ptr {{[^)]*}}) #[[W]] {
; KEY:      call void [[FN]](ptr byval(%struct.L) align 4 [[A]]){{$}}
; KEY-NEXT: ret void
; KEY-NOT:  define internal
; KEY:      attributes #[[W]] = { noinline "msil-sjlj-invoke"="2" }

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }

;--- win.ll
source_filename = "sjlj-win.c"

declare i32 @_setjmp(ptr, ptr) #0
declare i32 @_setjmpex(ptr, ptr) #0
declare i32 @_setjmp3(ptr, i32, ...) #0
declare void @ext(i32)
declare ptr @llvm.frameaddress.p0(i32 immarg)
declare ptr @llvm.sponentry.p0()

; WINF-LABEL: define i32 @msvc(ptr %b) {
; WINF:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; WINF-DAG:   store i32 826954573, ptr %b
; WINF-DAG:   %[[L:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 4{{$}}
; WINF-DAG:   store i32 1, ptr %[[L]]
; WINF-DAG:   %[[S:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 8{{$}}
; WINF-DAG:   store ptr %[[ST]], ptr %[[S]]
; WINF:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 1, ptr %[[ST]])
define i32 @msvc(ptr %b) {
entry:
  %f = call ptr @llvm.frameaddress.p0(i32 0)
  %r = call i32 @_setjmp(ptr %b, ptr %f) #1
  %c = icmp eq i32 %r, 0
  br i1 %c, label %then, label %done
then:
  call void @ext(i32 1)
  br label %done
done:
  ret i32 %r
}

; WINF-LABEL: define i32 @arm64(ptr %b) {
; WINF:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; WINF-DAG:   store i32 826954573, ptr %b
; WINF-DAG:   %[[L:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 4{{$}}
; WINF-DAG:   store i32 1, ptr %[[L]]
; WINF:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 2, ptr %[[ST]])
define i32 @arm64(ptr %b) {
entry:
  %s = call ptr @llvm.sponentry.p0()
  %r = call i32 @_setjmpex(ptr %b, ptr %s) #1
  %c = icmp eq i32 %r, 0
  br i1 %c, label %then, label %done
then:
  call void @ext(i32 2)
  br label %done
done:
  ret i32 %r
}

; clang's MSVC x86 form: a variadic declaration whose buffer is a fixed
; parameter is a site (ExpandVariadics gives the call a va_list argument).
; WINF-LABEL: define i32 @x86(ptr %b) {
; WINF:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; WINF-DAG:   store i32 826954573, ptr %b
; WINF-DAG:   %[[L:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 4{{$}}
; WINF-DAG:   store i32 1, ptr %[[L]]
; WINF-DAG:   %[[S:[^ ]+]] = getelementptr {{.*}}i8, ptr %b, i{{32|64}} 8{{$}}
; WINF-DAG:   store ptr %[[ST]], ptr %[[S]]
; WINF:       %[[R:[^ ]+]] = phi i32 [ 0, %{{[^ ]+}} ], [ %{{[^ ]+}}, %{{[^ ]+}} ]
; WINF:       icmp eq i32 %[[R]], 0
; WINF:       call void @__msil_sjlj_invoke.{{[0-9]+}}(i32 3, ptr %[[ST]]){{$}}
; WINF-NEXT:  load i32, ptr %[[ST]]
define i32 @x86(ptr %b) {
entry:
  %r = call i32 (ptr, i32, ...) @_setjmp3(ptr %b, i32 0) #1
  %c = icmp eq i32 %r, 0
  br i1 %c, label %then, label %done
then:
  call void @ext(i32 3)
  br label %done
done:
  ret i32 %r
}

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }

;--- kept.ll
source_filename = "sjlj-kept.c"

declare i32 @_setjmp(ptr, ptr) #0
declare ptr @llvm.frameaddress.p0(i32 immarg)

; KEPT-LABEL: define i32 @kept(ptr %b, ptr %q) {
; KEPT:       %[[F:[^ ]+]] = call ptr @llvm.frameaddress.p0(i32 0)
; KEPT:       store ptr %[[F]], ptr %q
; KEPT:       store i32 826954573, ptr %b
; KEPT:       ret i32 0
define i32 @kept(ptr %b, ptr %q) {
entry:
  %f = call ptr @llvm.frameaddress.p0(i32 0)
  store ptr %f, ptr %q
  %r = call i32 @_setjmp(ptr %b, ptr %f) #1
  ret i32 %r
}

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }

;--- untouched.ll
source_filename = "sjlj-untouched.c"

@h = global ptr @longjmp

declare void @ext(i32)
declare i32 @get(ptr)

define i32 @setjmp(ptr %b) {
  ret i32 0
}

define void @longjmp(ptr %b, i32 %v) {
  call void @ext(i32 %v)
  ret void
}

define i32 @user(ptr %b, ptr %fp) {
entry:
  %r = call i32 @setjmp(ptr %b)
  call void @longjmp(ptr %b, i32 %r)
  call void %fp(i32 1)
  %x = call i32 @get(ptr %b)
  ret i32 %x
}
