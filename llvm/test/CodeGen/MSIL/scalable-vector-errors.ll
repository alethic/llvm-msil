; Scalable vector types, target extension types and vscale are diagnosed
; (review-r2 finding 2, M10 fix-loop row for fix-7; ImplementationPlan.md
; section 2, 'Diagnostics leave well-formed output'). D7 accepts every native
; feature string, so clang compiles the SVE types on the aarch64 MSIL triples
; (-march=armv8-a+sve, __attribute__((target("sve2p1")))): __SVInt32_t is
; <vscale x 4 x i32> and __SVCount_t is target("aarch64.svcount"). Their size
; is a multiple of vscale, a property of the native vector unit that CIL does
; not have, and Design.md 3.2 maps only fixed-length vectors. Instruction
; selection used to stop with 'LLVM ERROR: Don't know how to legalize this
; scalable vector type', 'scalarization of scalable vectors is not supported'
; or 'Cannot select: vscale', or an assertion for svcount, and a scalable
; alloca compiled silently with vscale taken as 1. MSILPrepareIR now reports
; one error per definition that names the type (or the intrinsic that reads
; vscale) and deletes the body; a declaration with such a type in its
; signature is reported only when its address is taken, and a global variable
; whose type has a target extension type is reported and erased. llc exits
; with 1 without 'LLVM ERROR' or a crash report, and the other functions
; compile as usual. The types and intrinsics are part of the IR whichever
; backends are built (there is no AArch64 backend, decision D9), and the
; check does not depend on the triple.
;
; llc deletes its -o file after an error, so the modules that are checked and
; assembled are taken from stdout.

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not=error: --implicit-check-not=warning: \
; DEFINE:   --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}

; The IR that MSILPrepareIR leaves: the diagnosed definitions are
; declarations, no function reads vscale any more, and the addresses of the
; diagnosed functions are poison.
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %s -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not="call i64 @llvm.vscale" \
; RUN:     --implicit-check-not="call i32 @llvm.experimental.get.vector.length.i64(" --implicit-check-not=@refs

; DEFINE: %{il} = FileCheck %s --check-prefix=IL --implicit-check-not=.method --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=vscale --implicit-check-not="'sve_identity'" --implicit-check-not="'ext_svcount'" \
; DEFINE:   --implicit-check-not="'import_sve'" --implicit-check-not="'refs'"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s > %t.lnx.il
; RUN: %{il} --input-file=%t.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s > %t.win.o0.il
; RUN: %{il} --input-file=%t.win.o0.il
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s > %t.a64.o0.il
; RUN: %{il} --input-file=%t.a64.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.a64.o0.dll %t.a64.o0.il %}

source_filename = "scalable-vector-errors.c"

; A global variable whose type has a target extension type (the IR verifier
; rejects scalable types in globals, but not every target extension type) is
; erased, and its address becomes poison.
@refs = global { target("wasm.externref"), i64 } zeroinitializer

; The address of a declaration whose signature has a target extension type, in
; data: the declaration is reported and the address becomes poison.
; IR: @svcount_fn = global ptr poison
@svcount_fn = global ptr @ext_svcount

; IL: .assembly 'scalable-vector-errors'

; A function without scalable types before the diagnosed ones.
; IR: define void @sink(ptr %p)
; IL: .method {{.*}}'sink'(native int) cil managed{{$}}
define void @sink(ptr %p) {
  ret void
}

; __SVInt32_t id(__SVInt32_t a) { return a; }: in the signature.
; ERR: error: {{.*}}in function sve_identity <vscale x 4 x i32> (<vscale x 4 x i32>): scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
; IR: declare <vscale x 4 x i32> @sve_identity(<vscale x 4 x i32>)
define <vscale x 4 x i32> @sve_identity(<vscale x 4 x i32> %a) {
  ret <vscale x 4 x i32> %a
}

; A load and a store (*q = *p for two __SVInt32_t pointers).
; ERR: error: {{.*}}in function sve_copy void (ptr, ptr): scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
; IR: declare void @sve_copy(ptr, ptr)
define void @sve_copy(ptr %p, ptr %q) {
  %v = load <vscale x 4 x i32>, ptr %p, align 16
  store <vscale x 4 x i32> %v, ptr %q, align 16
  ret void
}

; A local __SVInt32_t at -O0: only the alloca has the type. It used to
; compile with vscale taken as 1 (a 16-byte localloc).
; ERR: error: {{.*}}in function sve_alloca void (): scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
; IR: declare void @sve_alloca()
define void @sve_alloca() {
  %a.addr = alloca <vscale x 4 x i32>, align 16
  call void @sink(ptr %a.addr)
  ret void
}

; An SVE tuple (svint32x2_t) is a structure of scalable vectors.
; ERR: error: {{.*}}in function sve_tuple { <vscale x 4 x i32>, <vscale x 4 x i32> } (ptr): scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
; IR: declare { <vscale x 4 x i32>, <vscale x 4 x i32> } @sve_tuple(ptr)
define { <vscale x 4 x i32>, <vscale x 4 x i32> } @sve_tuple(ptr %p) {
  %r = load { <vscale x 4 x i32>, <vscale x 4 x i32> }, ptr %p, align 16
  ret { <vscale x 4 x i32>, <vscale x 4 x i32> } %r
}

; Pointer arithmetic over a scalable type: the getelementptr has only pointer
; operands, but its offset is a multiple of vscale.
; ERR: error: {{.*}}in function sve_gep ptr (ptr): scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
; IR: declare ptr @sve_gep(ptr)
define ptr @sve_gep(ptr %p) {
  %q = getelementptr <vscale x 4 x i32>, ptr %p, i64 1
  ret ptr %q
}

