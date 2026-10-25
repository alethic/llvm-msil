; The library of each Phase-1 P/Invoke on every Phase-1 triple (Design.md 12,
; 'One hard-coded P/Invoke, puts -> DllImport("libc") on Linux'; Design.md
; 5.3; ImplementationPlan.md decisions D6 and D14, plan 5.1). An undefined
; function that a module calls and that is a Phase-1 import gets one
; declaration after the methods,
;   .method public hidebysig static pinvokeimpl("<lib>" nomangle cdecl) <ret> '<name>'(<params>) cil managed preservesig {}
; and <lib> depends only on the operating system and the environment of the
; triple, not on the architecture:
;   triple environment     puts (C library)    sqrt, sqrtf (C math library)
;   linux-gnu              "libc"              "libm.so.6" (glibc keeps libm
;                                              separate, and CoreCLR maps only
;                                              "libc" to the C library)
;   linux-musl             "libc"              "libc"
;   windows-msvc           "ucrtbase"          "ucrtbase"
;   windows-gnu            "msvcrt"            "msvcrt"
; Both Windows C runtime DLLs export sqrt and sqrtf, so the Windows import
; sets of the section 12 amendment of D14 ('Windows import sets follow the DLL
; exports') keep them; the excluded math functions are tested in
; pinvoke-errors.ll, and the other imports, the import attributes and the
; signatures in pinvoke.ll.
;
; sqrt is reached both by a direct call and through llvm.sqrt.f64, which the
; legalizer expands to the same libcall (there are no BCL intrinsics in Phase
; 1, D16), and llvm.sqrt.f32 becomes a call of sqrtf: the calls print as
; 'call <ret> '<name>'(<params>)', every import is declared exactly once, and
; nothing else is declared. The eight triples are x86_64 and aarch64 on Linux
; (gnu, musl) and Windows (msvc, gnu) (D6); x86_64-pc-win32-msil, which D3
; normalizes to x86_64-pc-windows-msil, is a Windows MSVC triple (LLVM's
; default environment on Windows) and also uses "ucrtbase". Every module
; compiles without a message and assembles (ilasm does not resolve the
; libraries); the aarch64 modules are assembled as ARM64 images.

; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=DisableRuntimeMarshalling
; DEFINE: %{count} = FileCheck %s --check-prefix=NPINV

; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.x64-gnu.il 2>&1 | count 0
; RUN: %{check} -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t.x64-gnu.il
; RUN: %{count} --input-file=%t.x64-gnu.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-musl-msil < %s -o %t.x64-musl.il 2>&1 | count 0
; RUN: %{check} -DCLIB=libc -DMLIB=libc --input-file=%t.x64-musl.il
; RUN: %{count} --input-file=%t.x64-musl.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.x64-msvc.il 2>&1 | count 0
; RUN: %{check} -DCLIB=ucrtbase -DMLIB=ucrtbase --input-file=%t.x64-msvc.il
; RUN: %{count} --input-file=%t.x64-msvc.il
; RUN: %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %s -o %t.x64-mingw.il 2>&1 | count 0
; RUN: %{check} -DCLIB=msvcrt -DMLIB=msvcrt --input-file=%t.x64-mingw.il
; RUN: %{count} --input-file=%t.x64-mingw.il
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil < %s -o %t.a64-gnu.il 2>&1 | count 0
; RUN: %{check} -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t.a64-gnu.il
; RUN: %{count} --input-file=%t.a64-gnu.il
; RUN: %{llc} -mtriple=aarch64-unknown-linux-musl-msil < %s -o %t.a64-musl.il 2>&1 | count 0
; RUN: %{check} -DCLIB=libc -DMLIB=libc --input-file=%t.a64-musl.il
; RUN: %{count} --input-file=%t.a64-musl.il
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil < %s -o %t.a64-msvc.il 2>&1 | count 0
; RUN: %{check} -DCLIB=ucrtbase -DMLIB=ucrtbase --input-file=%t.a64-msvc.il
; RUN: %{count} --input-file=%t.a64-msvc.il
; RUN: %{llc} -mtriple=aarch64-w64-windows-gnu-msil < %s -o %t.a64-mingw.il 2>&1 | count 0
; RUN: %{check} -DCLIB=msvcrt -DMLIB=msvcrt --input-file=%t.a64-mingw.il
; RUN: %{count} --input-file=%t.a64-mingw.il
; RUN: %{llc} -mtriple=x86_64-pc-win32-msil < %s -o %t.x64-win32.il 2>&1 | count 0
; RUN: %{check} -DCLIB=ucrtbase -DMLIB=ucrtbase --input-file=%t.x64-win32.il
; RUN: %{count} --input-file=%t.x64-win32.il

; RUN: %if ilasm %{ %ilasm-verify -output=%t.x64-gnu.dll %t.x64-gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.x64-musl.dll %t.x64-musl.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.x64-msvc.dll %t.x64-msvc.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.x64-mingw.dll %t.x64-mingw.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-gnu.dll %t.a64-gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-musl.dll %t.a64-musl.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-msvc.dll %t.a64-msvc.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-mingw.dll %t.a64-mingw.il %}

; Exactly three P/Invoke declarations.
; NPINV-NOT:     pinvokeimpl
; NPINV-COUNT-3: {{^}}.method public hidebysig static pinvokeimpl(
; NPINV-NOT:     pinvokeimpl

; CHECK:      {{^}}// MSIL module generated by LLVM{{$}}
; CHECK-NEXT: {{^}}// source_filename = "pinvoke-libs.c"{{$}}

source_filename = "pinvoke-libs.c"

declare i32 @puts(ptr)
declare double @sqrt(double)
declare double @llvm.sqrt.f64(double)
declare float @llvm.sqrt.f32(float)

; CHECK-LABEL: {{^}}.method public static int32 'say'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'say'{{$}}
define i32 @say(ptr %s) {
  %r = call i32 @puts(ptr %s)
  ret i32 %r
}

; CHECK-LABEL: {{^}}.method public static float64 'root'(float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float64 'sqrt'(float64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'root'{{$}}
define double @root(double %x) {
  %r = call double @sqrt(double %x)
  ret double %r
}

; CHECK-LABEL: {{^}}.method public static float64 'root_intrinsic'(float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float64 'sqrt'(float64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'root_intrinsic'{{$}}
define double @root_intrinsic(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

; CHECK-LABEL: {{^}}.method public static float32 'rootf_intrinsic'(float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float32 'sqrtf'(float32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'rootf_intrinsic'{{$}}
define float @rootf_intrinsic(float %x) {
  %r = call float @llvm.sqrt.f32(float %x)
  ret float %r
}

; The declarations follow the methods (their order is not pinned here), and
; nothing follows them: no data segment, no module initializer and no entry
; point in a module without globals and without main.
; CHECK-LABEL: {{^}}// ---- undefined external functions: P/Invoke ----{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[CLIB]]" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float32 'sqrtf'(float32) cil managed preservesig {}{{$}}
; CHECK-NOT:   {{.}}
