; The frame escape intrinsics are diagnosed in Phase 1 (ImplementationPlan.md
; section 2, 'Diagnostics leave well-formed output'; M7 review-r1 finding 4,
; amendment log 2026-10-14, fix-loop rows for fix-2). With -fms-extensions,
; which is on by default for windows-msvc, clang outlines a C __finally block
; or __except filter into a funclet that reaches the locals of its parent
; through llvm.localescape and llvm.localaddress in the parent and
; llvm.localrecover and llvm.eh.recoverfp in the funclet; at -O2 the
; __finally funclet is inlined, so the parent holds all of them. MSIL has no
; lowering for them: MSILPrepareIR reports each definition that calls one
; once, naming the first call, and deletes its body, so the parent and its
; funclet are both diagnosed and neither is printed. llc exits with 1 without
; 'LLVM ERROR' or a crash report, and the other functions compile as usual.
; An __except with a constant filter and no escaped locals uses none of these
; intrinsics and still compiles.
;
; llc deletes its -o file after an error, so the modules that are checked and
; assembled are taken from stdout.

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not=error: --implicit-check-not=warning: \
; DEFINE:   --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}

; The IR that MSILPrepareIR leaves: the diagnosed definitions are
; declarations, and no frame escape intrinsic is called any more.
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-prepare-ir < %s -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not="call void (...) @llvm.localescape" \
; RUN:     --implicit-check-not="call ptr @llvm.localrecover" --implicit-check-not="call ptr @llvm.localaddress" \
; RUN:     --implicit-check-not="call ptr @llvm.eh.recoverfp"

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.o0.il
; RUN: FileCheck %s --check-prefix=IL --implicit-check-not=.method --input-file=%t.lnx.o0.il
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.o2.il
; RUN: FileCheck %s --check-prefix=IL --implicit-check-not=.method --input-file=%t.lnx.o2.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.win.o0.il
; RUN: FileCheck %s --check-prefix=IL --implicit-check-not=.method --input-file=%t.win.o0.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.win.o2.il
; RUN: FileCheck %s --check-prefix=IL --implicit-check-not=.method --input-file=%t.win.o2.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o0.dll %t.lnx.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o2.dll %t.win.o2.il %}

source_filename = "frame-escape-errors.c"

; IL: .assembly 'frame-escape-errors'