; A call of a declaration with a scalable signature reports the caller; the
; declaration, used only by the deleted body, is not reported.
; ERR: error: {{.*}}in function call_ext_sve void (ptr): scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
; IR: declare void @call_ext_sve(ptr)
define void @call_ext_sve(ptr %p) {
  %v = load <vscale x 4 x i32>, ptr %p, align 16
  %r = call <vscale x 4 x i32> @ext_sve(<vscale x 4 x i32> %v)
  store <vscale x 4 x i32> %r, ptr %p, align 16
  ret void
}

; __SVCount_t f(__SVCount_t c) under __attribute__((target("sve2p1"))): a
; target extension type in the signature.
; ERR: error: {{.*}}in function svcount_arg i32 (target("aarch64.svcount")): type 'target("aarch64.svcount")' is not supported for MSIL{{$}}
; IR: declare i32 @svcount_arg(target("aarch64.svcount"))
define i32 @svcount_arg(target("aarch64.svcount") %c) {
  ret i32 0
}

; A local __SVCount_t at -O0.
; ERR: error: {{.*}}in function svcount_alloca void (): type 'target("aarch64.svcount")' is not supported for MSIL{{$}}
; IR: declare void @svcount_alloca()
define void @svcount_alloca() {
  %c.addr = alloca target("aarch64.svcount"), align 2
  call void @sink(ptr %c.addr)
  ret void
}

; svcntb() without the SVE intrinsic: llvm.vscale is a generic intrinsic with
; no scalable type in its signature. Called twice and reported once.
; ERR: error: {{.*}}in function get_vscale i64 (): intrinsic 'llvm.vscale.i64' is not supported for MSIL{{$}}
; IR: declare i64 @get_vscale()
define i64 @get_vscale() {
  %v1 = call i64 @llvm.vscale.i64()
  %v2 = call i64 @llvm.vscale.i64()
  %b = shl i64 %v2, 4
  %r = add i64 %v1, %b
  ret i64 %r
}

; The scalable form of llvm.experimental.get.vector.length reads vscale too.
; ERR: error: {{.*}}in function gvl_scalable i32 (i64): intrinsic 'llvm.experimental.get.vector.length.i64' is not supported for MSIL{{$}}
; IR: declare i32 @gvl_scalable(i64)
define i32 @gvl_scalable(i64 %n) {
  %r = call i32 @llvm.experimental.get.vector.length.i64(i64 %n, i32 4, i1 true)
  ret i32 %r
}

; Its fixed-length form is a umin, which compiles as usual.
; IR: define i32 @gvl_fixed(i32 %n)
; IR: call i32 @llvm.experimental.get.vector.length.i32(i32 %n, i32 4, i1 false)
; IL: .method {{.*}}'gvl_fixed'(int32) cil managed{{$}}
; IL: } // end of method 'gvl_fixed'{{$}}
define i32 @gvl_fixed(i32 %n) {
  %r = call i32 @llvm.experimental.get.vector.length.i32(i32 %n, i32 4, i1 false)
  ret i32 %r
}

; The address of a diagnosed definition is poison: no method, P/Invoke or
; address field names it.
; IR: define ptr @addr_of_sve_identity()
; IR-NEXT: ret ptr poison
; IL: .method {{.*}}'addr_of_sve_identity'() cil managed{{$}}
; IL: } // end of method 'addr_of_sve_identity'{{$}}
define ptr @addr_of_sve_identity() {
  ret ptr @sve_identity
}

; The address of a declaration with a target extension type in its signature,
; in code. It used to crash the AsmPrinter, which computes the CIL signature
; of an undefined function whose address is taken.
; IR: define ptr @addr_of_ext_svcount()
; IR-NEXT: ret ptr poison
; IL: .method {{.*}}'addr_of_ext_svcount'() cil managed{{$}}
; IL: } // end of method 'addr_of_ext_svcount'{{$}}
define ptr @addr_of_ext_svcount() {
  ret ptr @ext_svcount
}

; The same for a P/Invoke import with a scalable vector parameter.
; IR: define ptr @addr_of_import_sve()
; IR-NEXT: ret ptr poison
; IL: .method {{.*}}'addr_of_import_sve'() cil managed{{$}}
; IL: } // end of method 'addr_of_import_sve'{{$}}
define ptr @addr_of_import_sve() {
  ret ptr @import_sve
}

; The address of the erased global variable is poison.
; IR: define ptr @addr_of_refs()
; IR-NEXT: ret ptr poison
; IL: .method {{.*}}'addr_of_refs'() cil managed{{$}}
; IL: } // end of method 'addr_of_refs'{{$}}
define ptr @addr_of_refs() {
  ret ptr @refs
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

; The declarations whose addresses are taken are reported after the
; definitions, once in each module.
; ERR: error: {{.*}}in function ext_svcount void (target("aarch64.svcount")): type 'target("aarch64.svcount")' is not supported for MSIL{{$}}
; ERR: error: {{.*}}in function import_sve void (<vscale x 4 x i32>): scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
declare <vscale x 4 x i32> @ext_sve(<vscale x 4 x i32>)
declare void @ext_svcount(target("aarch64.svcount"))
declare void @import_sve(<vscale x 4 x i32>) "msil-import-module"="libsve"

; The global variables are checked last.
; ERR: error: global variable @refs: type 'target("wasm.externref")' is not supported for MSIL{{$}}

; The data that stays: the address field of @svcount_fn, filled with zero by
; the module initializer.
; IL: .field public static initonly native int 'svcount_fn'
; IL: .method {{.*}} .cctor() cil managed{{$}}

declare i64 @llvm.vscale.i64()
declare i32 @llvm.experimental.get.vector.length.i64(i64, i32 immarg, i1 immarg)
declare i32 @llvm.experimental.get.vector.length.i32(i32, i32 immarg, i1 immarg)
