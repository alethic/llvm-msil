; The exact IL types of a managed-call stub's narrow 'enum' parameters
; (ImplementationPlan.md 5.20.3 MS5 'enum' row, 5.20.7 MK2; milestone MC-1,
; section 12 rows 'fix-loop ownership: ... -> fix-3' of 2026-11-04). An enum
; argument is passed by an integer of the enum's width, and msil-ld's MK2
; compares the width of the stub's parameter with the enum's 'value__'. So a
; stub's 'enum'-converted parameter keeps the exact element type of its C
; type instead of the int32 that MSIL::Signature gives every narrow integer:
;   i8 signext  (signed char)    -> int8
;   i8 zeroext  (unsigned char)  -> unsigned int8 (uint8 in objects)
;   i1 zeroext  (_Bool)          -> unsigned int8
;   i16 signext (short)          -> int16
;   i16 zeroext (unsigned short) -> unsigned int16 (uint16 in objects)
; also after a 'this' handle (m_tune). An 'enum'-converted narrow RESULT keeps
; its exact type the same way (5.20.7 MK2 and T-M7 as amended in section 12 on
; 2026-11-04, which compare the stub's return element type with the enum's
; 'value__'; rows 'fix-loop ownership: ... -> fix-6'): m_r8, m_ru8 (after a
; 'this' handle), m_rb, m_r16 (with an int16 parameter) and m_ru16 (a field
; read). The '-S' method headers and every call site print the same types
; (M19 oracle), the MethodDef signature blobs and the call-site signatures in
; objects say the same through llvm-cil-dump, and the object that ilasm makes
; from the object-form text has the same canonical listing and link info as
; llc's object. The member's own signature (the MemberRef) is unchanged:
; 'valuetype' enum parameters and results. A narrow result's stub ends with
; the conversion that its IR attribute names, directly after the access and
; directly before 'ret' (5.20.3 MS5's 'enum' row as built; LLVM's meaning of
; 'signext' and 'zeroext' on a return value): 'conv.i1' (m_r8, signext i8),
; 'conv.u1' (m_ru8, zeroext i8), 'conv.i2' (m_r16, signext i16) and
; 'conv.u2' (m_ru16, zeroext i16), and m_rb's 'zeroext i1' ends with
; 'ldc.i4.0; cgt.un' (C11 6.3.1.2), in text and in objects.
; Whole-program text, which no link checks, also checks each 'enum' type at
; run time (T-M7's check), before anything else in the stub and without a
; local: per 'enum' parameter in C order and then for an 'enum' result,
; 'ldtoken E', System.Type's GetTypeFromHandle and get_IsEnum(), and 'sizeof
; E' against the C value's width in bytes (an i1 counts 8 bits), which
; throws T-M7's text as InvalidProgramException on a mismatch; the check
; takes the stub's first labels (m_r16 checks its parameter and its result),
; and a 'this-obj' handle (m_tune, m_ru8) is cast to the TypeSpec 'class
; Ns.Narrow', so a value type there fails to load (T-M16's check). Those
; lines are the WP ones; object-form text and objects have none of them
; (the OF lines). Whole-program and object-form text on both host x86_64
; triples and msil64-unknown-dotnet at -O0 and -O2; the whole-program text
; assembles.
; Objects: msil64-unknown-dotnet at -O0 and -O2, each host triple at one
; level.

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
; DEFINE:   %{canon} %t.of.%{t}%{o}.ilasm.o | sed -e "/^pe /d" > %t.%{t}%{o}.canon2 && \
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
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}

source_filename = "managed-call-narrow.c"