; IR: define i32 @g(ptr %p)
; IL: .method {{.*}}'g'(native int) cil managed{{$}}
define i32 @g(ptr %p) {
entry:
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

; Each intrinsic on its own.

; ERR: error: {{.*}}in function escape i32 (): frame escape ('llvm.localescape') is not supported for MSIL{{$}}
; IR: declare i32 @escape()
define i32 @escape() {
entry:
  %a = alloca i32, align 4
  call void (...) @llvm.localescape(ptr %a)
  store i32 7, ptr %a, align 4
  %v = load i32, ptr %a, align 4
  ret i32 %v
}

; ERR: error: {{.*}}in function recover ptr (ptr): frame escape ('llvm.localrecover') is not supported for MSIL{{$}}
; IR: declare ptr @recover(ptr)
define ptr @recover(ptr %fp) {
entry:
  %p = call ptr @llvm.localrecover(ptr @escape, ptr %fp, i32 0)
  ret ptr %p
}

; ERR: error: {{.*}}in function address ptr (): frame escape ('llvm.localaddress') is not supported for MSIL{{$}}
; IR: declare ptr @address()
define ptr @address() {
entry:
  %fp = call ptr @llvm.localaddress()
  ret ptr %fp
}

; ERR: error: {{.*}}in function recoverfp ptr (ptr): frame escape ('llvm.eh.recoverfp') is not supported for MSIL{{$}}
; IR: declare ptr @recoverfp(ptr)
define ptr @recoverfp(ptr %fp) {
entry:
  %p = call ptr @llvm.eh.recoverfp(ptr @escape, ptr %fp)
  ret ptr %p
}

; clang -O0 (attributes and metadata trimmed):
;   int h(int x) { int r = 0; __try { r = g(&x); } __finally { r += x; }
;                  return r; }
; The parent calls the funclet after the __try and from its cleanup pad.

; ERR: error: {{.*}}in function h i32 (i32): frame escape ('llvm.localescape') is not supported for MSIL{{$}}
; IR: declare i32 @h(i32 noundef)
define i32 @h(i32 noundef %x) personality ptr @__C_specific_handler {
entry:
  %x.addr = alloca i32, align 4
  %r = alloca i32, align 4
  call void (...) @llvm.localescape(ptr %r, ptr %x.addr)
  store i32 %x, ptr %x.addr, align 4
  store i32 0, ptr %r, align 4
  %call = invoke i32 @g(ptr noundef %x.addr)
          to label %invoke.cont unwind label %ehcleanup

invoke.cont:
  store i32 %call, ptr %r, align 4
  %0 = call ptr @llvm.localaddress()
  call void @"?fin$0@0@h@@"(i8 noundef 0, ptr noundef %0)
  %1 = load i32, ptr %r, align 4
  ret i32 %1

ehcleanup:
  %2 = cleanuppad within none []
  %3 = call ptr @llvm.localaddress()
  call void @"?fin$0@0@h@@"(i8 noundef 1, ptr noundef %3) [ "funclet"(token %2) ]
  cleanupret from %2 unwind to caller
}

; ERR: error: {{.*}}in function ?fin$0@0@h@@ void (i8, ptr): frame escape ('llvm.localrecover') is not supported for MSIL{{$}}
; IR: declare {{.*}}void @"?fin$0@0@h@@"(i8 noundef, ptr noundef)
define internal void @"?fin$0@0@h@@"(i8 noundef %abnormal_termination, ptr noundef %frame_pointer) {
entry:
  %frame_pointer.addr = alloca ptr, align 8
  %abnormal_termination.addr = alloca i8, align 1
  %r = call ptr @llvm.localrecover(ptr @h, ptr %frame_pointer, i32 0)
  %x.addr = call ptr @llvm.localrecover(ptr @h, ptr %frame_pointer, i32 1)
  store ptr %frame_pointer, ptr %frame_pointer.addr, align 8
  store i8 %abnormal_termination, ptr %abnormal_termination.addr, align 1
  %0 = load i32, ptr %x.addr, align 4
  %1 = load i32, ptr %r, align 4
  %add = add i32 %1, %0
  store i32 %add, ptr %r, align 4
  ret void
}

; The same at clang -O2: the funclet is inlined twice, so the parent calls
; llvm.localaddress twice and llvm.localrecover four times. It is reported
; once.

; ERR: error: {{.*}}in function h_o2 i32 (i32): frame escape ('llvm.localescape') is not supported for MSIL{{$}}
; IR: declare i32 @h_o2(i32 noundef)
define i32 @h_o2(i32 noundef %x) personality ptr @__C_specific_handler {
entry:
  %x.addr = alloca i32, align 4
  %r = alloca i32, align 4
  call void (...) @llvm.localescape(ptr nonnull %r, ptr nonnull %x.addr)
  store i32 %x, ptr %x.addr, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %r)
  store i32 0, ptr %r, align 4
  %call = invoke i32 @g(ptr noundef nonnull %x.addr)
          to label %invoke.cont unwind label %ehcleanup

invoke.cont:
  store i32 %call, ptr %r, align 4
  %0 = tail call ptr @llvm.localaddress()
  %r.i = call ptr @llvm.localrecover(ptr nonnull @h_o2, ptr %0, i32 0)
  %x.addr.i = call ptr @llvm.localrecover(ptr nonnull @h_o2, ptr %0, i32 1)
  %1 = load i32, ptr %x.addr.i, align 4
  %2 = load i32, ptr %r.i, align 4
  %add.i = add i32 %2, %1
  store i32 %add.i, ptr %r.i, align 4
  %3 = load i32, ptr %r, align 4
  call void @llvm.lifetime.end.p0(ptr nonnull %r)
  ret i32 %3

ehcleanup:
  %4 = cleanuppad within none []
  %5 = tail call ptr @llvm.localaddress()
  %r.i2 = call ptr @llvm.localrecover(ptr nonnull @h_o2, ptr %5, i32 0)
  %x.addr.i3 = call ptr @llvm.localrecover(ptr nonnull @h_o2, ptr %5, i32 1)
  %6 = load i32, ptr %x.addr.i3, align 4
  %7 = load i32, ptr %r.i2, align 4
  %add.i4 = add i32 %7, %6
  store i32 %add.i4, ptr %r.i2, align 4
  call void @llvm.lifetime.end.p0(ptr nonnull %r)
  cleanupret from %4 unwind to caller
}

