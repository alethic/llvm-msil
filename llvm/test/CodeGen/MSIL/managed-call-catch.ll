; The filter clause of managed-call stubs with "msil-managed-catch"
; (ImplementationPlan.md 5.20.8 MX1-MX3, 5.20.5 ML2 (2), 5.20.6 MW1;
; milestone MC-1). The whole stub body (argument conversions, access and
; result conversion) is one '.try' region, followed by one filter and one
; handler written through M27's emitExceptionClause:
; - the filter tests 'isinst <T_k>' for each 'msil_catch' entry in
;   declaration order (ORDER, DUMP), and stores CODE_k in a local on the
;   first match (the codes, CHECK-DAG); with no match it returns 0, so the
;   exception keeps propagating;
; - the handler is entered with the exception, which it pops, then stores
;   the code local through the trailing 'int *' parameter with 'stind.i4'
;   (ldarg.s 5 for m_fs_open's sixth C parameter, ldarg.1 for the others)
;   and leaves to a return of the zero value of the C return type, each
;   instruction pinned in order after the 'stind.i4': 'ldc.i4.0' then
;   'conv.i' for a null handle (m_fs_open), 'ldc.i4.0' alone for 0 (m_parse,
;   m_readbyte, m_parse16), 'ldc.i4.0' then 'conv.r8' for a float64 0.0
;   (m_parsed) and 'conv.r4' for a float32 0.0 (m_parsef), each stored in the
;   stub's result local, then the 'leave', after which that local is loaded
;   right before the 'ret' (in the objects the 'leave' targets that load);
;   for 'void' (m_delete) the 'leave' directly, to the 'ret';
; - the trailing 'int *' is not a CIL argument: m_fs_open's
;   FileStream::.ctor takes the first five C parameters (ldarg.1 to ldarg.s 4
;   after the 'utf8' conversion of the first);
; - the result conversion is inside the '.try': m_parse16's 'zeroext i16'
;   result of int16 Convert::ToInt16(string) (5.20.3 MS5's 'val' row as
;   built: an 8- or 16-bit result that the IR marks 'signext' or 'zeroext'
;   ends with 'conv.i1' or 'conv.i2', or 'conv.u1' or 'conv.u2') takes
;   'conv.u2' directly after the call and directly before the 'stloc' of the
;   result local, which the first instruction after the handler loads right
;   before the 'ret'; the stub returns int32, and its caller uses that int32
;   as the extended C value.
; A stub without "msil-managed-catch" has no clause (m_plain, MX1), and no
; filter names a catch-all or M27's ExternalException carrier (MX3).
; Whole-program text, which no link checks, also has the run-time checks of
; T-M7 and T-M16 (the WP lines; object-form text and objects have none, the
; OF lines): m_fs_open's three 'enum' parameters are checked before the
; '.try', so that no filter maps the InvalidProgramException that a check
; throws, without a local (the stub declares only its own locals), and the
; checks' labels come first ('.try' then starts at the stub's seventh
; label); inside the '.try', m_fs_open's constructed FileStream is cast to
; the TypeSpec 'class System.IO.FileStream' right after its 'newobj', and
; m_readbyte's 'this-obj' handle is cast to the TypeSpec 'class
; System.IO.Stream'.
; Whole-program text (CORE=mscorlib) and object-form text (CORE=msil.bcl) on
; both host x86_64 triples and msil64-unknown-dotnet at -O0 and -O2; the
; whole-program text assembles. The objects (msil64-unknown-dotnet at -O0 and
; -O2, each host triple at one level), through llvm-cil-dump: one
; clause per stub with a catch list, 'try L0 to L<a> filter L<a> handler L<b>
; to L<c>', the try from the first instruction to the filter, the handler
; from the 'pop' after the filter's last instruction 'endfilter' to the
; instruction after the handler's 'leave'; and the object oracle on the
; object-form text, which proves the written labels, the branch offsets of
; the filter and the clause offsets (both canonical listings drop their 'pe'
; line: on msil64-unknown-dotnet llc writes a PE32/I386 envelope, P-1a, while
; ilasm here always writes PE32+ x64). In the link-info symbol table each stub
; is a LOCAL FUNCTION symbol and never a GLOBAL or WEAK one (MW1 as amended).
; Not pinned: .maxstack values, the stubs' own locals' declarations and
; indices (each stub's result local is matched by pattern, then required
; again where it is loaded), labels in the text other than the enum check's,
; and the filter's branch shape.

