; The run-time checks of managed-call stubs in whole-program text
; (ImplementationPlan.md 5.20.6 MW2 and 5.20.7 MK2 and MK3, with T-M7's and
; T-M16's texts; milestone MC-1). msil-ld checks every declaration of a link
; against the referenced TypeDefs (T-M7, T-M16), but whole-program text
; (llc without -msil-object-form, clang -S, msil-ld --lto-emit-asm) reaches
; ilasm without a link, so a wrong declaration in it would run with a wrong
; result. Its stubs make those checks when they run instead, before the
; member access, and correct declarations behave as before:
; - Kind (T-M16): the handle of a 'this-obj' instance method or field is cast
;   to the TypeSpec 'class [<s>]T' (tc_sblen, tc_count, and tc_prio in its
;   ASSEMBLY's scope [System]), and that of a 'this-val' one is unboxed to
;   the TypeSpec 'valuetype [<s>]V' (tc_year, tc_ticks, tc_dow), which the
;   type loader refuses when T or V has the other kind (TypeLoadException,
;   'due to value type mismatch'), so such a stub fails at its first call.
;   System.String and System.Object as the 'this-obj' TYPE (tc_slen,
;   tc_ohash) and a value type that a signature spells in one byte as the
;   'this-val' TYPE (tc_icmp: System.Int32) keep the TypeRef: their kinds
;   are fixed and right, and such a TypeSpec would be the long form, which
;   PEVerify /md refuses. A 'this-gch' handle (tc_free) has no such cast,
;   and 'constrained.' keeps its TypeRef.
;   A constructor names its TYPE the same way, whatever its result
;   conversion: a class's 'newobj' is followed by 'castclass class [<s>]T'
;   (tc_sbnew with an 'obj' result, tc_sbvoid with a 'void' one), and a
;   value type's constructed object is boxed to the TypeSpec: the box of a
;   'boxed' result is 'box valuetype [<s>]V' (tc_dtnew, and tc_hrnew, the
;   accepted '.ctor(object, native int)' of HandleRef), and any other result
;   takes 'dup', 'box valuetype [<s>]V' and 'pop' right after the 'newobj'
;   (tc_dtvoid, whose .maxstack is then at least 2). So the wrong spelling
;   'valuetype System.Action' of a delegate's constructor, which slot P0
;   accepts (tc_actnew), fails to load at its first call. The constructors
;   of System.String and System.Object (tc_strnew, tc_objnew) and of a value
;   type that a signature spells in one byte (tc_ipnew: System.IntPtr) keep
;   the TypeRef.
; - Enum (T-M7): each 'enum' parameter in C order, then an 'enum' result, is
;   checked at the very start of the stub, without a local (C#'s
;   'typeof(E).IsEnum'), so no stub here declares '.locals' (tc_flip checks
;   its parameter and its result, tc_pick three enum types): 'ldtoken E',
;   'call' of [mscorlib]System.Type::GetTypeFromHandle, 'callvirt' of
;   [mscorlib]System.Type::get_IsEnum(), 'brfalse.s', 'sizeof E',
;   'ldc.i4.<w>' and 'beq.s', where <w> is the C value's width in bytes (its
;   IR integer width, at least 8 bits), and on a mismatch 'ldstr' of T-M7's
;   text (the parameter form names the member's 1-based CIL parameter, and
;   the result form), 'newobj' of System.InvalidProgramException's string
;   constructor and 'throw'. The checks take the stub's first labels, two
;   each (0 and 1, then 2 and 3, ...), and such a stub's .maxstack is at
;   least 2 (tc_dow, tc_prio, tc_setfg, tc_getfg, tc_flip, tc_pick). An enum
;   type that is a primitive type, never an enum (tc_prim's System.Int32,
;   tc_primr's System.String), gets the three throwing lines alone, so that
;   stub always throws.
; Object-form text (the OF lines; CORE and SYS are msil.bcl) and objects
; (the OBJ lines, through llvm-cil-dump) have none of these lines, and keep
; the TypeRefs: msil-ld checks an object's declarations in every link.
; Whole-program text (the WP lines; CORE=mscorlib, SYS=System) and
; object-form text are pinned whole from '.method' to '} // end of method'
; on both host x86_64 triples and msil64-unknown-dotnet at -O0 and -O2 (the
; stubs depend on neither); the whole-program text assembles. Not pinned:
; the function number in label names (each check's own labels are pinned by
; their numbers), the labels of the stubs' own code, and .maxstack except
; as above. clang/test/CodeGen/MSIL/msil-method-text-checks.c pins the same
; checks in clang -S text.

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,WP -DCORE=mscorlib -DSYS=System \
; DEFINE:     --input-file=%t.wp.%{t}%{o}.il && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,OF -DCORE=msil.bcl -DSYS=msil.bcl \
; DEFINE:     --input-file=%t.of.%{t}%{o}.il --implicit-check-not=InvalidProgramException \
; DEFINE:     --implicit-check-not=get_IsEnum

; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{il}
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.win.dll %t.wp.x86_64-pc-windows-msvc-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.lnx.dll %t.wp.x86_64-unknown-linux-gnu-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.p64.dll %t.wp.msil64-unknown-dotnet-O2.il %}

; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=OBJ --implicit-check-not=InvalidProgramException \
; DEFINE:     --implicit-check-not=get_IsEnum --implicit-check-not=ldstr \
; DEFINE:     --implicit-check-not=ldtoken --implicit-check-not=sizeof
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}

