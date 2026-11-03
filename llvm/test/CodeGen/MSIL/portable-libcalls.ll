; Library calls on msil64-unknown-dotnet (ImplementationPlan.md 5.18.1 PT7 and
; 5.18.2 PB1, milestone P-1a). RuntimeLibcalls.td's MSILSystemLibrary gives
; the portable triples their library-call names; without it the arches would
; have none at all. This is the test that the fork line of RuntimeLibcalls.td's
; hook block names.
; - memcpy is never a library call (D15): a constant length is cpblk, a
;   variable one [mscorlib]System.Buffer::MemoryCopy, and no 'memcpy' name
;   appears.
; - ldexp (double) and ldexpf (float) are library calls with their own names
;   and signatures: no promotion of the float form to ldexp on double (that
;   is the windows-gnu rule only; no conv.r8/conv.r4 around the call).
; - In whole-program text a library call binds to a function of that name
;   defined in the module (defined.ll); an undefined one is T-P3, as an
;   undefined function of the module is, since standalone text imports
;   nothing on the portable target (undef.ll; never a P/Invoke, and never the
;   host triples' 'is not a Phase-1 import').
; - As object-form text and in objects each library call is a symbol of
;   [msil.extern]'<msil>Symbols' (undef.ll again): 'ldexp' float64(float64, int32)
;   (signature 00 02 0D 0D 08) and 'ldexpf' float32(float32, int32)
;   (00 02 0C 0C 08).
; - No *ti* helper is offered (PT7): RuntimeLibcalls.td excludes the i128
;   library calls for the MSIL arches, so ExpandIRInsts expands sitofp/uitofp
;   i128 and sdiv/udiv i128 inline and no __floattidf, __floatuntisf,
;   __divti3 or __udivti3 call (or symbol) appears (i128.ll). With no i128
;   library call there is nothing for passInt128ByPointer (PB1; x86_64
;   Windows only) to apply to: the only frame in i128.ll is the 16-byte
;   temporary of the final i64-to-double (i32-to-float) bitcast (D16, as in
;   bitcast.ll); the divisions have none, and an i128 argument is its two
;   halves, (native int, native int).
; At -O0 and -O2; the texts without errors assemble.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{t} = -mtriple=msil64-unknown-dotnet -asm-verbose=false -verify-machineinstrs
; DEFINE: %{nolib} = --implicit-check-not=pinvokeimpl --implicit-check-not="'memcpy'" \
; DEFINE:   --implicit-check-not=conv.r8 --implicit-check-not=conv.r4

; defined.ll
; RUN: llc %{t} < %t/defined.ll -o %t/defined.o2.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=DEF,MEM %{nolib} --input-file=%t/defined.o2.il
; RUN: llc %{t} -O0 < %t/defined.ll -o %t/defined.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=DEF,MEM %{nolib} --input-file=%t/defined.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/defined.o2.dll %t/defined.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/defined.o0.dll %t/defined.o0.il %}

; undef.ll
; T-P3's whole text (5.18.6 PL5), never the host triples' 'is not a Phase-1
; import'.
; DEFINE: %{undef} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="Phase-1 import"
; RUN: not llc %{t} < %t/undef.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNDEF %{undef}
; RUN: not llc %{t} -O0 < %t/undef.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNDEF %{undef}
; UNDEF-DAG: {{^}}{{.*}}error: {{.*}}function 'ldexp' is not defined in this module: standalone text for msil64-unknown-dotnet imports nothing (link with msil-ld){{$}}
; UNDEF-DAG: {{^}}{{.*}}error: {{.*}}function 'ldexpf' is not defined in this module: standalone text for msil64-unknown-dotnet imports nothing (link with msil-ld){{$}}

