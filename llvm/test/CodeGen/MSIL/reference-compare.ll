; Comparing .NET references (ImplementationPlan.md 5.21.6 RI6, 5.21.7 RB4,
; RB5; milestone R-a, RV22): clang writes '==' on references as a call of
; the helper 'declare i1 @"__msil_ref.eq"(target("msil.ref"),
; target("msil.ref"))' (5.21.5 RG4; LLVM refuses 'icmp' on target types), and
; MSILTargetLowering::LowerCall turns each direct call of it into no call at
; all (RB5): two null operands give the constant 1, one null operand
; REF_IS_NULL of the other, and two references REF_EQ, i32 values whose bits
; 1-31 are known zero (RB4), selected as:
; - a value: 'ceq' (against 'ldnull' for REF_IS_NULL), with no masking of the
;   i1 when it is zero-extended to int or combined with 'and'; '!=' adds
;   'ldc.i4.1; xor';
; - a branch: 'beq' or 'bne.un' on two references, 'brtrue' or 'brfalse' on
;   one when the other is null, for the condition and for each of its
;   negations ('xor 1', 'icmp eq 0', 'icmp ne 1', 'icmp ne 0', 'icmp eq 1');
; - a select on the result: 'ceq', then SELECT_OBJ's brtrue triangle (RB4:
;   "Missed shapes compile correctly as ceq; brtrue").
; An undefined operand counts as null (RI5). The bodies are pinned whole at
; -O0 (O0) and from -O1 on (OPT), so no 'call' of the helper is left
; anywhere. The helper is never emitted: it appears neither in whole-program
; text, nor in object-form text, nor in an object (no MemberRef, no method,
; no signature), nor in the object's link info (no SYMTAB symbol, RB5, so
; msil-ld never binds it), and it never becomes a P/Invoke. On both host
; x86_64 triples, msil64-unknown-dotnet and aarch64; the text assembles.

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{p} =
; DEFINE: %{nots} = --implicit-check-not=__msil_ref --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=int64 --allow-unused-prefixes
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,%{p} --input-file=%t.%{t}%{o}.il %{nots} && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,%{p} --input-file=%t.of.%{t}%{o}.il %{nots}

; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; REDEFINE: %{p} = O0
; RUN: %{il}
; REDEFINE: %{o} = -O2
; REDEFINE: %{p} = OPT
; RUN: %{il}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O1
; RUN: %{il}
; REDEFINE: %{o} = -O3
; RUN: %{il}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{o} = -O0
; REDEFINE: %{p} = O0
; RUN: %{il}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{il}
; REDEFINE: %{o} = -O2
; REDEFINE: %{p} = OPT
; RUN: %{il}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.win0.dll %t.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx3.dll %t.x86_64-unknown-linux-gnu-msil-O3.il %}

; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=DUMP --implicit-check-not=__msil_ref && \
; DEFINE:   llvm-cil-dump --link-info %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=LINK --implicit-check-not=__msil_ref
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %if llvm-cil-dump %{ %{obj} %}

source_filename = "reference-compare.c"

declare i1 @"__msil_ref.eq"(target("msil.ref"), target("msil.ref")) memory(none) nounwind willreturn speculatable

