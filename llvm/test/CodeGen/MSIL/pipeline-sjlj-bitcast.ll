; A setjmp caller that calls a definition of the same module through a
; mismatched prototype (ImplementationPlan.md 5.13.6 slot P8, 5.15.4, M27).
; clang for MSIL triples emits fixed-arity K&R calls (function-bitcasts.ll),
; so 'void callee(); ... callee(buf, 5, 9)' with the definition 'void
; callee(char *b, int v)' is a call of @callee with the call type void (ptr,
; i32, i32). MSILFixFunctionBitcasts (P5) runs before MSILLowerSjLj (P8), so
; P5 has already replaced it with a call of the wrapper @callee_bitcast with
; the call's own type, and that call is the one P8 wraps: after P8 main calls
; '__msil_sjlj_invoke.0(ptr, i32, i32, ptr)', whose inner call is
; 'callee_bitcast(ptr, i32, i32)', and no call of @callee passes three
; arguments (were P8 before P5, its wrapper would call @callee with the
; mismatched type). The callee's own longjmp, in a function without a site,
; becomes a plain call of '__msil_longjmp'. The module is complete, so
; whole-program text (-O0 and -O2, both execution triples) compiles and
; assembles, and so does object-form text; in the IL main calls the wrapper,
; the wrapper's '.try' block calls 'callee_bitcast' with its three
; parameters, and 'callee_bitcast' calls 'callee' with its own signature. The
; object has exactly one exception clause, the wrapper's filter clause. If
; this test shows the order wrong, the orchestrator moves the slot by a
; section 12 row.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} =
; DEFINE: %{ir} = llc -mtriple=%{t} %{o} -stop-after=msil-lower-sjlj < %t/bc.ll -o - \
; DEFINE:   | FileCheck %s --check-prefix=IR \
; DEFINE:     --implicit-check-not="{{call void @callee\(ptr [^,]+, i32 [^,]+, i32 }}" \
; DEFINE:     --implicit-check-not="{{call .*@(_setjmp|longjmp)\(}}"
; DEFINE: %{ilchecks} = FileCheck %s --check-prefix=MAIN --input-file=%{il} && \
; DEFINE:   FileCheck %s --check-prefix=WRAP --input-file=%{il} && \
; DEFINE:   FileCheck %s --check-prefix=BC --input-file=%{il} \
; DEFINE:     --implicit-check-not="'callee'(native int, int32, int32)"
; DEFINE: %{wp} = %{ir} && \
; DEFINE:   llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/bc.ll -o %{il} 2>&1 | count 0 && \
; DEFINE:   %{ilchecks}
; DEFINE: %{of} = llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %t/bc.ll \
; DEFINE:   -o %{il} 2>&1 | count 0 && \
; DEFINE:   %{ilchecks} && \
; DEFINE:   llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/bc.ll -o %{il}.o 2>&1 | count 0

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; REDEFINE: %{il} = %t/wp.lnx.o0.il
; RUN: %{wp}
; REDEFINE: %{o} = -O2
; REDEFINE: %{il} = %t/wp.lnx.o2.il
; RUN: %{wp}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/wp.lnx.o2.dll %t/wp.lnx.o2.il %}
; REDEFINE: %{il} = %t/of.lnx.o2.il
; RUN: %{of}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/of.lnx.o2.ilasm.o %t/of.lnx.o2.il %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t/of.lnx.o2.il.o \
; RUN:   | FileCheck %s --check-prefix=DUMP --implicit-check-not=".try L" %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{il} = %t/wp.win.o2.il
; RUN: %{wp}
; REDEFINE: %{o} = -O0
; REDEFINE: %{il} = %t/wp.win.o0.il
; RUN: %{wp}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/wp.win.o0.dll %t/wp.win.o0.il %}
; REDEFINE: %{il} = %t/of.win.o0.il
; RUN: %{of}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/of.win.o0.ilasm.o %t/of.win.o0.il %}

; IR after P8 (P5's wrapper is the wrapped callee).
; IR-LABEL: define void @callee(ptr %b, i32 %v) {
; IR:       call void @__msil_longjmp(ptr %b, i32 %v)
; IR-LABEL: define i32 @main() {
; IR:       %[[ST:[^ ]+]] = alloca { i32, i32 }
; IR:       call void @__msil_sjlj_invoke.0(ptr %buf, i32 5, i32 9, ptr %[[ST]])
; IR:       define internal void @__msil_sjlj_invoke.0(ptr [[A:%[^ ,]+]], i32 [[B:%[^ ,]+]], i32 [[C:%[^ ,]+]], ptr {{[^)]*}}) #{{[0-9]+}} {
; IR:       call void @callee_bitcast(ptr [[A]], i32 [[B]], i32 [[C]])

; MAIN-LABEL: .method {{.*}}'main'(
; MAIN-NOT:   'callee
; MAIN:       {{^}}  call void '__msil_sjlj_invoke.0'(native int, int32, int32, native int){{$}}
; MAIN-NOT:   'callee
; MAIN:       {{^}}} // end of method 'main'{{$}}

; WRAP-LABEL: .method {{.*}} void '__msil_sjlj_invoke.0'(native int, int32, int32, native int) cil managed{{$}}
; WRAP:       {{^  \.try \{$}}
; WRAP-NEXT:  {{^}}@EH{{[0-9]+}}_0:{{$}}
; WRAP-NEXT:  {{^}}  ldarg.0{{$}}
; WRAP-NEXT:  {{^}}  ldarg.1{{$}}
; WRAP-NEXT:  {{^}}  ldarg.2{{$}}
; WRAP-NEXT:  {{^}}  call void 'callee_bitcast'(native int, int32, int32){{$}}
; WRAP:       {{^  \} filter \{$}}
; WRAP:       {{^}}} // end of method '__msil_sjlj_invoke.0'{{$}}

; BC-LABEL: .method {{.*}} void 'callee_bitcast'(native int, int32, int32) cil managed{{$}}
; BC:       {{^}}  call void 'callee'(native int, int32){{$}}
; BC:       {{^}}} // end of method 'callee_bitcast'{{$}}

; DUMP: .try L0 to L{{[0-9]+}} filter L{{[0-9]+}} handler L{{[0-9]+}} to L{{[0-9]+}}{{$}}

;--- bc.ll
source_filename = "pipeline-sjlj-bitcast.c"

declare i32 @_setjmp(ptr) #0
declare void @longjmp(ptr, i32) #1

define void @callee(ptr %b, i32 %v) {
entry:
  call void @longjmp(ptr %b, i32 %v) #3
  unreachable
}

define i32 @main() {
entry:
  %buf = alloca [256 x i8], align 16
  %r = call i32 @_setjmp(ptr %buf) #2
  %c = icmp eq i32 %r, 0
  br i1 %c, label %go, label %done
go:
  call void @callee(ptr %buf, i32 5, i32 9)
  br label %done
done:
  %x = phi i32 [ 0, %go ], [ %r, %entry ]
  ret i32 %x
}

attributes #0 = { nounwind returns_twice }
attributes #1 = { noreturn nounwind }
attributes #2 = { returns_twice }
attributes #3 = { noreturn }
