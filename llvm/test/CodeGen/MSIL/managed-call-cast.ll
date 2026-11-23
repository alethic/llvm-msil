; Cast stubs (ImplementationPlan.md 5.21.9 RM8, 5.21.5 RG5, 5.20 as amended
; by section 12's row 'MC-1's contract' of 2026-11-21: MM1 and MK2 'at most
; one member access; a cast stub makes none'; milestone R-a, RV25). A C cast
; from one .NET class to another calls a declaration that clang's CodeGen
; makes, '__msil_mc.<castclass>.__uniq.<n>', with "msil-managed-type" the
; class's 'cli_type' name, "msil-managed-member" '.castclass',
; "msil-managed-sig" 'object(object)', "msil-managed-conv" 'ref;ref' and an
; MR4 entry; slot P0 recognises the shape (cli::isManagedCastShape) and the
; AsmPrinter writes the body 'ldarg.0; castclass <TYPE>; ret', .maxstack 1,
; with no member access and no GCHandle:
; - a class (System.IO.Stream), an interface (System.IDisposable) and a
;   nested class (Ns.Outer/Inner): in whole-program text the kind-checking
;   TypeSpec 'class [mscorlib]T' (5.20.6 MW2 as amended), in object-form
;   text and objects the TypeRef '[msil.bcl]T';
; - System.String: its TypeRef in every form;
; - arrays: the SZARRAY TypeSpec ('string[]', 'class [<scope>]Ns.Widget[]').
; Each class has its own stub. The calls stay calls: a cast whose result is
; unused still runs (the declaration has no memory attributes, so nothing
; deletes the call; 'check_string' pops the result). Whole-program text
; (WP) and object-form text (OF) on both host x86_64 triples,
; msil64-unknown-dotnet and both aarch64 triples at -O0 and -O2; the
; whole-program text assembles; the objects (on
; x86_64-unknown-linux-gnu-msil, msil64-unknown-dotnet and
; aarch64-pc-windows-msvc-msil) through llvm-cil-dump (the castclass
; tokens, the stubs as LOCAL symbols, and no MemberRef named '.castclass' or
; '<castclass>': a stub is a method of the module) and the object oracle
; (5.12.6).
; Refusals, slot P0's invalid-attributes error pinned whole and the symbol
; printed as '<castclass>', in text and objects: a declaration with
; '.castclass' that is not the cast shape keeps the refused MEMBER ('member:
; '.castclass' is not a valid member name'): TYPE System.Object (a cast to
; Object is free and makes no stub; object.ll), a value type (vt.ll),
; another SIGNATURE (sig.ll) and other conversions (conv.ll); a cast stub
; with "msil-managed-decl" (decl.ll); and the address of a cast stub is
; MC-1's T-M10 (addr.ll, also on aarch64-unknown-linux-gnu-msil).

; RUN: rm -rf %t && split-file %s %t

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/casts.ll \
; DEFINE:   -o %t/wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %t/casts.ll --check-prefixes=CHECK,WP -DCORE=mscorlib --input-file=%t/wp.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=GCHandle --implicit-check-not=callvirt && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %t/casts.ll \
; DEFINE:   -o %t/of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %t/casts.ll --check-prefixes=CHECK,OF -DCORE=msil.bcl --input-file=%t/of.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=GCHandle --implicit-check-not=callvirt
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{il}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{il}
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; RUN: %{il}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/wp.win.dll %t/wp.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/wp.p64.dll %t/wp.msil64-unknown-dotnet-O2.il %}

; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/casts.ll \
; DEFINE:   -o %t/%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t/%{t}%{o}.o \
; DEFINE:   | FileCheck %t/casts.ll --check-prefix=DUMP --implicit-check-not="::'.castclass'" \
; DEFINE:     --implicit-check-not="::'<castclass>'" && \
; DEFINE:   llvm-cil-dump --link-info %t/%{t}%{o}.o | FileCheck %t/casts.ll --check-prefix=LINK
; DEFINE: %{oracle} = %ilasm-verify -output=%t/of.%{t}%{o}.ilasm.o %t/of.%{t}%{o}.il && \
; DEFINE:   %{canon} %t/%{t}%{o}.o | sed -e "/^pe /d" > %t/%{t}%{o}.canon1 && \
; DEFINE:   %{canon} %t/of.%{t}%{o}.ilasm.o | sed -e "/^pe /d" -e "/^assemblyref 'mscorlib' /d" \
; DEFINE:     -e "s/\[mscorlib\]System\.String::/[msil.bcl]System.String::/g" \
; DEFINE:     -e "s/\[mscorlib\]System\.String$/[msil.bcl]System.String/" > %t/%{t}%{o}.canon2 && \
; DEFINE:   diff %t/%{t}%{o}.canon1 %t/%{t}%{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t/%{t}%{o}.o > %t/%{t}%{o}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t/of.%{t}%{o}.ilasm.o > %t/%{t}%{o}.link2 && \
; DEFINE:   diff %t/%{t}%{o}.link1 %t/%{t}%{o}.link2
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}

