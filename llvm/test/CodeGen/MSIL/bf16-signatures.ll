; bfloat parameters and results (ImplementationPlan.md M24, plan 5.17.18 and
; fp-helpers F2 (a); D14 as M24 amends it; plan section 12, U2 row (2) for
; M24.f). Since M24 a scalar bfloat parameter or result is its 16-bit pattern
; in an int32, as a half is, so C's __bf16 (LLVM's bfloat) needs no
; conversion to cross a call:
;   __bf16 id(__bf16 x) { return x; }    // int32 'id_o0'(int32) at -O0
;   float to_f(__bf16 x) { return x; }   // float32 'to_f_o0'(int32)
;   __bf16 call_id(__bf16 x) { return id(x); }
; (main.ll has the IR that clang emits for these at -O0, where the parameter
; is spilled to the frame with stind.i2 and read back with ldind.u2, and the
; -O2 forms.) A pass-through, a call, a freeze, a load (ldind.u2) and a store
; (stind.i2) of a bfloat compile with no conversion and no runtime helper at
; -O2 and at -O0. Until M24 the parameter was a float32, so the -O0
; pass-through asked for the compiler-rt pair __truncsfbf2 and __extendbfsf2,
; which MSIL cannot call yet.
; - The extension to float32 is inline: the int32 shifted left by 16 is
;   stored to the frame slot and read back as a float32 (ldind.r4), then
;   conv.r8 for float64; __extendbfsf2 is never named.
; - The rounding conversions from float32 and float64 are still the
;   compiler-rt helpers __truncsfbf2 and __truncdfbf2 (helpers.ll): in
;   whole-program text each is Phase 1's "runtime helper '<name>' is not
;   available for MSIL yet (phase 3)", and in an object each is a call of an
;   UNDEFINED FUNCTION symbol, which the link has to resolve.
; - A vector of bfloat passes as the vector of half of the same length does:
;   each lane is its bits in an int32, a <1 x bfloat> is one int32, and a
;   longer vector result goes through the hidden result pointer. vec_ident
;   (<2 x bfloat>) is line for line its half twin vec_ident_h. vec.ll has
;   <1 x bfloat>, <2 x bfloat> and <4 x bfloat> parameters (last*), results
;   (make*), both (id*) and calls (call*), and its text and objects are those
;   of its half twin vech.ll byte for byte, with no float32, no helper and no
;   UNDEFINED symbol: a <1 x bfloat> names neither __truncsfbf2 nor
;   __extendbfsf2. (Before M24 the lanes were float32, and a <1 x bfloat>
;   called __truncsfbf2.)
; - A P/Invoke cannot pass a bfloat natively: in an object, every function
;   with a bfloat in its IR signature, defined or imported, has a
;   NATIVE_INCOMPATIBLE signature record, as a half has (the control 'plain'
;   has none); in whole-program text an import is M8.c's error, whose text
;   M24 leaves as it is (import.ll; pinvoke-errors.ll SIG and call-errors.ll
;   HALFIMP have more).
; Three triples (x86_64-unknown-linux-gnu-msil, x86_64-pc-windows-msvc-msil,
; aarch64-unknown-linux-gnu-msil), -O2 and -O0; the whole-program outputs of
; main.ll and vec.ll assemble.

; RUN: split-file %s %t
; DEFINE: %{check} = FileCheck %s --implicit-check-not="'__" --implicit-check-not=pinvokeimpl
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.lnx.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.win.o0.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.a64.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.a64.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.a64.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/main.a64.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.lnx.dll %t/main.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.win.o0.dll %t/main.win.o0.il %}

; Objects: the same signatures, and the signature records.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o0.o 2>&1 | count 0
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.a64.o 2>&1 | count 0
; DEFINE: %{cil} = FileCheck %s --check-prefix=CIL --implicit-check-not="'__" \
; DEFINE:   --implicit-check-not="[msil.extern]" --implicit-check-not=pinvokeimpl
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/main.lnx.o | FileCheck %s --check-prefix=LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/main.win.o0.o | FileCheck %s --check-prefix=LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/main.a64.o | FileCheck %s --check-prefix=LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t/main.lnx.o | %{cil} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t/main.win.o0.o | %{cil} %}

