; A global constructor that takes (argc, argv, envp) in whole-program text
; (ImplementationPlan.md section 12, 2026-10-26, 'M22.b extended (Rust-found
; defects 2 and 4)' item (4), '5.9.10 amended (constructor arguments)' and
; 'M22.b extended (defect 4) clarified'; 2026-10-27, its 'as built' row and
; 'M23.c extended (M22.b edge cases, llc tests)').
;
; glibc and musl call every '.init_array' entry as void (int, char **,
; char **), so on every MSIL triple whose OS is not Windows a constructor
; may take the IL parameters (int32), (int32, native int) or (int32, native
; int, native int). llc writes the trailer of 5.9.10 for this module, whose
; llvm.global_ctors entry init3 takes all three and whose main takes (int,
; char **):
; - '<Module>::.cctor' builds argc and argv once (from
;   Environment.GetCommandLineArgs, argv[argc] null), stores them in the
;   trailer fields '<msil>argc' and '<msil>argv', and calls init3 with argc,
;   argv and the value of the C library's environ;
; - environ is the data-import field '<msil>environ', which
;   '<msil>init_dataimports' fills, before the constructors run, through
;   dlopen and dlsym (P/Invokes) from the C library of the triple: in
;   standalone llc text 'libc.so.6' on linux-gnu (x86_64 and aarch64) and
;   'libc.so' on linux-musl;
; - '<msil>Main' passes main the same argc and argv instead of building a
;   second argv.
; The three texts are the same except for the triple comment and the C
; library's name, and each assembles (the aarch64 one as an ARM64 image).
;
; Windows triples (msvc and MinGW alike): the CRT calls '.CRT$XC*' entries
; without arguments, so the same module is the error 'global constructor
; 'init3' takes arguments, which MSIL does not pass', in text and in object
; output, with exit status 1 and no crash.

; DEFINE: %{llc} = llc -verify-machineinstrs
; DEFINE: %{triple} = sed -e '/^\/\/ target triple   = /d'

; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.x64-gnu.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=CHECK,GNU --input-file=%t.x64-gnu.il
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil < %s -o %t.a64-gnu.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=CHECK,GNU --input-file=%t.a64-gnu.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-musl-msil < %s -o %t.x64-musl.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=CHECK,MUSL --input-file=%t.x64-musl.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.x64-gnu.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=CHECK,GNU --input-file=%t.x64-gnu.o0.il

; Apart from the triple comment, aarch64 linux-gnu is x86_64 linux-gnu.
; RUN: %{triple} %t.x64-gnu.il > %t.x64-gnu.notriple
; RUN: %{triple} %t.a64-gnu.il > %t.a64-gnu.notriple
; RUN: diff %t.x64-gnu.notriple %t.a64-gnu.notriple

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.msvc.il 2>&1 | %{diag}
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj < %s -o %t.msvc.o 2>&1 | %{diag}
; RUN: not %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %s -o %t.mingw.il 2>&1 | %{diag}
; RUN: not %{llc} -mtriple=x86_64-w64-windows-gnu-msil -filetype=obj < %s -o %t.mingw.o 2>&1 | %{diag}
; ERR: {{^}}error: global constructor 'init3' takes arguments, which MSIL does not pass{{$}}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.x64-gnu.dll %t.x64-gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.x64-musl.dll %t.x64-musl.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.x64-gnu.o0.dll %t.x64-gnu.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-gnu.dll %t.a64-gnu.il %}

; CHECK-LABEL: {{^}}.method assembly static void 'init3'(int32, native int, native int) cil managed{{$}}
; CHECK-LABEL: {{^}}.method public static int32 'main'(int32, native int) cil managed{{$}}

; The helper's P/Invokes come from the triple's C library.
; GNU:  {{^}}.method public hidebysig static pinvokeimpl("libc.so.6" nomangle cdecl) native int 'dlopen'(native int, int32) cil managed preservesig {}{{$}}
; GNU:  {{^}}.method public hidebysig static pinvokeimpl("libc.so.6" nomangle cdecl) native int 'dlsym'(native int, native int) cil managed preservesig {}{{$}}
; MUSL: {{^}}.method public hidebysig static pinvokeimpl("libc.so" nomangle cdecl) native int 'dlopen'(native int, int32) cil managed preservesig {}{{$}}
; MUSL: {{^}}.method public hidebysig static pinvokeimpl("libc.so" nomangle cdecl) native int 'dlsym'(native int, native int) cil managed preservesig {}{{$}}

; The data-import field of environ, then the two trailer fields.
; CHECK:      {{^}}.field public static native int '<msil>environ'{{$}}
; CHECK:      {{^}}.field private static initonly native int '<msil>argc'{{$}}
; CHECK-NEXT: {{^}}.field private static initonly native int '<msil>argv'{{$}}

; CHECK-LABEL: {{^}}.method private hidebysig static native int '<msil>dataimport'(string, string) cil managed{{$}}
; CHECK:       {{^}}  call native int 'dlopen'(native int, int32){{$}}
; CHECK:       {{^}}  call native int 'dlsym'(native int, native int){{$}}

; environ is looked up in the triple's C library.
; CHECK-LABEL: {{^}}.method private hidebysig static void '<msil>init_dataimports'() cil managed{{$}}
; GNU-NEXT:    {{^\{$}}
; MUSL-NEXT:   {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; GNU-NEXT:    {{^}}  ldstr "libc.so.6"{{$}}
; MUSL-NEXT:   {{^}}  ldstr "libc.so"{{$}}
; CHECK-NEXT:  {{^}}  ldstr "environ"{{$}}
; CHECK-NEXT:  {{^}}  call native int '<msil>dataimport'(string, string){{$}}
; CHECK:       {{^}}  stsfld native int '<msil>environ'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}

; The module initializer fills the import before it builds argv, keeps
; argc and argv, and calls init3 with argc, argv and *environ.
; CHECK-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK:       {{^}}  call void '<msil>init_dataimports'(){{$}}
; CHECK-NEXT:  {{^}}  call string[] [mscorlib]System.Environment::GetCommandLineArgs(){{$}}
; CHECK:       {{^}}  stsfld native int '<msil>argc'{{$}}
; CHECK-NEXT:  {{^}}  ldloc.2{{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>argv'{{$}}
; CHECK-NEXT:  {{^}}  ldloc.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc.2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>environ'{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  call void 'init3'(int32, native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '.cctor'{{$}}

; main gets the same argc and argv.
; CHECK-LABEL: {{^}}.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .entrypoint{{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>argc'{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>argv'{{$}}
; CHECK-NEXT:  {{^}}  call int32 'main'(int32, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}

@seen = internal global i32 0, align 4
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init3, ptr null }]

; Records argc plus 1 when envp is not null.
define internal void @init3(i32 %argc, ptr %argv, ptr %envp) {
  %e = icmp ne ptr %envp, null
  %v = zext i1 %e to i32
  %s = add i32 %argc, %v
  store i32 %s, ptr @seen, align 4
  ret void
}

define i32 @main(i32 %argc, ptr %argv) {
  %s = load i32, ptr @seen, align 4
  %r = add i32 %s, %argc
  ret i32 %r
}