; CHECK-LABEL: .method assembly static void '__msil_mc.m_s8'(int8) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.Mode8{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.Mode8{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_s8': parameter 1 passes 8 bits for 'Ns.Mode8', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]Ns.Narrow::Set8(valuetype {{\[}}[[CORE]]]Ns.Mode8){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_s8'{{$}}
declare void @__msil_mc.m_s8(i8 noundef signext) #0

; CHECK-LABEL: .method assembly static void '__msil_mc.m_u8'(unsigned int8) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_u8': parameter 1 passes 8 bits for 'Ns.ModeU8', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]Ns.Narrow::SetU8(valuetype {{\[}}[[CORE]]]Ns.ModeU8){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_u8'{{$}}
declare void @__msil_mc.m_u8(i8 noundef zeroext) #1

; CHECK-LABEL: .method assembly static void '__msil_mc.m_b'(unsigned int8) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_b': parameter 1 passes 8 bits for 'Ns.ModeU8', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]Ns.Narrow::SetU8(valuetype {{\[}}[[CORE]]]Ns.ModeU8){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_b'{{$}}
declare void @__msil_mc.m_b(i1 noundef zeroext) #2

; CHECK-LABEL: .method assembly static void '__msil_mc.m_s16'(int16) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.Mode16{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.Mode16{{$}}
; WP-NEXT:     {{^}}  ldc.i4.2{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_s16': parameter 1 passes 16 bits for 'Ns.Mode16', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]Ns.Narrow::Set16(valuetype {{\[}}[[CORE]]]Ns.Mode16){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_s16'{{$}}
declare void @__msil_mc.m_s16(i16 noundef signext) #3

; CHECK-LABEL: .method assembly static void '__msil_mc.m_u16'(unsigned int16) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.ModeU16{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.ModeU16{{$}}
; WP-NEXT:     {{^}}  ldc.i4.2{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_u16': parameter 1 passes 16 bits for 'Ns.ModeU16', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]Ns.Narrow::SetU16(valuetype {{\[}}[[CORE]]]Ns.ModeU16){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_u16'{{$}}
declare void @__msil_mc.m_u16(i16 noundef zeroext) #4

; Narrow and wide 'enum' parameters after a 'this' handle; the int32 one
; stays int32.
; CHECK-LABEL: .method assembly static void '__msil_mc.m_tune'(native int, int16, unsigned int8, int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.Mode16{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.Mode16{{$}}
; WP-NEXT:     {{^}}  ldc.i4.2{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_tune': parameter 1 passes 16 bits for 'Ns.Mode16', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F]]_2{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_3{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_2:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_tune': parameter 2 passes 8 bits for 'Ns.ModeU8', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_3:{{$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]System.IO.FileMode{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F]]_4{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]System.IO.FileMode{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_5{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_4:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_tune': parameter 3 passes 32 bits for 'System.IO.FileMode', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_5:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP:          {{^}}  castclass class {{\[}}[[CORE]]]Ns.Narrow{{$}}
; OF:          {{^}}  castclass {{\[}}[[CORE]]]Ns.Narrow{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance void {{\[}}[[CORE]]]Ns.Narrow::Tune(valuetype {{\[}}[[CORE]]]Ns.Mode16, valuetype {{\[}}[[CORE]]]Ns.ModeU8, valuetype {{\[}}[[CORE]]]System.IO.FileMode){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_tune'{{$}}
declare void @__msil_mc.m_tune(ptr noundef, i16 noundef signext, i8 noundef zeroext, i32 noundef) #5

; Narrow 'enum' results: the return type is the exact type of the C result,
; and the conversion that the IR attribute names comes directly after the
; access and directly before 'ret'.
; CHECK-LABEL: .method assembly static int8 '__msil_mc.m_r8'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.Mode8{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.Mode8{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_r8': the result has 8 bits for 'Ns.Mode8', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]Ns.Mode8 {{\[}}[[CORE]]]Ns.Narrow::Get8(){{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_r8'{{$}}
declare signext i8 @__msil_mc.m_r8() #6

; CHECK-LABEL: .method assembly static unsigned int8 '__msil_mc.m_ru8'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_ru8': the result has 8 bits for 'Ns.ModeU8', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; WP:          {{^}}  castclass class {{\[}}[[CORE]]]Ns.Narrow{{$}}
; OF:          {{^}}  castclass {{\[}}[[CORE]]]Ns.Narrow{{$}}
; CHECK-NEXT:  {{^}}@MC{{[0-9]+}}_{{[0-9]+}}:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance valuetype {{\[}}[[CORE]]]Ns.ModeU8 {{\[}}[[CORE]]]Ns.Narrow::get_ModeU8(){{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_ru8'{{$}}
declare zeroext i8 @__msil_mc.m_ru8(ptr noundef) #7

; CHECK-LABEL: .method assembly static unsigned int8 '__msil_mc.m_rb'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_rb': the result has 8 bits for 'Ns.ModeU8', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]Ns.ModeU8 {{\[}}[[CORE]]]Ns.Narrow::GetU8(){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_rb'{{$}}
declare zeroext i1 @__msil_mc.m_rb() #8

; CHECK-LABEL: .method assembly static int16 '__msil_mc.m_r16'(int16) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.Mode16{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.Mode16{{$}}
; WP-NEXT:     {{^}}  ldc.i4.2{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_r16': parameter 1 passes 16 bits for 'Ns.Mode16', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.Mode16{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F]]_2{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.Mode16{{$}}
; WP-NEXT:     {{^}}  ldc.i4.2{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_3{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_2:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_r16': the result has 16 bits for 'Ns.Mode16', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_3:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]Ns.Mode16 {{\[}}[[CORE]]]Ns.Narrow::Flip16(valuetype {{\[}}[[CORE]]]Ns.Mode16){{$}}
; CHECK-NEXT:  {{^}}  conv.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_r16'{{$}}
declare signext i16 @__msil_mc.m_r16(i16 noundef signext) #9

; CHECK-LABEL: .method assembly static unsigned int16 '__msil_mc.m_ru16'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.ModeU16{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.ModeU16{{$}}
; WP-NEXT:     {{^}}  ldc.i4.2{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_ru16': the result has 16 bits for 'Ns.ModeU16', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldsfld valuetype {{\[}}[[CORE]]]Ns.ModeU16 {{\[}}[[CORE]]]Ns.Narrow::Default{{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_ru16'{{$}}
declare zeroext i16 @__msil_mc.m_ru16() #10

; The call sites name the same types as the method headers.
; CHECK-LABEL: .method public static void 'use_narrow'(native int) cil managed
; CHECK:       {{^}}  call void '__msil_mc.m_s8'(int8){{$}}
; CHECK:       {{^}}  call void '__msil_mc.m_u8'(unsigned int8){{$}}
; CHECK:       {{^}}  call void '__msil_mc.m_b'(unsigned int8){{$}}
; CHECK:       {{^}}  call void '__msil_mc.m_s16'(int16){{$}}
; CHECK:       {{^}}  call void '__msil_mc.m_u16'(unsigned int16){{$}}
; CHECK:       {{^}}  call void '__msil_mc.m_tune'(native int, int16, unsigned int8, int32){{$}}
; CHECK:       {{^}}} // end of method 'use_narrow'{{$}}
define void @use_narrow(ptr %h) {
  call void @__msil_mc.m_s8(i8 noundef signext -2)
  call void @__msil_mc.m_u8(i8 noundef zeroext 200)
  call void @__msil_mc.m_b(i1 noundef zeroext true)
  call void @__msil_mc.m_s16(i16 noundef signext -300)
  call void @__msil_mc.m_u16(i16 noundef zeroext 60000)
  call void @__msil_mc.m_tune(ptr noundef %h, i16 noundef signext 7, i8 noundef zeroext 1, i32 noundef 3)
  ret void
}

; CHECK-LABEL: .method public static int32 'use_results'(native int) cil managed
; CHECK:       {{^}}  call int8 '__msil_mc.m_r8'(){{$}}
; CHECK:       {{^}}  call unsigned int8 '__msil_mc.m_ru8'(native int){{$}}
; CHECK:       {{^}}  call unsigned int8 '__msil_mc.m_rb'(){{$}}
; CHECK:       {{^}}  call int16 '__msil_mc.m_r16'(int16){{$}}
; CHECK:       {{^}}  call unsigned int16 '__msil_mc.m_ru16'(){{$}}
; CHECK:       {{^}}} // end of method 'use_results'{{$}}
define i32 @use_results(ptr %h) {
  %a = call signext i8 @__msil_mc.m_r8()
  %b = call zeroext i8 @__msil_mc.m_ru8(ptr noundef %h)
  %c = call zeroext i1 @__msil_mc.m_rb()
  %d = call signext i16 @__msil_mc.m_r16(i16 noundef signext -3)
  %e = call zeroext i16 @__msil_mc.m_ru16()
  %a1 = sext i8 %a to i32
  %b1 = zext i8 %b to i32
  %c1 = zext i1 %c to i32
  %d1 = sext i16 %d to i32
  %e1 = zext i16 %e to i32
  %s1 = add i32 %a1, %b1
  %s2 = add i32 %s1, %c1
  %s3 = add i32 %s2, %d1
  %s4 = add i32 %s3, %e1
  ret i32 %s4
}

; Objects: the MethodDef signature blobs (each '.method' line), the
; MemberRef signatures of the members, and the call-site signatures.
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_s8' void(int8) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call void(valuetype [msil.bcl]Ns.Mode8) [msil.bcl]Ns.Narrow::'Set8'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_u8' void(uint8) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call void(valuetype [msil.bcl]Ns.ModeU8) [msil.bcl]Ns.Narrow::'SetU8'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_b' void(uint8) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call void(valuetype [msil.bcl]Ns.ModeU8) [msil.bcl]Ns.Narrow::'SetU8'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_s16' void(int16) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call void(valuetype [msil.bcl]Ns.Mode16) [msil.bcl]Ns.Narrow::'Set16'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_u16' void(uint16) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call void(valuetype [msil.bcl]Ns.ModeU16) [msil.bcl]Ns.Narrow::'SetU16'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_tune' void(native int, int16, uint8, int32) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: callvirt instance void(valuetype [msil.bcl]Ns.Mode16, valuetype [msil.bcl]Ns.ModeU8, valuetype [msil.bcl]System.IO.FileMode) [msil.bcl]Ns.Narrow::'Tune'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_r8' int8() flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call valuetype [msil.bcl]Ns.Mode8() [msil.bcl]Ns.Narrow::'Get8'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: conv.i1{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_ru8' uint8(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: callvirt instance valuetype [msil.bcl]Ns.ModeU8() [msil.bcl]Ns.Narrow::'get_ModeU8'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: conv.u1{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_rb' uint8() flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call valuetype [msil.bcl]Ns.ModeU8() [msil.bcl]Ns.Narrow::'GetU8'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: cgt.un{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_r16' int16(int16) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call valuetype [msil.bcl]Ns.Mode16(valuetype [msil.bcl]Ns.Mode16) [msil.bcl]Ns.Narrow::'Flip16'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: conv.i2{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_ru16' uint16() flags=
; DUMP:       {{^}}    L{{[0-9]+}}: ldsfld valuetype [msil.bcl]Ns.ModeU16 [msil.bcl]Ns.Narrow::'Default'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: conv.u2{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'use_narrow' void(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call void(int8) '<Module>'::'__msil_mc.m_s8'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call void(uint8) '<Module>'::'__msil_mc.m_u8'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call void(uint8) '<Module>'::'__msil_mc.m_b'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call void(int16) '<Module>'::'__msil_mc.m_s16'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call void(uint16) '<Module>'::'__msil_mc.m_u16'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call void(native int, int16, uint8, int32) '<Module>'::'__msil_mc.m_tune'{{$}}
; DUMP-LABEL: {{^}}  .method 'use_results' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call int8() '<Module>'::'__msil_mc.m_r8'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call uint8(native int) '<Module>'::'__msil_mc.m_ru8'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call uint8() '<Module>'::'__msil_mc.m_rb'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int16(int16) '<Module>'::'__msil_mc.m_r16'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call uint16() '<Module>'::'__msil_mc.m_ru16'{{$}}

attributes #0 = { "msil-managed-conv"="enum;void" "msil-managed-member"="Set8" "msil-managed-sig"="void(valuetype Ns.Mode8)" "msil-managed-type"="Ns.Narrow" }
attributes #1 = { "msil-managed-conv"="enum;void" "msil-managed-member"="SetU8" "msil-managed-sig"="void(valuetype Ns.ModeU8)" "msil-managed-type"="Ns.Narrow" }
attributes #2 = { "msil-managed-conv"="enum;void" "msil-managed-member"="SetU8" "msil-managed-sig"="void(valuetype Ns.ModeU8)" "msil-managed-type"="Ns.Narrow" }
attributes #3 = { "msil-managed-conv"="enum;void" "msil-managed-member"="Set16" "msil-managed-sig"="void(valuetype Ns.Mode16)" "msil-managed-type"="Ns.Narrow" }
attributes #4 = { "msil-managed-conv"="enum;void" "msil-managed-member"="SetU16" "msil-managed-sig"="void(valuetype Ns.ModeU16)" "msil-managed-type"="Ns.Narrow" }
attributes #5 = { "msil-managed-conv"="this-obj,enum,enum,enum;void" "msil-managed-member"="Tune" "msil-managed-sig"="instance void(valuetype Ns.Mode16, valuetype Ns.ModeU8, valuetype System.IO.FileMode)" "msil-managed-type"="Ns.Narrow" }
attributes #6 = { "msil-managed-conv"=";enum" "msil-managed-member"="Get8" "msil-managed-sig"="valuetype Ns.Mode8()" "msil-managed-type"="Ns.Narrow" }
attributes #7 = { "msil-managed-conv"="this-obj;enum" "msil-managed-member"="get_ModeU8" "msil-managed-sig"="instance valuetype Ns.ModeU8()" "msil-managed-type"="Ns.Narrow" }
attributes #8 = { "msil-managed-conv"=";enum" "msil-managed-member"="GetU8" "msil-managed-sig"="valuetype Ns.ModeU8()" "msil-managed-type"="Ns.Narrow" }
attributes #9 = { "msil-managed-conv"="enum;enum" "msil-managed-member"="Flip16" "msil-managed-sig"="valuetype Ns.Mode16(valuetype Ns.Mode16)" "msil-managed-type"="Ns.Narrow" }
attributes #10 = { "msil-managed-conv"=";enum" "msil-managed-member"="Default" "msil-managed-sig"="field valuetype Ns.ModeU16" "msil-managed-type"="Ns.Narrow" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10}
!0 = !{!"__msil_mc.m_s8", !"Ns.Narrow", !"Set8", !"void(valuetype Ns.Mode8)", !"", !"enum;void", !""}
!1 = !{!"__msil_mc.m_u8", !"Ns.Narrow", !"SetU8", !"void(valuetype Ns.ModeU8)", !"", !"enum;void", !""}
!2 = !{!"__msil_mc.m_b", !"Ns.Narrow", !"SetU8", !"void(valuetype Ns.ModeU8)", !"", !"enum;void", !""}
!3 = !{!"__msil_mc.m_s16", !"Ns.Narrow", !"Set16", !"void(valuetype Ns.Mode16)", !"", !"enum;void", !""}
!4 = !{!"__msil_mc.m_u16", !"Ns.Narrow", !"SetU16", !"void(valuetype Ns.ModeU16)", !"", !"enum;void", !""}
!5 = !{!"__msil_mc.m_tune", !"Ns.Narrow", !"Tune", !"instance void(valuetype Ns.Mode16, valuetype Ns.ModeU8, valuetype System.IO.FileMode)", !"", !"this-obj,enum,enum,enum;void", !""}
!6 = !{!"__msil_mc.m_r8", !"Ns.Narrow", !"Get8", !"valuetype Ns.Mode8()", !"", !";enum", !""}
!7 = !{!"__msil_mc.m_ru8", !"Ns.Narrow", !"get_ModeU8", !"instance valuetype Ns.ModeU8()", !"", !"this-obj;enum", !""}
!8 = !{!"__msil_mc.m_rb", !"Ns.Narrow", !"GetU8", !"valuetype Ns.ModeU8()", !"", !";enum", !""}
!9 = !{!"__msil_mc.m_r16", !"Ns.Narrow", !"Flip16", !"valuetype Ns.Mode16(valuetype Ns.Mode16)", !"", !"enum;enum", !""}
!10 = !{!"__msil_mc.m_ru16", !"Ns.Narrow", !"Default", !"field valuetype Ns.ModeU16", !"", !";enum", !""}