; undef.ll as object-form text and objects
; RUN: llc %{t} -msil-object-form < %t/undef.ll -o %t/of.o2.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=OF %{nolib} --input-file=%t/of.o2.il
; RUN: llc %{t} -O0 -msil-object-form < %t/undef.ll -o %t/of.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=OF %{nolib} --input-file=%t/of.o0.il
; RUN: llc -mtriple=msil64-unknown-dotnet -filetype=obj -verify-machineinstrs < %t/undef.ll -o %t/of.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=msil64-unknown-dotnet -O0 -filetype=obj -verify-machineinstrs < %t/undef.ll -o %t/of.o0.o 2>&1 \
; RUN:   | count 0
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --tables %t/of.o | FileCheck %s --check-prefix=OBJ \
; RUN:   --implicit-check-not="Name='memcpy'" --implicit-check-not="Table: ImplMap " %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --tables %t/of.o0.o | FileCheck %s --check-prefix=OBJ \
; RUN:   --implicit-check-not="Name='memcpy'" --implicit-check-not="Table: ImplMap " %}
; OBJ:     {{^}}Table: MemberRef rows={{[0-9]+}} rowsize={{[0-9]+}}{{$}}
; OBJ-DAG: {{^}}  MemberRef[{{[0-9]+}}]: Class=0x{{[0-9A-F]+}} Name='ldexp' Signature=00 02 0D 0D 08{{$}}
; OBJ-DAG: {{^}}  MemberRef[{{[0-9]+}}]: Class=0x{{[0-9A-F]+}} Name='ldexpf' Signature=00 02 0C 0C 08{{$}}

; i128.ll
; DEFINE: %{noti} = --implicit-check-not=__floattidf --implicit-check-not=__floatuntisf \
; DEFINE:   --implicit-check-not=__divti3 --implicit-check-not=__udivti3
; DEFINE: %{i128chk} = FileCheck %s --check-prefix=I128 %{noti} --implicit-check-not=localloc \
; DEFINE:   --implicit-check-not="{{^ *}}call " --implicit-check-not=pinvokeimpl
; RUN: llc %{t} -msil-object-form < %t/i128.ll -o %t/i128.o2.il 2>&1 | count 0
; RUN: %{i128chk} --input-file=%t/i128.o2.il
; RUN: llc %{t} -O0 -msil-object-form < %t/i128.ll -o %t/i128.o0.il 2>&1 | count 0
; RUN: %{i128chk} --input-file=%t/i128.o0.il
; RUN: llc -mtriple=msil64-unknown-dotnet -filetype=obj -verify-machineinstrs < %t/i128.ll -o %t/i128.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=msil64-unknown-dotnet -O0 -filetype=obj -verify-machineinstrs < %t/i128.ll -o %t/i128.o0.o 2>&1 \
; RUN:   | count 0
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --tables %t/i128.o | FileCheck %s --check-prefix=I128OBJ %{noti} \
; RUN:   --implicit-check-not="Table: ImplMap " %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --tables %t/i128.o0.o | FileCheck %s --check-prefix=I128OBJ %{noti} \
; RUN:   --implicit-check-not="Table: ImplMap " %}
; I128OBJ:      {{^}}Table: MethodDef rows=4 rowsize={{[0-9]+}}{{$}}
; I128OBJ-NEXT: {{^}}  MethodDef[1]: {{.*}} Name='sitofp128' Signature=00 02 0D 18 18 {{.*}}
; I128OBJ-NEXT: {{^}}  MethodDef[2]: {{.*}} Name='uitofp128' Signature=00 02 0C 18 18 {{.*}}
; I128OBJ-NEXT: {{^}}  MethodDef[3]: {{.*}} Name='sdiv128' Signature=00 05 01 18 18 18 18 18 {{.*}}
; I128OBJ-NEXT: {{^}}  MethodDef[4]: {{.*}} Name='udiv128' Signature=00 05 01 18 18 18 18 18 {{.*}}

;--- defined.ll
source_filename = "portable-libcalls.c"

define double @ldexp(double %x, i32 %e) {
  ret double %x
}

define float @ldexpf(float %x, i32 %e) {
  ret float %x
}

