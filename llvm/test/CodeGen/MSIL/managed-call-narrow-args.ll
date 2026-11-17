; The narrowing of a managed-call stub's narrow integer parameters that have
; no extension attribute (ImplementationPlan.md 5.20.3 MS5 as amended in
; section 12, the row '5.20.3 MS5 amended (...): a stub parameter of IR type
; i1, i8 or i16 that has neither signext nor zeroext carries undefined upper
; bits ...' of 2026-11-06; milestone MC-1). clang gives narrow integer
; parameters no extension attribute on the Windows ABIs (on
; x86_64-pc-windows-msvc-msil 'char', 'signed char', 'unsigned char', 'short',
; 'unsigned short' and 'wchar_t'; on aarch64-pc-windows-msvc-msil '_Bool'
; too) and on non-Darwin AAPCS64 (aarch64-unknown-linux-gnu-msil, '_Bool'
; included). The int32 argument slot of such a parameter carries undefined
; upper bits: @use passes its 'trunc' results as they are, as -O2 code does.
; So the stub narrows the argument before the row's IL:
; - the 'bool' row: 'conv.u1' for an i8 (b2i_c), 'conv.u2' for an i16 (b2i_s)
;   and 'ldc.i4.1; and' for an i1 (b2i_b), each followed by the row's
;   'ldc.i4.0; cgt.un', so (char)256 and (short)65536 (C value 0) and a
;   _Bool 0 in an int32 slot holding 2 reach the member as false;
; - the 'val' row of an 8-bit CIL type and the 'enum' row: 'ldc.i4.1; and'
;   for an i1 (u8_b: int32 System.Convert::ToInt32(uint8); max8_b: both
;   parameters of uint8 System.Math::Max(uint8, uint8); mode_b: an enum
;   parameter, whose stub parameter keeps its exact type 'unsigned int8',
;   uint8 in objects, as managed-call-narrow.ll describes). An i8 or i16 there
;   needs nothing: the call truncates the int32 to the CIL parameter's width
;   (ECMA-335 III.1.6), and MS5 matches the widths (s8_c to int8, s16_s to
;   int16, mode8_c to an int8 enum, isdigit16 to char).
; The stub's .maxstack counts the added instructions. It is pinned where the
; added 'ldc.i4.1' sets the peak: 2 for u8_b and mode_b (1 without the
; narrowing), 3 for max8_b (its second 'ldc.i4.1' sits on two values); the
; other stubs' .maxstack is not pinned.
; Controls, with the IL these stubs had before the narrowing: parameters with
; signext or zeroext, which x86_64-unknown-linux-gnu-msil writes (b2i_sc 'i8
; signext', b2i_us 'i16 zeroext', b2i_bz 'i1 zeroext'), take only the row's
; IL, and so do the unextended i8 and i16 parameters of the 'val' and 'enum'
; rows above (s8_c, s16_s, mode8_c, isdigit16).
; Each declaration is the one clang writes for the C declaration in its
; comment, on the triple named there. The module runs on the triples whose
; clang writes the unextended forms (x86_64-pc-windows-msvc-msil,
; aarch64-unknown-linux-gnu-msil, aarch64-pc-windows-msvc-msil) and on
; x86_64-unknown-linux-gnu-msil and msil64-unknown-dotnet, at -O2 and -O0
; (the stubs depend on neither). Whole-program text (CORE=mscorlib) and
; object-form text (CORE=msil.bcl) are pinned whole from '.method' to
; '} // end of method', and the call sites name the stubs' parameter types;
; the whole-program text assembles. The two 'enum' stubs (mode_b, mode8_c)
; differ there: whole-program text, which no link checks, starts with T-M7's
; check made at run time (the WP lines: 'ldtoken E', System.Type's
; GetTypeFromHandle and get_IsEnum(), 'sizeof E' against the parameter's
; width in bytes, and the T-M7 text thrown as InvalidProgramException on a
; mismatch, with .maxstack 2 and no local), and object-form text has none
; (the OF lines), as the objects do. The objects, through llvm-cil-dump, have
; the same bodies (DUMP), and the object that ilasm makes from the
; object-form text has the same canonical listing and link info as llc's
; object (5.12.6's object oracle), which checks the encoding of the added
; instructions.

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,WP -DCORE=mscorlib --input-file=%t.wp.%{t}%{o}.il && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,OF -DCORE=msil.bcl --input-file=%t.of.%{t}%{o}.il

; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; RUN: %{il}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{il}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{il}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.win.dll %t.wp.x86_64-pc-windows-msvc-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.a64.dll %t.wp.aarch64-unknown-linux-gnu-msil-O0.il %}
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
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}

source_filename = "managed-call-narrow-args.c"

; int b2i_c(char) on x86_64-pc-windows-msvc-msil: the 'bool' row narrows an
; unextended i8 with conv.u1.
; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.b2i_c'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Convert::ToInt32(bool){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.b2i_c'{{$}}
declare i32 @__msil_mc.b2i_c(i8 noundef) local_unnamed_addr #0

; int b2i_s(short) on x86_64-pc-windows-msvc-msil: conv.u2 for an unextended
; i16.
; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.b2i_s'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Convert::ToInt32(bool){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.b2i_s'{{$}}
declare i32 @__msil_mc.b2i_s(i16 noundef) local_unnamed_addr #0

; int b2i_b(_Bool) on aarch64-unknown-linux-gnu-msil and
; aarch64-pc-windows-msvc-msil: 'ldc.i4.1; and' for an unextended i1, then
; the row's 'ldc.i4.0; cgt.un'.
; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.b2i_b'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Convert::ToInt32(bool){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.b2i_b'{{$}}
declare i32 @__msil_mc.b2i_b(i1 noundef) local_unnamed_addr #0

; int u8_b(_Bool) for int32 System.Convert::ToInt32(uint8), the 'val' row of
; an 8-bit CIL type, on the aarch64 triples: 'ldc.i4.1; and' before the call,
; which sets the stub's peak.
; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.u8_b'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Convert::ToInt32(uint8){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.u8_b'{{$}}
declare i32 @__msil_mc.u8_b(i1 noundef) local_unnamed_addr #1

; unsigned char max8_b(_Bool, _Bool) for uint8 System.Math::Max(uint8, uint8)
; on aarch64-unknown-linux-gnu-msil: each parameter is masked, and the second
; 'ldc.i4.1' sets the peak at 3.
; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.max8_b'(int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 3$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  call uint8 {{\[}}[[CORE]]]System.Math::Max(uint8, uint8){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.max8_b'{{$}}
declare i8 @__msil_mc.max8_b(i1 noundef, i1 noundef) local_unnamed_addr #2

; void mode_b(_Bool) for an enum with an 8-bit underlying type, on the
; aarch64 triples: the 'enum' row masks the i1, and the stub parameter keeps
; its exact type. Whole-program text starts with the enum check (an i1
; counts 8 bits); object-form text does not have it.
; CHECK-LABEL: {{^}}.method assembly static void '__msil_mc.mode_b'(unsigned int8) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.ModeU8{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'mode_b': parameter 1 passes 8 bits for 'Ns.ModeU8', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]Ns.Narrow::SetU8(valuetype {{\[}}[[CORE]]]Ns.ModeU8){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.mode_b'{{$}}
declare void @__msil_mc.mode_b(i1 noundef) local_unnamed_addr #3

; Controls: int b2i_sc(char), int b2i_us(unsigned short) and int
; b2i_bz(_Bool) on x86_64-unknown-linux-gnu-msil, whose extension attributes
; leave only the row's IL.
; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.b2i_sc'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Convert::ToInt32(bool){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.b2i_sc'{{$}}
declare i32 @__msil_mc.b2i_sc(i8 noundef signext) local_unnamed_addr #0

; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.b2i_us'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Convert::ToInt32(bool){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.b2i_us'{{$}}
declare i32 @__msil_mc.b2i_us(i16 noundef zeroext) local_unnamed_addr #0

; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.b2i_bz'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Convert::ToInt32(bool){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.b2i_bz'{{$}}
declare i32 @__msil_mc.b2i_bz(i1 noundef zeroext) local_unnamed_addr #0

; Controls: the unextended i8 and i16 of the 'val' and 'enum' rows need
; nothing. int s8_c(signed char) for int32 System.Convert::ToInt32(int8) and
; int s16_s(short) for ToInt32(int16) on x86_64-pc-windows-msvc-msil.
; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.s8_c'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Convert::ToInt32(int8){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.s8_c'{{$}}
declare i32 @__msil_mc.s8_c(i8 noundef) local_unnamed_addr #4

; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.s16_s'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Convert::ToInt32(int16){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.s16_s'{{$}}
declare i32 @__msil_mc.s16_s(i16 noundef) local_unnamed_addr #5

; void mode8_c(signed char) for an enum with an int8 underlying type on
; x86_64-pc-windows-msvc-msil: the stub parameter is int8, no narrowing. The
; whole-program text's enum check needs .maxstack 2.
; CHECK-LABEL: {{^}}.method assembly static void '__msil_mc.mode8_c'(int8) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; WP-NEXT:     {{^  \.maxstack 2$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.Mode8{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.Mode8{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'mode8_c': parameter 1 passes 8 bits for 'Ns.Mode8', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; OF-NEXT:     {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]Ns.Narrow::Set8(valuetype {{\[}}[[CORE]]]Ns.Mode8){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.mode8_c'{{$}}
declare void @__msil_mc.mode8_c(i8 noundef) local_unnamed_addr #6

; _Bool isdigit16(char16_t) for bool System.Char::IsDigit(char) on
; x86_64-pc-windows-msvc-msil: a 16-bit 'val' row; only the 'bool' result is
; normalized.
; CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.isdigit16'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call bool {{\[}}[[CORE]]]System.Char::IsDigit(char){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.isdigit16'{{$}}
declare zeroext i1 @__msil_mc.isdigit16(i16 noundef) local_unnamed_addr #7

; The callers pass the truncated values as they are; the call sites name the
; stubs' parameter types.
; CHECK-LABEL: {{^}}.method public static int32 'use'(int32) cil managed{{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.b2i_c'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.b2i_s'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.b2i_b'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.u8_b'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.max8_b'(int32, int32){{$}}
; CHECK:       {{^}}  call void '__msil_mc.mode_b'(unsigned int8){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.b2i_sc'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.b2i_us'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.b2i_bz'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.s8_c'(int32){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.s16_s'(int32){{$}}
; CHECK:       {{^}}  call void '__msil_mc.mode8_c'(int8){{$}}
; CHECK:       {{^}}  call int32 '__msil_mc.isdigit16'(int32){{$}}
; CHECK:       {{^}}} // end of method 'use'{{$}}
define i32 @use(i32 noundef %v) local_unnamed_addr {
entry:
  %c = trunc i32 %v to i8
  %s = trunc i32 %v to i16
  %b = trunc i32 %v to i1
  %r1 = tail call i32 @__msil_mc.b2i_c(i8 noundef %c)
  %r2 = tail call i32 @__msil_mc.b2i_s(i16 noundef %s)
  %r3 = tail call i32 @__msil_mc.b2i_b(i1 noundef %b)
  %r4 = tail call i32 @__msil_mc.u8_b(i1 noundef %b)
  %r5 = tail call i8 @__msil_mc.max8_b(i1 noundef %b, i1 noundef %b)
  tail call void @__msil_mc.mode_b(i1 noundef %b)
  %r6 = tail call i32 @__msil_mc.b2i_sc(i8 noundef signext %c)
  %r7 = tail call i32 @__msil_mc.b2i_us(i16 noundef zeroext %s)
  %r8 = tail call i32 @__msil_mc.b2i_bz(i1 noundef zeroext %b)
  %r9 = tail call i32 @__msil_mc.s8_c(i8 noundef %c)
  %r10 = tail call i32 @__msil_mc.s16_s(i16 noundef %s)
  tail call void @__msil_mc.mode8_c(i8 noundef %c)
  %r11 = tail call zeroext i1 @__msil_mc.isdigit16(i16 noundef %s)
  %r5x = zext i8 %r5 to i32
  %r11x = zext i1 %r11 to i32
  %a1 = add i32 %r1, %r2
  %a2 = add i32 %a1, %r3
  %a3 = add i32 %a2, %r4
  %a4 = add i32 %a3, %r5x
  %a5 = add i32 %a4, %r6
  %a6 = add i32 %a5, %r7
  %a7 = add i32 %a6, %r8
  %a8 = add i32 %a7, %r9
  %a9 = add i32 %a8, %r10
  %a10 = add i32 %a9, %r11x
  ret i32 %a10
}

; Objects: the same bodies, the MethodDef signature blobs and the call-site
; signatures.
; DUMP-LABEL: {{^}}  .method '__msil_mc.b2i_c' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: conv.u1{{$}}
; DUMP-NEXT:  {{^}}    L2: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L3: cgt.un{{$}}
; DUMP-NEXT:  {{^}}    L4: call int32(bool) [msil.bcl]System.Convert::'ToInt32'{{$}}
; DUMP-NEXT:  {{^}}    L5: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.b2i_s' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: conv.u2{{$}}
; DUMP-NEXT:  {{^}}    L2: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L3: cgt.un{{$}}
; DUMP-NEXT:  {{^}}    L4: call int32(bool) [msil.bcl]System.Convert::'ToInt32'{{$}}
; DUMP-NEXT:  {{^}}    L5: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.b2i_b' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: ldc.i4.1{{$}}
; DUMP-NEXT:  {{^}}    L2: and{{$}}
; DUMP-NEXT:  {{^}}    L3: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L4: cgt.un{{$}}
; DUMP-NEXT:  {{^}}    L5: call int32(bool) [msil.bcl]System.Convert::'ToInt32'{{$}}
; DUMP-NEXT:  {{^}}    L6: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.u8_b' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack 2{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: ldc.i4.1{{$}}
; DUMP-NEXT:  {{^}}    L2: and{{$}}
; DUMP-NEXT:  {{^}}    L3: call int32(uint8) [msil.bcl]System.Convert::'ToInt32'{{$}}
; DUMP-NEXT:  {{^}}    L4: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.max8_b' int32(int32, int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack 3{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: ldc.i4.1{{$}}
; DUMP-NEXT:  {{^}}    L2: and{{$}}
; DUMP-NEXT:  {{^}}    L3: ldarg.1{{$}}
; DUMP-NEXT:  {{^}}    L4: ldc.i4.1{{$}}
; DUMP-NEXT:  {{^}}    L5: and{{$}}
; DUMP-NEXT:  {{^}}    L6: call uint8(uint8, uint8) [msil.bcl]System.Math::'Max'{{$}}
; DUMP-NEXT:  {{^}}    L7: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.mode_b' void(uint8) flags=
; DUMP-NEXT:  {{^}}    .maxstack 2{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: ldc.i4.1{{$}}
; DUMP-NEXT:  {{^}}    L2: and{{$}}
; DUMP-NEXT:  {{^}}    L3: call void(valuetype [msil.bcl]Ns.ModeU8) [msil.bcl]Ns.Narrow::'SetU8'{{$}}
; DUMP-NEXT:  {{^}}    L4: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.b2i_sc' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L2: cgt.un{{$}}
; DUMP-NEXT:  {{^}}    L3: call int32(bool) [msil.bcl]System.Convert::'ToInt32'{{$}}
; DUMP-NEXT:  {{^}}    L4: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.b2i_us' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L2: cgt.un{{$}}
; DUMP-NEXT:  {{^}}    L3: call int32(bool) [msil.bcl]System.Convert::'ToInt32'{{$}}
; DUMP-NEXT:  {{^}}    L4: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.b2i_bz' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L2: cgt.un{{$}}
; DUMP-NEXT:  {{^}}    L3: call int32(bool) [msil.bcl]System.Convert::'ToInt32'{{$}}
; DUMP-NEXT:  {{^}}    L4: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.s8_c' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call int32(int8) [msil.bcl]System.Convert::'ToInt32'{{$}}
; DUMP-NEXT:  {{^}}    L2: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.s16_s' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call int32(int16) [msil.bcl]System.Convert::'ToInt32'{{$}}
; DUMP-NEXT:  {{^}}    L2: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.mode8_c' void(int8) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call void(valuetype [msil.bcl]Ns.Mode8) [msil.bcl]Ns.Narrow::'Set8'{{$}}
; DUMP-NEXT:  {{^}}    L2: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.isdigit16' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call bool(char) [msil.bcl]System.Char::'IsDigit'{{$}}
; DUMP-NEXT:  {{^}}    L2: ldc.i4.0{{$}}
; DUMP-NEXT:  {{^}}    L3: cgt.un{{$}}
; DUMP-NEXT:  {{^}}    L4: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'use' int32(int32) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.b2i_c'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.b2i_s'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.b2i_b'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.u8_b'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32, int32) '<Module>'::'__msil_mc.max8_b'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call void(uint8) '<Module>'::'__msil_mc.mode_b'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.b2i_sc'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.b2i_us'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.b2i_bz'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.s8_c'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.s16_s'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call void(int8) '<Module>'::'__msil_mc.mode8_c'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.isdigit16'{{$}}

attributes #0 = { "msil-managed-conv"="bool;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(bool)" "msil-managed-type"="System.Convert" }
attributes #1 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(uint8)" "msil-managed-type"="System.Convert" }
attributes #2 = { "msil-managed-conv"="val,val;val" "msil-managed-member"="Max" "msil-managed-sig"="uint8(uint8, uint8)" "msil-managed-type"="System.Math" }
attributes #3 = { "msil-managed-conv"="enum;void" "msil-managed-member"="SetU8" "msil-managed-sig"="void(valuetype Ns.ModeU8)" "msil-managed-type"="Ns.Narrow" }
attributes #4 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(int8)" "msil-managed-type"="System.Convert" }
attributes #5 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(int16)" "msil-managed-type"="System.Convert" }
attributes #6 = { "msil-managed-conv"="enum;void" "msil-managed-member"="Set8" "msil-managed-sig"="void(valuetype Ns.Mode8)" "msil-managed-type"="Ns.Narrow" }
attributes #7 = { "msil-managed-conv"="val;bool" "msil-managed-member"="IsDigit" "msil-managed-sig"="bool(char)" "msil-managed-type"="System.Char" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12}
!0 = !{!"__msil_mc.b2i_c", !"System.Convert", !"ToInt32", !"int32(bool)", !"", !"bool;val", !""}
!1 = !{!"__msil_mc.b2i_s", !"System.Convert", !"ToInt32", !"int32(bool)", !"", !"bool;val", !""}
!2 = !{!"__msil_mc.b2i_b", !"System.Convert", !"ToInt32", !"int32(bool)", !"", !"bool;val", !""}
!3 = !{!"__msil_mc.u8_b", !"System.Convert", !"ToInt32", !"int32(uint8)", !"", !"val;val", !""}
!4 = !{!"__msil_mc.max8_b", !"System.Math", !"Max", !"uint8(uint8, uint8)", !"", !"val,val;val", !""}
!5 = !{!"__msil_mc.mode_b", !"Ns.Narrow", !"SetU8", !"void(valuetype Ns.ModeU8)", !"", !"enum;void", !""}
!6 = !{!"__msil_mc.b2i_sc", !"System.Convert", !"ToInt32", !"int32(bool)", !"", !"bool;val", !""}
!7 = !{!"__msil_mc.b2i_us", !"System.Convert", !"ToInt32", !"int32(bool)", !"", !"bool;val", !""}
!8 = !{!"__msil_mc.b2i_bz", !"System.Convert", !"ToInt32", !"int32(bool)", !"", !"bool;val", !""}
!9 = !{!"__msil_mc.s8_c", !"System.Convert", !"ToInt32", !"int32(int8)", !"", !"val;val", !""}
!10 = !{!"__msil_mc.s16_s", !"System.Convert", !"ToInt32", !"int32(int16)", !"", !"val;val", !""}
!11 = !{!"__msil_mc.mode8_c", !"Ns.Narrow", !"Set8", !"void(valuetype Ns.Mode8)", !"", !"enum;void", !""}
!12 = !{!"__msil_mc.isdigit16", !"System.Char", !"IsDigit", !"bool(char)", !"", !"val;bool", !""}
