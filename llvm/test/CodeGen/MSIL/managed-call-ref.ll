; Managed calls with .NET references (ImplementationPlan.md 5.21.9 RM1,
; RM6, RM7, RM9, RM12, 5.21.8 RO1-RO2; 5.20 as amended by section 12's row
; 'MC-1's contract' of 2026-11-21; milestone R-a, RV25). A reference crosses
; a managed call as an IL object, with no GCHandle, no null test and no box:
; - a 'ref' argument (the IR type target("msil.ref"), the stub's parameter
;   'object') is 'ldarg' and one 'castclass' to the member's parameter type
;   (the TypeRef of a class or System.String, the TypeSpec of an array),
;   none for an 'object' or 'class System.Object' parameter (r_cmp, r_keep,
;   r_flush, r_copy, r_join, r_arrs; objclass.ll, text only: ilasm writes
;   a 'class System.Object' of a signature as 'object', so that member
;   stays out of the object oracle);
; - a 'ref' result is returned as it is (r_join, r_tostr, r_eqobj); a class
;   '.ctor' with a 'ref' result is 'newobj', then in whole-program text the
;   kind check 'castclass class T' that every class constructor takes
;   (5.20.6 MW2 as amended), then 'ret' (r_newsb);
; - a 'this-ref' 'this' is 'ldarg.0' and 'castclass' to TYPE as the stubs
;   name it (in whole-program text the kind-checking TypeSpec 'class T',
;   the TypeRef for System.String and in object-form text and objects), none
;   for System.Object, and then 'callvirt', so an override runs, for a
;   class, an interface, System.String and System.Object (r_eqobj,
;   r_dispose, r_len, r_tostr), or 'ldfld' for a field (r_count);
; - a field of a reference type (string, a class or an array, RM2) takes a
;   'ref' result, returned as it is: a static field is 'ldsfld' and 'ret'
;   (r_empty, System.String::Empty, 'field string'; r_types,
;   System.Type::EmptyTypes, 'field class System.Type[]'), an instance
;   field 'ldarg.0', 'castclass' to TYPE, 'ldfld' and 'ret' through
;   'this-ref;ref' (r_name, 'instance field string'; r_next, 'instance
;   field class Ns.Holder');
; - tickets stay MC-1's rows (RM9): GCHandle::Alloc as 'ref;gch' (r_alloc)
;   and get_Target as 'this-gch;ref' (r_target, whose 'object' result is
;   returned as it is; its access is MC-1's: 'call instance' in
;   whole-program text, the 'constrained.' placeholder that msil-ld rewrites
;   in object-form text and objects);
; - with 'msil_catch', the result local is 'object' and the handler's zero
;   is 'ldnull' (r_tostr_c; MX2, RM12).
; Whole-program text (WP, CORE=mscorlib) and object-form text (OF,
; CORE=msil.bcl) on both host x86_64 triples, msil64-unknown-dotnet and both
; aarch64 triples at -O0 and -O2, pinned whole from '.method' to
; '} // end of method'; the whole-program text assembles. The objects (on
; x86_64-pc-windows-msvc-msil, aarch64-unknown-linux-gnu-msil and
; msil64-unknown-dotnet), through llvm-cil-dump: 'object' in the stubs'
; signatures and locals, the castclass TypeRefs and TypeSpecs,
; the field stubs' 'ldsfld' and 'ldfld' with their field signatures, the
; stubs as LOCAL symbols; and the object oracle (5.12.6) on the
; object-form text (ilasm writes the TypeRefs of System.String and
; System.Object into mscorlib, whatever scope the text names, so the oracle
; maps them back to [msil.bcl], as MC-1's oracles do for System.String).
; The fit checks of 5.21.9 RM6 (cli::fitsManagedConversion) as slot P0's
; invalid-attributes error, 'managed call '<f>' has invalid
; "msil-managed-*" attributes: <reason>' (5.20.5 ML2 (1) as built), each
; pinned whole, in text and objects: 'this-ref' on a value type TYPE
; (thisref-vt.ll), with a pointer IR type (thisref-ptr.ll), and on a static
; member, where it would be the member's first parameter
; (thisref-static.ll); 'ref' for an int32 parameter (ref-int.ll), for an
; int32 result (ref-result.ll) and with a pointer IR type (ref-ptr.ll); a
; handle code ('obj') with the reference IR type (obj-ref.ll); and
; "msil-managed-decl" on a 'this-ref' method (decl.ll, MC-1's reason). The
; address of a managed call with references is MC-1's T-M10 alone, never a
; second error from P1 (addr.ll, also on aarch64-pc-windows-msvc-msil).

; RUN: rm -rf %t && split-file %s %t

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/stubs.ll \
; DEFINE:   -o %t/wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %t/stubs.ll --check-prefixes=CHECK,WP -DCORE=mscorlib --input-file=%t/wp.%{t}%{o}.il && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %t/stubs.ll \
; DEFINE:   -o %t/of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %t/stubs.ll --check-prefixes=CHECK,OF -DCORE=msil.bcl --input-file=%t/of.%{t}%{o}.il
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
; RUN: %if ilasm %{ %ilasm-verify -output=%t/wp.lnx.dll %t/wp.x86_64-unknown-linux-gnu-msil-O2.il %}

; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/stubs.ll \
; DEFINE:   -o %t/%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t/%{t}%{o}.o \
; DEFINE:   | FileCheck %t/stubs.ll --check-prefix=DUMP && \
; DEFINE:   llvm-cil-dump --link-info %t/%{t}%{o}.o | FileCheck %t/stubs.ll --check-prefix=LINK
; DEFINE: %{oracle} = %ilasm-verify -output=%t/of.%{t}%{o}.ilasm.o %t/of.%{t}%{o}.il && \
; DEFINE:   %{canon} %t/%{t}%{o}.o | sed -e "/^pe /d" > %t/%{t}%{o}.canon1 && \
; DEFINE:   %{canon} %t/of.%{t}%{o}.ilasm.o | sed -e "/^pe /d" -e "/^assemblyref 'mscorlib' /d" \
; DEFINE:     -e "s/\[mscorlib\]System\.String::/[msil.bcl]System.String::/g" \
; DEFINE:     -e "s/\[mscorlib\]System\.String$/[msil.bcl]System.String/" \
; DEFINE:     -e "s/\[mscorlib\]System\.Object::/[msil.bcl]System.Object::/g" > %t/%{t}%{o}.canon2 && \
; DEFINE:   diff %t/%{t}%{o}.canon1 %t/%{t}%{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t/%{t}%{o}.o > %t/%{t}%{o}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t/of.%{t}%{o}.ilasm.o > %t/%{t}%{o}.link2 && \
; DEFINE:   diff %t/%{t}%{o}.link1 %t/%{t}%{o}.link2
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false < %t/objclass.ll 2>&1 \
; RUN:   | FileCheck %t/objclass.ll -DCORE=mscorlib
; RUN: llc -mtriple=msil64-unknown-dotnet -O2 -msil-object-form -asm-verbose=false < %t/objclass.ll 2>&1 \
; RUN:   | FileCheck %t/objclass.ll -DCORE=msil.bcl

; DEFINE: %{f} =
; DEFINE: %{err} = not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/%{f}.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %t/%{f}.ll --implicit-check-not=error: --implicit-check-not="LLVM ERROR" \
; DEFINE:     --implicit-check-not="PLEASE submit" && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj < %t/%{f}.ll -o %t/%{f}.o 2>&1 \
; DEFINE:   | FileCheck %t/%{f}.ll --implicit-check-not=error: --implicit-check-not="LLVM ERROR" \
; DEFINE:     --implicit-check-not="PLEASE submit"
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{f} = thisref-vt
; RUN: %{err}
; REDEFINE: %{f} = thisref-ptr
; RUN: %{err}
; REDEFINE: %{f} = thisref-static
; RUN: %{err}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; REDEFINE: %{f} = ref-int
; RUN: %{err}
; REDEFINE: %{f} = ref-result
; RUN: %{err}
; REDEFINE: %{f} = ref-ptr
; RUN: %{err}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{f} = obj-ref
; RUN: %{err}
; REDEFINE: %{f} = decl
; RUN: %{err}
; REDEFINE: %{f} = addr
; RUN: %{err}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; RUN: %{err}

;--- stubs.ll
source_filename = "managed-call-ref.c"

; Two 'ref' string arguments, each cast.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.r_cmp'(object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  castclass {{\[}}[[CORE]]]System.String{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  castclass {{\[}}[[CORE]]]System.String{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.String::Compare(string, string){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_cmp'{{$}}
declare i32 @__msil_mc.r_cmp(target("msil.ref"), target("msil.ref")) #0

; An 'object' parameter: no castclass.
; CHECK-LABEL: .method assembly static void '__msil_mc.r_keep'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]System.GC::KeepAlive(object){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_keep'{{$}}
declare void @__msil_mc.r_keep(target("msil.ref")) #1

; A class parameter: the class's TypeRef.
; CHECK-LABEL: .method assembly static void '__msil_mc.r_flush'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  castclass {{\[}}[[CORE]]]System.IO.Stream{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]Ns.Util::Flush(class {{\[}}[[CORE]]]System.IO.Stream){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_flush'{{$}}
declare void @__msil_mc.r_flush(target("msil.ref")) #2

; An array parameter beside value parameters.
; CHECK-LABEL: .method assembly static void '__msil_mc.r_copy'(object, int32, native int, int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 4$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  castclass uint8[]{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]System.Runtime.InteropServices.Marshal::Copy(uint8[], int32, native int, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_copy'{{$}}
declare void @__msil_mc.r_copy(target("msil.ref"), i32, ptr, i32) #3

; Two 'object' parameters (no castclass) and a string result returned as it
; is.
; CHECK-LABEL: .method assembly static object '__msil_mc.r_join'(object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call string {{\[}}[[CORE]]]System.String::Concat(object, object){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_join'{{$}}
declare target("msil.ref") @__msil_mc.r_join(target("msil.ref"), target("msil.ref")) #4

; 'this-ref' of System.Object: no castclass, callvirt (an override runs).
; CHECK-LABEL: .method assembly static object '__msil_mc.r_tostr'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance string {{\[}}[[CORE]]]System.Object::ToString(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_tostr'{{$}}
declare target("msil.ref") @__msil_mc.r_tostr(target("msil.ref")) #5

; 'this-ref' of System.String: its TypeRef in every form.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.r_len'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  castclass {{\[}}[[CORE]]]System.String{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.String::get_Length(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_len'{{$}}
declare i32 @__msil_mc.r_len(target("msil.ref")) #6

; 'this-ref' of an interface: the kind-checking TypeSpec in whole-program text.
; CHECK-LABEL: .method assembly static void '__msil_mc.r_dispose'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.IDisposable{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]System.IDisposable{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance void {{\[}}[[CORE]]]System.IDisposable::Dispose(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_dispose'{{$}}
declare void @__msil_mc.r_dispose(target("msil.ref")) #7

; 'this-ref' of a class's field: ldfld.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.r_count'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]Ns.Holder{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]Ns.Holder{{$}}
; CHECK-NEXT:  {{^}}  ldfld int32 {{\[}}[[CORE]]]Ns.Holder::Count{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_count'{{$}}
declare i32 @__msil_mc.r_count(target("msil.ref")) #8

; A class constructor with a 'ref' result.
; CHECK-LABEL: .method assembly static object '__msil_mc.r_newsb'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.Text.StringBuilder::.ctor(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.Text.StringBuilder{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_newsb'{{$}}
declare target("msil.ref") @__msil_mc.r_newsb() #9

; A ticket from a reference ('ref;gch') and a reference from a ticket
; ('this-gch;ref').
; CHECK-LABEL: .method assembly static native int '__msil_mc.r_alloc'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_alloc'{{$}}
declare i64 @__msil_mc.r_alloc(target("msil.ref")) #10

; CHECK-LABEL: .method assembly static object '__msil_mc.r_target'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; WP-NEXT:     {{^}}  call instance object {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; OF-NEXT:     {{^}}  callvirt instance object {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_target'{{$}}
declare target("msil.ref") @__msil_mc.r_target(i64) #11

; 'msil_catch' with a reference result: an 'object' result local, 'ldnull'
; in the handler.
; CHECK-LABEL: .method assembly static object '__msil_mc.r_tostr_c'(object, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^  \.locals \(int32, object\)$}}
; CHECK-NEXT:  {{^  \.try \{$}}
; CHECK-NEXT:  {{^@MC[0-9]+_[0-9]+:$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.Text.StringBuilder{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]System.Text.StringBuilder{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance string {{\[}}[[CORE]]]System.Text.StringBuilder::ToString(){{$}}
; CHECK-NEXT:  {{^}}  stloc.1{{$}}
; CHECK-NEXT:  {{^}}  leave{{(\.s)?}} [[END:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK:       {{^}}  isinst {{\[}}[[CORE]]]System.FormatException{{$}}
; CHECK:       {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK-NEXT:  {{^@MC[0-9]+_[0-9]+:$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  stloc.1{{$}}
; CHECK-NEXT:  {{^}}  leave{{(\.s)?}} [[END]]{{$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NEXT:  {{^}}[[END]]:{{$}}
; CHECK-NEXT:  {{^}}  ldloc.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_tostr_c'{{$}}
declare target("msil.ref") @__msil_mc.r_tostr_c(target("msil.ref"), ptr) #12

; Array parameters: the string array's and the class array's TypeSpecs.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.r_arrs'(object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  castclass string[]{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  castclass class {{\[}}[[CORE]]]System.IO.Stream[]{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]Ns.Util::Both(string[], class {{\[}}[[CORE]]]System.IO.Stream[]){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_arrs'{{$}}
declare i32 @__msil_mc.r_arrs(target("msil.ref"), target("msil.ref")) #13

; 'this-ref' of a class with an 'object' result.
; CHECK-LABEL: .method assembly static object '__msil_mc.r_eqobj'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.IO.Stream{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]System.IO.Stream{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance object {{\[}}[[CORE]]]System.IO.Stream::Clone(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_eqobj'{{$}}
declare target("msil.ref") @__msil_mc.r_eqobj(target("msil.ref")) #14

; A static string field: 'ldsfld', the object returned as it is.
; CHECK-LABEL: .method assembly static object '__msil_mc.r_empty'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldsfld string {{\[}}[[CORE]]]System.String::Empty{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_empty'{{$}}
declare target("msil.ref") @__msil_mc.r_empty() #15

; A static field of an array type.
; CHECK-LABEL: .method assembly static object '__msil_mc.r_types'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldsfld class {{\[}}[[CORE]]]System.Type[] {{\[}}[[CORE]]]System.Type::EmptyTypes{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_types'{{$}}
declare target("msil.ref") @__msil_mc.r_types() #16

; Instance fields of a string type and of a class type, read through
; 'this-ref': castclass to TYPE, then ldfld.
; CHECK-LABEL: .method assembly static object '__msil_mc.r_name'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]Ns.Holder{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]Ns.Holder{{$}}
; CHECK-NEXT:  {{^}}  ldfld string {{\[}}[[CORE]]]Ns.Holder::Name{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_name'{{$}}
declare target("msil.ref") @__msil_mc.r_name(target("msil.ref")) #17

; CHECK-LABEL: .method assembly static object '__msil_mc.r_next'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]Ns.Holder{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]Ns.Holder{{$}}
; CHECK-NEXT:  {{^}}  ldfld class {{\[}}[[CORE]]]Ns.Holder {{\[}}[[CORE]]]Ns.Holder::Next{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_next'{{$}}
declare target("msil.ref") @__msil_mc.r_next(target("msil.ref")) #18

; The caller passes and receives objects.
; CHECK-LABEL: .method public static int32 'main'() cil managed
; CHECK:       {{^}}  call object '__msil_mc.r_newsb'(){{$}}
; CHECK:       {{^}}  call object '__msil_mc.r_tostr'(object){{$}}
; CHECK:       {{^}}  call native int '__msil_mc.r_alloc'(object){{$}}
; CHECK:       {{^}}  call object '__msil_mc.r_target'(native int){{$}}
; CHECK:       {{^}}  call object '__msil_mc.r_empty'(){{$}}
; CHECK:       {{^}}  call object '__msil_mc.r_types'(){{$}}
; CHECK:       {{^}}  call object '__msil_mc.r_next'(object){{$}}
; CHECK:       {{^}}  call object '__msil_mc.r_name'(object){{$}}
; CHECK:       {{^}}} // end of method 'main'{{$}}
define i32 @main() {
  %sb = call target("msil.ref") @__msil_mc.r_newsb()
  %s = call target("msil.ref") @__msil_mc.r_tostr(target("msil.ref") %sb)
  %n = call i32 @__msil_mc.r_cmp(target("msil.ref") %s, target("msil.ref") %s)
  call void @__msil_mc.r_keep(target("msil.ref") %s)
  call void @__msil_mc.r_flush(target("msil.ref") zeroinitializer)
  call void @__msil_mc.r_copy(target("msil.ref") zeroinitializer, i32 0, ptr null, i32 0)
  %j = call target("msil.ref") @__msil_mc.r_join(target("msil.ref") %s, target("msil.ref") zeroinitializer)
  %l = call i32 @__msil_mc.r_len(target("msil.ref") %j)
  call void @__msil_mc.r_dispose(target("msil.ref") zeroinitializer)
  %c = call i32 @__msil_mc.r_count(target("msil.ref") zeroinitializer)
  %t = call i64 @__msil_mc.r_alloc(target("msil.ref") %sb)
  %o = call target("msil.ref") @__msil_mc.r_target(i64 %t)
  %err = alloca i32, align 4
  %q = call target("msil.ref") @__msil_mc.r_tostr_c(target("msil.ref") %o, ptr %err)
  %a = call i32 @__msil_mc.r_arrs(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %e = call target("msil.ref") @__msil_mc.r_eqobj(target("msil.ref") %q)
  %fe = call target("msil.ref") @__msil_mc.r_empty()
  %ft = call target("msil.ref") @__msil_mc.r_types()
  %fh = call target("msil.ref") @__msil_mc.r_next(target("msil.ref") %e)
  %fn = call target("msil.ref") @__msil_mc.r_name(target("msil.ref") %fh)
  call void @__msil_mc.r_keep(target("msil.ref") %fe)
  call void @__msil_mc.r_keep(target("msil.ref") %ft)
  call void @__msil_mc.r_keep(target("msil.ref") %fn)
  %r1 = add i32 %n, %l
  %r2 = add i32 %r1, %c
  %r3 = add i32 %r2, %a
  ret i32 %r3
}

; The objects: 'object' signatures and locals, TypeRefs and TypeSpecs.
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_cmp' int32(object, object) flags=
; DUMP:       {{^}}    L1: castclass [msil.bcl]System.String{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_keep' void(object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call void(object) [msil.bcl]System.GC::'KeepAlive'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_flush' void(object) flags=
; DUMP:       {{^}}    L1: castclass [msil.bcl]System.IO.Stream{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_tostr' object(object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: callvirt instance string() [msil.bcl]System.Object::'ToString'{{$}}
; DUMP-NEXT:  {{^}}    L2: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_dispose' void(object) flags=
; DUMP:       {{^}}    L1: castclass [msil.bcl]System.IDisposable{{$}}
; DUMP-NEXT:  {{^}}    L2: callvirt instance void() [msil.bcl]System.IDisposable::'Dispose'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_tostr_c' object(object, native int) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    .locals (int32, object){{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: ldnull{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_arrs' int32(object, object) flags=
; DUMP:       {{^}}    L1: castclass string[]{{$}}
; DUMP:       {{^}}    L3: castclass class [msil.bcl]System.IO.Stream[]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_empty' object() flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldsfld string [msil.bcl]System.String::'Empty'{{$}}
; DUMP-NEXT:  {{^}}    L1: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_types' object() flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldsfld class [msil.bcl]System.Type[] [msil.bcl]System.Type::'EmptyTypes'{{$}}
; DUMP-NEXT:  {{^}}    L1: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_name' object(object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: castclass [msil.bcl]Ns.Holder{{$}}
; DUMP-NEXT:  {{^}}    L2: ldfld string [msil.bcl]Ns.Holder::'Name'{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.r_next' object(object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: castclass [msil.bcl]Ns.Holder{{$}}
; DUMP-NEXT:  {{^}}    L2: ldfld class [msil.bcl]Ns.Holder [msil.bcl]Ns.Holder::'Next'{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}

; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.r_cmp' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.r_tostr' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.r_tostr_c' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.r_empty' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.r_name' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT 'main' method=0x06{{[0-9A-F]+$}}

attributes #0 = { "msil-managed-conv"="ref,ref;val" "msil-managed-member"="Compare" "msil-managed-sig"="int32(string, string)" "msil-managed-type"="System.String" }
attributes #1 = { "msil-managed-conv"="ref;void" "msil-managed-member"="KeepAlive" "msil-managed-sig"="void(object)" "msil-managed-type"="System.GC" }
attributes #2 = { "msil-managed-conv"="ref;void" "msil-managed-member"="Flush" "msil-managed-sig"="void(class System.IO.Stream)" "msil-managed-type"="Ns.Util" }
attributes #3 = { "msil-managed-conv"="ref,val,val,val;void" "msil-managed-member"="Copy" "msil-managed-sig"="void(uint8[], int32, native int, int32)" "msil-managed-type"="System.Runtime.InteropServices.Marshal" }
attributes #4 = { "msil-managed-conv"="ref,ref;ref" "msil-managed-member"="Concat" "msil-managed-sig"="string(object, object)" "msil-managed-type"="System.String" }
attributes #5 = { "msil-managed-conv"="this-ref;ref" "msil-managed-member"="ToString" "msil-managed-sig"="instance string()" "msil-managed-type"="System.Object" }
attributes #6 = { "msil-managed-conv"="this-ref;val" "msil-managed-member"="get_Length" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.String" }
attributes #7 = { "msil-managed-conv"="this-ref;void" "msil-managed-member"="Dispose" "msil-managed-sig"="instance void()" "msil-managed-type"="System.IDisposable" }
attributes #8 = { "msil-managed-conv"="this-ref;val" "msil-managed-member"="Count" "msil-managed-sig"="instance field int32" "msil-managed-type"="Ns.Holder" }
attributes #9 = { "msil-managed-conv"=";ref" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void()" "msil-managed-type"="System.Text.StringBuilder" }
attributes #10 = { "msil-managed-conv"="ref;gch" "msil-managed-member"="Alloc" "msil-managed-sig"="valuetype System.Runtime.InteropServices.GCHandle(object)" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #11 = { "msil-managed-conv"="this-gch;ref" "msil-managed-member"="get_Target" "msil-managed-sig"="instance object()" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #12 = { "msil-managed-catch"="System.FormatException=22" "msil-managed-conv"="this-ref,err;ref" "msil-managed-member"="ToString" "msil-managed-sig"="instance string()" "msil-managed-type"="System.Text.StringBuilder" }
attributes #13 = { "msil-managed-conv"="ref,ref;val" "msil-managed-member"="Both" "msil-managed-sig"="int32(string[], class System.IO.Stream[])" "msil-managed-type"="Ns.Util" }
attributes #14 = { "msil-managed-conv"="this-ref;ref" "msil-managed-member"="Clone" "msil-managed-sig"="instance object()" "msil-managed-type"="System.IO.Stream" }
attributes #15 = { "msil-managed-conv"=";ref" "msil-managed-member"="Empty" "msil-managed-sig"="field string" "msil-managed-type"="System.String" }
attributes #16 = { "msil-managed-conv"=";ref" "msil-managed-member"="EmptyTypes" "msil-managed-sig"="field class System.Type[]" "msil-managed-type"="System.Type" }
attributes #17 = { "msil-managed-conv"="this-ref;ref" "msil-managed-member"="Name" "msil-managed-sig"="instance field string" "msil-managed-type"="Ns.Holder" }
attributes #18 = { "msil-managed-conv"="this-ref;ref" "msil-managed-member"="Next" "msil-managed-sig"="instance field class Ns.Holder" "msil-managed-type"="Ns.Holder" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12, !13, !14, !15, !16, !17, !18}
!0 = !{!"__msil_mc.r_cmp", !"System.String", !"Compare", !"int32(string, string)", !"", !"ref,ref;val", !""}
!1 = !{!"__msil_mc.r_keep", !"System.GC", !"KeepAlive", !"void(object)", !"", !"ref;void", !""}
!2 = !{!"__msil_mc.r_flush", !"Ns.Util", !"Flush", !"void(class System.IO.Stream)", !"", !"ref;void", !""}
!3 = !{!"__msil_mc.r_copy", !"System.Runtime.InteropServices.Marshal", !"Copy", !"void(uint8[], int32, native int, int32)", !"", !"ref,val,val,val;void", !""}
!4 = !{!"__msil_mc.r_join", !"System.String", !"Concat", !"string(object, object)", !"", !"ref,ref;ref", !""}
!5 = !{!"__msil_mc.r_tostr", !"System.Object", !"ToString", !"instance string()", !"", !"this-ref;ref", !""}
!6 = !{!"__msil_mc.r_len", !"System.String", !"get_Length", !"instance int32()", !"", !"this-ref;val", !""}
!7 = !{!"__msil_mc.r_dispose", !"System.IDisposable", !"Dispose", !"instance void()", !"", !"this-ref;void", !""}
!8 = !{!"__msil_mc.r_count", !"Ns.Holder", !"Count", !"instance field int32", !"", !"this-ref;val", !""}
!9 = !{!"__msil_mc.r_newsb", !"System.Text.StringBuilder", !".ctor", !"instance void()", !"", !";ref", !""}
!10 = !{!"__msil_mc.r_alloc", !"valuetype System.Runtime.InteropServices.GCHandle", !"Alloc", !"valuetype System.Runtime.InteropServices.GCHandle(object)", !"", !"ref;gch", !""}
!11 = !{!"__msil_mc.r_target", !"valuetype System.Runtime.InteropServices.GCHandle", !"get_Target", !"instance object()", !"", !"this-gch;ref", !""}
!12 = !{!"__msil_mc.r_tostr_c", !"System.Text.StringBuilder", !"ToString", !"instance string()", !"", !"this-ref,err;ref", !"System.FormatException=22"}
!13 = !{!"__msil_mc.r_arrs", !"Ns.Util", !"Both", !"int32(string[], class System.IO.Stream[])", !"", !"ref,ref;val", !""}
!14 = !{!"__msil_mc.r_eqobj", !"System.IO.Stream", !"Clone", !"instance object()", !"", !"this-ref;ref", !""}
!15 = !{!"__msil_mc.r_empty", !"System.String", !"Empty", !"field string", !"", !";ref", !""}
!16 = !{!"__msil_mc.r_types", !"System.Type", !"EmptyTypes", !"field class System.Type[]", !"", !";ref", !""}
!17 = !{!"__msil_mc.r_name", !"Ns.Holder", !"Name", !"instance field string", !"", !"this-ref;ref", !""}
!18 = !{!"__msil_mc.r_next", !"Ns.Holder", !"Next", !"instance field class Ns.Holder", !"", !"this-ref;ref", !""}

;--- objclass.ll
; A 'class System.Object' parameter takes no castclass either.
; CHECK-LABEL: .method assembly static void '__msil_mc.r_hold'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]Ns.Util::Hold(class {{\[}}[[CORE]]]System.Object){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.r_hold'{{$}}
declare void @__msil_mc.r_hold(target("msil.ref")) #0

define i32 @main() {
  call void @__msil_mc.r_hold(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="ref;void" "msil-managed-member"="Hold" "msil-managed-sig"="void(class System.Object)" "msil-managed-type"="Ns.Util" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.r_hold", !"Ns.Util", !"Hold", !"void(class System.Object)", !"", !"ref;void", !""}

;--- thisref-vt.ll
; CHECK: {{^}}error: managed call 'm_ticks' has invalid "msil-managed-*" attributes: parameter 1: conversion 'this-ref' does not fit the .NET type 'valuetype System.DateTime' and the IR type 'target("msil.ref")'{{$}}
declare i64 @__msil_mc.m_ticks(target("msil.ref")) #0

define i32 @main() {
  %t = call i64 @__msil_mc.m_ticks(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="this-ref;val" "msil-managed-member"="get_Ticks" "msil-managed-sig"="instance int64()" "msil-managed-type"="valuetype System.DateTime" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_ticks", !"valuetype System.DateTime", !"get_Ticks", !"instance int64()", !"", !"this-ref;val", !""}

;--- thisref-ptr.ll
; CHECK: {{^}}error: managed call 'm_len' has invalid "msil-managed-*" attributes: parameter 1: conversion 'this-ref' does not fit the .NET type 'class System.String' and the IR type 'ptr'{{$}}
declare i32 @__msil_mc.m_len(ptr) #0

define i32 @main() {
  %l = call i32 @__msil_mc.m_len(ptr null)
  ret i32 %l
}

attributes #0 = { "msil-managed-conv"="this-ref;val" "msil-managed-member"="get_Length" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.String" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_len", !"System.String", !"get_Length", !"instance int32()", !"", !"this-ref;val", !""}

;--- thisref-static.ll
; CHECK: {{^}}error: managed call 'm_keep' has invalid "msil-managed-*" attributes: parameter 1: conversion 'this-ref' does not fit the .NET type 'object' and the IR type 'target("msil.ref")'{{$}}
declare void @__msil_mc.m_keep(target("msil.ref")) #0

define i32 @main() {
  call void @__msil_mc.m_keep(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="this-ref;void" "msil-managed-member"="KeepAlive" "msil-managed-sig"="void(object)" "msil-managed-type"="System.GC" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_keep", !"System.GC", !"KeepAlive", !"void(object)", !"", !"this-ref;void", !""}

;--- ref-int.ll
; CHECK: {{^}}error: managed call 'm_abs' has invalid "msil-managed-*" attributes: parameter 1: conversion 'ref' does not fit the .NET type 'int32' and the IR type 'target("msil.ref")'{{$}}
declare i32 @__msil_mc.m_abs(target("msil.ref")) #0

define i32 @main() {
  %a = call i32 @__msil_mc.m_abs(target("msil.ref") zeroinitializer)
  ret i32 %a
}

attributes #0 = { "msil-managed-conv"="ref;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"ref;val", !""}

;--- ref-result.ll
; CHECK: {{^}}error: managed call 'm_tick' has invalid "msil-managed-*" attributes: the result: conversion 'ref' does not fit the .NET type 'int32' and the IR type 'target("msil.ref")'{{$}}
declare target("msil.ref") @__msil_mc.m_tick() #0

define i32 @main() {
  %t = call target("msil.ref") @__msil_mc.m_tick()
  ret i32 0
}

attributes #0 = { "msil-managed-conv"=";ref" "msil-managed-member"="get_TickCount" "msil-managed-sig"="int32()" "msil-managed-type"="System.Environment" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_tick", !"System.Environment", !"get_TickCount", !"int32()", !"", !";ref", !""}

;--- ref-ptr.ll
; CHECK: {{^}}error: managed call 'm_empty' has invalid "msil-managed-*" attributes: parameter 1: conversion 'ref' does not fit the .NET type 'string' and the IR type 'ptr'{{$}}
declare i32 @__msil_mc.m_empty(ptr) #0

define i32 @main() {
  %e = call i32 @__msil_mc.m_empty(ptr null)
  ret i32 %e
}

attributes #0 = { "msil-managed-conv"="ref;val" "msil-managed-member"="IsNullOrEmpty" "msil-managed-sig"="bool(string)" "msil-managed-type"="System.String" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_empty", !"System.String", !"IsNullOrEmpty", !"bool(string)", !"", !"ref;val", !""}

;--- obj-ref.ll
; CHECK: {{^}}error: managed call 'm_hold' has invalid "msil-managed-*" attributes: parameter 1: conversion 'obj' does not fit the .NET type 'object' and the IR type 'target("msil.ref")'{{$}}
declare void @__msil_mc.m_hold(target("msil.ref")) #0

define i32 @main() {
  call void @__msil_mc.m_hold(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="obj;void" "msil-managed-member"="KeepAlive" "msil-managed-sig"="void(object)" "msil-managed-type"="System.GC" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_hold", !"System.GC", !"KeepAlive", !"void(object)", !"", !"obj;void", !""}

;--- decl.ll
; CHECK: {{^}}error: managed call 'm_decl' has invalid "msil-managed-*" attributes: "msil-managed-decl" belongs only to a static method or to an instance method whose 'this' conversion is 'this-val' or 'this-gch'{{$}}
declare target("msil.ref") @__msil_mc.m_decl(target("msil.ref")) #0

define i32 @main() {
  %r = call target("msil.ref") @__msil_mc.m_decl(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="this-ref;ref" "msil-managed-decl"="System.Object" "msil-managed-member"="ToString" "msil-managed-sig"="instance string()" "msil-managed-type"="System.Text.StringBuilder" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_decl", !"System.Text.StringBuilder", !"ToString", !"instance string()", !"", !"this-ref;ref", !""}

;--- addr.ll
; CHECK: {{^}}error: <unknown>:0:0: in function take ptr (): taking the address of the managed-call function 'm_str' is not supported for MSIL yet{{$}}
declare target("msil.ref") @__msil_mc.m_str(target("msil.ref")) #0

define internal ptr @take() {
  ret ptr @__msil_mc.m_str
}

define i32 @main() {
  %p = call ptr @take()
  %r = call target("msil.ref") @__msil_mc.m_str(target("msil.ref") zeroinitializer)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="this-ref;ref" "msil-managed-member"="ToString" "msil-managed-sig"="instance string()" "msil-managed-type"="System.Object" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_str", !"System.Object", !"ToString", !"instance string()", !"", !"this-ref;ref", !""}