; DEF-LABEL: {{^}}.method public static float64 'ldexp_f64'(float64, int32) cil managed{{$}}
; DEF-NEXT:  {{^\{$}}
; DEF-NEXT:  {{^}}  .maxstack 2{{$}}
; DEF-NEXT:  {{^}}  ldarg.0{{$}}
; DEF-NEXT:  {{^}}  ldarg.1{{$}}
; DEF-NEXT:  {{^}}  call float64 'ldexp'(float64, int32){{$}}
; DEF-NEXT:  {{^}}  ret{{$}}
; DEF-NEXT:  {{^}}} // end of method 'ldexp_f64'{{$}}
define double @ldexp_f64(double %x, i32 %e) {
  %r = call double @llvm.ldexp.f64.i32(double %x, i32 %e)
  ret double %r
}

; DEF-LABEL: {{^}}.method public static float32 'ldexp_f32'(float32, int32) cil managed{{$}}
; DEF-NEXT:  {{^\{$}}
; DEF-NEXT:  {{^}}  .maxstack 2{{$}}
; DEF-NEXT:  {{^}}  ldarg.0{{$}}
; DEF-NEXT:  {{^}}  ldarg.1{{$}}
; DEF-NEXT:  {{^}}  call float32 'ldexpf'(float32, int32){{$}}
; DEF-NEXT:  {{^}}  ret{{$}}
; DEF-NEXT:  {{^}}} // end of method 'ldexp_f32'{{$}}
define float @ldexp_f32(float %x, i32 %e) {
  %r = call float @llvm.ldexp.f32.i32(float %x, i32 %e)
  ret float %r
}

; MEM-LABEL: {{^}}.method public static void 'copy_64'(native int, native int) cil managed{{$}}
; MEM-NEXT:  {{^\{$}}
; MEM-NEXT:  {{^}}  .maxstack 3{{$}}
; MEM-NEXT:  {{^}}  ldarg.0{{$}}
; MEM-NEXT:  {{^}}  ldarg.1{{$}}
; MEM-NEXT:  {{^}}  ldc.i4.s 64{{$}}
; MEM-NEXT:  {{^}}  cpblk{{$}}
; MEM-NEXT:  {{^}}  ret{{$}}
; MEM-NEXT:  {{^}}} // end of method 'copy_64'{{$}}
define void @copy_64(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 64, i1 false)
  ret void
}

; MEM-LABEL: {{^}}.method public static void 'copy_var'(native int, native int, native int) cil managed{{$}}
; MEM-NEXT:  {{^\{$}}
; MEM-NEXT:  {{^}}  .maxstack 4{{$}}
; MEM-NEXT:  {{^}}  ldarg.1{{$}}
; MEM-NEXT:  {{^}}  ldarg.0{{$}}
; MEM-NEXT:  {{^}}  ldarg.2{{$}}
; MEM-NEXT:  {{^}}  conv.i8{{$}}
; MEM-NEXT:  {{^}}  dup{{$}}
; MEM-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; MEM-NEXT:  {{^}}  ret{{$}}
; MEM-NEXT:  {{^}}} // end of method 'copy_var'{{$}}
define void @copy_var(ptr %d, ptr %s, i64 %n) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 %n, i1 false)
  ret void
}

declare double @llvm.ldexp.f64.i32(double, i32)
declare float @llvm.ldexp.f32.i32(float, i32)
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

;--- undef.ll
source_filename = "portable-libcalls-of.c"

; OF-LABEL: {{^}}.method public static float64 'ldexp_f64'(float64, int32) cil managed{{$}}
; OF-NEXT:  {{^\{$}}
; OF-NEXT:  {{^}}  .maxstack 2{{$}}
; OF-NEXT:  {{^}}  ldarg.0{{$}}
; OF-NEXT:  {{^}}  ldarg.1{{$}}
; OF-NEXT:  {{^}}  call float64 [msil.extern]'<msil>Symbols'::'ldexp'(float64, int32){{$}}
; OF-NEXT:  {{^}}  ret{{$}}
; OF-NEXT:  {{^}}} // end of method 'ldexp_f64'{{$}}
define double @ldexp_f64(double %x, i32 %e) {
  %r = call double @llvm.ldexp.f64.i32(double %x, i32 %e)
  ret double %r
}