; The rounding conversions.
; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=HELPER
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=HELPER
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=HELPER
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/helpers.ll \
; RUN:   -o %t/helpers.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs < %t/helpers.ll \
; RUN:   -o %t/helpers.win.o0.o 2>&1 | count 0
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/helpers.lnx.o | FileCheck %s --check-prefix=HELPER-LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/helpers.win.o0.o | FileCheck %s --check-prefix=HELPER-LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t/helpers.lnx.o \
; RUN:   | FileCheck %s --check-prefix=HELPER-CIL --implicit-check-not=__extendbfsf2 %}

; Imports.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/import.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=IMPORT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/import.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=IMPORT
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/import.ll \
; RUN:   -o %t/import.win.o 2>&1 | count 0
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -filetype=obj -verify-machineinstrs < %t/import.ll \
; RUN:   -o %t/import.a64.o0.o 2>&1 | count 0
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/import.win.o | FileCheck %s --check-prefix=IMPORT-LINK %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/import.a64.o0.o | FileCheck %s --check-prefix=IMPORT-LINK %}

; Vectors: vec.ll (bfloat lanes) against its half twin vech.ll, which defines
; the same functions under the same names and module name.
; DEFINE: %{run} =
; DEFINE: %{tag} =
; DEFINE: %{vec} = llc %{run} -asm-verbose=false -verify-machineinstrs < %t/vec.ll -o %t/vec.%{tag}.il \
; DEFINE:   2> %t/vec.%{tag}.err && count 0 < %t/vec.%{tag}.err && \
; DEFINE:   llc %{run} -asm-verbose=false -verify-machineinstrs < %t/vech.ll -o %t/vech.%{tag}.il && \
; DEFINE:   diff %t/vec.%{tag}.il %t/vech.%{tag}.il && \
; DEFINE:   FileCheck %s --check-prefix=VEC --implicit-check-not="'__" --implicit-check-not=__truncsfbf2 \
; DEFINE:     --implicit-check-not=__extendbfsf2 --implicit-check-not=float32 --implicit-check-not=pinvokeimpl \
; DEFINE:     --input-file=%t/vec.%{tag}.il
; DEFINE: %{vecobj} = llc %{run} -filetype=obj -verify-machineinstrs < %t/vec.ll -o %t/vec.%{tag}.o \
; DEFINE:   2> %t/vec.%{tag}.o.err && count 0 < %t/vec.%{tag}.o.err && \
; DEFINE:   llc %{run} -filetype=obj -verify-machineinstrs < %t/vech.ll -o %t/vech.%{tag}.o && \
; DEFINE:   cmp %t/vec.%{tag}.o %t/vech.%{tag}.o
; REDEFINE: %{run} = -mtriple=x86_64-unknown-linux-gnu-msil
; REDEFINE: %{tag} = lnx
; RUN: %{vec}
; REDEFINE: %{run} = -mtriple=x86_64-unknown-linux-gnu-msil -O0
; REDEFINE: %{tag} = lnx.o0
; RUN: %{vec}
; RUN: %{vecobj}
; REDEFINE: %{run} = -mtriple=x86_64-pc-windows-msvc-msil
; REDEFINE: %{tag} = win
; RUN: %{vec}
; RUN: %{vecobj}
; REDEFINE: %{run} = -mtriple=x86_64-pc-windows-msvc-msil -O0
; REDEFINE: %{tag} = win.o0
; RUN: %{vec}
; REDEFINE: %{run} = -mtriple=aarch64-unknown-linux-gnu-msil
; REDEFINE: %{tag} = a64
; RUN: %{vec}
; RUN: %{vecobj}
; REDEFINE: %{run} = -mtriple=aarch64-unknown-linux-gnu-msil -O0
; REDEFINE: %{tag} = a64.o0
; RUN: %{vec}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vec.win.dll %t/vec.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vec.lnx.o0.dll %t/vec.lnx.o0.il %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/vec.win.o \
; RUN:   | FileCheck %s --check-prefix=VEC-LINK --implicit-check-not=UNDEFINED %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/vec.lnx.o0.o \
; RUN:   | FileCheck %s --check-prefix=VEC-LINK --implicit-check-not=UNDEFINED %}

;--- main.ll
source_filename = "bf16-signatures.c"

; CHECK-LABEL: {{^}}.method public static int32 'ident'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ident'{{$}}
define bfloat @ident(bfloat %x) {
  ret bfloat %x
}

