; The IR type of a .NET reference, target("msil.ref") (ImplementationPlan.md
; 5.21.6 RI1, RI5, RI6; milestone R-a, RV20): a target extension type with no
; parameters that LLVM's core registers (getTargetTypeInfo in
; llvm/lib/IR/Type.cpp) with the layout of a pointer and the properties
; HasZeroInit and CanBeLocal, and not CanBeGlobal. The IR is checked with the
; tools that read and verify it (llvm-as, opt -passes=verify, and llc, which
; verifies its input), on both host x86_64 triples, msil64-unknown-dotnet and
; aarch64 (data layouts stay native: a target type, not an address space):
; - values.ll: what the back end accepts (RI4) is valid IR: parameters and
;   results, the null reference 'zeroinitializer' (HasZeroInit) in a store,
;   a call, a 'ret', a 'phi' and a 'select', 'freeze', 'undef' and 'poison',
;   a single-element stack slot (CanBeLocal), and the comparison helper
;   '__msil_ref.eq' with RI6's attributes; opt prints it back unchanged;
; - layout.ll: the type takes the place of a pointer: InstCombine folds a
;   'getelementptr' over it to the pointer size (8 bytes on the 64-bit
;   triples, 4 on msil32-unknown-dotnet, whose data layout opt still
;   computes) and gives its stack slot the pointer's alignment; SROA promotes
;   a slot of it, as P15 does at every level;
; - optimized.ll: the IR that clang writes at -O0 for references (every
;   value in a slot of the type, 5.21.5 RG1, RG3), run through opt -O2, keeps
;   the type, loses every slot (SROA) and compiles with llc: object
;   parameters and results, no 'localloc', no memory access of an object;
; - the refusals, LLVM's own texts pinned whole: a global of the type, also
;   inside an array or a structure (not CanBeGlobal: 'Global @<g> has illegal
;   target extension type'; llc refuses its input before any MSIL pass,
;   reference-errors.ll checks P1's net under -disable-verify); a parameter
;   on the type ('target extension type msil.ref should have no
;   parameters', the check that R-a adds to TargetExtType::checkParams, for
;   a type and for an integer parameter); 'bitcast', 'ptrtoint' and
;   'inttoptr' ('invalid cast opcode'), and 'icmp' ('icmp requires integer
;   operands'), which LLVM refuses on every target type, so a reference never
;   becomes a number or an address in IR.

; RUN: rm -rf %t && split-file %s %t

; DEFINE: %{t} =
; DEFINE: %{values} = llvm-as < %t/values.ll -o /dev/null && \
; DEFINE:   opt -mtriple=%{t} -passes=verify -S < %t/values.ll | FileCheck %t/values.ll
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{values}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{values}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{values}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{values}

; RUN: opt -mtriple=x86_64-pc-windows-msvc-msil -passes=instcombine,sroa -S < %t/layout.ll \
; RUN:   | FileCheck %t/layout.ll --check-prefixes=CHECK,P64
; RUN: opt -mtriple=x86_64-unknown-linux-gnu-msil -passes=instcombine,sroa -S < %t/layout.ll \
; RUN:   | FileCheck %t/layout.ll --check-prefixes=CHECK,P64
; RUN: opt -mtriple=msil64-unknown-dotnet -passes=instcombine,sroa -S < %t/layout.ll \
; RUN:   | FileCheck %t/layout.ll --check-prefixes=CHECK,P64
; RUN: opt -mtriple=aarch64-pc-windows-msvc-msil -passes=instcombine,sroa -S < %t/layout.ll \
; RUN:   | FileCheck %t/layout.ll --check-prefixes=CHECK,P64
; RUN: opt -mtriple=msil32-unknown-dotnet -passes=instcombine,sroa -S < %t/layout.ll \
; RUN:   | FileCheck %t/layout.ll --check-prefixes=CHECK,P32

; DEFINE: %{opt} = opt -mtriple=%{t} -O2 -S < %t/optimized.ll -o %t/optimized.%{t}.ll && \
; DEFINE:   FileCheck %t/optimized.ll --check-prefix=IR --input-file=%t/optimized.%{t}.ll \
; DEFINE:     --implicit-check-not=alloca --implicit-check-not="load target" \
; DEFINE:     --implicit-check-not="store target" && \
; DEFINE:   llc -mtriple=%{t} -verify-machineinstrs < %t/optimized.%{t}.ll -o %t/optimized.%{t}.il 2>&1 \
; DEFINE:   | count 0 && \
; DEFINE:   FileCheck %t/optimized.ll --check-prefix=IL --input-file=%t/optimized.%{t}.il
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{opt}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{opt}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{opt}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{opt}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/optimized.dll %t/optimized.x86_64-pc-windows-msvc-msil.il %}

; RUN: not opt -mtriple=x86_64-pc-windows-msvc-msil -passes=verify -disable-output < %t/global.ll 2>&1 \
; RUN:   | FileCheck %t/global.ll
; RUN: not llvm-as < %t/global.ll -o /dev/null 2>&1 | FileCheck %t/global.ll
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/global.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %t/global.ll --check-prefixes=CHECK,LLC --implicit-check-not="LLVM ERROR" \
; RUN:     --implicit-check-not="PLEASE submit"
; RUN: not llvm-as < %t/global-array.ll -o /dev/null 2>&1 | FileCheck %t/global-array.ll
; RUN: not llvm-as < %t/global-struct.ll -o /dev/null 2>&1 | FileCheck %t/global-struct.ll
; RUN: not llvm-as < %t/global-extern.ll -o /dev/null 2>&1 | FileCheck %t/global-extern.ll

; RUN: not llvm-as < %t/param-type.ll -o /dev/null 2>&1 | FileCheck %t/param-type.ll
; RUN: not llvm-as < %t/param-int.ll -o /dev/null 2>&1 | FileCheck %t/param-int.ll
; RUN: not opt -mtriple=msil64-unknown-dotnet -passes=verify -disable-output < %t/param-int.ll 2>&1 \
; RUN:   | FileCheck %t/param-int.ll

; RUN: not llvm-as < %t/bitcast.ll -o /dev/null 2>&1 | FileCheck %t/bitcast.ll
; RUN: not llvm-as < %t/ptrtoint.ll -o /dev/null 2>&1 | FileCheck %t/ptrtoint.ll
; RUN: not llvm-as < %t/inttoptr.ll -o /dev/null 2>&1 | FileCheck %t/inttoptr.ll
; RUN: not llvm-as < %t/icmp.ll -o /dev/null 2>&1 | FileCheck %t/icmp.ll

;--- values.ll
; CHECK-LABEL: define internal target("msil.ref") @pass(target("msil.ref") %a) {
; CHECK-NEXT:    ret target("msil.ref") %a
define internal target("msil.ref") @pass(target("msil.ref") %a) {
  ret target("msil.ref") %a
}

; CHECK-LABEL: define internal target("msil.ref") @null() {
; CHECK-NEXT:    ret target("msil.ref") zeroinitializer
define internal target("msil.ref") @null() {
  ret target("msil.ref") zeroinitializer
}

; CHECK-LABEL: define internal target("msil.ref") @undefs(i1 %c) {
; CHECK-NEXT:    %u = select i1 %c, target("msil.ref") undef, target("msil.ref") poison
; CHECK-NEXT:    %f = freeze target("msil.ref") %u
; CHECK-NEXT:    ret target("msil.ref") %f
define internal target("msil.ref") @undefs(i1 %c) {
  %u = select i1 %c, target("msil.ref") undef, target("msil.ref") poison
  %f = freeze target("msil.ref") %u
  ret target("msil.ref") %f
}

; CHECK-LABEL: define internal i32 @slot(target("msil.ref") %a, i1 %c) {
; CHECK:         %s = alloca target("msil.ref"), align {{[48]}}
; CHECK-NEXT:    store target("msil.ref") zeroinitializer, ptr %s
; CHECK-NEXT:    br i1 %c, label %set, label %join
; CHECK:         store target("msil.ref") %a, ptr %s
; CHECK:         %p = phi target("msil.ref") [ zeroinitializer, %entry ], [ %a, %set ]
; CHECK-NEXT:    %v = load target("msil.ref"), ptr %s
; CHECK-NEXT:    %e = call i1 @__msil_ref.eq(target("msil.ref") %v, target("msil.ref") %p)
; CHECK-NEXT:    %n = call i1 @__msil_ref.eq(target("msil.ref") %v, target("msil.ref") zeroinitializer)
; CHECK-NEXT:    %r = call target("msil.ref") @pass(target("msil.ref") zeroinitializer)
define internal i32 @slot(target("msil.ref") %a, i1 %c) {
entry:
  %s = alloca target("msil.ref")
  store target("msil.ref") zeroinitializer, ptr %s
  br i1 %c, label %set, label %join
set:
  store target("msil.ref") %a, ptr %s
  br label %join
join:
  %p = phi target("msil.ref") [ zeroinitializer, %entry ], [ %a, %set ]
  %v = load target("msil.ref"), ptr %s
  %e = call i1 @__msil_ref.eq(target("msil.ref") %v, target("msil.ref") %p)
  %n = call i1 @__msil_ref.eq(target("msil.ref") %v, target("msil.ref") zeroinitializer)
  %r = call target("msil.ref") @pass(target("msil.ref") zeroinitializer)
  %x = xor i1 %e, %n
  %z = zext i1 %x to i32
  ret i32 %z
}

; CHECK: declare i1 @__msil_ref.eq(target("msil.ref"), target("msil.ref")) #[[EQ:[0-9]+]]
; CHECK: attributes #[[EQ]] = { nounwind speculatable willreturn memory(none) }
declare i1 @"__msil_ref.eq"(target("msil.ref"), target("msil.ref")) memory(none) nounwind willreturn speculatable

;--- layout.ll
; CHECK-LABEL: define ptr @next(ptr %p) {
; P64-NEXT:      %q = getelementptr i8, ptr %p, i64 8
; P32-NEXT:      %q = getelementptr i8, ptr %p, i32 4
define ptr @next(ptr %p) {
  %q = getelementptr target("msil.ref"), ptr %p, i64 1
  ret ptr %q
}

; CHECK-LABEL: define i64 @size() {
; P64-NEXT:      ret i64 8
; P32-NEXT:      ret i64 4
define i64 @size() {
  ret i64 ptrtoint (ptr getelementptr (target("msil.ref"), ptr null, i64 1) to i64)
}

; The slot of an escaping reference keeps the pointer's alignment.
; CHECK-LABEL: define void @slot_align() {
; P64-NEXT:      %s = alloca target("msil.ref"), align 8
; P32-NEXT:      %s = alloca target("msil.ref"), align 4
define void @slot_align() {
  %s = alloca target("msil.ref")
  call void @use(ptr %s)
  ret void
}

; SROA promotes a slot that only plain loads and stores of the type use: the
; value is a phi of the reference and the null reference.
; CHECK-LABEL: define internal target("msil.ref") @promoted(target("msil.ref") %a, i1 %c) {
; CHECK-NOT:     alloca
; CHECK-NOT:     load
; CHECK:         %[[V:.+]] = phi target("msil.ref") {{\[ %a, %set \], \[ zeroinitializer, %[0-9a-z]+ \]|\[ zeroinitializer, %[0-9a-z]+ \], \[ %a, %set \]}}
; CHECK-NEXT:    ret target("msil.ref") %[[V]]
define internal target("msil.ref") @promoted(target("msil.ref") %a, i1 %c) {
  %s = alloca target("msil.ref")
  store target("msil.ref") zeroinitializer, ptr %s
  br i1 %c, label %set, label %out
set:
  store target("msil.ref") %a, ptr %s
  br label %out
out:
  %v = load target("msil.ref"), ptr %s
  ret target("msil.ref") %v
}

declare void @use(ptr)

;--- optimized.ll
; The object comes from a managed call (5.21.9: System.Object's '.ctor' with
; a 'ref' result), so the optimizer cannot fold the choice away.
; IR-LABEL: define internal {{.*}}target("msil.ref") @choose(i32
; IR:         select i1 %{{.+}}, target("msil.ref") {{.+}}, target("msil.ref") {{.+}}
; IR-LABEL: define {{.*}}i32 @main(i32
; IR:         call {{.*}}target("msil.ref") @__msil_mc.m_new()
; IR:         call {{.*}}target("msil.ref") @choose(i32
; IL-LABEL: .method assembly static object 'choose'(int32,
; IL-NOT:     {{localloc|ldind|stind}}
; IL:         brtrue
; IL-NOT:     {{localloc|ldind|stind}}
; IL:       } // end of method 'choose'
; IL-LABEL: .method public static int32 'main'(int32, native int) cil managed
; IL-NOT:     {{localloc|ldind|stind}}
; IL:         call object '__msil_mc.m_new'()
; IL-NOT:     {{localloc|ldind|stind}}
; IL:         call object 'choose'(int32,
; IL-NOT:     {{localloc|ldind|stind}}
; IL:       } // end of method 'main'
; IL-LABEL: .method assembly static object '__msil_mc.m_new'() cil managed
; IL:         newobj instance void [mscorlib]System.Object::.ctor()
; IL-NEXT:    ret
define internal target("msil.ref") @choose(i32 %c, target("msil.ref") %a, target("msil.ref") %b) noinline {
entry:
  %c.addr = alloca i32, align 4
  %a.addr = alloca target("msil.ref"), align 8
  %b.addr = alloca target("msil.ref"), align 8
  %r = alloca target("msil.ref"), align 8
  store i32 %c, ptr %c.addr, align 4
  store target("msil.ref") %a, ptr %a.addr, align 8
  store target("msil.ref") %b, ptr %b.addr, align 8
  store target("msil.ref") zeroinitializer, ptr %r, align 8
  %cv = load i32, ptr %c.addr, align 4
  %t = icmp ne i32 %cv, 0
  br i1 %t, label %then, label %else
then:
  %av = load target("msil.ref"), ptr %a.addr, align 8
  store target("msil.ref") %av, ptr %r, align 8
  br label %done
else:
  %bv = load target("msil.ref"), ptr %b.addr, align 8
  store target("msil.ref") %bv, ptr %r, align 8
  br label %done
done:
  %rv = load target("msil.ref"), ptr %r, align 8
  ret target("msil.ref") %rv
}

define i32 @main(i32 %argc, ptr %argv) {
entry:
  %x = alloca target("msil.ref"), align 8
  %o = call target("msil.ref") @__msil_mc.m_new()
  store target("msil.ref") %o, ptr %x, align 8
  %xv = load target("msil.ref"), ptr %x, align 8
  %y = call target("msil.ref") @choose(i32 %argc, target("msil.ref") %xv, target("msil.ref") zeroinitializer)
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %y, target("msil.ref") zeroinitializer)
  %r = zext i1 %e to i32
  ret i32 %r
}

declare i1 @"__msil_ref.eq"(target("msil.ref"), target("msil.ref")) memory(none) nounwind willreturn speculatable
declare target("msil.ref") @__msil_mc.m_new() #0

attributes #0 = { "msil-managed-conv"=";ref" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void()" "msil-managed-type"="System.Object" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_new", !"System.Object", !".ctor", !"instance void()", !"", !";ref", !""}

;--- global.ll
; CHECK: {{^}}Global @g has illegal target extension type{{$}}
; LLC:   error: '<stdin>': input module cannot be verified
@g = internal global target("msil.ref") zeroinitializer

;--- global-array.ll
; CHECK: {{^}}Global @ga has illegal target extension type{{$}}
@ga = internal global [2 x target("msil.ref")] zeroinitializer

;--- global-struct.ll
; CHECK: {{^}}Global @gs has illegal target extension type{{$}}
@gs = internal global { i32, target("msil.ref") } zeroinitializer

;--- global-extern.ll
; CHECK: {{^}}Global @ge has illegal target extension type{{$}}
@ge = external global target("msil.ref")

;--- param-type.ll
; CHECK: error: target extension type msil.ref should have no parameters{{$}}
define void @f(target("msil.ref", i32) %a) {
  ret void
}

;--- param-int.ll
; CHECK: error: target extension type msil.ref should have no parameters{{$}}
define void @f(target("msil.ref", 0) %a) {
  ret void
}

;--- bitcast.ll
; CHECK: error: invalid cast opcode for cast from 'target("msil.ref")' to 'ptr'{{$}}
define ptr @f(target("msil.ref") %a) {
  %b = bitcast target("msil.ref") %a to ptr
  ret ptr %b
}

;--- ptrtoint.ll
; CHECK: error: invalid cast opcode for cast from 'target("msil.ref")' to 'i64'{{$}}
define i64 @f(target("msil.ref") %a) {
  %b = ptrtoint target("msil.ref") %a to i64
  ret i64 %b
}

;--- inttoptr.ll
; CHECK: error: invalid cast opcode for cast from 'i64' to 'target("msil.ref")'{{$}}
define target("msil.ref") @f(i64 %a) {
  %b = inttoptr i64 %a to target("msil.ref")
  ret target("msil.ref") %b
}

;--- icmp.ll
; CHECK: error: icmp requires integer operands{{$}}
define i1 @f(target("msil.ref") %a, target("msil.ref") %b) {
  %c = icmp eq target("msil.ref") %a, %b
  ret i1 %c
}