; OF-LABEL: {{^}}.method public static float32 'ldexp_f32'(float32, int32) cil managed{{$}}
; OF-NEXT:  {{^\{$}}
; OF-NEXT:  {{^}}  .maxstack 2{{$}}
; OF-NEXT:  {{^}}  ldarg.0{{$}}
; OF-NEXT:  {{^}}  ldarg.1{{$}}
; OF-NEXT:  {{^}}  call float32 [msil.extern]'<msil>Symbols'::'ldexpf'(float32, int32){{$}}
; OF-NEXT:  {{^}}  ret{{$}}
; OF-NEXT:  {{^}}} // end of method 'ldexp_f32'{{$}}
define float @ldexp_f32(float %x, i32 %e) {
  %r = call float @llvm.ldexp.f32.i32(float %x, i32 %e)
  ret float %r
}

; In object form the BCL is [msil.bcl], which msil-ld binds to mscorlib.
; OF-LABEL: {{^}}.method public static void 'copy_64'(native int, native int) cil managed{{$}}
; OF:       {{^}}  cpblk{{$}}
; OF-NEXT:  {{^}}  ret{{$}}
; OF-LABEL: {{^}}.method public static void 'copy_var'(native int, native int, native int) cil managed{{$}}
; OF:       {{^}}  call void [msil.bcl]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; OF-NEXT:  {{^}}  ret{{$}}
define void @copy_64(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 64, i1 false)
  ret void
}

define void @copy_var(ptr %d, ptr %s, i64 %n) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 %n, i1 false)
  ret void
}

declare double @llvm.ldexp.f64.i32(double, i32)
declare float @llvm.ldexp.f32.i32(float, i32)
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

;--- i128.ll
source_filename = "portable-i128.c"

; The inline expansion; its one frame is the bitcast temporary at the end.
; I128-LABEL: {{^}}.method public static float64 'sitofp128'(native int, native int) cil managed{{$}}
; I128-NEXT:  {{^\{$}}
; I128-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; I128-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; I128-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; I128-NEXT:  {{^}}  localloc{{$}}
; I128-NEXT:  {{^}}  stloc.0{{$}}
; I128:       {{^}}  stind.i{{$}}
; I128-NEXT:  {{^}}  ldloc{{.*}}
; I128:       {{^}}  ldind.r8{{$}}
; I128:       {{^}}} // end of method 'sitofp128'{{$}}
define double @sitofp128(i128 %x) {
  %r = sitofp i128 %x to double
  ret double %r
}

; I128-LABEL: {{^}}.method public static float32 'uitofp128'(native int, native int) cil managed{{$}}
; I128-NEXT:  {{^\{$}}
; I128-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; I128-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; I128-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; I128-NEXT:  {{^}}  localloc{{$}}
; I128-NEXT:  {{^}}  stloc.0{{$}}
; I128:       {{^}}  stind.i4{{$}}
; I128-NEXT:  {{^}}  ldloc{{.*}}
; I128:       {{^}}  ldind.r4{{$}}
; I128:       {{^}}} // end of method 'uitofp128'{{$}}
define float @uitofp128(i128 %x) {
  %r = uitofp i128 %x to float
  ret float %r
}

; The divisions need no frame at all (the i128 result is returned through
; the caller's buffer, the first parameter).
; I128-LABEL: {{^}}.method public static void 'sdiv128'(native int, native int, native int, native int, native int) cil managed{{$}}
; I128:       {{^}}} // end of method 'sdiv128'{{$}}
define i128 @sdiv128(i128 %a, i128 %b) {
  %r = sdiv i128 %a, %b
  ret i128 %r
}

; I128-LABEL: {{^}}.method public static void 'udiv128'(native int, native int, native int, native int, native int) cil managed{{$}}
; I128:       {{^}}} // end of method 'udiv128'{{$}}
define i128 @udiv128(i128 %a, i128 %b) {
  %r = udiv i128 %a, %b
  ret i128 %r
}