; CHECK-LABEL: {{^}}.method public static int32 'ident_noundef'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ident_noundef'{{$}}
define noundef bfloat @ident_noundef(bfloat noundef returned %x) {
  ret bfloat %x
}

; CHECK-LABEL: {{^}}.method public static int32 'call_ident'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'ident'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_ident'{{$}}
define bfloat @call_ident(bfloat %x) {
  %r = call bfloat @ident(bfloat %x)
  ret bfloat %r
}

; CHECK-LABEL: {{^}}.method public static int32 'two'(int32, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'two'{{$}}
define bfloat @two(bfloat %a, i32 %i, bfloat %b) {
  ret bfloat %b
}

; A freeze of a bfloat that is not noundef names no helper either.
; CHECK-LABEL: {{^}}.method public static int32 'freeze_bf'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'freeze_bf'{{$}}
define bfloat @freeze_bf(bfloat %x) {
  %f = freeze bfloat %x
  ret bfloat %f
}

; CHECK-LABEL: {{^}}.method public static int32 'load_bf'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'load_bf'{{$}}
define bfloat @load_bf(ptr %p) {
  %v = load bfloat, ptr %p, align 2
  ret bfloat %v
}

; CHECK-LABEL: {{^}}.method public static void 'store_bf'(native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'store_bf'{{$}}
define void @store_bf(ptr %p, bfloat %v) {
  store bfloat %v, ptr %p, align 2
  ret void
}

; CHECK-LABEL: {{^}}.method public static float32 'to_f32'(int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK:       {{^}}  stind.i4{{$}}
; CHECK:       {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'to_f32'{{$}}
define float @to_f32(bfloat %x) {
  %r = fpext bfloat %x to float
  ret float %r
}

; CHECK-LABEL: {{^}}.method public static float64 'to_f64'(int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK:       {{^}}  stind.i4{{$}}
; CHECK:       {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'to_f64'{{$}}
define double @to_f64(bfloat %x) {
  %r = fpext bfloat %x to double
  ret double %r
}

; The IR of clang -O0 for the C functions above.
; CHECK-LABEL: {{^}}.method public static int32 'id_o0'(int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK:       {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'id_o0'{{$}}
define bfloat @id_o0(bfloat noundef %x) #0 {
entry:
  %x.addr = alloca bfloat, align 2
  store bfloat %x, ptr %x.addr, align 2
  %0 = load bfloat, ptr %x.addr, align 2
  ret bfloat %0
}

; CHECK-LABEL: {{^}}.method public static float32 'to_f_o0'(int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK:       {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK:       {{^}}  stind.i4{{$}}
; CHECK:       {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'to_f_o0'{{$}}
define float @to_f_o0(bfloat noundef %x) #0 {
entry:
  %x.addr = alloca bfloat, align 2
  store bfloat %x, ptr %x.addr, align 2
  %0 = load bfloat, ptr %x.addr, align 2
  %conv = fpext bfloat %0 to float
  ret float %conv
}

; CHECK-LABEL: {{^}}.method public static int32 'call_id_o0'(int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK:       {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  call int32 'id_o0'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_id_o0'{{$}}
define bfloat @call_id_o0(bfloat noundef %x) #0 {
entry:
  %x.addr = alloca bfloat, align 2
  store bfloat %x, ptr %x.addr, align 2
  %0 = load bfloat, ptr %x.addr, align 2
  %call = call bfloat @id_o0(bfloat noundef %0)
  ret bfloat %call
}

; A <2 x bfloat> is what its half twin vec_ident_h is: two int32 lanes, and
; the result through the hidden result pointer, stored lane by lane.
; CHECK-LABEL: {{^}}.method public static void 'vec_ident'(native int, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'vec_ident'{{$}}
define <2 x bfloat> @vec_ident(<2 x bfloat> %v) {
  ret <2 x bfloat> %v
}

; CHECK-LABEL: {{^}}.method public static void 'vec_ident_h'(native int, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'vec_ident_h'{{$}}
define <2 x half> @vec_ident_h(<2 x half> %v) {
  ret <2 x half> %v
}

; CHECK-LABEL: {{^}}.method public static int32 'plain'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'plain'{{$}}
; CHECK-NOT:   {{^}}.method
define i32 @plain(i32 %x) {
  ret i32 %x
}

attributes #0 = { noinline nounwind optnone }

; LINK:      {{^}}LinkInfo v1.0 triple={{.*}}
; LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT 'ident' method=0x06000001{{$}}
; LINK-NEXT: {{^}}Symbol 1 FUNCTION GLOBAL DEFAULT 'ident_noundef' method=0x06000002{{$}}
; LINK-NEXT: {{^}}Symbol 2 FUNCTION GLOBAL DEFAULT 'call_ident' method=0x06000003{{$}}
; LINK-NEXT: {{^}}Symbol 3 FUNCTION GLOBAL DEFAULT 'two' method=0x06000004{{$}}
; LINK-NEXT: {{^}}Symbol 4 FUNCTION GLOBAL DEFAULT 'freeze_bf' method=0x06000005{{$}}
; LINK-NEXT: {{^}}Symbol 5 FUNCTION GLOBAL DEFAULT 'load_bf' method=0x06000006{{$}}
; LINK-NEXT: {{^}}Symbol 6 FUNCTION GLOBAL DEFAULT 'store_bf' method=0x06000007{{$}}
; LINK-NEXT: {{^}}Symbol 7 FUNCTION GLOBAL DEFAULT 'to_f32' method=0x06000008{{$}}
; LINK-NEXT: {{^}}Symbol 8 FUNCTION GLOBAL DEFAULT 'to_f64' method=0x06000009{{$}}
; LINK-NEXT: {{^}}Symbol 9 FUNCTION GLOBAL DEFAULT 'id_o0' method=0x0600000A{{$}}
; LINK-NEXT: {{^}}Symbol 10 FUNCTION GLOBAL DEFAULT 'to_f_o0' method=0x0600000B{{$}}
; LINK-NEXT: {{^}}Symbol 11 FUNCTION GLOBAL DEFAULT 'call_id_o0' method=0x0600000C{{$}}
; LINK-NEXT: {{^}}Symbol 12 FUNCTION GLOBAL DEFAULT 'vec_ident' method=0x0600000D{{$}}
; LINK-NEXT: {{^}}Symbol 13 FUNCTION GLOBAL DEFAULT 'vec_ident_h' method=0x0600000E{{$}}
; LINK-NEXT: {{^}}Symbol 14 FUNCTION GLOBAL DEFAULT 'plain' method=0x0600000F{{$}}
; LINK-NEXT: {{^}}Signature 0x06000001 'ident' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x06000002 'ident_noundef' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x06000003 'call_ident' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x06000004 'two' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x06000005 'freeze_bf' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x06000006 'load_bf' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x06000007 'store_bf' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x06000008 'to_f32' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x06000009 'to_f64' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x0600000A 'id_o0' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x0600000B 'to_f_o0' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x0600000C 'call_id_o0' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x0600000D 'vec_ident' NATIVE_INCOMPATIBLE{{$}}
; LINK-NEXT: {{^}}Signature 0x0600000E 'vec_ident_h' NATIVE_INCOMPATIBLE{{$}}
; LINK-NOT:  {{.}}

; CIL: {{^}}  .method 'ident' int32(int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'ident_noundef' int32(int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'call_ident' int32(int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^ +}}L{{[0-9]+}}: call int32(int32) '<Module>'::'ident'{{$}}
; CIL: {{^}}  .method 'two' int32(int32, int32, int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'freeze_bf' int32(int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'load_bf' int32(native int) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'store_bf' void(native int, int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'to_f32' float32(int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'to_f64' float64(int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'id_o0' int32(int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'to_f_o0' float32(int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'call_id_o0' int32(int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^ +}}L{{[0-9]+}}: call int32(int32) '<Module>'::'id_o0'{{$}}
; CIL: {{^}}  .method 'vec_ident' void(native int, int32, int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'vec_ident_h' void(native int, int32, int32) flags=0x0016 impl=0x0000{{$}}
; CIL: {{^}}  .method 'plain' int32(int32) flags=0x0016 impl=0x0000{{$}}

;--- helpers.ll
source_filename = "bf16-helpers.c"

; HELPER: {{^}}error: {{.*}}in function from_f32 bfloat (float): runtime helper '__truncsfbf2' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function from_f64 bfloat (double): runtime helper '__truncdfbf2' is not available for MSIL yet (phase 3){{$}}

; HELPER-LINK:      {{^}}LinkInfo v1.0 triple={{.*}}
; HELPER-LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT 'from_f32' method=0x06000001{{$}}
; HELPER-LINK-NEXT: {{^}}Symbol 1 FUNCTION GLOBAL DEFAULT 'from_f64' method=0x06000002{{$}}
; HELPER-LINK-NEXT: {{^}}Symbol 2 FUNCTION GLOBAL DEFAULT UNDEFINED '__truncsfbf2'{{$}}
; HELPER-LINK-NEXT: {{^}}Symbol 3 FUNCTION GLOBAL DEFAULT UNDEFINED '__truncdfbf2'{{$}}
; HELPER-LINK-NEXT: {{^}}Signature 0x06000001 'from_f32' NATIVE_INCOMPATIBLE{{$}}
; HELPER-LINK-NEXT: {{^}}Signature 0x06000002 'from_f64' NATIVE_INCOMPATIBLE{{$}}
; HELPER-LINK-NOT:  {{.}}

; HELPER-CIL: {{^}}  .method 'from_f32' int32(float32) flags=0x0016 impl=0x0000{{$}}
; HELPER-CIL: {{^ +}}L{{[0-9]+}}: call int32(float32) [msil.extern]<msil>Symbols::'__truncsfbf2'{{$}}
; HELPER-CIL: {{^}}  .method 'from_f64' int32(float64) flags=0x0016 impl=0x0000{{$}}
; HELPER-CIL: {{^ +}}L{{[0-9]+}}: call int32(float64) [msil.extern]<msil>Symbols::'__truncdfbf2'{{$}}

define bfloat @from_f32(float %x) {
  %r = fptrunc float %x to bfloat
  ret bfloat %r
}

define bfloat @from_f64(double %x) {
  %r = fptrunc double %x to bfloat
  ret bfloat %r
}

;--- import.ll
source_filename = "bf16-import.c"

; IMPORT:      {{^}}error: imported function 'takes_bf16' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}
; IMPORT-NEXT: {{^}}error: imported function 'ret_bf16' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}

; IMPORT-LINK:      {{^}}LinkInfo v1.0 triple={{.*}}
; IMPORT-LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'takes_bf16'{{$}}
; IMPORT-LINK-NEXT: {{^}}Symbol 1 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT 'ret_bf16'{{$}}
; IMPORT-LINK-NEXT: {{^}}Symbol 2 FUNCTION GLOBAL DEFAULT 'caller' method=0x06000001{{$}}
; IMPORT-LINK-NEXT: {{^}}Signature 0x06000001 'caller' NATIVE_INCOMPATIBLE{{$}}
; IMPORT-LINK-NEXT: {{^}}Signature 0x06000002 'takes_bf16' NATIVE_INCOMPATIBLE{{$}}
; IMPORT-LINK-NEXT: {{^}}Signature 0x06000003 'ret_bf16' NATIVE_INCOMPATIBLE{{$}}
; IMPORT-LINK-NOT:  {{.}}

declare i32 @takes_bf16(bfloat) "msil-import-module"="mylib"
declare bfloat @ret_bf16(i32) "msil-import-module"="mylib"

define i32 @caller(bfloat %b) {
  %c = call i32 @takes_bf16(bfloat %b)
  %r = call bfloat @ret_bf16(i32 %c)
  %i = bitcast bfloat %r to i16
  %z = zext i16 %i to i32
  ret i32 %z
}

;--- vec.ll
; Vectors of 1, 2 and 4 bfloat lanes as parameters, results, both, and at a
; call. vech.ll is this file with half lanes.
source_filename = "bf16-vectors.c"

; VEC-LABEL: {{^}}.method public static int32 'id1'(int32) cil managed{{$}}
; VEC-NEXT:  {{^\{$}}
; VEC-NEXT:  {{^}}  .maxstack 1{{$}}
; VEC-NEXT:  {{^}}  ldarg.0{{$}}
; VEC-NEXT:  {{^}}  ret{{$}}
; VEC-NEXT:  {{^}}} // end of method 'id1'{{$}}
define <1 x bfloat> @id1(<1 x bfloat> %v) {
  ret <1 x bfloat> %v
}

; VEC-LABEL:   {{^}}.method public static void 'id2'(native int, int32, int32) cil managed{{$}}
; VEC-COUNT-2: {{^}}  stind.i2{{$}}
; VEC-NEXT:    {{^}}  ret{{$}}
; VEC-NEXT:    {{^}}} // end of method 'id2'{{$}}
define <2 x bfloat> @id2(<2 x bfloat> %v) {
  ret <2 x bfloat> %v
}

; VEC-LABEL:   {{^}}.method public static void 'id4'(native int, int32, int32, int32, int32) cil managed{{$}}
; VEC-COUNT-4: {{^}}  stind.i2{{$}}
; VEC-NEXT:    {{^}}  ret{{$}}
; VEC-NEXT:    {{^}}} // end of method 'id4'{{$}}
define <4 x bfloat> @id4(<4 x bfloat> %v) {
  ret <4 x bfloat> %v
}

; VEC-LABEL: {{^}}.method public static int32 'last1'(int32) cil managed{{$}}
; VEC-NEXT:  {{^\{$}}
; VEC-NEXT:  {{^}}  .maxstack 1{{$}}
; VEC-NEXT:  {{^}}  ldarg.0{{$}}
; VEC-NEXT:  {{^}}  ret{{$}}
; VEC-NEXT:  {{^}}} // end of method 'last1'{{$}}
define bfloat @last1(<1 x bfloat> %v) {
  %e = extractelement <1 x bfloat> %v, i32 0
  ret bfloat %e
}

; VEC-LABEL: {{^}}.method public static int32 'last2'(int32, int32) cil managed{{$}}
; VEC-NEXT:  {{^\{$}}
; VEC-NEXT:  {{^}}  .maxstack 1{{$}}
; VEC-NEXT:  {{^}}  ldarg.1{{$}}
; VEC-NEXT:  {{^}}  ret{{$}}
; VEC-NEXT:  {{^}}} // end of method 'last2'{{$}}
define bfloat @last2(<2 x bfloat> %v) {
  %e = extractelement <2 x bfloat> %v, i32 1
  ret bfloat %e
}

; VEC-LABEL: {{^}}.method public static int32 'last4'(int32, int32, int32, int32) cil managed{{$}}
; VEC-NEXT:  {{^\{$}}
; VEC-NEXT:  {{^}}  .maxstack 1{{$}}
; VEC-NEXT:  {{^}}  ldarg.3{{$}}
; VEC-NEXT:  {{^}}  ret{{$}}
; VEC-NEXT:  {{^}}} // end of method 'last4'{{$}}
define bfloat @last4(<4 x bfloat> %v) {
  %e = extractelement <4 x bfloat> %v, i32 3
  ret bfloat %e
}

; VEC-LABEL: {{^}}.method public static int32 'make1'(int32) cil managed{{$}}
; VEC-NEXT:  {{^\{$}}
; VEC-NEXT:  {{^}}  .maxstack 1{{$}}
; VEC-NEXT:  {{^}}  ldarg.0{{$}}
; VEC-NEXT:  {{^}}  ret{{$}}
; VEC-NEXT:  {{^}}} // end of method 'make1'{{$}}
define <1 x bfloat> @make1(bfloat %a) {
  %v = insertelement <1 x bfloat> poison, bfloat %a, i32 0
  ret <1 x bfloat> %v
}

; VEC-LABEL:   {{^}}.method public static void 'make2'(native int, int32, int32) cil managed{{$}}
; VEC-COUNT-2: {{^}}  stind.i2{{$}}
; VEC-NEXT:    {{^}}  ret{{$}}
; VEC-NEXT:    {{^}}} // end of method 'make2'{{$}}
define <2 x bfloat> @make2(bfloat %a, bfloat %b) {
  %v0 = insertelement <2 x bfloat> poison, bfloat %a, i32 0
  %v1 = insertelement <2 x bfloat> %v0, bfloat %b, i32 1
  ret <2 x bfloat> %v1
}

; VEC-LABEL:   {{^}}.method public static void 'make4'(native int, int32, int32, int32, int32) cil managed{{$}}
; VEC-COUNT-4: {{^}}  stind.i2{{$}}
; VEC-NEXT:    {{^}}  ret{{$}}
; VEC-NEXT:    {{^}}} // end of method 'make4'{{$}}
define <4 x bfloat> @make4(bfloat %a, bfloat %b, bfloat %c, bfloat %d) {
  %v0 = insertelement <4 x bfloat> poison, bfloat %a, i32 0
  %v1 = insertelement <4 x bfloat> %v0, bfloat %b, i32 1
  %v2 = insertelement <4 x bfloat> %v1, bfloat %c, i32 2
  %v3 = insertelement <4 x bfloat> %v2, bfloat %d, i32 3
  ret <4 x bfloat> %v3
}

; VEC-LABEL: {{^}}.method public static int32 'call1'(int32) cil managed{{$}}
; VEC-NEXT:  {{^\{$}}
; VEC-NEXT:  {{^}}  .maxstack 1{{$}}
; VEC-NEXT:  {{^}}  ldarg.0{{$}}
; VEC-NEXT:  {{^}}  call int32 'id1'(int32){{$}}
; VEC-NEXT:  {{^}}  ret{{$}}
; VEC-NEXT:  {{^}}} // end of method 'call1'{{$}}
define <1 x bfloat> @call1(<1 x bfloat> %v) {
  %r = call <1 x bfloat> @id1(<1 x bfloat> %v)
  ret <1 x bfloat> %r
}

; VEC-LABEL: {{^}}.method public static void 'call2'(native int, int32, int32) cil managed{{$}}
; VEC:       {{^}}  call void 'id2'(native int, int32, int32){{$}}
; VEC:       {{^}}} // end of method 'call2'{{$}}
define <2 x bfloat> @call2(<2 x bfloat> %v) {
  %r = call <2 x bfloat> @id2(<2 x bfloat> %v)
  ret <2 x bfloat> %r
}

; VEC-LABEL: {{^}}.method public static void 'call4'(native int, int32, int32, int32, int32) cil managed{{$}}
; VEC:       {{^}}  call void 'id4'(native int, int32, int32, int32, int32){{$}}
; VEC:       {{^}}} // end of method 'call4'{{$}}
; VEC-NOT:   {{^}}.method
define <4 x bfloat> @call4(<4 x bfloat> %v) {
  %r = call <4 x bfloat> @id4(<4 x bfloat> %v)
  ret <4 x bfloat> %r
}

; VEC-LINK:      {{^}}LinkInfo v1.0 triple={{.*}}
; VEC-LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT 'id1' method=0x06000001{{$}}
; VEC-LINK:      {{^}}Symbol 11 FUNCTION GLOBAL DEFAULT 'call4' method=0x0600000C{{$}}
; VEC-LINK-NEXT: {{^}}Signature 0x06000001 'id1' NATIVE_INCOMPATIBLE{{$}}

;--- vech.ll
; vec.ll with half lanes.
source_filename = "bf16-vectors.c"

define <1 x half> @id1(<1 x half> %v) {
  ret <1 x half> %v
}

define <2 x half> @id2(<2 x half> %v) {
  ret <2 x half> %v
}

define <4 x half> @id4(<4 x half> %v) {
  ret <4 x half> %v
}

define half @last1(<1 x half> %v) {
  %e = extractelement <1 x half> %v, i32 0
  ret half %e
}

define half @last2(<2 x half> %v) {
  %e = extractelement <2 x half> %v, i32 1
  ret half %e
}

define half @last4(<4 x half> %v) {
  %e = extractelement <4 x half> %v, i32 3
  ret half %e
}

define <1 x half> @make1(half %a) {
  %v = insertelement <1 x half> poison, half %a, i32 0
  ret <1 x half> %v
}

define <2 x half> @make2(half %a, half %b) {
  %v0 = insertelement <2 x half> poison, half %a, i32 0
  %v1 = insertelement <2 x half> %v0, half %b, i32 1
  ret <2 x half> %v1
}

define <4 x half> @make4(half %a, half %b, half %c, half %d) {
  %v0 = insertelement <4 x half> poison, half %a, i32 0
  %v1 = insertelement <4 x half> %v0, half %b, i32 1
  %v2 = insertelement <4 x half> %v1, half %c, i32 2
  %v3 = insertelement <4 x half> %v2, half %d, i32 3
  ret <4 x half> %v3
}

define <1 x half> @call1(<1 x half> %v) {
  %r = call <1 x half> @id1(<1 x half> %v)
  ret <1 x half> %r
}

define <2 x half> @call2(<2 x half> %v) {
  %r = call <2 x half> @id2(<2 x half> %v)
  ret <2 x half> %r
}

define <4 x half> @call4(<4 x half> %v) {
  %r = call <4 x half> @id4(<4 x half> %v)
  ret <4 x half> %r
}