; clang -O0:
;   int f(int x) { int r = 0; __try { r = g(&x); } __except (x > 0) { r = -1; }
;                  return r; }
; The filter funclet finds the parent's frame with llvm.eh.recoverfp.

; ERR: error: {{.*}}in function f i32 (i32): frame escape ('llvm.localescape') is not supported for MSIL{{$}}
; IR: declare i32 @f(i32 noundef)
define i32 @f(i32 noundef %x) personality ptr @__C_specific_handler {
entry:
  %x.addr = alloca i32, align 4
  %r = alloca i32, align 4
  %__exception_code = alloca i32, align 4
  call void (...) @llvm.localescape(ptr %x.addr)
  store i32 %x, ptr %x.addr, align 4
  store i32 0, ptr %r, align 4
  %call = invoke i32 @g(ptr noundef %x.addr)
          to label %invoke.cont unwind label %catch.dispatch

catch.dispatch:
  %0 = catchswitch within none [label %__except.ret] unwind to caller

__except.ret:
  %1 = catchpad within %0 [ptr @"?filt$0@0@f@@"]
  catchret from %1 to label %__except

__except:
  %2 = call i32 @llvm.eh.exceptioncode(token %1)
  store i32 %2, ptr %__exception_code, align 4
  store i32 -1, ptr %r, align 4
  br label %__try.cont

__try.cont:
  %3 = load i32, ptr %r, align 4
  ret i32 %3

invoke.cont:
  store i32 %call, ptr %r, align 4
  br label %__try.cont
}

; ERR: error: {{.*}}in function ?filt$0@0@f@@ i32 (ptr, ptr): frame escape ('llvm.eh.recoverfp') is not supported for MSIL{{$}}
; IR: declare {{.*}}i32 @"?filt$0@0@f@@"(ptr noundef, ptr noundef)
define internal i32 @"?filt$0@0@f@@"(ptr noundef %exception_pointers, ptr noundef %frame_pointer) {
entry:
  %frame_pointer.addr = alloca ptr, align 8
  %exception_pointers.addr = alloca ptr, align 8
  %0 = call ptr @llvm.eh.recoverfp(ptr @f, ptr %frame_pointer)
  %x.addr = call ptr @llvm.localrecover(ptr @f, ptr %0, i32 0)
  %__exception_code = alloca i32, align 4
  store ptr %frame_pointer, ptr %frame_pointer.addr, align 8
  store ptr %exception_pointers, ptr %exception_pointers.addr, align 8
  %1 = getelementptr inbounds nuw { ptr, ptr }, ptr %exception_pointers, i32 0, i32 0
  %2 = load ptr, ptr %1, align 8
  %3 = load i32, ptr %2, align 4
  store i32 %3, ptr %__exception_code, align 4
  %4 = load i32, ptr %x.addr, align 4
  %cmp = icmp sgt i32 %4, 0
  %conv = zext i1 %cmp to i32
  ret i32 %conv
}

; The same at clang -O2: the filter is not inlined.