; DEFINE: %{nots} = --implicit-check-not=ExternalException --implicit-check-not=SEHException \
; DEFINE:   --implicit-check-not="{{\]System\.(Object|Exception|SystemException)$}}" \
; DEFINE:   --implicit-check-not=.try --implicit-check-not=endfilter \
; DEFINE:   --implicit-check-not="LLVM ERROR"
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,WP -DCORE=mscorlib --input-file=%t.wp.%{t}%{o}.il %{nots} && \
; DEFINE:   FileCheck %s --check-prefix=ORDER -DCORE=mscorlib --input-file=%t.wp.%{t}%{o}.il && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,OF -DCORE=msil.bcl --input-file=%t.of.%{t}%{o}.il %{nots} && \
; DEFINE:   FileCheck %s --check-prefix=ORDER -DCORE=msil.bcl --input-file=%t.of.%{t}%{o}.il

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
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.p64.dll %t.wp.msil64-unknown-dotnet-O0.il %}

; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=DUMP --implicit-check-not=".try L" --implicit-check-not=endfilter && \
; DEFINE:   llvm-cil-dump --link-info %t.%{t}%{o}.o > %t.%{t}%{o}.symtab && \
; DEFINE:   FileCheck %s --check-prefix=LINK --input-file=%t.%{t}%{o}.symtab && \
; DEFINE:   grep -c __msil_mc %t.%{t}%{o}.symtab | FileCheck %s --check-prefix=NSTUB
; DEFINE: %{oracle} = %ilasm-verify -output=%t.of.%{t}%{o}.ilasm.o %t.of.%{t}%{o}.il && \
; DEFINE:   %{canon} %t.%{t}%{o}.o | sed -e "/^pe /d" > %t.%{t}%{o}.canon1 && \
; DEFINE:   %{canon} %t.of.%{t}%{o}.ilasm.o | sed -e "/^pe /d" -e "/^assemblyref 'mscorlib' /d" \
; DEFINE:     -e "s/\[mscorlib\]System\.String::/[msil.bcl]System.String::/g" \
; DEFINE:     -e "s/\[mscorlib\]System\.String$/[msil.bcl]System.String/" > %t.%{t}%{o}.canon2 && \
; DEFINE:   diff %t.%{t}%{o}.canon1 %t.%{t}%{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t.%{t}%{o}.o > %t.%{t}%{o}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t.of.%{t}%{o}.ilasm.o > %t.%{t}%{o}.link2 && \
; DEFINE:   diff %t.%{t}%{o}.link1 %t.%{t}%{o}.link2
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}

source_filename = "managed-call-catch.c"