source_filename = "managed-call-text-checks.c"

; 'this-obj' instance method (System.Text.StringBuilder::get_Length).
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_sblen'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.Text.StringBuilder{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]System.Text.StringBuilder{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Text.StringBuilder::get_Length(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_sblen'{{$}}
declare i32 @__msil_mc.tc_sblen(ptr noundef) #0

; 'this-obj' instance field (Ns.Holder::Count).
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_count'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]Ns.Holder{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]Ns.Holder{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldfld int32 {{\[}}[[CORE]]]Ns.Holder::Count{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_count'{{$}}
declare i32 @__msil_mc.tc_count(ptr noundef) #1

; 'this-val' instance method (System.DateTime::get_Year): the final 'call'
; in whole-program text, the placeholder in object-form text.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_year'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}mscorlib]System.DateTime{{$}}
; WP-NEXT:     {{^}}  call instance int32 {{\[}}mscorlib]System.DateTime::get_Year(){{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}msil.bcl]System.DateTime{{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}msil.bcl]System.DateTime{{$}}
; OF-NEXT:     {{^}}  callvirt instance int32 {{\[}}msil.bcl]System.DateTime::get_Year(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_year'{{$}}
declare i32 @__msil_mc.tc_year(ptr noundef) #2

; 'this-val' instance field (Ns.Stamp::Ticks), an int64 read as a C
; 64-bit integer.
; CHECK-LABEL: .method assembly static native int '__msil_mc.tc_ticks'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}mscorlib]Ns.Stamp{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}msil.bcl]Ns.Stamp{{$}}
; CHECK-NEXT:  {{^}}  ldfld int64 {{\[}}[[CORE]]]Ns.Stamp::Ticks{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_ticks'{{$}}
declare i64 @__msil_mc.tc_ticks(ptr noundef) #3

; 'this-val' instance method with an 'enum' result (System.DateTime's
; get_DayOfWeek): the enum check, then the kind check.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_dow'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}mscorlib]System.DayOfWeek{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}mscorlib]System.DayOfWeek{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_dow': the result has 32 bits for 'System.DayOfWeek', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}mscorlib]System.DateTime{{$}}
; WP-NEXT:     {{^}}  call instance valuetype {{\[}}mscorlib]System.DayOfWeek {{\[}}mscorlib]System.DateTime::get_DayOfWeek(){{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}msil.bcl]System.DateTime{{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}msil.bcl]System.DateTime{{$}}
; OF-NEXT:     {{^}}  callvirt instance valuetype {{\[}}msil.bcl]System.DayOfWeek {{\[}}msil.bcl]System.DateTime::get_DayOfWeek(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_dow'{{$}}
declare i32 @__msil_mc.tc_dow(ptr noundef) #4

; 'this-obj' instance method with an 'enum' parameter, with ASSEMBLY
; "System": the TypeSpec and E in [System], System.Enum and
; InvalidProgramException in [mscorlib], the GCHandle rows too.
; CHECK-LABEL: .method assembly static void '__msil_mc.tc_prio'(native int, int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}System]System.Diagnostics.ProcessPriorityClass{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}System]System.Diagnostics.ProcessPriorityClass{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_prio': parameter 1 passes 32 bits for 'System.Diagnostics.ProcessPriorityClass', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}System]System.Diagnostics.Process{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}msil.bcl]System.Diagnostics.Process{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance void {{\[}}[[SYS]]]System.Diagnostics.Process::set_PriorityClass(valuetype {{\[}}[[SYS]]]System.Diagnostics.ProcessPriorityClass){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_prio'{{$}}
declare void @__msil_mc.tc_prio(ptr noundef, i32 noundef) #5

; An 'enum' parameter (Console::set_ForegroundColor).
; CHECK-LABEL: .method assembly static void '__msil_mc.tc_setfg'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}mscorlib]System.ConsoleColor{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}mscorlib]System.ConsoleColor{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_setfg': parameter 1 passes 32 bits for 'System.ConsoleColor', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]System.Console::set_ForegroundColor(valuetype {{\[}}[[CORE]]]System.ConsoleColor){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_setfg'{{$}}
declare void @__msil_mc.tc_setfg(i32 noundef) #6

; An 'enum' result (Console::get_ForegroundColor).
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_getfg'() cil managed
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}mscorlib]System.ConsoleColor{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}mscorlib]System.ConsoleColor{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_getfg': the result has 32 bits for 'System.ConsoleColor', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.ConsoleColor {{\[}}[[CORE]]]System.Console::get_ForegroundColor(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_getfg'{{$}}
declare i32 @__msil_mc.tc_getfg() #7

; An int64-backed enum as parameter and result: one local, two checks,
; 'ldc.i4.8'; no int64 local.
; CHECK-LABEL: .method assembly static native int '__msil_mc.tc_flip'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}mscorlib]Ns.Big{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}mscorlib]Ns.Big{{$}}
; WP-NEXT:     {{^}}  ldc.i4.8{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_flip': parameter 1 passes 64 bits for 'Ns.Big', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}mscorlib]Ns.Big{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F]]_2{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}mscorlib]Ns.Big{{$}}
; WP-NEXT:     {{^}}  ldc.i4.8{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_3{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_2:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_flip': the result has 64 bits for 'Ns.Big', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_3:{{$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]Ns.Big {{\[}}[[CORE]]]Ns.Enums::Flip(valuetype {{\[}}[[CORE]]]Ns.Big){{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_flip'{{$}}
declare i64 @__msil_mc.tc_flip(i64 noundef) #8

; Two 'enum' parameters and an 'enum' result of three enum types.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_pick'(int32, int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}mscorlib]System.IO.FileMode{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}mscorlib]System.IO.FileMode{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_pick': parameter 1 passes 32 bits for 'System.IO.FileMode', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}mscorlib]System.IO.FileAccess{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F]]_2{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}mscorlib]System.IO.FileAccess{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_3{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_2:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_pick': parameter 2 passes 32 bits for 'System.IO.FileAccess', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_3:{{$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}mscorlib]System.DayOfWeek{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F]]_4{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}mscorlib]System.DayOfWeek{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_5{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_4:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_pick': the result has 32 bits for 'System.DayOfWeek', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_5:{{$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.DayOfWeek {{\[}}[[CORE]]]Ns.Files::Pick(valuetype {{\[}}[[CORE]]]System.IO.FileMode, valuetype {{\[}}[[CORE]]]System.IO.FileAccess){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_pick'{{$}}
declare i32 @__msil_mc.tc_pick(i32 noundef, i32 noundef) #9

; A primitive type named as an 'enum' parameter: the throw alone.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_prim'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack [1-9][0-9]*$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_prim': parameter 1 passes 32 bits for 'System.Int32', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]Ns.Prim::Take(valuetype {{\[}}[[CORE]]]System.Int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_prim'{{$}}
declare i32 @__msil_mc.tc_prim(i32 noundef) #10

; System.String named as an 'enum' result: the throw alone.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_primr'() cil managed
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack [1-9][0-9]*$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'tc_primr': the result has 32 bits for 'System.String', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.String {{\[}}[[CORE]]]Ns.Prim::Make(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_primr'{{$}}
declare i32 @__msil_mc.tc_primr() #11

; No kind check: 'this-obj' TYPE System.String ...
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_slen'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}  castclass {{\[}}[[CORE]]]System.String{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.String::get_Length(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_slen'{{$}}
declare i32 @__msil_mc.tc_slen(ptr noundef) #12

; ... and System.Object keep their TypeRef ...
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_ohash'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}  castclass {{\[}}[[CORE]]]System.Object{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Object::GetHashCode(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_ohash'{{$}}
declare i32 @__msil_mc.tc_ohash(ptr noundef) #13

; ... and so does the 'this-val' TYPE System.Int32 ...
; CHECK-LABEL: .method assembly static int32 '__msil_mc.tc_icmp'(native int, int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Int32{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; WP-NEXT:     {{^}}  call instance int32 {{\[}}mscorlib]System.Int32::CompareTo(int32){{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}msil.bcl]System.Int32{{$}}
; OF-NEXT:     {{^}}  callvirt instance int32 {{\[}}msil.bcl]System.Int32::CompareTo(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_icmp'{{$}}
declare i32 @__msil_mc.tc_icmp(ptr noundef, i32 noundef) #14

; ... and a 'this-gch' handle has no cast to check.
; CHECK-LABEL: .method assembly static void '__msil_mc.tc_free'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; WP-NEXT:     {{^}}  call instance void {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::Free(){{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; OF-NEXT:     {{^}}  callvirt instance void {{\[}}msil.bcl]System.Runtime.InteropServices.GCHandle::Free(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_free'{{$}}
declare void @__msil_mc.tc_free(ptr noundef) #15

; Constructors. A class (System.Text.StringBuilder::.ctor(string)) with an
; 'obj' result: 'castclass class T' right after the 'newobj'.
; CHECK-LABEL: .method assembly static native int '__msil_mc.tc_sbnew'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}  castclass {{\[}}[[CORE]]]System.String{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.Text.StringBuilder::.ctor(string){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.Text.StringBuilder{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_sbnew'{{$}}
declare ptr @__msil_mc.tc_sbnew(ptr noundef) #16

; A value type (System.DateTime::.ctor(int64)) with a 'boxed' result: the
; box names the TypeSpec.
; CHECK-LABEL: .method assembly static native int '__msil_mc.tc_dtnew'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.DateTime::.ctor(int64){{$}}
; WP-NEXT:     {{^}}  box valuetype {{\[}}mscorlib]System.DateTime{{$}}
; OF-NEXT:     {{^}}  box {{\[}}msil.bcl]System.DateTime{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_dtnew'{{$}}
declare ptr @__msil_mc.tc_dtnew(i64 noundef) #17

; HandleRef's '.ctor(object, native int)', the shape of a delegate's
; constructor on a value type, which is accepted: its box names the
; TypeSpec too.
; CHECK-LABEL: .method assembly static native int '__msil_mc.tc_hrnew'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.Runtime.InteropServices.HandleRef::.ctor(object, native int){{$}}
; WP-NEXT:     {{^}}  box valuetype {{\[}}mscorlib]System.Runtime.InteropServices.HandleRef{{$}}
; OF-NEXT:     {{^}}  box {{\[}}msil.bcl]System.Runtime.InteropServices.HandleRef{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_hrnew'{{$}}
declare ptr @__msil_mc.tc_hrnew(ptr noundef, i64 noundef) #18

; No kind token: the constructors of System.String ...
; CHECK-LABEL: .method assembly static native int '__msil_mc.tc_strnew'(int32, int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.String::.ctor(char, int32){{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_strnew'{{$}}
declare ptr @__msil_mc.tc_strnew(i16 noundef zeroext, i32 noundef) #19

; ... and System.Object ...
; CHECK-LABEL: .method assembly static native int '__msil_mc.tc_objnew'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.Object::.ctor(){{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_objnew'{{$}}
declare ptr @__msil_mc.tc_objnew() #20

; ... and of a value type that a signature spells in one byte
; (System.IntPtr).
; CHECK-LABEL: .method assembly static native int '__msil_mc.tc_ipnew'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.IntPtr::.ctor(int64){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.IntPtr{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_ipnew'{{$}}
declare ptr @__msil_mc.tc_ipnew(i64 noundef) #21

; The wrong spelling 'valuetype System.Action' of a delegate's constructor
; (Sema and slot P0 accept it, as for HandleRef): whole-program text boxes
; to the TypeSpec 'valuetype [mscorlib]System.Action', which fails to load;
; object-form text keeps the TypeRef (msil-ld refuses the declaration in a
; link, T-M16 or T-M8).
; CHECK-LABEL: .method assembly static native int '__msil_mc.tc_actnew'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br.s @MC{{[0-9]+}}_{{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.Action::.ctor(object, native int){{$}}
; WP-NEXT:     {{^}}  box valuetype {{\[}}mscorlib]System.Action{{$}}
; OF-NEXT:     {{^}}  box {{\[}}msil.bcl]System.Action{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_actnew'{{$}}
declare ptr @__msil_mc.tc_actnew(ptr noundef, i64 noundef) #22

; A 'void' result, which only hand-written IR has (clang's Sema gives a
; constructor a handle result): the class's 'castclass' comes before the
; 'pop' ...
; CHECK-LABEL: .method assembly static void '__msil_mc.tc_sbvoid'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.Text.StringBuilder::.ctor(int32){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.Text.StringBuilder{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_sbvoid'{{$}}
declare void @__msil_mc.tc_sbvoid(i32 noundef) #23

; ... and a value type's object is boxed to the TypeSpec by 'dup', 'box
; valuetype V' and 'pop', which raise .maxstack to at least 2.
; CHECK-LABEL: .method assembly static void '__msil_mc.tc_dtvoid'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.DateTime::.ctor(int64){{$}}
; WP-NEXT:     {{^}}  dup{{$}}
; WP-NEXT:     {{^}}  box valuetype {{\[}}mscorlib]System.DateTime{{$}}
; WP-NEXT:     {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.tc_dtvoid'{{$}}
declare void @__msil_mc.tc_dtvoid(i64 noundef) #24

; Objects: no check (the object run's implicit-check-nots) and the TypeRefs.
; OBJ-LABEL: {{^}}  .method '__msil_mc.tc_sblen' int32(native int) flags=
; OBJ:       {{^}}    L{{[0-9]+}}: castclass [msil.bcl]System.Text.StringBuilder{{$}}
; OBJ-LABEL: {{^}}  .method '__msil_mc.tc_year' int32(native int) flags=
; OBJ:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.DateTime{{$}}
; OBJ-LABEL: {{^}}  .method '__msil_mc.tc_setfg' void(int32) flags=
; OBJ-NEXT:  {{^}}    .maxstack 1{{$}}
; OBJ-NEXT:  {{^}}    L0: ldarg.0{{$}}
; OBJ-NEXT:  {{^}}    L1: call void(valuetype [msil.bcl]System.ConsoleColor) [msil.bcl]System.Console::'set_ForegroundColor'{{$}}
; OBJ-NEXT:  {{^}}    L2: ret{{$}}
; OBJ-LABEL: {{^}}  .method '__msil_mc.tc_sbnew' native int(native int) flags=
; OBJ:       {{^}}    L{{[0-9]+}}: newobj instance void(string) [msil.bcl]System.Text.StringBuilder::'.ctor'{{$}}
; OBJ-NEXT:  {{^}}    L{{[0-9]+}}: dup{{$}}
; OBJ-LABEL: {{^}}  .method '__msil_mc.tc_dtnew' native int(native int) flags=
; OBJ:       {{^}}    L{{[0-9]+}}: newobj instance void(int64) [msil.bcl]System.DateTime::'.ctor'{{$}}
; OBJ-NEXT:  {{^}}    L{{[0-9]+}}: box [msil.bcl]System.DateTime{{$}}
; OBJ-LABEL: {{^}}  .method '__msil_mc.tc_actnew' native int(native int, native int) flags=
; OBJ:       {{^}}    L{{[0-9]+}}: newobj instance void(object, native int) [msil.bcl]System.Action::'.ctor'{{$}}
; OBJ-NEXT:  {{^}}    L{{[0-9]+}}: box [msil.bcl]System.Action{{$}}
; OBJ-LABEL: {{^}}  .method '__msil_mc.tc_dtvoid' void(native int) flags=
; OBJ:       {{^}}    L{{[0-9]+}}: newobj instance void(int64) [msil.bcl]System.DateTime::'.ctor'{{$}}
; OBJ-NEXT:  {{^}}    L{{[0-9]+}}: pop{{$}}

define i32 @use(ptr %h, i32 %v, i64 %w) {
  %a = call i32 @__msil_mc.tc_sblen(ptr noundef %h)
  %b = call i32 @__msil_mc.tc_count(ptr noundef %h)
  %c = call i32 @__msil_mc.tc_year(ptr noundef %h)
  %d = call i64 @__msil_mc.tc_ticks(ptr noundef %h)
  %e = call i32 @__msil_mc.tc_dow(ptr noundef %h)
  call void @__msil_mc.tc_prio(ptr noundef %h, i32 noundef %v)
  call void @__msil_mc.tc_setfg(i32 noundef %v)
  %f = call i32 @__msil_mc.tc_getfg()
  %g = call i64 @__msil_mc.tc_flip(i64 noundef %w)
  %i = call i32 @__msil_mc.tc_pick(i32 noundef %v, i32 noundef %v)
  %j = call i32 @__msil_mc.tc_prim(i32 noundef %v)
  %k = call i32 @__msil_mc.tc_primr()
  %l = call i32 @__msil_mc.tc_slen(ptr noundef %h)
  %m = call i32 @__msil_mc.tc_ohash(ptr noundef %h)
  %n = call i32 @__msil_mc.tc_icmp(ptr noundef %h, i32 noundef %v)
  call void @__msil_mc.tc_free(ptr noundef %h)
  %c16 = trunc i32 %v to i16
  %o1 = call ptr @__msil_mc.tc_sbnew(ptr noundef %h)
  %o2 = call ptr @__msil_mc.tc_dtnew(i64 noundef %w)
  %o3 = call ptr @__msil_mc.tc_hrnew(ptr noundef %o1, i64 noundef %w)
  %o4 = call ptr @__msil_mc.tc_strnew(i16 noundef zeroext %c16, i32 noundef %v)
  %o5 = call ptr @__msil_mc.tc_objnew()
  %o6 = call ptr @__msil_mc.tc_ipnew(i64 noundef %w)
  %o7 = call ptr @__msil_mc.tc_actnew(ptr noundef %o5, i64 noundef %w)
  call void @__msil_mc.tc_sbvoid(i32 noundef %v)
  call void @__msil_mc.tc_dtvoid(i64 noundef %w)
  %d32 = trunc i64 %d to i32
  %g32 = trunc i64 %g to i32
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d32
  %s4 = add i32 %s3, %e
  %s5 = add i32 %s4, %f
  %s6 = add i32 %s5, %g32
  %s7 = add i32 %s6, %i
  %s8 = add i32 %s7, %j
  %s9 = add i32 %s8, %k
  %s10 = add i32 %s9, %l
  %s11 = add i32 %s10, %m
  %s12 = add i32 %s11, %n
  ret i32 %s12
}

attributes #0 = { "msil-managed-conv"="this-obj;val" "msil-managed-member"="get_Length" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.Text.StringBuilder" }
attributes #1 = { "msil-managed-conv"="this-obj;val" "msil-managed-member"="Count" "msil-managed-sig"="instance field int32" "msil-managed-type"="Ns.Holder" }
attributes #2 = { "msil-managed-conv"="this-val;val" "msil-managed-member"="get_Year" "msil-managed-sig"="instance int32()" "msil-managed-type"="valuetype System.DateTime" }
attributes #3 = { "msil-managed-conv"="this-val;val" "msil-managed-member"="Ticks" "msil-managed-sig"="instance field int64" "msil-managed-type"="valuetype Ns.Stamp" }
attributes #4 = { "msil-managed-conv"="this-val;enum" "msil-managed-member"="get_DayOfWeek" "msil-managed-sig"="instance valuetype System.DayOfWeek()" "msil-managed-type"="valuetype System.DateTime" }
attributes #5 = { "msil-managed-asm"="System" "msil-managed-conv"="this-obj,enum;void" "msil-managed-member"="set_PriorityClass" "msil-managed-sig"="instance void(valuetype System.Diagnostics.ProcessPriorityClass)" "msil-managed-type"="System.Diagnostics.Process" }
attributes #6 = { "msil-managed-conv"="enum;void" "msil-managed-member"="set_ForegroundColor" "msil-managed-sig"="void(valuetype System.ConsoleColor)" "msil-managed-type"="System.Console" }
attributes #7 = { "msil-managed-conv"=";enum" "msil-managed-member"="get_ForegroundColor" "msil-managed-sig"="valuetype System.ConsoleColor()" "msil-managed-type"="System.Console" }
attributes #8 = { "msil-managed-conv"="enum;enum" "msil-managed-member"="Flip" "msil-managed-sig"="valuetype Ns.Big(valuetype Ns.Big)" "msil-managed-type"="Ns.Enums" }
attributes #9 = { "msil-managed-conv"="enum,enum;enum" "msil-managed-member"="Pick" "msil-managed-sig"="valuetype System.DayOfWeek(valuetype System.IO.FileMode, valuetype System.IO.FileAccess)" "msil-managed-type"="Ns.Files" }
attributes #10 = { "msil-managed-conv"="enum;val" "msil-managed-member"="Take" "msil-managed-sig"="int32(valuetype System.Int32)" "msil-managed-type"="Ns.Prim" }
attributes #11 = { "msil-managed-conv"=";enum" "msil-managed-member"="Make" "msil-managed-sig"="valuetype System.String()" "msil-managed-type"="Ns.Prim" }
attributes #12 = { "msil-managed-conv"="this-obj;val" "msil-managed-member"="get_Length" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.String" }
attributes #13 = { "msil-managed-conv"="this-obj;val" "msil-managed-member"="GetHashCode" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.Object" }
attributes #14 = { "msil-managed-conv"="this-val,val;val" "msil-managed-member"="CompareTo" "msil-managed-sig"="instance int32(int32)" "msil-managed-type"="valuetype System.Int32" }
attributes #15 = { "msil-managed-conv"="this-gch;void" "msil-managed-member"="Free" "msil-managed-sig"="instance void()" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #16 = { "msil-managed-conv"="obj;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(string)" "msil-managed-type"="System.Text.StringBuilder" }
attributes #17 = { "msil-managed-conv"="val;boxed" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int64)" "msil-managed-type"="valuetype System.DateTime" }
attributes #18 = { "msil-managed-conv"="obj,val;boxed" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(object, native int)" "msil-managed-type"="valuetype System.Runtime.InteropServices.HandleRef" }
attributes #19 = { "msil-managed-conv"="val,val;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(char, int32)" "msil-managed-type"="System.String" }
attributes #20 = { "msil-managed-conv"=";obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void()" "msil-managed-type"="System.Object" }
attributes #21 = { "msil-managed-conv"="val;boxed" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int64)" "msil-managed-type"="valuetype System.IntPtr" }
attributes #22 = { "msil-managed-conv"="obj,val;boxed" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(object, native int)" "msil-managed-type"="valuetype System.Action" }
attributes #23 = { "msil-managed-conv"="val;void" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int32)" "msil-managed-type"="System.Text.StringBuilder" }
attributes #24 = { "msil-managed-conv"="val;void" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int64)" "msil-managed-type"="valuetype System.DateTime" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12, !13, !14, !15, !16, !17, !18, !19, !20, !21, !22, !23, !24}
!0 = !{!"__msil_mc.tc_sblen", !"System.Text.StringBuilder", !"get_Length", !"instance int32()", !"", !"this-obj;val", !""}
!1 = !{!"__msil_mc.tc_count", !"Ns.Holder", !"Count", !"instance field int32", !"", !"this-obj;val", !""}
!2 = !{!"__msil_mc.tc_year", !"valuetype System.DateTime", !"get_Year", !"instance int32()", !"", !"this-val;val", !""}
!3 = !{!"__msil_mc.tc_ticks", !"valuetype Ns.Stamp", !"Ticks", !"instance field int64", !"", !"this-val;val", !""}
!4 = !{!"__msil_mc.tc_dow", !"valuetype System.DateTime", !"get_DayOfWeek", !"instance valuetype System.DayOfWeek()", !"", !"this-val;enum", !""}
!5 = !{!"__msil_mc.tc_prio", !"System.Diagnostics.Process", !"set_PriorityClass", !"instance void(valuetype System.Diagnostics.ProcessPriorityClass)", !"System", !"this-obj,enum;void", !""}
!6 = !{!"__msil_mc.tc_setfg", !"System.Console", !"set_ForegroundColor", !"void(valuetype System.ConsoleColor)", !"", !"enum;void", !""}
!7 = !{!"__msil_mc.tc_getfg", !"System.Console", !"get_ForegroundColor", !"valuetype System.ConsoleColor()", !"", !";enum", !""}
!8 = !{!"__msil_mc.tc_flip", !"Ns.Enums", !"Flip", !"valuetype Ns.Big(valuetype Ns.Big)", !"", !"enum;enum", !""}
!9 = !{!"__msil_mc.tc_pick", !"Ns.Files", !"Pick", !"valuetype System.DayOfWeek(valuetype System.IO.FileMode, valuetype System.IO.FileAccess)", !"", !"enum,enum;enum", !""}
!10 = !{!"__msil_mc.tc_prim", !"Ns.Prim", !"Take", !"int32(valuetype System.Int32)", !"", !"enum;val", !""}
!11 = !{!"__msil_mc.tc_primr", !"Ns.Prim", !"Make", !"valuetype System.String()", !"", !";enum", !""}
!12 = !{!"__msil_mc.tc_slen", !"System.String", !"get_Length", !"instance int32()", !"", !"this-obj;val", !""}
!13 = !{!"__msil_mc.tc_ohash", !"System.Object", !"GetHashCode", !"instance int32()", !"", !"this-obj;val", !""}
!14 = !{!"__msil_mc.tc_icmp", !"valuetype System.Int32", !"CompareTo", !"instance int32(int32)", !"", !"this-val,val;val", !""}
!15 = !{!"__msil_mc.tc_free", !"valuetype System.Runtime.InteropServices.GCHandle", !"Free", !"instance void()", !"", !"this-gch;void", !""}
!16 = !{!"__msil_mc.tc_sbnew", !"System.Text.StringBuilder", !".ctor", !"instance void(string)", !"", !"obj;obj", !""}
!17 = !{!"__msil_mc.tc_dtnew", !"valuetype System.DateTime", !".ctor", !"instance void(int64)", !"", !"val;boxed", !""}
!18 = !{!"__msil_mc.tc_hrnew", !"valuetype System.Runtime.InteropServices.HandleRef", !".ctor", !"instance void(object, native int)", !"", !"obj,val;boxed", !""}
!19 = !{!"__msil_mc.tc_strnew", !"System.String", !".ctor", !"instance void(char, int32)", !"", !"val,val;obj", !""}
!20 = !{!"__msil_mc.tc_objnew", !"System.Object", !".ctor", !"instance void()", !"", !";obj", !""}
!21 = !{!"__msil_mc.tc_ipnew", !"valuetype System.IntPtr", !".ctor", !"instance void(int64)", !"", !"val;boxed", !""}
!22 = !{!"__msil_mc.tc_actnew", !"valuetype System.Action", !".ctor", !"instance void(object, native int)", !"", !"obj,val;boxed", !""}
!23 = !{!"__msil_mc.tc_sbvoid", !"System.Text.StringBuilder", !".ctor", !"instance void(int32)", !"", !"val;void", !""}
!24 = !{!"__msil_mc.tc_dtvoid", !"valuetype System.DateTime", !".ctor", !"instance void(int64)", !"", !"val;void", !""}