; DEFINE: %{f} =
; DEFINE: %{err} = not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/%{f}.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %t/%{f}.ll --implicit-check-not=error: --implicit-check-not="LLVM ERROR" \
; DEFINE:     --implicit-check-not="PLEASE submit" && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj < %t/%{f}.ll -o %t/%{f}.o 2>&1 \
; DEFINE:   | FileCheck %t/%{f}.ll --implicit-check-not=error: --implicit-check-not="LLVM ERROR" \
; DEFINE:     --implicit-check-not="PLEASE submit"
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{f} = object
; RUN: %{err}
; REDEFINE: %{f} = vt
; RUN: %{err}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; REDEFINE: %{f} = sig
; RUN: %{err}
; REDEFINE: %{f} = conv
; RUN: %{err}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{f} = decl
; RUN: %{err}
; REDEFINE: %{f} = addr
; RUN: %{err}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{err}

;--- casts.ll
source_filename = "managed-call-cast.c"

; CHECK-LABEL: .method assembly static object '__msil_mc.<castclass>.__uniq.101'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.IO.Stream{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]System.IO.Stream{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.<castclass>.__uniq.101'{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.101"(target("msil.ref")) #0

; CHECK-LABEL: .method assembly static object '__msil_mc.<castclass>.__uniq.102'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  castclass {{\[}}[[CORE]]]System.String{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.<castclass>.__uniq.102'{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.102"(target("msil.ref")) #1

; CHECK-LABEL: .method assembly static object '__msil_mc.<castclass>.__uniq.103'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  castclass string[]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.<castclass>.__uniq.103'{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.103"(target("msil.ref")) #2

; CHECK-LABEL: .method assembly static object '__msil_mc.<castclass>.__uniq.104'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  castclass class {{\[}}[[CORE]]]Ns.Widget[]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.<castclass>.__uniq.104'{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.104"(target("msil.ref")) #3

; CHECK-LABEL: .method assembly static object '__msil_mc.<castclass>.__uniq.105'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]Ns.Outer/Inner{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]Ns.Outer/Inner{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.<castclass>.__uniq.105'{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.105"(target("msil.ref")) #4

; CHECK-LABEL: .method assembly static object '__msil_mc.<castclass>.__uniq.106'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.IDisposable{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]System.IDisposable{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.<castclass>.__uniq.106'{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.106"(target("msil.ref")) #5

; CHECK-LABEL: .method assembly static object 'as_stream'(object) cil managed
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call object '__msil_mc.<castclass>.__uniq.101'(object){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define internal target("msil.ref") @as_stream(target("msil.ref") %o) {
  %r = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.101"(target("msil.ref") %o)
  ret target("msil.ref") %r
}

; An unused cast still runs.
; CHECK-LABEL: .method assembly static void 'check_string'(object) cil managed
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call object '__msil_mc.<castclass>.__uniq.102'(object){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define internal void @check_string(target("msil.ref") %o) {
  %r = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.102"(target("msil.ref") %o)
  ret void
}

; CHECK-LABEL: .method public static int32 'main'() cil managed
; CHECK:       {{^}}  call object '__msil_mc.<castclass>.__uniq.103'(object){{$}}
; CHECK-NEXT:  {{^}}  call object '__msil_mc.<castclass>.__uniq.104'(object){{$}}
; CHECK-NEXT:  {{^}}  call object '__msil_mc.<castclass>.__uniq.105'(object){{$}}
; CHECK-NEXT:  {{^}}  call object '__msil_mc.<castclass>.__uniq.106'(object){{$}}
define i32 @main() {
  %a = call target("msil.ref") @as_stream(target("msil.ref") zeroinitializer)
  call void @check_string(target("msil.ref") zeroinitializer)
  %c = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.103"(target("msil.ref") zeroinitializer)
  %d = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.104"(target("msil.ref") %c)
  %e = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.105"(target("msil.ref") %d)
  %f = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.106"(target("msil.ref") %e)
  ret i32 0
}

; DUMP-LABEL: {{^}}  .method '__msil_mc.<castclass>.__uniq.101' object(object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: castclass [msil.bcl]System.IO.Stream{{$}}
; DUMP-NEXT:  {{^}}    L2: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.<castclass>.__uniq.102' object(object) flags=
; DUMP:       {{^}}    L1: castclass [msil.bcl]System.String{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.<castclass>.__uniq.103' object(object) flags=
; DUMP:       {{^}}    L1: castclass string[]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.<castclass>.__uniq.104' object(object) flags=
; DUMP:       {{^}}    L1: castclass class [msil.bcl]Ns.Widget[]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.<castclass>.__uniq.105' object(object) flags=
; DUMP:       {{^}}    L1: castclass [msil.bcl]Ns.Outer/Inner{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.<castclass>.__uniq.106' object(object) flags=
; DUMP:       {{^}}    L1: castclass [msil.bcl]System.IDisposable{{$}}
; DUMP-LABEL: {{^}}  .method 'check_string' void(object) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call object(object) '<Module>'::'__msil_mc.<castclass>.__uniq.102'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: pop{{$}}

; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.<castclass>.__uniq.101' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.<castclass>.__uniq.106' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT 'main' method=0x06{{[0-9A-F]+$}}

attributes #0 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.IO.Stream" }
attributes #1 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.String" }
attributes #2 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="string[]" }
attributes #3 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="class Ns.Widget[]" }
attributes #4 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="Ns.Outer/Inner" }
attributes #5 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.IDisposable" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5}
!0 = !{!"__msil_mc.<castclass>.__uniq.101", !"System.IO.Stream", !".castclass", !"object(object)", !"", !"ref;ref", !""}
!1 = !{!"__msil_mc.<castclass>.__uniq.102", !"System.String", !".castclass", !"object(object)", !"", !"ref;ref", !""}
!2 = !{!"__msil_mc.<castclass>.__uniq.103", !"string[]", !".castclass", !"object(object)", !"", !"ref;ref", !""}
!3 = !{!"__msil_mc.<castclass>.__uniq.104", !"class Ns.Widget[]", !".castclass", !"object(object)", !"", !"ref;ref", !""}
!4 = !{!"__msil_mc.<castclass>.__uniq.105", !"Ns.Outer/Inner", !".castclass", !"object(object)", !"", !"ref;ref", !""}
!5 = !{!"__msil_mc.<castclass>.__uniq.106", !"System.IDisposable", !".castclass", !"object(object)", !"", !"ref;ref", !""}

;--- object.ll
; CHECK: {{^}}error: managed call '<castclass>' has invalid "msil-managed-*" attributes: member: '.castclass' is not a valid member name{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref")) #0

define i32 @main() {
  %r = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.Object" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.<castclass>.__uniq.7", !"System.Object", !".castclass", !"object(object)", !"", !"ref;ref", !""}

;--- vt.ll
; CHECK: {{^}}error: managed call '<castclass>' has invalid "msil-managed-*" attributes: member: '.castclass' is not a valid member name{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref")) #0

define i32 @main() {
  %r = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="valuetype System.DateTime" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.<castclass>.__uniq.7", !"valuetype System.DateTime", !".castclass", !"object(object)", !"", !"ref;ref", !""}

;--- sig.ll
; CHECK: {{^}}error: managed call '<castclass>' has invalid "msil-managed-*" attributes: member: '.castclass' is not a valid member name{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref")) #0

define i32 @main() {
  %r = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="string(object)" "msil-managed-type"="System.String" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.<castclass>.__uniq.7", !"System.String", !".castclass", !"string(object)", !"", !"ref;ref", !""}

;--- conv.ll
; CHECK: {{^}}error: managed call '<castclass>' has invalid "msil-managed-*" attributes: member: '.castclass' is not a valid member name{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref")) #0

define i32 @main() {
  %r = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="obj;obj" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.String" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.<castclass>.__uniq.7", !"System.String", !".castclass", !"object(object)", !"", !"obj;obj", !""}

;--- decl.ll
; CHECK: {{^}}error: managed call '<castclass>' has invalid "msil-managed-*" attributes: "msil-managed-decl" belongs only to a static method or to an instance method whose 'this' conversion is 'this-val' or 'this-gch'{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref")) #0

define i32 @main() {
  %r = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="ref;ref" "msil-managed-decl"="System.String" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.String" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.<castclass>.__uniq.7", !"System.String", !".castclass", !"object(object)", !"", !"ref;ref", !""}

;--- addr.ll
; CHECK: {{^}}error: <unknown>:0:0: in function take ptr (): taking the address of the managed-call function '<castclass>' is not supported for MSIL yet{{$}}
declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref")) #0

define internal ptr @take() {
  ret ptr @"__msil_mc.<castclass>.__uniq.7"
}

define i32 @main() {
  %p = call ptr @take()
  %r = call target("msil.ref") @"__msil_mc.<castclass>.__uniq.7"(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.String" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.<castclass>.__uniq.7", !"System.String", !".castclass", !"object(object)", !"", !"ref;ref", !""}