; Six entries, two of them with the same code; the most derived type first.
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_fs_open'(native int, int32, int32, int32, int32, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; OF-NOT:      {{^  [a-z]}}
; OF:          {{^  \.try \{$}}
; WP-NEXT:     {{^  \.locals \(.+\)$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]System.IO.FileMode{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]System.IO.FileMode{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_fs_open': parameter 2 passes 32 bits for 'System.IO.FileMode', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]System.IO.FileAccess{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F]]_2{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]System.IO.FileAccess{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_3{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_2:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_fs_open': parameter 3 passes 32 bits for 'System.IO.FileAccess', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_3:{{$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]System.IO.FileShare{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F]]_4{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]System.IO.FileShare{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_5{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_4:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_fs_open': parameter 4 passes 32 bits for 'System.IO.FileShare', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_5:{{$}}
; WP-NEXT:     {{^  \.try \{$}}
; WP-NEXT:     {{^}}@MC[[F]]_6:{{$}}
; CHECK:       {{^}}  ldind.{{[iu]}}1{{$}}
; CHECK:       {{^}}  callvirt instance string {{\[}}[[CORE]]]System.Text.Encoding::GetString(uint8*, int32){{$}}
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.IO.FileStream::.ctor(string, valuetype {{\[}}[[CORE]]]System.IO.FileMode, valuetype {{\[}}[[CORE]]]System.IO.FileAccess, valuetype {{\[}}[[CORE]]]System.IO.FileShare, int32){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.IO.FileStream{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK:       {{^  \} filter \{$}}
; CHECK-DAG:   {{^}}  ldc.i4.2{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 20{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 13{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 36{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 84{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.5{{$}}
; CHECK:       {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK:       {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 5{{$}}
; CHECK-NEXT:  {{^}}  ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  stloc[[Z0:\.[0-3]|\.s [0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^}}  ldloc[[Z0]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_fs_open'{{$}}
declare ptr @__msil_mc.m_fs_open(ptr noundef, i32 noundef, i32 noundef, i32 noundef, i32 noundef, ptr noundef) #0

; A 'void' result.
; CHECK-LABEL: .method assembly static void '__msil_mc.m_delete'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^  \.try \{$}}
; CHECK:       {{^}}  call void {{\[}}[[CORE]]]System.IO.File::Delete(string){{$}}
; CHECK:       {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK:       {{^  \} filter \{$}}
; CHECK-DAG:   {{^}}  ldc.i4.5{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 13{{$}}
; CHECK:       {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK:       {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_delete'{{$}}
declare void @__msil_mc.m_delete(ptr noundef, ptr noundef) #1

; An int32 result, after the 'utf16' conversion.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_parse'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^  \.try \{$}}
; CHECK:       {{^}}  newobj instance void {{\[}}[[CORE]]]System.String::.ctor(char*){{$}}
; CHECK:       {{^}}  call int32 {{\[}}[[CORE]]]System.Int32::Parse(string){{$}}
; CHECK:       {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK:       {{^  \} filter \{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 22{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 34{{$}}
; CHECK:       {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK:       {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  stloc[[Z2:\.[0-3]|\.s [0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^}}  ldloc[[Z2]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_parse'{{$}}
declare i32 @__msil_mc.m_parse(ptr noundef, ptr noundef) #2

; A float64 result.
; CHECK-LABEL: .method assembly static float64 '__msil_mc.m_parsed'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^  \.try \{$}}
; CHECK:       {{^}}  call float64 {{\[}}[[CORE]]]System.Double::Parse(string){{$}}
; CHECK:       {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK:       {{^  \} filter \{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 22{{$}}
; CHECK:       {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK:       {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  stloc[[Z3:\.[0-3]|\.s [0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^}}  ldloc[[Z3]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_parsed'{{$}}
declare double @__msil_mc.m_parsed(ptr noundef, ptr noundef) #3

; A float32 result: its zero value is 'ldc.i4.0' then 'conv.r4'.
; CHECK-LABEL: .method assembly static float32 '__msil_mc.m_parsef'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^  \.try \{$}}
; CHECK:       {{^}}  call float32 {{\[}}[[CORE]]]System.Single::Parse(string){{$}}
; CHECK:       {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK:       {{^  \} filter \{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 22{{$}}
; CHECK:       {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK:       {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  stloc[[Z7:\.[0-3]|\.s [0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^}}  ldloc[[Z7]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_parsef'{{$}}
declare float @__msil_mc.m_parsef(ptr noundef, ptr noundef) #7

; An instance method ('this-obj'), the handle dereference inside the '.try'.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_readbyte'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^  \.try \{$}}
; CHECK:       {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}[[CORE]]]System.IO.Stream{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}[[CORE]]]System.IO.Stream{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.IO.Stream::ReadByte(){{$}}
; CHECK:       {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK:       {{^  \} filter \{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 9{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.5{{$}}
; CHECK:       {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK:       {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  stloc[[Z4:\.[0-3]|\.s [0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^}}  ldloc[[Z4]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_readbyte'{{$}}
declare i32 @__msil_mc.m_readbyte(ptr noundef, ptr noundef) #4

; A narrow result ('zeroext i16' for int16): 'conv.u2' directly after the
; call and directly before the 'stloc' of the result local (RES), the local
; the handler stores its 0 in and the first instruction after the handler
; loads right before the 'ret'.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_parse16'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^  \.try \{$}}
; CHECK:       {{^}}  ldind.{{[iu]}}1{{$}}
; CHECK:       {{^}}  callvirt instance string {{\[}}[[CORE]]]System.Text.Encoding::GetString(uint8*, int32){{$}}
; CHECK:       {{^}}  call int16 {{\[}}[[CORE]]]System.Convert::ToInt16(string){{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  stloc[[RES:\.[0-3]|\.s [0-9]+]]{{$}}
; CHECK:       {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK:       {{^  \} filter \{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 22{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 34{{$}}
; CHECK:       {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK:       {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  stloc[[RES]]{{$}}
; CHECK-NEXT:  {{^}}  leave{{(\.s)?}} {{[^ ]+$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NOT:   {{^  [a-z]}}
; CHECK:       {{^}}  ldloc[[RES]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_parse16'{{$}}
declare zeroext i16 @__msil_mc.m_parse16(ptr noundef, ptr noundef) #6

; No catch list: no clause.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_plain'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Math::Abs(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_plain'{{$}}
declare i32 @__msil_mc.m_plain(i32 noundef) #5

; CHECK-LABEL: .method public static int32 'use_all'(native int, native int, native int) cil managed
; CHECK:       call native int '__msil_mc.m_fs_open'(native int, int32, int32, int32, int32, native int)
; CHECK:       call void '__msil_mc.m_delete'(native int, native int)
; CHECK:       call int32 '__msil_mc.m_parse'(native int, native int)
; CHECK:       call float64 '__msil_mc.m_parsed'(native int, native int)
; CHECK:       call float32 '__msil_mc.m_parsef'(native int, native int)
; CHECK:       call int32 '__msil_mc.m_readbyte'(native int, native int)
; CHECK:       call int32 '__msil_mc.m_parse16'(native int, native int)
; CHECK:       call int32 '__msil_mc.m_plain'(int32)
define i32 @use_all(ptr %path, ptr %w, ptr %err) {
  %h = call ptr @__msil_mc.m_fs_open(ptr noundef %path, i32 noundef 3, i32 noundef 1, i32 noundef 1, i32 noundef 4096, ptr noundef %err)
  call void @__msil_mc.m_delete(ptr noundef %path, ptr noundef %err)
  %a = call i32 @__msil_mc.m_parse(ptr noundef %w, ptr noundef %err)
  %d = call double @__msil_mc.m_parsed(ptr noundef %path, ptr noundef %err)
  %f = call float @__msil_mc.m_parsef(ptr noundef %path, ptr noundef %err)
  %b = call i32 @__msil_mc.m_readbyte(ptr noundef %h, ptr noundef %err)
  %e = call zeroext i16 @__msil_mc.m_parse16(ptr noundef %path, ptr noundef %err)
  %c = call i32 @__msil_mc.m_plain(i32 noundef %b)
  %di = fptosi double %d to i32
  %fi = fptosi float %f to i32
  %ei = zext i16 %e to i32
  %r1 = add i32 %a, %c
  %r2 = add i32 %r1, %di
  %r3 = add i32 %r2, %ei
  %r4 = add i32 %r3, %fi
  ret i32 %r4
}

; The filters test the listed types in declaration order.
; ORDER-LABEL: '__msil_mc.m_fs_open'(
; ORDER:       {{^  \} filter \{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.IO.FileNotFoundException{{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.IO.DirectoryNotFoundException{{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.UnauthorizedAccessException{{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.IO.PathTooLongException{{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.Text.DecoderFallbackException{{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.IO.IOException{{$}}
; ORDER:       {{^}}  endfilter{{$}}
; ORDER-LABEL: '__msil_mc.m_delete'(
; ORDER:       {{^  \} filter \{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.IO.IOException{{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.UnauthorizedAccessException{{$}}
; ORDER:       {{^}}  endfilter{{$}}
; ORDER-LABEL: '__msil_mc.m_parse'(
; ORDER:       {{^  \} filter \{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.FormatException{{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.OverflowException{{$}}
; ORDER:       {{^}}  endfilter{{$}}
; ORDER-LABEL: '__msil_mc.m_parsed'(
; ORDER:       {{^  \} filter \{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.FormatException{{$}}
; ORDER:       {{^}}  endfilter{{$}}
; ORDER-LABEL: '__msil_mc.m_parsef'(
; ORDER:       {{^  \} filter \{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.FormatException{{$}}
; ORDER:       {{^}}  endfilter{{$}}
; ORDER-LABEL: '__msil_mc.m_readbyte'(
; ORDER:       {{^  \} filter \{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.ObjectDisposedException{{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.IO.IOException{{$}}
; ORDER:       {{^}}  endfilter{{$}}
; ORDER-LABEL: '__msil_mc.m_parse16'(
; ORDER:       {{^  \} filter \{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.FormatException{{$}}
; ORDER:       {{^}}  isinst {{\[}}[[CORE]]]System.OverflowException{{$}}
; ORDER:       {{^}}  endfilter{{$}}

; Objects: the clause table, the filter's TypeRefs and the handler's zero value.
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_fs_open' native int(native int, int32, int32, int32, int32, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.IO.FileNotFoundException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.IO.DirectoryNotFoundException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.UnauthorizedAccessException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.IO.PathTooLongException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.Text.DecoderFallbackException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.IO.IOException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:  {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldarg.s 5{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stind.i4{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: conv.i{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stloc[[Z:\.[0-3]|\.s [0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: leave{{(\.s)?}} L[[HE:[0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L[[HE]]: ldloc[[Z]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP:       {{^}}    .try L0 to L[[FB:[0-9]+]] filter L[[FB]] handler L[[HB]] to L[[HE]]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_delete' void(native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.IO.IOException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.UnauthorizedAccessException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:  {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldarg.1{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stind.i4{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: leave{{(\.s)?}} L[[HE:[0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L[[HE]]: ret{{$}}
; DUMP:       {{^}}    .try L0 to L[[FB:[0-9]+]] filter L[[FB]] handler L[[HB]] to L[[HE]]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_parse' int32(native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.FormatException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.OverflowException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:  {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldarg.1{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stind.i4{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stloc[[Z:\.[0-3]|\.s [0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: leave{{(\.s)?}} L[[HE:[0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L[[HE]]: ldloc[[Z]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP:       {{^}}    .try L0 to L[[FB:[0-9]+]] filter L[[FB]] handler L[[HB]] to L[[HE]]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_parsed' float64(native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.FormatException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:  {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldarg.1{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stind.i4{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: conv.r8{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stloc[[Z:\.[0-3]|\.s [0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: leave{{(\.s)?}} L[[HE:[0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L[[HE]]: ldloc[[Z]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP:       {{^}}    .try L0 to L[[FB:[0-9]+]] filter L[[FB]] handler L[[HB]] to L[[HE]]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_parsef' float32(native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call float32(string) [msil.bcl]System.Single::'Parse'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.FormatException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:  {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldarg.1{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stind.i4{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: conv.r4{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stloc[[Z:\.[0-3]|\.s [0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: leave{{(\.s)?}} L[[HE:[0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L[[HE]]: ldloc[[Z]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP:       {{^}}    .try L0 to L[[FB:[0-9]+]] filter L[[FB]] handler L[[HB]] to L[[HE]]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_readbyte' int32(native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.ObjectDisposedException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.IO.IOException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:  {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldarg.1{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stind.i4{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stloc[[Z:\.[0-3]|\.s [0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: leave{{(\.s)?}} L[[HE:[0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L[[HE]]: ldloc[[Z]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP:       {{^}}    .try L0 to L[[FB:[0-9]+]] filter L[[FB]] handler L[[HB]] to L[[HE]]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_parse16' int32(native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call int16(string) [msil.bcl]System.Convert::'ToInt16'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: conv.u2{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stloc[[RES:\.[0-3]|\.s [0-9]+]]{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.FormatException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: isinst [msil.bcl]System.OverflowException{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:  {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldarg.1{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldloc{{(\.[0-3]|\.s [0-9]+)$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stind.i4{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: stloc[[RES]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: leave{{(\.s)?}} L[[HE:[0-9]+]]{{$}}
; DUMP-NEXT:  {{^}}    L[[HE]]: ldloc[[RES]]{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP:       {{^}}    .try L0 to L[[FB:[0-9]+]] filter L[[FB]] handler L[[HB]] to L[[HE]]{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_plain' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call int32(int32) [msil.bcl]System.Math::'Abs'{{$}}
; DUMP-NEXT:  {{^}}    L2: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'use_all'

; The stubs are internal MethodDefs, each a LOCAL FUNCTION symbol (5.20.6
; MW1 as amended in section 12 on 2026-11-04, and 5.8.5 C3), never a GLOBAL
; or WEAK one; NSTUB counts the table's '__msil_mc.' lines. use_all is a
; GLOBAL symbol.
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_fs_open' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_delete' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_parse' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_parsed' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_parsef' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_readbyte' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_parse16' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_plain' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT 'use_all' method=0x06{{[0-9A-F]+$}}
; NSTUB: {{^}}8{{$}}

attributes #0 = { "msil-managed-catch"="System.IO.FileNotFoundException=2;System.IO.DirectoryNotFoundException=20;System.UnauthorizedAccessException=13;System.IO.PathTooLongException=36;System.Text.DecoderFallbackException=84;System.IO.IOException=5" "msil-managed-conv"="utf8,enum,enum,enum,val,err;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(string, valuetype System.IO.FileMode, valuetype System.IO.FileAccess, valuetype System.IO.FileShare, int32)" "msil-managed-type"="System.IO.FileStream" }
attributes #1 = { "msil-managed-catch"="System.IO.IOException=5;System.UnauthorizedAccessException=13" "msil-managed-conv"="utf8,err;void" "msil-managed-member"="Delete" "msil-managed-sig"="void(string)" "msil-managed-type"="System.IO.File" }
attributes #2 = { "msil-managed-catch"="System.FormatException=22;System.OverflowException=34" "msil-managed-conv"="utf16,err;val" "msil-managed-member"="Parse" "msil-managed-sig"="int32(string)" "msil-managed-type"="System.Int32" }
attributes #3 = { "msil-managed-catch"="System.FormatException=22" "msil-managed-conv"="utf8,err;val" "msil-managed-member"="Parse" "msil-managed-sig"="float64(string)" "msil-managed-type"="System.Double" }
attributes #4 = { "msil-managed-catch"="System.ObjectDisposedException=9;System.IO.IOException=5" "msil-managed-conv"="this-obj,err;val" "msil-managed-member"="ReadByte" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.IO.Stream" }
attributes #5 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #6 = { "msil-managed-catch"="System.FormatException=22;System.OverflowException=34" "msil-managed-conv"="utf8,err;val" "msil-managed-member"="ToInt16" "msil-managed-sig"="int16(string)" "msil-managed-type"="System.Convert" }
attributes #7 = { "msil-managed-catch"="System.FormatException=22" "msil-managed-conv"="utf8,err;val" "msil-managed-member"="Parse" "msil-managed-sig"="float32(string)" "msil-managed-type"="System.Single" }
!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7}
!0 = !{!"__msil_mc.m_fs_open", !"System.IO.FileStream", !".ctor", !"instance void(string, valuetype System.IO.FileMode, valuetype System.IO.FileAccess, valuetype System.IO.FileShare, int32)", !"", !"utf8,enum,enum,enum,val,err;obj", !"System.IO.FileNotFoundException=2;System.IO.DirectoryNotFoundException=20;System.UnauthorizedAccessException=13;System.IO.PathTooLongException=36;System.Text.DecoderFallbackException=84;System.IO.IOException=5"}
!1 = !{!"__msil_mc.m_delete", !"System.IO.File", !"Delete", !"void(string)", !"", !"utf8,err;void", !"System.IO.IOException=5;System.UnauthorizedAccessException=13"}
!2 = !{!"__msil_mc.m_parse", !"System.Int32", !"Parse", !"int32(string)", !"", !"utf16,err;val", !"System.FormatException=22;System.OverflowException=34"}
!3 = !{!"__msil_mc.m_parsed", !"System.Double", !"Parse", !"float64(string)", !"", !"utf8,err;val", !"System.FormatException=22"}
!4 = !{!"__msil_mc.m_readbyte", !"System.IO.Stream", !"ReadByte", !"instance int32()", !"", !"this-obj,err;val", !"System.ObjectDisposedException=9;System.IO.IOException=5"}
!5 = !{!"__msil_mc.m_plain", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!6 = !{!"__msil_mc.m_parse16", !"System.Convert", !"ToInt16", !"int16(string)", !"", !"utf8,err;val", !"System.FormatException=22;System.OverflowException=34"}
!7 = !{!"__msil_mc.m_parsef", !"System.Single", !"Parse", !"float32(string)", !"", !"utf8,err;val", !"System.FormatException=22"}