; a == b as an int: ceq, no masking.
; CHECK-LABEL: .method assembly static int32 'eq_val'(object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  ceq$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'eq_val'
define internal i32 @eq_val(target("msil.ref") %a, target("msil.ref") %b) {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") %b)
  %r = zext i1 %e to i32
  ret i32 %r
}

; a != b as an int.
; CHECK-LABEL: .method assembly static int32 'ne_val'(object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  ceq$}}
; CHECK-NEXT:  {{^  ldc\.i4\.1$}}
; CHECK-NEXT:  {{^  xor$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'ne_val'
define internal i32 @ne_val(target("msil.ref") %a, target("msil.ref") %b) {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") %b)
  %n = xor i1 %e, true
  %r = zext i1 %n to i32
  ret i32 %r
}

; a == NULL and NULL == a: REF_IS_NULL, 'ldnull; ceq'.
; CHECK-LABEL: .method assembly static int32 'isnull_val'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  ceq$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'isnull_val'
define internal i32 @isnull_val(target("msil.ref") %a) {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") zeroinitializer)
  %r = zext i1 %e to i32
  ret i32 %r
}

; CHECK-LABEL: .method assembly static int32 'nullis_val'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  ceq$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'nullis_val'
define internal i32 @nullis_val(target("msil.ref") %a) {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") zeroinitializer, target("msil.ref") %a)
  %r = zext i1 %e to i32
  ret i32 %r
}

; a != NULL as an int.
; CHECK-LABEL: .method assembly static int32 'notnull_val'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  ceq$}}
; CHECK-NEXT:  {{^  ldc\.i4\.1$}}
; CHECK-NEXT:  {{^  xor$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'notnull_val'
define internal i32 @notnull_val(target("msil.ref") %a) {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") zeroinitializer)
  %n = xor i1 %e, true
  %r = zext i1 %n to i32
  ret i32 %r
}

; NULL == NULL: the constant 1.
; CHECK-LABEL: .method assembly static int32 'nulls_val'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldc\.i4\.1$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'nulls_val'
define internal i32 @nulls_val() {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %r = zext i1 %e to i32
  ret i32 %r
}

; An undefined operand is a null one.
; CHECK-LABEL: .method assembly static int32 'undef_val'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  ceq$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'undef_val'
define internal i32 @undef_val(target("msil.ref") %a) {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") undef)
  %r = zext i1 %e to i32
  ret i32 %r
}

; Two results combined with 'and': still no masking.
; CHECK-LABEL: .method assembly static int32 'both'(object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [34]$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  ceq$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  ceq$}}
; CHECK-NEXT:  {{^  and$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'both'
define internal i32 @both(target("msil.ref") %a, target("msil.ref") %b) {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") %b)
  %f = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") zeroinitializer)
  %x = and i1 %e, %f
  %r = zext i1 %x to i32
  ret i32 %r
}

; Branches: 10 is returned when the condition holds, 20 otherwise. At -O0
; the layout keeps the branch to the next block.
; if (a == b): the false edge is 'bne.un'.
; CHECK-LABEL: .method assembly static int32 'eq_br'(object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  bne\.un\.s @BB[0-9]+_2$}}
; O0-NEXT:     {{^  br\.s @BB[0-9]+_1$}}
; O0-NEXT:     {{^@BB[0-9]+_1:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 10$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'eq_br'
define internal i32 @eq_br(target("msil.ref") %a, target("msil.ref") %b) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") %b)
  br i1 %e, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; if (a != b), and the two forms of !(a == b) that compare the i1: 'beq'.
; CHECK-LABEL: .method assembly static int32 'ne_br'(object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  beq\.s @BB[0-9]+_2$}}
; O0-NEXT:     {{^  br\.s @BB[0-9]+_1$}}
; O0-NEXT:     {{^@BB[0-9]+_1:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 10$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'ne_br'
define internal i32 @ne_br(target("msil.ref") %a, target("msil.ref") %b) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") %b)
  %n = xor i1 %e, true
  br i1 %n, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; CHECK-LABEL: .method assembly static int32 'eq_eqfalse_br'(object, object) cil managed
; CHECK:       {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  beq\.s @BB[0-9]+_2$}}
; CHECK:       {{^  ldc\.i4\.s 10$}}
; CHECK:       {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
define internal i32 @eq_eqfalse_br(target("msil.ref") %a, target("msil.ref") %b) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") %b)
  %n = icmp eq i1 %e, false
  br i1 %n, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; CHECK-LABEL: .method assembly static int32 'eq_netrue_br'(object, object) cil managed
; CHECK:       {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  beq\.s @BB[0-9]+_2$}}
; CHECK:       {{^  ldc\.i4\.s 10$}}
; CHECK:       {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
define internal i32 @eq_netrue_br(target("msil.ref") %a, target("msil.ref") %b) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") %b)
  %n = icmp ne i1 %e, true
  br i1 %n, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; if (a == NULL), and its forms that compare the i1 with 1 or test it
; against 0: a non-null a goes to 20 ('brtrue').
; CHECK-LABEL: .method assembly static int32 'isnull_br'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  brtrue\.s @BB[0-9]+_2$}}
; O0-NEXT:     {{^  br\.s @BB[0-9]+_1$}}
; O0-NEXT:     {{^@BB[0-9]+_1:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 10$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'isnull_br'
define internal i32 @isnull_br(target("msil.ref") %a) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") zeroinitializer)
  br i1 %e, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; CHECK-LABEL: .method assembly static int32 'isnull_eqtrue_br'(object) cil managed
; CHECK:       {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  brtrue\.s @BB[0-9]+_2$}}
; CHECK:       {{^  ldc\.i4\.s 10$}}
; CHECK:       {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
define internal i32 @isnull_eqtrue_br(target("msil.ref") %a) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") zeroinitializer)
  %n = icmp eq i1 %e, true
  br i1 %n, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; CHECK-LABEL: .method assembly static int32 'isnull_nefalse_br'(object) cil managed
; CHECK:       {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  brtrue\.s @BB[0-9]+_2$}}
; CHECK:       {{^  ldc\.i4\.s 10$}}
; CHECK:       {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
define internal i32 @isnull_nefalse_br(target("msil.ref") %a) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") zeroinitializer)
  %n = icmp ne i1 %e, false
  br i1 %n, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; if (a != NULL), and the forms that compare the i1 with 0 or test it
; against 1: a null a goes to 20 ('brfalse').
; CHECK-LABEL: .method assembly static int32 'notnull_br'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  brfalse\.s @BB[0-9]+_2$}}
; O0-NEXT:     {{^  br\.s @BB[0-9]+_1$}}
; O0-NEXT:     {{^@BB[0-9]+_1:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 10$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'notnull_br'
define internal i32 @notnull_br(target("msil.ref") %a) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") zeroinitializer)
  %n = xor i1 %e, true
  br i1 %n, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; CHECK-LABEL: .method assembly static int32 'isnull_eqfalse_br'(object) cil managed
; CHECK:       {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  brfalse\.s @BB[0-9]+_2$}}
; CHECK:       {{^  ldc\.i4\.s 10$}}
; CHECK:       {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
define internal i32 @isnull_eqfalse_br(target("msil.ref") %a) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") zeroinitializer)
  %n = icmp eq i1 %e, false
  br i1 %n, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; CHECK-LABEL: .method assembly static int32 'isnull_netrue_br'(object) cil managed
; CHECK:       {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  brfalse\.s @BB[0-9]+_2$}}
; CHECK:       {{^  ldc\.i4\.s 10$}}
; CHECK:       {{^@BB[0-9]+_2:$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 20$}}
define internal i32 @isnull_netrue_br(target("msil.ref") %a) {
entry:
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") zeroinitializer)
  %n = icmp ne i1 %e, true
  br i1 %n, label %yes, label %no
yes:
  ret i32 10
no:
  ret i32 20
}

; A select on a == b: 'ceq', then the brtrue triangle.
; CHECK-LABEL: .method assembly static object 'sel_eq'(object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; O0-NEXT:     {{^  \.locals \(int32, object, object\)$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  ceq$}}
; CHECK:       {{^  brtrue\.s @BB[0-9]+_2$}}
; CHECK:       {{^@BB[0-9]+_2:$}}
; CHECK:       {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'sel_eq'
define internal target("msil.ref") @sel_eq(target("msil.ref") %a, target("msil.ref") %b) {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") %b)
  %r = select i1 %e, target("msil.ref") %a, target("msil.ref") zeroinitializer
  ret target("msil.ref") %r
}

; CHECK-LABEL: .method public static int32 'main'() cil managed
; CHECK:       {{^  call int32 'eq_val'\(object, object\)$}}
; CHECK:       {{^  call object 'sel_eq'\(object, object\)$}}
; CHECK:       {{^}}} // end of method 'main'
define i32 @main() {
  %a = call i32 @eq_val(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %b = call i32 @ne_val(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %c = call i32 @isnull_val(target("msil.ref") zeroinitializer)
  %d = call i32 @nullis_val(target("msil.ref") zeroinitializer)
  %e = call i32 @notnull_val(target("msil.ref") zeroinitializer)
  %f = call i32 @nulls_val()
  %g = call i32 @undef_val(target("msil.ref") zeroinitializer)
  %h = call i32 @both(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %i = call i32 @eq_br(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %j = call i32 @ne_br(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %k = call i32 @eq_eqfalse_br(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %l = call i32 @eq_netrue_br(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %m = call i32 @isnull_br(target("msil.ref") zeroinitializer)
  %n = call i32 @isnull_eqtrue_br(target("msil.ref") zeroinitializer)
  %o = call i32 @isnull_nefalse_br(target("msil.ref") zeroinitializer)
  %p = call i32 @notnull_br(target("msil.ref") zeroinitializer)
  %q = call i32 @isnull_eqfalse_br(target("msil.ref") zeroinitializer)
  %r = call i32 @isnull_netrue_br(target("msil.ref") zeroinitializer)
  %s = call target("msil.ref") @sel_eq(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %t = call i32 @isnull_val(target("msil.ref") %s)
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  %s4 = add i32 %s3, %e
  %s5 = add i32 %s4, %f
  %s6 = add i32 %s5, %g
  %s7 = add i32 %s6, %h
  %s8 = add i32 %s7, %i
  %s9 = add i32 %s8, %j
  %s10 = add i32 %s9, %k
  %s11 = add i32 %s10, %l
  %s12 = add i32 %s11, %m
  %s13 = add i32 %s12, %n
  %s14 = add i32 %s13, %o
  %s15 = add i32 %s14, %p
  %s16 = add i32 %s15, %q
  %s17 = add i32 %s16, %r
  %s18 = add i32 %s17, %t
  ret i32 %s18
}

; The objects: the methods are there, the helper is not (the implicit check
; covers the whole listing: MemberRefs, methods and signatures).
; DUMP:       {{^}}.class '<Module>'
; DUMP:       {{^}}  .method 'eq_val' int32(object, object) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: ceq{{$}}
; DUMP:       {{^}}  .method 'eq_br' int32(object, object) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: bne.un.s L{{[0-9]+}}{{$}}
; DUMP:       {{^}}  .method 'isnull_br' int32(object) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: brtrue.s L{{[0-9]+}}{{$}}
; DUMP:       {{^}}  .method 'notnull_br' int32(object) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: brfalse.s L{{[0-9]+}}{{$}}
; DUMP:       {{^}}  .method 'main' int32() flags=

; LINK:     {{^}}LinkInfo v1.{{[0-9]+}} triple={{.+}}{{$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT 'eq_val' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT 'main' method=0x06{{[0-9A-F]+$}}
