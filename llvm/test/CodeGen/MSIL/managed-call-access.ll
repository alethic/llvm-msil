; The member access of a managed-call stub whose 'this' is a value type, and
; of a static method (ImplementationPlan.md 5.20.3 MS5, 5.20.5 ML2 (2) and
; 5.20.7 MK2 as amended again in section 12 on 2026-11-04, the row '5.20.3
; MS5, 5.20.5 ML2 (2) and 5.20.7 MK2 amended again', refined for text by the
; row '5.20.5 ML2 (2) and 5.20.6 MW2 amended; refines (d) ...', and for
; static methods by MC-1's review-r5 item 1; milestone MC-1). The access of
; a 'this-val' or 'this-gch' instance METHOD, and of a static method, depends
; on the bound member's declaring type, which the IR attribute
; "msil-managed-decl" (ML1's cli::ManagedDeclAttr) carries when msil-ld's LTO
; code generation has bound the member:
; - (a) Without the attribute (clang -c, clang -S, llc) the object and the
;   object-form text (OF, the text of an object, which msil-ld links) have the
;   placeholder 'constrained. V' on the line before 'callvirt instance <sig>
;   V::M', which msil-ld rewrites after binding (m_ticks: DateTime::get_Ticks,
;   which V declares; m_fthash: ComTypes.FILETIME::GetHashCode, which V only
;   inherits; m_free: the 'this-gch' GCHandle::Free; m_dtstr, m_dteq:
;   DateTime's ToString and Equals(object); m_gtype: the 'this-gch'
;   GCHandle::GetType). Whole-program text (WP, standalone: no msil-ld
;   rewrites it) never has the placeholder: for one of System.Object's four
;   public instance methods with its own signature (bool Equals(object),
;   int32 GetHashCode(), string ToString(), class System.Type GetType()) it
;   is 'constrained. V' then 'callvirt instance <sig>
;   [mscorlib]System.Object::M' whether or not V overrides M (m_fthash,
;   m_dtstr, m_dteq, m_gtype), and otherwise 'call instance <sig> V::M'
;   (m_ticks, m_free), with no 'constrained.'.
; - (b) With "msil-managed-decl" equal to V's own full name, the final form
;   'call instance <sig> V::M', with no 'constrained.' (d_ticks; d_free, the
;   'this-gch' GCHandle::Free), in every output form.
; - (c) With "msil-managed-decl" naming System.ValueType, System.Object or
;   System.Enum (V only inherits M), 'constrained. V', then 'callvirt
;   instance <sig> D::M' of a member whose parent is D, not V (d_fthash:
;   FILETIME::GetHashCode from System.ValueType; d_fttype: FILETIME::GetType
;   from System.Object, with an 'obj' result; d_mhash: FileMode::GetHashCode
;   from System.Enum; d_gtype: the 'this-gch' GCHandle::GetType from
;   System.Object), in every output form.
; - (d) A 'this-obj' method keeps 'callvirt V::M' (m_len, System.Array's
;   get_Length), and a field access keeps 'ldfld' (m_stamp, a 'this-val'
;   instance field of Ns.Stamp); neither has a 'constrained.' prefix.
; - (e) A static method that the value type V only inherits is not found
;   through V at run time (MissingMethodException), so "msil-managed-decl"
;   naming System.ValueType, System.Object or System.Enum makes the access
;   'call <sig> D::M' of a member whose parent is D's TypeRef, in every
;   output form ('[mscorlib]D::M' in whole-program text, '[msil.bcl]D::M' in
;   object-form text and objects), whether TYPE is written 'valuetype V' or
;   'V': ds_names (System.Enum's GetNames(class System.Type) for TYPE
;   'valuetype System.DayOfWeek'), ds_names_ref (the same for TYPE
;   'System.DayOfWeek') and ds_refeq (System.Object's
;   ReferenceEquals(object, object) for 'valuetype System.Guid'). TYPE's own
;   name (ds_newguid: "System.Guid" for 'valuetype System.Guid' and
;   Guid::NewGuid) and no attribute (ms_names: DayOfWeek::GetNames) leave
;   'call V::M' unchanged: '[mscorlib]V::M' in whole-program text, and the
;   TypeSpec parent 'valuetype [msil.bcl]V' of a TYPE written 'valuetype V'
;   in object-form text and objects (managed-call-typespec.ll). The
;   attribute's errors (on a field or a 'this-obj' method, and a declaring
;   type of a static method that is none of these) are managed-call-errors.ll's
;   (decl.ll).
; The stub-internal GCHandle rows that the conversions use (FromIntPtr,
; get_Target, Alloc, ToIntPtr) keep 'call' in every case. A 'this-obj' or
; 'this-val' handle of 0 is null (5.20.9 MH1 as built in section 12 on
; 2026-11-04): the dereference ends at the join label of the null test
; (managed-call-bodies.ll pins the whole test), after 'castclass V' for
; 'this-obj' and before 'unbox V' for 'this-val'; a 'this-gch' handle goes
; to GCHandle::FromIntPtr directly. In whole-program text, which no link
; checks, that 'castclass' (m_len) and every 'this-val' stub's 'unbox' name
; TYPE as a TypeSpec of the declared kind ('class [mscorlib]System.Array',
; 'valuetype [mscorlib]V'), so a TYPE of the other kind fails to load at
; the stub's first call (T-M16's check at run time); object-form text and
; objects keep the TypeRef, which msil-ld checks. 'constrained. V' keeps its
; TypeRef in every form.
; Whole-program text (CORE=mscorlib) and object-form text (CORE=msil.bcl) on
; both host x86_64 triples and msil64-unknown-dotnet at -O0 and -O2; the
; whole-program text assembles. The objects, through llvm-cil-dump: the same
; accesses as the object-form text, with 'constrained.' taking a TypeRef to
; V and the MemberRef's parent a TypeRef to V or to D (or, for a static
; method of a TYPE written 'valuetype V' that keeps 'V::M', the TypeSpec
; 'valuetype V'); and the object oracle
; (5.12.6): llc's object and the object that ilasm makes from the
; object-form text have the same canonical listing and link info, which
; proves the encoding of the 'constrained.' prefix (FE 16 and a TypeRef
; token: the 6 bytes that msil-ld's rewrite replaces with nops) and of the
; 'call' and 'callvirt' sites. So llc's whole-program text and llc's object
; of a case (a) stub differ by design at the access.
; ilasm writes a member of System.ValueType, System.Object or System.Enum into
; mscorlib, whatever scope the text names (an assembly reference it
; autodetects, as for System.String in managed-call-bodies.ll), so the oracle
; maps the ilasm side's '[mscorlib]D::' back to '[msil.bcl]D::' and drops that
; reference; llc's object must name [msil.bcl] (DUMP).
; The int64 results (m_ticks, d_ticks, m_stamp) end 'conv.i; ret', pinned
; line by line, because a C 64-bit integer is native int in the stub's
; signature (D2) and the user's mc1-int64 pick says "Fix it inside MC-1:
; conv.i8 on int64 arguments, conv.i on int64 results".
; Not pinned: .maxstack values and label names.

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,WP -DCORE=mscorlib --input-file=%t.wp.%{t}%{o}.il && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,OF -DCORE=msil.bcl --input-file=%t.of.%{t}%{o}.il

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{il}
; REDEFINE: %{o} = -O0
; RUN: %{il}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.lnx.dll %t.wp.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.win.dll %t.wp.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.p64.dll %t.wp.msil64-unknown-dotnet-O2.il %}

; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=DUMP
; DEFINE: %{oracle} = %ilasm-verify -output=%t.of.%{t}%{o}.ilasm.o %t.of.%{t}%{o}.il && \
; DEFINE:   %{canon} %t.%{t}%{o}.o | sed -e "/^pe /d" > %t.%{t}%{o}.canon1 && \
; DEFINE:   %{canon} %t.of.%{t}%{o}.ilasm.o | sed -e "/^pe /d" -e "/^assemblyref 'mscorlib' /d" \
; DEFINE:     -e "s/\[mscorlib\]System\.ValueType::/[msil.bcl]System.ValueType::/g" \
; DEFINE:     -e "s/\[mscorlib\]System\.Object::/[msil.bcl]System.Object::/g" \
; DEFINE:     -e "s/\[mscorlib\]System\.Enum::/[msil.bcl]System.Enum::/g" > %t.%{t}%{o}.canon2 && \
; DEFINE:   diff %t.%{t}%{o}.canon1 %t.%{t}%{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t.%{t}%{o}.o > %t.%{t}%{o}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t.of.%{t}%{o}.ilasm.o > %t.%{t}%{o}.link2 && \
; DEFINE:   diff %t.%{t}%{o}.link1 %t.%{t}%{o}.link2
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

source_filename = "managed-call-access.c"

; (a) No "msil-managed-decl": the final forms in whole-program text (WP), the
; placeholder in object-form text (OF).
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_ticks'(native int) cil managed
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]System.DateTime{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]System.DateTime{{$}}
; WP-NEXT:     {{^}}  call instance int64 {{\[}}[[CORE]]]System.DateTime::get_Ticks(){{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}[[CORE]]]System.DateTime{{$}}
; OF-NEXT:     {{^}}  callvirt instance int64 {{\[}}[[CORE]]]System.DateTime::get_Ticks(){{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_ticks'{{$}}
declare i64 @__msil_mc.m_ticks(ptr noundef) #0

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_fthash'(native int) cil managed
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; CHECK-NEXT:  {{^}}  constrained. {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; WP-NEXT:     {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Object::GetHashCode(){{$}}
; OF-NEXT:     {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME::GetHashCode(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_fthash'{{$}}
declare i32 @__msil_mc.m_fthash(ptr noundef) #1

; CHECK-LABEL: .method assembly static void '__msil_mc.m_free'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; WP-NEXT:     {{^}}  call instance void {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Free(){{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; OF-NEXT:     {{^}}  callvirt instance void {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Free(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_free'{{$}}
declare void @__msil_mc.m_free(ptr noundef) #2

; ToString and Equals(object), which DateTime overrides: System.Object's
; members in whole-program text all the same.
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_dtstr'(native int) cil managed
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]System.DateTime{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]System.DateTime{{$}}
; CHECK-NEXT:  {{^}}  constrained. {{\[}}[[CORE]]]System.DateTime{{$}}
; WP-NEXT:     {{^}}  callvirt instance string {{\[}}[[CORE]]]System.Object::ToString(){{$}}
; OF-NEXT:     {{^}}  callvirt instance string {{\[}}[[CORE]]]System.DateTime::ToString(){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_dtstr'{{$}}
declare ptr @__msil_mc.m_dtstr(ptr noundef) #11

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_dteq'(native int, native int) cil managed
; WP:          {{^}}  unbox valuetype {{\[}}[[CORE]]]System.DateTime{{$}}
; OF:          {{^}}  unbox {{\[}}[[CORE]]]System.DateTime{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  constrained. {{\[}}[[CORE]]]System.DateTime{{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}[[CORE]]]System.Object::Equals(object){{$}}
; OF-NEXT:     {{^}}  callvirt instance bool {{\[}}[[CORE]]]System.DateTime::Equals(object){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_dteq'{{$}}
declare zeroext i1 @__msil_mc.m_dteq(ptr noundef, ptr noundef) #12

; GetType on a 'this-gch' GCHandle (which GCHandle only inherits).
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_gtype'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  constrained. {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; WP-NEXT:     {{^}}  callvirt instance class {{\[}}[[CORE]]]System.Type {{\[}}[[CORE]]]System.Object::GetType(){{$}}
; OF-NEXT:     {{^}}  callvirt instance class {{\[}}[[CORE]]]System.Type {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::GetType(){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_gtype'{{$}}
declare ptr @__msil_mc.m_gtype(ptr noundef) #13

; (b) "msil-managed-decl" is V's own full name: 'call V::M'.
; CHECK-LABEL: .method assembly static native int '__msil_mc.d_ticks'(native int) cil managed
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]System.DateTime{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]System.DateTime{{$}}
; CHECK-NEXT:  {{^}}  call instance int64 {{\[}}[[CORE]]]System.DateTime::get_Ticks(){{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.d_ticks'{{$}}
declare i64 @__msil_mc.d_ticks(ptr noundef) #3

; CHECK-LABEL: .method assembly static void '__msil_mc.d_free'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance void {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Free(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.d_free'{{$}}
declare void @__msil_mc.d_free(ptr noundef) #4

; (c) "msil-managed-decl" names the base type D that declares M:
; 'constrained. V', then 'callvirt D::M'.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.d_fthash'(native int) cil managed
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; CHECK-NEXT:  {{^}}  constrained. {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.ValueType::GetHashCode(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.d_fthash'{{$}}
declare i32 @__msil_mc.d_fthash(ptr noundef) #5

; CHECK-LABEL: .method assembly static native int '__msil_mc.d_fttype'(native int) cil managed
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; CHECK-NEXT:  {{^}}  constrained. {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance class {{\[}}[[CORE]]]System.Type {{\[}}[[CORE]]]System.Object::GetType(){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.d_fttype'{{$}}
declare ptr @__msil_mc.d_fttype(ptr noundef) #6

; CHECK-LABEL: .method assembly static int32 '__msil_mc.d_mhash'(native int) cil managed
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]System.IO.FileMode{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]System.IO.FileMode{{$}}
; CHECK-NEXT:  {{^}}  constrained. {{\[}}[[CORE]]]System.IO.FileMode{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Enum::GetHashCode(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.d_mhash'{{$}}
declare i32 @__msil_mc.d_mhash(ptr noundef) #7

; CHECK-LABEL: .method assembly static native int '__msil_mc.d_gtype'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  constrained. {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance class {{\[}}[[CORE]]]System.Type {{\[}}[[CORE]]]System.Object::GetType(){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.d_gtype'{{$}}
declare ptr @__msil_mc.d_gtype(ptr noundef) #8

; (d) 'this-obj' keeps 'callvirt V::M'; a field keeps 'ldfld'.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_len'(native int) cil managed
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}[[CORE]]]System.Array{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}[[CORE]]]System.Array{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Array::get_Length(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_len'{{$}}
declare i32 @__msil_mc.m_len(ptr noundef) #9

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_stamp'(native int) cil managed
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]Ns.Stamp{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]Ns.Stamp{{$}}
; CHECK-NEXT:  {{^}}  ldfld int64 {{\[}}[[CORE]]]Ns.Stamp::Ticks{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_stamp'{{$}}
declare i64 @__msil_mc.m_stamp(ptr noundef) #10

; (e) Static methods. "msil-managed-decl" names System.Enum or System.Object:
; 'call D::M', whose parent is D's TypeRef, whether TYPE is written
; 'valuetype V' or 'V'.
; CHECK-LABEL: .method assembly static native int '__msil_mc.ds_names'(native int) cil managed
; CHECK:       {{^}}  castclass {{\[}}[[CORE]]]System.Type{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  call string[] {{\[}}[[CORE]]]System.Enum::GetNames(class {{\[}}[[CORE]]]System.Type){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.ds_names'{{$}}
declare ptr @__msil_mc.ds_names(ptr noundef) #14

; CHECK-LABEL: .method assembly static native int '__msil_mc.ds_names_ref'(native int) cil managed
; CHECK:       {{^}}  castclass {{\[}}[[CORE]]]System.Type{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  call string[] {{\[}}[[CORE]]]System.Enum::GetNames(class {{\[}}[[CORE]]]System.Type){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.ds_names_ref'{{$}}
declare ptr @__msil_mc.ds_names_ref(ptr noundef) #15

; CHECK-LABEL: .method assembly static int32 '__msil_mc.ds_refeq'(native int, native int) cil managed
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  call bool {{\[}}[[CORE]]]System.Object::ReferenceEquals(object, object){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.ds_refeq'{{$}}
declare zeroext i1 @__msil_mc.ds_refeq(ptr noundef, ptr noundef) #16

; TYPE's own name, and no attribute: 'call V::M' as without "msil-managed-decl",
; with the TypeSpec parent 'valuetype V' in object-form text.
; CHECK-LABEL: .method assembly static native int '__msil_mc.ds_newguid'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  call valuetype {{\[}}mscorlib]System.Guid {{\[}}mscorlib]System.Guid::NewGuid(){{$}}
; OF-NEXT:     {{^}}  call valuetype {{\[}}msil.bcl]System.Guid valuetype {{\[}}msil.bcl]System.Guid::NewGuid(){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Guid{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.ds_newguid'{{$}}
declare ptr @__msil_mc.ds_newguid() #17

; CHECK-LABEL: .method assembly static native int '__msil_mc.ms_names'(native int) cil managed
; CHECK:       {{^}}  castclass {{\[}}[[CORE]]]System.Type{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  call string[] {{\[}}mscorlib]System.DayOfWeek::GetNames(class {{\[}}mscorlib]System.Type){{$}}
; OF-NEXT:     {{^}}  call string[] valuetype {{\[}}msil.bcl]System.DayOfWeek::GetNames(class {{\[}}msil.bcl]System.Type){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.ms_names'{{$}}
declare ptr @__msil_mc.ms_names(ptr noundef) #18

; CHECK-LABEL: .method public static int32 'use_access'(native int) cil managed
; CHECK:       call native int '__msil_mc.m_ticks'(native int)
; CHECK:       call native int '__msil_mc.m_stamp'(native int)
define i32 @use_access(ptr %h) {
  %a = call i64 @__msil_mc.m_ticks(ptr noundef %h)
  %b = call i32 @__msil_mc.m_fthash(ptr noundef %h)
  call void @__msil_mc.m_free(ptr noundef %h)
  %c = call i64 @__msil_mc.d_ticks(ptr noundef %h)
  call void @__msil_mc.d_free(ptr noundef %h)
  %d = call i32 @__msil_mc.d_fthash(ptr noundef %h)
  %e = call ptr @__msil_mc.d_fttype(ptr noundef %h)
  %f = call i32 @__msil_mc.d_mhash(ptr noundef %h)
  %g = call ptr @__msil_mc.d_gtype(ptr noundef %h)
  %i = call i32 @__msil_mc.m_len(ptr noundef %h)
  %j = call i64 @__msil_mc.m_stamp(ptr noundef %h)
  %k = call ptr @__msil_mc.m_dtstr(ptr noundef %h)
  %l = call zeroext i1 @__msil_mc.m_dteq(ptr noundef %h, ptr noundef %h)
  %m = call ptr @__msil_mc.m_gtype(ptr noundef %h)
  %n = call ptr @__msil_mc.ds_names(ptr noundef %h)
  %o = call ptr @__msil_mc.ds_names_ref(ptr noundef %h)
  %p = call zeroext i1 @__msil_mc.ds_refeq(ptr noundef %h, ptr noundef %h)
  %q = call ptr @__msil_mc.ds_newguid()
  %s = call ptr @__msil_mc.ms_names(ptr noundef %h)
  %r1 = add i32 %b, %d
  %r2 = add i32 %r1, %f
  %r3 = add i32 %r2, %i
  ret i32 %r3
}

; Objects: the same accesses; 'constrained.' takes a TypeRef to V, and the
; MemberRef's parent is V (placeholder and case (b)) or D (case (c)).
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_ticks' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance int64() [msil.bcl]System.DateTime::'get_Ticks'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_fthash' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance int32() [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME::'GetHashCode'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_free' void(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance void() [msil.bcl]System.Runtime.InteropServices.GCHandle::'Free'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_dtstr' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance string() [msil.bcl]System.DateTime::'ToString'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_dteq' int32(native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance bool(object) [msil.bcl]System.DateTime::'Equals'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_gtype' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance class [msil.bcl]System.Type() [msil.bcl]System.Runtime.InteropServices.GCHandle::'GetType'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.d_ticks' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call instance int64() [msil.bcl]System.DateTime::'get_Ticks'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.d_free' void(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call instance void() [msil.bcl]System.Runtime.InteropServices.GCHandle::'Free'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.d_fthash' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance int32() [msil.bcl]System.ValueType::'GetHashCode'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.d_fttype' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance class [msil.bcl]System.Type() [msil.bcl]System.Object::'GetType'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.d_mhash' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.IO.FileMode{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.IO.FileMode{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance int32() [msil.bcl]System.Enum::'GetHashCode'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.d_gtype' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance class [msil.bcl]System.Type() [msil.bcl]System.Object::'GetType'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_len' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: castclass [msil.bcl]System.Array{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance int32() [msil.bcl]System.Array::'get_Length'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_stamp' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]Ns.Stamp{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldfld int64 [msil.bcl]Ns.Stamp::'Ticks'{{$}}
; Static methods: D's TypeRef as the parent (case (e)), and the TypeSpec
; 'valuetype V' for TYPE's own name and without the attribute.
; DUMP-LABEL: {{^}}  .method '__msil_mc.ds_names' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: castclass [msil.bcl]System.Type{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call string[](class [msil.bcl]System.Type) [msil.bcl]System.Enum::'GetNames'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.ds_names_ref' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: castclass [msil.bcl]System.Type{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call string[](class [msil.bcl]System.Type) [msil.bcl]System.Enum::'GetNames'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.ds_refeq' int32(native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call bool(object, object) [msil.bcl]System.Object::'ReferenceEquals'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldc.i4.0{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.ds_newguid' native int() flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: call valuetype [msil.bcl]System.Guid() valuetype [msil.bcl]System.Guid::'NewGuid'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: box [msil.bcl]System.Guid{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.ms_names' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: castclass [msil.bcl]System.Type{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call string[](class [msil.bcl]System.Type) valuetype [msil.bcl]System.DayOfWeek::'GetNames'{{$}}
; DUMP-LABEL: {{^}}  .method 'use_access' int32(native int) flags=

attributes #0 = { "msil-managed-conv"="this-val;val" "msil-managed-member"="get_Ticks" "msil-managed-sig"="instance int64()" "msil-managed-type"="valuetype System.DateTime" }
attributes #1 = { "msil-managed-conv"="this-val;val" "msil-managed-member"="GetHashCode" "msil-managed-sig"="instance int32()" "msil-managed-type"="valuetype System.Runtime.InteropServices.ComTypes.FILETIME" }
attributes #2 = { "msil-managed-conv"="this-gch;void" "msil-managed-member"="Free" "msil-managed-sig"="instance void()" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #3 = { "msil-managed-conv"="this-val;val" "msil-managed-decl"="System.DateTime" "msil-managed-member"="get_Ticks" "msil-managed-sig"="instance int64()" "msil-managed-type"="valuetype System.DateTime" }
attributes #4 = { "msil-managed-conv"="this-gch;void" "msil-managed-decl"="System.Runtime.InteropServices.GCHandle" "msil-managed-member"="Free" "msil-managed-sig"="instance void()" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #5 = { "msil-managed-conv"="this-val;val" "msil-managed-decl"="System.ValueType" "msil-managed-member"="GetHashCode" "msil-managed-sig"="instance int32()" "msil-managed-type"="valuetype System.Runtime.InteropServices.ComTypes.FILETIME" }
attributes #6 = { "msil-managed-conv"="this-val;obj" "msil-managed-decl"="System.Object" "msil-managed-member"="GetType" "msil-managed-sig"="instance class System.Type()" "msil-managed-type"="valuetype System.Runtime.InteropServices.ComTypes.FILETIME" }
attributes #7 = { "msil-managed-conv"="this-val;val" "msil-managed-decl"="System.Enum" "msil-managed-member"="GetHashCode" "msil-managed-sig"="instance int32()" "msil-managed-type"="valuetype System.IO.FileMode" }
attributes #8 = { "msil-managed-conv"="this-gch;obj" "msil-managed-decl"="System.Object" "msil-managed-member"="GetType" "msil-managed-sig"="instance class System.Type()" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #9 = { "msil-managed-conv"="this-obj;val" "msil-managed-member"="get_Length" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.Array" }
attributes #10 = { "msil-managed-conv"="this-val;val" "msil-managed-member"="Ticks" "msil-managed-sig"="instance field int64" "msil-managed-type"="valuetype Ns.Stamp" }
attributes #11 = { "msil-managed-conv"="this-val;obj" "msil-managed-member"="ToString" "msil-managed-sig"="instance string()" "msil-managed-type"="valuetype System.DateTime" }
attributes #12 = { "msil-managed-conv"="this-val,obj;bool" "msil-managed-member"="Equals" "msil-managed-sig"="instance bool(object)" "msil-managed-type"="valuetype System.DateTime" }
attributes #13 = { "msil-managed-conv"="this-gch;obj" "msil-managed-member"="GetType" "msil-managed-sig"="instance class System.Type()" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #14 = { "msil-managed-conv"="obj;obj" "msil-managed-decl"="System.Enum" "msil-managed-member"="GetNames" "msil-managed-sig"="string[](class System.Type)" "msil-managed-type"="valuetype System.DayOfWeek" }
attributes #15 = { "msil-managed-conv"="obj;obj" "msil-managed-decl"="System.Enum" "msil-managed-member"="GetNames" "msil-managed-sig"="string[](class System.Type)" "msil-managed-type"="System.DayOfWeek" }
attributes #16 = { "msil-managed-conv"="obj,obj;bool" "msil-managed-decl"="System.Object" "msil-managed-member"="ReferenceEquals" "msil-managed-sig"="bool(object, object)" "msil-managed-type"="valuetype System.Guid" }
attributes #17 = { "msil-managed-conv"=";boxed" "msil-managed-decl"="System.Guid" "msil-managed-member"="NewGuid" "msil-managed-sig"="valuetype System.Guid()" "msil-managed-type"="valuetype System.Guid" }
attributes #18 = { "msil-managed-conv"="obj;obj" "msil-managed-member"="GetNames" "msil-managed-sig"="string[](class System.Type)" "msil-managed-type"="valuetype System.DayOfWeek" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12, !13, !14, !15, !16, !17, !18}
!0 = !{!"__msil_mc.m_ticks", !"valuetype System.DateTime", !"get_Ticks", !"instance int64()", !"", !"this-val;val", !""}
!1 = !{!"__msil_mc.m_fthash", !"valuetype System.Runtime.InteropServices.ComTypes.FILETIME", !"GetHashCode", !"instance int32()", !"", !"this-val;val", !""}
!2 = !{!"__msil_mc.m_free", !"valuetype System.Runtime.InteropServices.GCHandle", !"Free", !"instance void()", !"", !"this-gch;void", !""}
!3 = !{!"__msil_mc.d_ticks", !"valuetype System.DateTime", !"get_Ticks", !"instance int64()", !"", !"this-val;val", !""}
!4 = !{!"__msil_mc.d_free", !"valuetype System.Runtime.InteropServices.GCHandle", !"Free", !"instance void()", !"", !"this-gch;void", !""}
!5 = !{!"__msil_mc.d_fthash", !"valuetype System.Runtime.InteropServices.ComTypes.FILETIME", !"GetHashCode", !"instance int32()", !"", !"this-val;val", !""}
!6 = !{!"__msil_mc.d_fttype", !"valuetype System.Runtime.InteropServices.ComTypes.FILETIME", !"GetType", !"instance class System.Type()", !"", !"this-val;obj", !""}
!7 = !{!"__msil_mc.d_mhash", !"valuetype System.IO.FileMode", !"GetHashCode", !"instance int32()", !"", !"this-val;val", !""}
!8 = !{!"__msil_mc.d_gtype", !"valuetype System.Runtime.InteropServices.GCHandle", !"GetType", !"instance class System.Type()", !"", !"this-gch;obj", !""}
!9 = !{!"__msil_mc.m_len", !"System.Array", !"get_Length", !"instance int32()", !"", !"this-obj;val", !""}
!10 = !{!"__msil_mc.m_stamp", !"valuetype Ns.Stamp", !"Ticks", !"instance field int64", !"", !"this-val;val", !""}
!11 = !{!"__msil_mc.m_dtstr", !"valuetype System.DateTime", !"ToString", !"instance string()", !"", !"this-val;obj", !""}
!12 = !{!"__msil_mc.m_dteq", !"valuetype System.DateTime", !"Equals", !"instance bool(object)", !"", !"this-val,obj;bool", !""}
!13 = !{!"__msil_mc.m_gtype", !"valuetype System.Runtime.InteropServices.GCHandle", !"GetType", !"instance class System.Type()", !"", !"this-gch;obj", !""}
!14 = !{!"__msil_mc.ds_names", !"valuetype System.DayOfWeek", !"GetNames", !"string[](class System.Type)", !"", !"obj;obj", !""}
!15 = !{!"__msil_mc.ds_names_ref", !"System.DayOfWeek", !"GetNames", !"string[](class System.Type)", !"", !"obj;obj", !""}
!16 = !{!"__msil_mc.ds_refeq", !"valuetype System.Guid", !"ReferenceEquals", !"bool(object, object)", !"", !"obj,obj;bool", !""}
!17 = !{!"__msil_mc.ds_newguid", !"valuetype System.Guid", !"NewGuid", !"valuetype System.Guid()", !"", !";boxed", !""}
!18 = !{!"__msil_mc.ms_names", !"valuetype System.DayOfWeek", !"GetNames", !"string[](class System.Type)", !"", !"obj;obj", !""}