; ERR: error: {{.*}}in function f_o2 i32 (i32): frame escape ('llvm.localescape') is not supported for MSIL{{$}}
; IR: declare i32 @f_o2(i32 noundef)
define i32 @f_o2(i32 noundef %x) personality ptr @__C_specific_handler {
entry:
  %x.addr = alloca i32, align 4
  call void (...) @llvm.localescape(ptr nonnull %x.addr)
  store i32 %x, ptr %x.addr, align 4
  %call = invoke i32 @g(ptr noundef nonnull %x.addr)
          to label %__try.cont unwind label %catch.dispatch

catch.dispatch:
  %0 = catchswitch within none [label %__except.ret] unwind to caller

__except.ret:
  %1 = catchpad within %0 [ptr @"?filt$0@0@f_o2@@"]
  catchret from %1 to label %__except

__except:
  %2 = call i32 @llvm.eh.exceptioncode(token %1)
  br label %__try.cont

__try.cont:
  %r.0 = phi i32 [ -1, %__except ], [ %call, %entry ]
  ret i32 %r.0
}

; ERR: error: {{.*}}in function ?filt$0@0@f_o2@@ i32 (ptr, ptr): frame escape ('llvm.eh.recoverfp') is not supported for MSIL{{$}}
; IR: declare {{.*}}i32 @"?filt$0@0@f_o2@@"(ptr {{.*}}, ptr {{.*}})
define internal range(i32 0, 2) i32 @"?filt$0@0@f_o2@@"(ptr nofree readnone captures(none) %exception_pointers, ptr nofree noundef readonly %frame_pointer) {
entry:
  %0 = tail call ptr @llvm.eh.recoverfp(ptr nonnull @f_o2, ptr %frame_pointer)
  %x.addr = tail call ptr @llvm.localrecover(ptr nonnull @f_o2, ptr %0, i32 0)
  %1 = load i32, ptr %x.addr, align 4
  %cmp = icmp sgt i32 %1, 0
  %conv = zext i1 %cmp to i32
  ret i32 %conv
}

; clang -O0 for an __except with a constant filter that reads no local: no
; frame escape, and the function compiles.
;   int f_const(int x) { int r = 0; __try { r = g(&x); } __except (1) { r = -1; }
;                        return r; }

; IR: define i32 @f_const(i32 noundef %x) personality ptr @__C_specific_handler
; IL: .method {{.*}}'f_const'(int32) cil managed{{$}}
; IL: call int32 'g'(native int)
; IL: } // end of method 'f_const'{{$}}
define i32 @f_const(i32 noundef %x) personality ptr @__C_specific_handler {
entry:
  %x.addr = alloca i32, align 4
  %r = alloca i32, align 4
  %__exception_code = alloca i32, align 4
  store i32 %x, ptr %x.addr, align 4
  store i32 0, ptr %r, align 4
  %call = invoke i32 @g(ptr noundef %x.addr)
          to label %invoke.cont unwind label %catch.dispatch

catch.dispatch:
  %0 = catchswitch within none [label %__except] unwind to caller

__except:
  %1 = catchpad within %0 [ptr null]
  catchret from %1 to label %__except1

__except1:
  %2 = call i32 @llvm.eh.exceptioncode(token %1)
  store i32 %2, ptr %__exception_code, align 4
  store i32 -1, ptr %r, align 4
  br label %__try.cont

__try.cont:
  %3 = load i32, ptr %r, align 4
  ret i32 %3

invoke.cont:
  store i32 %call, ptr %r, align 4
  br label %__try.cont
}

; The function after them compiles as usual.
; IR: define i32 @after(i32 %a)
; IL-LABEL: .method {{.*}}'after'(
; IL-SAME:  int32) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.1{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'after'{{$}}
define i32 @after(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

declare i32 @__C_specific_handler(...)
declare void @llvm.localescape(...)
declare ptr @llvm.localrecover(ptr, ptr, i32 immarg)
declare ptr @llvm.localaddress()
declare ptr @llvm.eh.recoverfp(ptr, ptr)
declare i32 @llvm.eh.exceptioncode(token)
declare void @llvm.lifetime.start.p0(ptr captures(none))
declare void @llvm.lifetime.end.p0(ptr captures(none))
