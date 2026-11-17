; The resolution scope of a managed-call stub's member and types in text
; output (ImplementationPlan.md 5.20.6 MW2 as amended in section 12 on
; 2026-11-04, the row '5.20.3 T-M2 and 5.20.6 MW2 amended'; milestone MC-1).
; In whole-program text (WP, llc's standalone text) a declaration's
; "msil-managed-asm" ASSEMBLY is the scope of its member and of every class
; and value type the stub names (the member's owner, its signature's class
; and value types, the 'castclass' of an 'obj' or 'this-obj' handle and of
; a class constructor's object (m_ctor), the 'box' of a value type result
; returned as a handle ('boxed', m_point), the filter's 'isinst' of each
; type in a catch list (m_check) and the 'newarr' element of an array
; constructor): an ILAsm dotted name is
; printed bare ('[System.Runtime]'), any other name single-quoted as
; MSIL::printQuotedName prints names: a space ("['my lib']"), a ']'
; ("['a]b']") and a reserved word of ilasm's lexer ("['int32']"). The
; built-in table's GCHandle rows that the conversions use keep
; '[mscorlib]'. The object-form text (OF, the text of an object) and the
; object name '[msil.bcl]' for everything whatever the ASSEMBLY (MW1), and
; neither has an assembly reference to it.
; clang/test/CodeGen/MSIL/msil-method.c pins the call form for "my lib" from
; clang; this test covers the backend on its own, with type operands.
; The member's type and the member are named the same way: each
; '/'-separated part of the TYPE (also where it is a signature type) and the
; MEMBER are printed bare when they are ILAsm dotted names and single-quoted
; otherwise ("[mscorlib]'value'", "Ns.Outer/'value'", "Ns.Type::'int32'"),
; and '.ctor' stays bare ('.cctor' never reaches the text: T-M2 refuses it);
; llvm-cil-dump prints the names unquoted. In scope.ll every name is ASCII and
; neither text has a byte order mark. utf8.ll's names are UTF-8 (TYPE
; "Ns.Caf\C3\A9", MEMBER "R\C3\A9" and ASSEMBLY "Caf\C3\A9" with IR escapes:
; an 'e' with an acute accent): they are quoted, and both texts start with a
; UTF-8 byte order mark (decision D11), without which ilasm reads them in the
; ANSI code page; the object oracle shows that ilasm read them as UTF-8.
; Whole-program and object-form text on both host x86_64 triples and
; msil64-unknown-dotnet at -O0 and -O2; the whole-program text assembles
; (ilasm warns that it cannot autodetect the named assemblies). The objects,
; through llvm-cil-dump, and the object oracle (5.12.6): llc's object and the
; object that ilasm makes from the object-form text have the same canonical
; listing and link info (for utf8.ll too).
; Not pinned: .maxstack values, label names and the null tests of the handle
; conversions (managed-call-bodies.ll pins them).

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/scope.ll \
; DEFINE:   -o %t.wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,WP --input-file=%t.wp.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=msil.bcl && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %t/scope.ll \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,OF --input-file=%t.of.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=mscorlib --implicit-check-not=lib] \
; DEFINE:     --implicit-check-not=System.Runtime] --implicit-check-not=int32] \
; DEFINE:     --implicit-check-not=b]
; DEFINE: %{u8} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/utf8.ll \
; DEFINE:   -o %t.wp.u8.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=U8,U8WP --input-file=%t.wp.u8.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=msil.bcl && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %t/utf8.ll \
; DEFINE:   -o %t.of.u8.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=U8,U8OF --input-file=%t.of.u8.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=mscorlib

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{il}
; RUN: %{u8}
; REDEFINE: %{o} = -O0
; RUN: %{il}
; RUN: %{u8}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{il}
; RUN: %{u8}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; RUN: %{u8}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{il}
; RUN: %{u8}
; REDEFINE: %{o} = -O0
; RUN: %{il}
; RUN: %{u8}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.lnx.dll %t.wp.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.win.dll %t.wp.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.p64.dll %t.wp.msil64-unknown-dotnet-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.u8.lnx.dll %t.wp.u8.x86_64-unknown-linux-gnu-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.u8.win.dll %t.wp.u8.x86_64-pc-windows-msvc-msil-O2.il %}

; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/scope.ll \
; DEFINE:   -o %t.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=DUMP --implicit-check-not=mscorlib \
; DEFINE:     --implicit-check-not=lib] --implicit-check-not=System.Runtime] \
; DEFINE:     --implicit-check-not=int32] --implicit-check-not=b]
; DEFINE: %{u8obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/utf8.ll \
; DEFINE:   -o %t.u8.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.u8.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=U8DUMP --implicit-check-not=mscorlib
; DEFINE: %{b} =
; DEFINE: %{oracle} = %ilasm-verify -output=%t.of%{b}.%{t}%{o}.ilasm.o %t.of%{b}.%{t}%{o}.il && \
; DEFINE:   %{canon} %t%{b}.%{t}%{o}.o | sed -e "/^pe /d" > %t%{b}.%{t}%{o}.canon1 && \
; DEFINE:   %{canon} %t.of%{b}.%{t}%{o}.ilasm.o | sed -e "/^pe /d" > %t%{b}.%{t}%{o}.canon2 && \
; DEFINE:   diff %t%{b}.%{t}%{o}.canon1 %t%{b}.%{t}%{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t%{b}.%{t}%{o}.o > %t%{b}.%{t}%{o}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t.of%{b}.%{t}%{o}.ilasm.o > %t%{b}.%{t}%{o}.link2 && \
; DEFINE:   diff %t%{b}.%{t}%{o}.link1 %t%{b}.%{t}%{o}.link2
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}

; utf8.ll's objects and oracle; the object-form text of each run above.
; REDEFINE: %{b} = .u8
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{u8obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{u8obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{u8obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}

; The UTF-8 module: a byte order mark first, then the quoted names.
; U8WP-NOT:    {{.}}
; U8WP:        {{^}}{{\xEF\xBB\xBF}}// MSIL module generated by LLVM{{$}}
; U8OF-NOT:    {{.}}
; U8OF:        {{^}}{{\xEF\xBB\xBF}}// MSIL object generated by LLVM{{$}}
; U8-LABEL:    {{^}}.method assembly static void '__msil_mc.m_cafe'() cil managed{{$}}
; U8WP:        {{^}}  call void {{\[}}'Caf{{\xC3\xA9}}']'Ns.Caf{{\xC3\xA9}}'::'R{{\xC3\xA9}}'(){{$}}
; U8OF:        {{^}}  call void {{\[}}msil.bcl]'Ns.Caf{{\xC3\xA9}}'::'R{{\xC3\xA9}}'(){{$}}
; U8:          {{^}}} // end of method '__msil_mc.m_cafe'{{$}}
; U8-LABEL:    {{^}}.method public static void 'use_cafe'() cil managed{{$}}
; U8:          {{^}}  call void '__msil_mc.m_cafe'(){{$}}
; U8DUMP:      {{^}}    L{{[0-9]+}}: call void() {{\[}}msil.bcl]Ns.Caf{{\xC3\xA9}}::'R{{\xC3\xA9}}'{{$}}

;--- scope.ll
source_filename = "managed-call-scope.c"

; ASCII names only: no byte order mark.
; WP-NOT:  {{.}}
; WP:      {{^}}// MSIL module generated by LLVM{{$}}
; OF-NOT:  {{.}}
; OF:      {{^}}// MSIL object generated by LLVM{{$}}

; WP:      {{^}}.assembly extern mscorlib {{.*}}{{$}}
; OF:      {{^}}.assembly extern 'msil.bcl' {{.*}}{{$}}
; DUMP:    {{^}}assemblyref 'msil.bcl' {{.*}}{{$}}

; A space: quoted.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_abs'(int32) cil managed
; WP:          {{^}}  call int32 {{\[}}'my lib']System.Math::Abs(int32){{$}}
; OF:          {{^}}  call int32 {{\[}}msil.bcl]System.Math::Abs(int32){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_abs'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call int32(int32) {{\[}}msil.bcl]System.Math::'Abs'{{$}}
declare i32 @__msil_mc.m_abs(i32 noundef) #0

; A class parameter and result: the 'castclass' of the 'obj' argument and
; both class types of the member's signature take the scope; the GCHandle
; rows keep [mscorlib].
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_make'(native int) cil managed
; WP:          {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; OF:          {{^}}  call valuetype {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; WP:          {{^}}  castclass {{\[}}'my lib']Ns.Widget{{$}}
; OF:          {{^}}  castclass {{\[}}msil.bcl]Ns.Widget{{$}}
; WP:          {{^}}  call class {{\[}}'my lib']Ns.Widget {{\[}}'my lib']Ns.Factory::Make(class {{\[}}'my lib']Ns.Widget){{$}}
; OF:          {{^}}  call class {{\[}}msil.bcl]Ns.Widget {{\[}}msil.bcl]Ns.Factory::Make(class {{\[}}msil.bcl]Ns.Widget){{$}}
; WP:          {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; OF:          {{^}}  call valuetype {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_make'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: castclass {{\[}}msil.bcl]Ns.Widget{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call class {{\[}}msil.bcl]Ns.Widget(class {{\[}}msil.bcl]Ns.Widget) {{\[}}msil.bcl]Ns.Factory::'Make'{{$}}
declare ptr @__msil_mc.m_make(ptr noundef) #1

; A 'this-obj' instance method of a class in "my lib": in whole-program
; text the handle's 'castclass' names the TypeSpec 'class ['my lib']T' (the
; run-time kind check), in the scope of the declaration.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_size'(native int) cil managed
; WP:          {{^}}  castclass class {{\[}}'my lib']Ns.Widget{{$}}
; OF:          {{^}}  castclass {{\[}}msil.bcl]Ns.Widget{{$}}
; WP:          {{^}}  callvirt instance int32 {{\[}}'my lib']Ns.Widget::get_Size(){{$}}
; OF:          {{^}}  callvirt instance int32 {{\[}}msil.bcl]Ns.Widget::get_Size(){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_size'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: castclass {{\[}}msil.bcl]Ns.Widget{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: callvirt instance int32() {{\[}}msil.bcl]Ns.Widget::'get_Size'{{$}}
declare i32 @__msil_mc.m_size(ptr noundef) #2

; An array of a class in "my lib": the 'newarr' element type.
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_newwidgets'(int32) cil managed
; WP:          {{^}}  newarr {{\[}}'my lib']Ns.Widget{{$}}
; OF:          {{^}}  newarr {{\[}}msil.bcl]Ns.Widget{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_newwidgets'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: newarr {{\[}}msil.bcl]Ns.Widget{{$}}
declare ptr @__msil_mc.m_newwidgets(i32 noundef) #3

; A reserved word of ilasm's lexer: quoted.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_kw'(int32) cil managed
; WP:          {{^}}  call int32 {{\[}}'int32']System.Math::Abs(int32){{$}}
; OF:          {{^}}  call int32 {{\[}}msil.bcl]System.Math::Abs(int32){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_kw'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call int32(int32) {{\[}}msil.bcl]System.Math::'Abs'{{$}}
declare i32 @__msil_mc.m_kw(i32 noundef) #4

; A dotted name: bare.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_rt'(int32) cil managed
; WP:          {{^}}  call int32 {{\[}}System.Runtime]System.Math::Abs(int32){{$}}
; OF:          {{^}}  call int32 {{\[}}msil.bcl]System.Math::Abs(int32){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_rt'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call int32(int32) {{\[}}msil.bcl]System.Math::'Abs'{{$}}
declare i32 @__msil_mc.m_rt(i32 noundef) #5

; A ']': quoted.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_brk'(int32) cil managed
; WP:          {{^}}  call int32 {{\[}}'a]b']System.Math::Abs(int32){{$}}
; OF:          {{^}}  call int32 {{\[}}msil.bcl]System.Math::Abs(int32){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_brk'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call int32(int32) {{\[}}msil.bcl]System.Math::'Abs'{{$}}
declare i32 @__msil_mc.m_brk(i32 noundef) #6

; A TYPE that is a reserved word of ilasm's lexer: quoted.
; CHECK-LABEL: .method assembly static void '__msil_mc.m_tv'() cil managed
; WP:          {{^}}  call void {{\[}}mscorlib]'value'::Run(){{$}}
; OF:          {{^}}  call void {{\[}}msil.bcl]'value'::Run(){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_tv'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call void() {{\[}}msil.bcl]value::'Run'{{$}}
declare void @__msil_mc.m_tv() #7

; MEMBERs that are reserved words: quoted.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_mv'() cil managed
; WP:          {{^}}  call int32 {{\[}}mscorlib]Ns.Type::'value'(){{$}}
; OF:          {{^}}  call int32 {{\[}}msil.bcl]Ns.Type::'value'(){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_mv'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call int32() {{\[}}msil.bcl]Ns.Type::'value'{{$}}
declare i32 @__msil_mc.m_mv() #8
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_mi'() cil managed
; WP:          {{^}}  call int32 {{\[}}mscorlib]Ns.Type::'int32'(){{$}}
; OF:          {{^}}  call int32 {{\[}}msil.bcl]Ns.Type::'int32'(){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_mi'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call int32() {{\[}}msil.bcl]Ns.Type::'int32'{{$}}
declare i32 @__msil_mc.m_mi() #9

; A nested type whose inner name is a reserved word: only that part is
; quoted, in the owner and in the signature, and in whole-program text in
; the operands of the enum check made at run time ('ldtoken' and 'sizeof'
; of E), whose T-M7 text names E as the declaration spells it.
; CHECK-LABEL: .method assembly static void '__msil_mc.m_nv'(int32) cil managed
; WP:          {{^}}  ldtoken {{\[}}mscorlib]Ns.Outer/'value'{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}mscorlib]Ns.Outer/'value'{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_nv': parameter 1 passes 32 bits for 'Ns.Outer/value', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; WP:          {{^}}  call void {{\[}}mscorlib]Ns.Outer/'value'::Run(valuetype {{\[}}mscorlib]Ns.Outer/'value'){{$}}
; OF:          {{^}}  call void {{\[}}msil.bcl]Ns.Outer/'value'::Run(valuetype {{\[}}msil.bcl]Ns.Outer/'value'){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_nv'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call void(valuetype {{\[}}msil.bcl]Ns.Outer/value) {{\[}}msil.bcl]Ns.Outer/value::'Run'{{$}}
declare void @__msil_mc.m_nv(i32 noundef) #10

; '.ctor' stays bare. In whole-program text the 'castclass class T' that
; follows a class's 'newobj' (the run-time kind check of the constructor's
; TYPE) takes the scope too; object-form text has none.
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_ctor'() cil managed
; WP:          {{^}}  newobj instance void {{\[}}'my lib']Ns.Widget::.ctor(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}'my lib']Ns.Widget{{$}}
; OF:          {{^}}  newobj instance void {{\[}}msil.bcl]Ns.Widget::.ctor(){{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_ctor'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: newobj instance void() {{\[}}msil.bcl]Ns.Widget::'.ctor'{{$}}
declare ptr @__msil_mc.m_ctor() #11

; A catch list ("msil-managed-catch", the trailing 'int *' last): the
; filter's 'isinst' of each listed type takes the scope, in list order.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_check'(int32, native int) cil managed
; WP:          {{^}}  call int32 {{\[}}'my lib']Ns.Widget::Check(int32){{$}}
; OF:          {{^}}  call int32 {{\[}}msil.bcl]Ns.Widget::Check(int32){{$}}
; CHECK:       {{^  \} filter \{$}}
; WP:          {{^}}  isinst {{\[}}'my lib']Ns.WidgetException{{$}}
; OF:          {{^}}  isinst {{\[}}msil.bcl]Ns.WidgetException{{$}}
; WP:          {{^}}  isinst {{\[}}'my lib']Ns.Widget/Broken{{$}}
; OF:          {{^}}  isinst {{\[}}msil.bcl]Ns.Widget/Broken{{$}}
; CHECK:       {{^}}  endfilter{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_check'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call int32(int32) {{\[}}msil.bcl]Ns.Widget::'Check'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: isinst {{\[}}msil.bcl]Ns.WidgetException{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: isinst {{\[}}msil.bcl]Ns.Widget/Broken{{$}}
declare i32 @__msil_mc.m_check(i32 noundef, ptr noundef) #12

; A value type returned as a handle ('boxed'): the result type of the
; member's signature and the 'box' take the scope; the GCHandle rows keep
; [mscorlib].
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_point'() cil managed
; WP:          {{^}}  call valuetype {{\[}}'my lib']Ns.Point {{\[}}'my lib']Ns.Factory::MakePoint(){{$}}
; OF:          {{^}}  call valuetype {{\[}}msil.bcl]Ns.Point {{\[}}msil.bcl]Ns.Factory::MakePoint(){{$}}
; WP-NEXT:     {{^}}  box {{\[}}'my lib']Ns.Point{{$}}
; OF-NEXT:     {{^}}  box {{\[}}msil.bcl]Ns.Point{{$}}
; WP-NEXT:     {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; OF-NEXT:     {{^}}  call valuetype {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_point'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: call valuetype {{\[}}msil.bcl]Ns.Point() {{\[}}msil.bcl]Ns.Factory::'MakePoint'{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: box {{\[}}msil.bcl]Ns.Point{{$}}
declare ptr @__msil_mc.m_point() #13

; CHECK-LABEL: .method public static int32 'use_all'(int32, native int, native int) cil managed
; CHECK:       {{^}}  call int32 '__msil_mc.m_abs'(int32){{$}}
; CHECK:       {{^}}  call native int '__msil_mc.m_make'(native int){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.m_size'(native int){{$}}
; CHECK:       {{^}}  call native int '__msil_mc.m_newwidgets'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.m_kw'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.m_rt'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.m_brk'(int32){{$}}
; CHECK:       {{^}}  call void '__msil_mc.m_tv'(){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.m_mv'(){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.m_mi'(){{$}}
; CHECK:       {{^}}  call void '__msil_mc.m_nv'(int32){{$}}
; CHECK:       {{^}}  call native int '__msil_mc.m_ctor'(){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.m_check'(int32, native int){{$}}
; CHECK:       {{^}}  call native int '__msil_mc.m_point'(){{$}}
define i32 @use_all(i32 %x, ptr %h, ptr %err) {
  %a = call i32 @__msil_mc.m_abs(i32 noundef %x)
  %w = call ptr @__msil_mc.m_make(ptr noundef %h)
  %s = call i32 @__msil_mc.m_size(ptr noundef %w)
  %arr = call ptr @__msil_mc.m_newwidgets(i32 noundef %s)
  %k = call i32 @__msil_mc.m_kw(i32 noundef %a)
  %r = call i32 @__msil_mc.m_rt(i32 noundef %k)
  %b = call i32 @__msil_mc.m_brk(i32 noundef %r)
  call void @__msil_mc.m_tv()
  %v = call i32 @__msil_mc.m_mv()
  %i = call i32 @__msil_mc.m_mi()
  call void @__msil_mc.m_nv(i32 noundef %v)
  %c = call ptr @__msil_mc.m_ctor()
  %ck = call i32 @__msil_mc.m_check(i32 noundef %x, ptr noundef %err)
  %pt = call ptr @__msil_mc.m_point()
  %p = ptrtoint ptr %arr to i32
  %pc = ptrtoint ptr %c to i32
  %ppt = ptrtoint ptr %pt to i32
  %sum = add i32 %b, %p
  %sum2 = add i32 %sum, %i
  %sum3 = add i32 %sum2, %pc
  %sum4 = add i32 %sum3, %ck
  %sum5 = add i32 %sum4, %ppt
  ret i32 %sum5
}

attributes #0 = { "msil-managed-asm"="my lib" "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #1 = { "msil-managed-asm"="my lib" "msil-managed-conv"="obj;obj" "msil-managed-member"="Make" "msil-managed-sig"="class Ns.Widget(class Ns.Widget)" "msil-managed-type"="Ns.Factory" }
attributes #2 = { "msil-managed-asm"="my lib" "msil-managed-conv"="this-obj;val" "msil-managed-member"="get_Size" "msil-managed-sig"="instance int32()" "msil-managed-type"="Ns.Widget" }
attributes #3 = { "msil-managed-asm"="my lib" "msil-managed-conv"="val;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int32)" "msil-managed-type"="class Ns.Widget[]" }
attributes #4 = { "msil-managed-asm"="int32" "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #5 = { "msil-managed-asm"="System.Runtime" "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #6 = { "msil-managed-asm"="a]b" "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #7 = { "msil-managed-conv"=";void" "msil-managed-member"="Run" "msil-managed-sig"="void()" "msil-managed-type"="value" }
attributes #8 = { "msil-managed-conv"=";val" "msil-managed-member"="value" "msil-managed-sig"="int32()" "msil-managed-type"="Ns.Type" }
attributes #9 = { "msil-managed-conv"=";val" "msil-managed-member"="int32" "msil-managed-sig"="int32()" "msil-managed-type"="Ns.Type" }
attributes #10 = { "msil-managed-conv"="enum;void" "msil-managed-member"="Run" "msil-managed-sig"="void(valuetype Ns.Outer/value)" "msil-managed-type"="Ns.Outer/value" }
attributes #11 = { "msil-managed-asm"="my lib" "msil-managed-conv"=";obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void()" "msil-managed-type"="Ns.Widget" }
attributes #12 = { "msil-managed-asm"="my lib" "msil-managed-catch"="Ns.WidgetException=5;Ns.Widget/Broken=22" "msil-managed-conv"="val,err;val" "msil-managed-member"="Check" "msil-managed-sig"="int32(int32)" "msil-managed-type"="Ns.Widget" }
attributes #13 = { "msil-managed-asm"="my lib" "msil-managed-conv"=";boxed" "msil-managed-member"="MakePoint" "msil-managed-sig"="valuetype Ns.Point()" "msil-managed-type"="Ns.Factory" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12, !13}
!0 = !{!"__msil_mc.m_abs", !"System.Math", !"Abs", !"int32(int32)", !"my lib", !"val;val", !""}
!1 = !{!"__msil_mc.m_make", !"Ns.Factory", !"Make", !"class Ns.Widget(class Ns.Widget)", !"my lib", !"obj;obj", !""}
!2 = !{!"__msil_mc.m_size", !"Ns.Widget", !"get_Size", !"instance int32()", !"my lib", !"this-obj;val", !""}
!3 = !{!"__msil_mc.m_newwidgets", !"class Ns.Widget[]", !".ctor", !"instance void(int32)", !"my lib", !"val;obj", !""}
!4 = !{!"__msil_mc.m_kw", !"System.Math", !"Abs", !"int32(int32)", !"int32", !"val;val", !""}
!5 = !{!"__msil_mc.m_rt", !"System.Math", !"Abs", !"int32(int32)", !"System.Runtime", !"val;val", !""}
!6 = !{!"__msil_mc.m_brk", !"System.Math", !"Abs", !"int32(int32)", !"a]b", !"val;val", !""}
!7 = !{!"__msil_mc.m_tv", !"value", !"Run", !"void()", !"", !";void", !""}
!8 = !{!"__msil_mc.m_mv", !"Ns.Type", !"value", !"int32()", !"", !";val", !""}
!9 = !{!"__msil_mc.m_mi", !"Ns.Type", !"int32", !"int32()", !"", !";val", !""}
!10 = !{!"__msil_mc.m_nv", !"Ns.Outer/value", !"Run", !"void(valuetype Ns.Outer/value)", !"", !"enum;void", !""}
!11 = !{!"__msil_mc.m_ctor", !"Ns.Widget", !".ctor", !"instance void()", !"my lib", !";obj", !""}
!12 = !{!"__msil_mc.m_check", !"Ns.Widget", !"Check", !"int32(int32)", !"my lib", !"val,err;val", !"Ns.WidgetException=5;Ns.Widget/Broken=22"}
!13 = !{!"__msil_mc.m_point", !"Ns.Factory", !"MakePoint", !"valuetype Ns.Point()", !"my lib", !";boxed", !""}

;--- utf8.ll
source_filename = "managed-call-scope-utf8.c"

declare void @__msil_mc.m_cafe() #0

define void @use_cafe() {
  call void @__msil_mc.m_cafe()
  ret void
}

attributes #0 = { "msil-managed-asm"="Caf\C3\A9" "msil-managed-conv"=";void" "msil-managed-member"="R\C3\A9" "msil-managed-sig"="void()" "msil-managed-type"="Ns.Caf\C3\A9" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_cafe", !"Ns.Caf\C3\A9", !"R\C3\A9", !"void()", !"Caf\C3\A9", !";void", !""}
