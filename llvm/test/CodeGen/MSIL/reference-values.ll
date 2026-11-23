; .NET references as IL values (ImplementationPlan.md 5.21.6 RI4, RI5,
; 5.21.7 RB1-RB9, 5.21.8 RO1-RO2; milestone R-a, RV21): target("msil.ref")
; is MVT::externref (RI2), the register class OBJ (RB2) and the CIL type
; 'object' (RB1), so a reference lives only in IL arguments, locals, results
; and the evaluation stack:
; - method headers and call sites spell it 'object' (RO2), and '.locals'
;   holds 'object' slots beside the native int ones, never one slot for both
;   classes (RB8: RegColoring merges registers of one class only; 'mixed');
; - the structural records of the OBJ class (RB3) print as the other
;   classes' do: ldarg.0-3 and ldarg.s (ARG_GET_OBJ), starg.s (ARG_SET_OBJ),
;   'dup; starg.s' (ARG_TEE_OBJ), ldloc/stloc with their short forms
;   (LOCAL_GET_OBJ, LOCAL_SET_OBJ), 'dup; stloc' (LOCAL_TEE_OBJ), 'pop'
;   (DROP_OBJ for an unused result);
; - the null reference 'zeroinitializer' is 'ldnull' (CONST_NULL, RI5); a
;   'select' is a brtrue triangle (SELECT_OBJ, decision D34); 'freeze' is
;   free; phis go through IL locals or arguments; direct calls pass and
;   return objects (RI4);
; - an undefined reference is 'ldnull' at every level (RI5 and RB7 as built,
;   section 12's row '5.21.7 RB7 and 5.21.6 RI5 as built' of 2026-11-21): at
;   -O0 its IMPLICIT_DEF reaches ExplicitLocals, which writes 'ldnull' for
;   it; from -O1 on the generic ProcessImplicitDefs pass erases an
;   IMPLICIT_DEF whose users are not phis and marks those uses 'undef', and
;   ExplicitLocals writes 'ldnull' for each such use of an object instead of
;   a read of a local. So a returned 'undef' or 'poison' is 'ldnull' and no
;   local ('undef_ref', 'poison_ref'), the 'undef' input of a phi is stored
;   as 'ldnull' into the phi's slot ('undef_phi'), and 'poison' and 'undef'
;   arguments are 'ldnull' in a method with a C frame, where RegColoring
;   gives their register the object local of a later call result ('framed':
;   the same code at every level, and that local is written before it is
;   read). No object local is read before it is written: 5.21.11's
;   measurement (recon/p3/references-files/zinit.il) covered only object
;   locals that are never written, and a read of a local that a later
;   instruction writes gave a non-null object at run time;
; - a reference function where control can reach the closing brace ends
;   there in llvm.trap and unreachable (5.21.5 RG12): 'falls' returns its
;   object on one path and is 'ldnull' and 'throw' on the other (decision
;   D8), its result 'object' at every level;
; - .maxstack counts objects like any other value (RB9: StackCheck
;   unchanged): six for the six-argument call.
; The whole bodies are pinned at -O0 (O0) and at -O1, -O2 and -O3 (OPT,
; which give the same code here) on both host x86_64 triples,
; msil64-unknown-dotnet and aarch64, whole-program text and object-form text
; alike (the bodies hold no scope). Only 'framed' has a C frame: 'localloc'
; appears only in its pinned body, and no 'ldind' or 'stind' anywhere (the
; implicit checks). The objects (RO1) are read back through
; llvm-cil-dump: ELEMENT_TYPE_OBJECT in every signature and in .locals, the
; same instructions, and no reference function as a GLOBAL symbol of the
; link info (each is internal). The object oracle (5.12.6) compares the
; object llc writes with the one ilasm assembles from the object-form text,
; and the whole-program text assembles with ilasm.

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{p} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,%{p} --input-file=%t.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=int64 --implicit-check-not=localloc --implicit-check-not=ldind \
; DEFINE:     --implicit-check-not=stind --implicit-check-not=__msil_ref && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,%{p} --input-file=%t.of.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=int64 --implicit-check-not=localloc --implicit-check-not=ldind \
; DEFINE:     --implicit-check-not=stind --implicit-check-not=__msil_ref

; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; REDEFINE: %{p} = O0
; RUN: %{il}
; REDEFINE: %{o} = -O1
; REDEFINE: %{p} = OPT
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{o} = -O3
; RUN: %{il}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{o} = -O0
; REDEFINE: %{p} = O0
; RUN: %{il}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{il}
; REDEFINE: %{o} = -O2
; REDEFINE: %{p} = OPT
; RUN: %{il}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{il}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; REDEFINE: %{p} = O0
; RUN: %{il}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.win0.dll %t.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win3.dll %t.x86_64-pc-windows-msvc-msil-O3.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx2.dll %t.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.p64.dll %t.msil64-unknown-dotnet-O0.il %}

; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefixes=DUMP,DUMP-%{p} --implicit-check-not=__msil_ref && \
; DEFINE:   llvm-cil-dump --link-info %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=LINK --implicit-check-not=GLOBAL --implicit-check-not=WEAK
; DEFINE: %{oracle} = %ilasm-verify -output=%t.of.%{t}%{o}.ilasm.o %t.of.%{t}%{o}.il && \
; DEFINE:   %{canon} %t.%{t}%{o}.o | sed -e "/^pe /d" > %t.%{t}%{o}.canon1 && \
; DEFINE:   %{canon} %t.of.%{t}%{o}.ilasm.o | sed -e "/^pe /d" > %t.%{t}%{o}.canon2 && \
; DEFINE:   diff %t.%{t}%{o}.canon1 %t.%{t}%{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t.%{t}%{o}.o > %t.%{t}%{o}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t.of.%{t}%{o}.ilasm.o > %t.%{t}%{o}.link2 && \
; DEFINE:   diff %t.%{t}%{o}.link1 %t.%{t}%{o}.link2
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; REDEFINE: %{p} = O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; REDEFINE: %{p} = OPT
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{o} = -O0
; REDEFINE: %{p} = O0
; RUN: %if llvm-cil-dump %{ %{obj} %}

source_filename = "reference-values.c"

; CHECK-LABEL: .method assembly static object 'ident'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'ident'
define internal target("msil.ref") @ident(target("msil.ref") %a) {
  ret target("msil.ref") %a
}

; CHECK-LABEL: .method assembly static object 'null_ref'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'null_ref'
define internal target("msil.ref") @null_ref() {
  ret target("msil.ref") zeroinitializer
}

; An undefined result: 'ldnull' and no local at every level.
; CHECK-LABEL: .method assembly static object 'undef_ref'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'undef_ref'
define internal target("msil.ref") @undef_ref() {
  ret target("msil.ref") undef
}

; CHECK-LABEL: .method assembly static object 'poison_ref'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'poison_ref'
define internal target("msil.ref") @poison_ref() {
  ret target("msil.ref") poison
}

; CHECK-LABEL: .method assembly static object 'frozen'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'frozen'
define internal target("msil.ref") @frozen(target("msil.ref") %a) {
  %f = freeze target("msil.ref") %a
  ret target("msil.ref") %f
}

; A select of two objects: the brtrue triangle of SELECT_OBJ.
; CHECK-LABEL: .method assembly static object 'pick'(int32, object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; O0-NEXT:     {{^  \.locals \(int32, object\)$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ldc\.i4\.0$}}
; CHECK-NEXT:  {{^  cgt\.un$}}
; O0-NEXT:     {{^  stloc\.0$}}
; O0-NEXT:     {{^  ldarg\.1$}}
; O0-NEXT:     {{^  stloc\.1$}}
; O0-NEXT:     {{^  ldloc\.0$}}
; CHECK-NEXT:  {{^  brtrue\.s (@BB[0-9]+_[0-9]+)$}}
; O0-NEXT:     {{^  ldarg\.2$}}
; O0-NEXT:     {{^  stloc\.1$}}
; O0-NEXT:     {{^@BB[0-9]+_[0-9]+:$}}
; O0-NEXT:     {{^  ldloc\.1$}}
; OPT-NEXT:    {{^  ldarg\.2$}}
; OPT-NEXT:    {{^  starg\.s 1$}}
; OPT-NEXT:    {{^@BB[0-9]+_[0-9]+:$}}
; OPT-NEXT:    {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'pick'
define internal target("msil.ref") @pick(i32 %c, target("msil.ref") %a, target("msil.ref") %b) {
  %t = icmp ne i32 %c, 0
  %r = select i1 %t, target("msil.ref") %a, target("msil.ref") %b
  ret target("msil.ref") %r
}

; The sixth argument is read with ldarg.s.
; CHECK-LABEL: .method assembly static object 'many'(int32, object, int32, object, object, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldarg\.s 5$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'many'
define internal target("msil.ref") @many(i32 %a, target("msil.ref") %b, i32 %c, target("msil.ref") %d, target("msil.ref") %e, target("msil.ref") %f) {
  ret target("msil.ref") %f
}

; An unused object result is popped.
; CHECK-LABEL: .method assembly static void 'drop'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  call object 'frozen'\(object\)$}}
; CHECK-NEXT:  {{^  pop$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'drop'
define internal void @drop(target("msil.ref") %a) {
  %r = call target("msil.ref") @frozen(target("msil.ref") %a)
  ret void
}

; An object used four times: an object local at -O0, a 'dup; starg.s' of the
; argument it replaces from -O1 on; six values on the stack for the call.
; CHECK-LABEL: .method assembly static int32 'twice'(object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 6$}}
; O0-NEXT:     {{^  \.locals \(object, int32\)$}}
; O0-NEXT:     {{^  ldarg\.0$}}
; O0-NEXT:     {{^  call object 'frozen'\(object\)$}}
; O0-NEXT:     {{^  stloc\.0$}}
; O0-NEXT:     {{^  ldc\.i4\.2$}}
; O0-NEXT:     {{^  stloc\.1$}}
; O0-NEXT:     {{^  ldc\.i4\.1$}}
; O0-NEXT:     {{^  ldloc\.0$}}
; O0-NEXT:     {{^  ldloc\.1$}}
; O0-NEXT:     {{^  ldloc\.0$}}
; O0-NEXT:     {{^  ldloc\.0$}}
; O0-NEXT:     {{^  ldloc\.0$}}
; OPT-NEXT:    {{^  ldc\.i4\.1$}}
; OPT-NEXT:    {{^  ldarg\.0$}}
; OPT-NEXT:    {{^  call object 'frozen'\(object\)$}}
; OPT-NEXT:    {{^  dup$}}
; OPT-NEXT:    {{^  starg\.s 0$}}
; OPT-NEXT:    {{^  ldc\.i4\.2$}}
; OPT-NEXT:    {{^  ldarg\.0$}}
; OPT-NEXT:    {{^  ldarg\.0$}}
; OPT-NEXT:    {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  call object 'many'\(int32, object, int32, object, object, object\)$}}
; CHECK-NEXT:  {{^  pop$}}
; CHECK-NEXT:  {{^  ldc\.i4\.0$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'twice'
define internal i32 @twice(target("msil.ref") %a) {
  %r = call target("msil.ref") @frozen(target("msil.ref") %a)
  %x = call target("msil.ref") @many(i32 1, target("msil.ref") %r, i32 2, target("msil.ref") %r, target("msil.ref") %r, target("msil.ref") %r)
  ret i32 0
}

; The undefined input of a phi is written as 'ldnull' into the phi's slot, a
; local at -O0 and the argument from -O1 on.
; CHECK-LABEL: .method assembly static object 'undef_phi'(int32, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; O0-NEXT:     {{^  \.locals \(object\)$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  brtrue\.s (@BB[0-9]+_[0-9]+)$}}
; O0-NEXT:     {{^  br\.s @BB[0-9]+_[0-9]+$}}
; O0-NEXT:     {{^@BB[0-9]+_[0-9]+:$}}
; CHECK-NEXT:  {{^  ldnull$}}
; O0-NEXT:     {{^  stloc\.0$}}
; O0-NEXT:     {{^  br\.s @BB[0-9]+_[0-9]+$}}
; O0-NEXT:     {{^@BB[0-9]+_[0-9]+:$}}
; O0-NEXT:     {{^  ldarg\.1$}}
; O0-NEXT:     {{^  stloc\.0$}}
; O0-NEXT:     {{^  br\.s @BB[0-9]+_[0-9]+$}}
; O0-NEXT:     {{^@BB[0-9]+_[0-9]+:$}}
; O0-NEXT:     {{^  ldloc\.0$}}
; OPT-NEXT:    {{^  starg\.s 1$}}
; OPT-NEXT:    {{^@BB[0-9]+_[0-9]+:$}}
; OPT-NEXT:    {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'undef_phi'
define internal target("msil.ref") @undef_phi(i32 %c, target("msil.ref") %a) {
entry:
  %t = icmp eq i32 %c, 0
  br i1 %t, label %yes, label %no
yes:
  br label %join
no:
  br label %join
join:
  %p = phi target("msil.ref") [ undef, %yes ], [ %a, %no ]
  ret target("msil.ref") %p
}

; Objects and native ints in one loop: two object phis that swap and a
; native int counter. Each class keeps its own locals (RB8); from -O1 on
; RegColoring leaves one native int local and one object local.
; CHECK-LABEL: .method assembly static native int 'mixed'(native int, object, object, int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 2$}}
; O0-NEXT:     {{^  \.locals \(native int, object, object, object, object, native int, native int\)$}}
; OPT-NEXT:    {{^  \.locals \(native int, object\)$}}
; CHECK:       {{^  blt\.s @BB[0-9]+_[0-9]+$}}
; CHECK:       {{^  call object 'frozen'\(object\)$}}
; CHECK-NEXT:  {{^  pop$}}
; CHECK:       {{^  call object 'frozen'\(object\)$}}
; CHECK-NEXT:  {{^  pop$}}
; CHECK:       {{^}}} // end of method 'mixed'
define internal i64 @mixed(i64 %n, target("msil.ref") %a, target("msil.ref") %b, i32 %c) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %x = phi target("msil.ref") [ %a, %entry ], [ %y, %loop ]
  %y = phi target("msil.ref") [ %b, %entry ], [ %x, %loop ]
  %i1 = add i64 %i, %n
  %d = icmp slt i64 %i1, 100
  br i1 %d, label %loop, label %exit
exit:
  %z = call target("msil.ref") @frozen(target("msil.ref") %y)
  %w = call target("msil.ref") @frozen(target("msil.ref") %x)
  ret i64 %i1
}

; RG12: clang ends a reference function with llvm.trap and unreachable where
; control can reach its closing brace; that path is 'ldnull' and 'throw'.
; CHECK-LABEL: .method assembly static object 'falls'(int32, object) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  }}brfalse.s [[TRAP:@BB[0-9]+_[0-9]+]]{{$}}
; O0-NEXT:     {{^  }}br.s [[THEN:@BB[0-9]+_[0-9]+]]{{$}}
; O0-NEXT:     {{^}}[[THEN]]:{{$}}
; CHECK-NEXT:  {{^  ldarg\.1$}}
; CHECK-NEXT:  {{^  call object 'frozen'\(object\)$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}[[TRAP]]:{{$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  throw$}}
; CHECK-NEXT:  {{^}}} // end of method 'falls'
define internal target("msil.ref") @falls(i32 %c, target("msil.ref") %a) {
entry:
  %t = icmp eq i32 %c, 0
  br i1 %t, label %end, label %then
then:
  %r = call target("msil.ref") @frozen(target("msil.ref") %a)
  ret target("msil.ref") %r
end:
  call void @llvm.trap()
  unreachable
}

declare void @llvm.trap()

; The address of %buf escapes, so 'framed' has a C frame: 'localloc' at
; entry and its pointer in a native int local. The 'poison' and 'undef'
; arguments are 'ldnull', and the object local that keeps %o is written
; before either read of it, at every level: from -O1 on the arguments are
; 'undef' uses whose register RegColoring merges with that of %o.
; CHECK-LABEL: .method assembly static void 'sink'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 0$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'sink'
; CHECK-LABEL: .method assembly static int32 'framed'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 1$}}
; CHECK-NEXT:  {{^  \.locals \(native int, object\)$}}
; CHECK-NEXT:  {{^  ldc\.i4\.s 16$}}
; CHECK-NEXT:  {{^  localloc$}}
; CHECK-NEXT:  {{^  stloc\.0$}}
; CHECK-NEXT:  {{^  ldloc\.0$}}
; CHECK-NEXT:  {{^  call void 'sink'\(native int\)$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  call object 'frozen'\(object\)$}}
; CHECK-NEXT:  {{^  pop$}}
; CHECK-NEXT:  {{^  ldnull$}}
; CHECK-NEXT:  {{^  call object 'frozen'\(object\)$}}
; CHECK-NEXT:  {{^  pop$}}
; CHECK-NEXT:  {{^  call object 'null_ref'\(\)$}}
; CHECK-NEXT:  {{^  stloc\.1$}}
; CHECK-NEXT:  {{^  ldloc\.0$}}
; CHECK-NEXT:  {{^  call void 'sink'\(native int\)$}}
; CHECK-NEXT:  {{^  ldloc\.1$}}
; CHECK-NEXT:  {{^  call object 'frozen'\(object\)$}}
; CHECK-NEXT:  {{^  pop$}}
; CHECK-NEXT:  {{^  ldloc\.0$}}
; CHECK-NEXT:  {{^  call void 'sink'\(native int\)$}}
; CHECK-NEXT:  {{^  ldloc\.1$}}
; CHECK-NEXT:  {{^  call object 'frozen'\(object\)$}}
; CHECK-NEXT:  {{^  pop$}}
; CHECK-NEXT:  {{^  ldarg\.0$}}
; CHECK-NEXT:  {{^  ret$}}
; CHECK-NEXT:  {{^}}} // end of method 'framed'
define internal void @sink(ptr %p) {
  ret void
}

define internal i32 @framed(i32 %n) {
entry:
  %buf = alloca [4 x i32], align 4
  call void @sink(ptr %buf)
  %p = call target("msil.ref") @frozen(target("msil.ref") poison)
  %u = call target("msil.ref") @frozen(target("msil.ref") undef)
  %o = call target("msil.ref") @null_ref()
  call void @sink(ptr %buf)
  %x = call target("msil.ref") @frozen(target("msil.ref") %o)
  call void @sink(ptr %buf)
  %y = call target("msil.ref") @frozen(target("msil.ref") %o)
  ret i32 %n
}

; The callers: 'zeroinitializer' arguments are 'ldnull', object results feed
; calls through the stack or object locals.
; CHECK-LABEL: .method public static int32 'main'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack 6$}}
; O0-NEXT:     {{^  \.locals \((object|int32)(, (object|int32))*\)$}}
; OPT-NEXT:    {{^  \.locals \(object(, object)*\)$}}
; CHECK:       {{^  call object 'null_ref'\(\)$}}
; CHECK:       {{^  call object 'ident'\(object\)$}}
; CHECK:       {{^  call object 'undef_ref'\(\)$}}
; CHECK:       {{^  call object 'poison_ref'\(\)$}}
; CHECK:       {{^  call object 'pick'\(int32, object, object\)$}}
; OPT-NEXT:    {{^  dup$}}
; OPT-NEXT:    {{^  stloc\.0$}}
; CHECK:       {{^  ldnull$}}
; CHECK:       {{^  call object 'many'\(int32, object, int32, object, object, object\)$}}
; CHECK:       {{^  call void 'drop'\(object\)$}}
; CHECK:       {{^  call int32 'twice'\(object\)$}}
; CHECK:       {{^  call object 'undef_phi'\(int32, object\)$}}
; CHECK:       {{^  call native int 'mixed'\(native int, object, object, int32\)$}}
; CHECK:       {{^  call object 'falls'\(int32, object\)$}}
; CHECK-NEXT:  {{^  call void 'drop'\(object\)$}}
; CHECK:       {{^  call int32 'framed'\(int32\)$}}
; CHECK:       {{^}}} // end of method 'main'
define i32 @main() {
  %n = call target("msil.ref") @null_ref()
  %i = call target("msil.ref") @ident(target("msil.ref") %n)
  %u = call target("msil.ref") @undef_ref()
  %p = call target("msil.ref") @poison_ref()
  %k = call target("msil.ref") @pick(i32 1, target("msil.ref") %i, target("msil.ref") %u)
  %m = call target("msil.ref") @many(i32 1, target("msil.ref") %k, i32 2, target("msil.ref") %p, target("msil.ref") zeroinitializer, target("msil.ref") %k)
  call void @drop(target("msil.ref") %m)
  %t = call i32 @twice(target("msil.ref") %m)
  %q = call target("msil.ref") @undef_phi(i32 %t, target("msil.ref") %m)
  %f = call target("msil.ref") @frozen(target("msil.ref") %q)
  %x = call i64 @mixed(i64 3, target("msil.ref") %f, target("msil.ref") %m, i32 1)
  %r = trunc i64 %x to i32
  %l = call target("msil.ref") @falls(i32 %r, target("msil.ref") %m)
  call void @drop(target("msil.ref") %l)
  %g = call i32 @framed(i32 2)
  ret i32 %g
}

; The objects: 'object' in the MethodDef signatures, the MemberRef-free call
; signatures and .locals, and the same instructions.
; DUMP-LABEL: {{^}}  .method 'ident' object(object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'null_ref' object() flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldnull{{$}}
; DUMP-NEXT:  {{^}}    L1: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'undef_ref' object() flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldnull{{$}}
; DUMP-NEXT:  {{^}}    L1: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'poison_ref' object() flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldnull{{$}}
; DUMP-NEXT:  {{^}}    L1: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'frozen' object(object) flags=
; DUMP-LABEL: {{^}}  .method 'pick' object(int32, object, object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 2{{$}}
; DUMP-O0-NEXT: {{^}}    .locals (int32, object){{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: brtrue.s L{{[0-9]+}}{{$}}
; DUMP-LABEL: {{^}}  .method 'many' object(int32, object, int32, object, object, object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.s 5{{$}}
; DUMP-NEXT:  {{^}}    L1: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'drop' void(object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call object(object) '<Module>'::'frozen'{{$}}
; DUMP-NEXT:  {{^}}    L2: pop{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'twice' int32(object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 6{{$}}
; DUMP-O0-NEXT: {{^}}    .locals (object, int32){{$}}
; DUMP-OPT:   {{^}}    L{{[0-9]+}}: dup{{$}}
; DUMP-OPT-NEXT: {{^}}    L{{[0-9]+}}: starg.s 0{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call object(int32, object, int32, object, object, object) '<Module>'::'many'{{$}}
; DUMP-LABEL: {{^}}  .method 'undef_phi' object(int32, object) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: ldnull{{$}}
; DUMP-LABEL: {{^}}  .method 'mixed' native int(native int, object, object, int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack 2{{$}}
; DUMP-O0-NEXT: {{^}}    .locals (native int, object, object, object, object, native int, native int){{$}}
; DUMP-OPT-NEXT: {{^}}    .locals (native int, object){{$}}
; DUMP-LABEL: {{^}}  .method 'falls' object(int32, object) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: brfalse.s L[[DTRAP:[0-9]+]]{{$}}
; DUMP-O0-NEXT: {{^}}    L2: br.s L3{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldarg.1{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call object(object) '<Module>'::'frozen'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-NEXT:  {{^}}    L[[DTRAP]]: ldnull{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: throw{{$}}
; DUMP-LABEL: {{^}}  .method 'sink' void(native int) flags=
; DUMP-NEXT:  {{^}}    .maxstack 0{{$}}
; DUMP-NEXT:  {{^}}    L0: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'framed' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack 1{{$}}
; DUMP-NEXT:  {{^}}    .locals (native int, object){{$}}
; DUMP-NEXT:  {{^}}    L0: ldc.i4.s 16{{$}}
; DUMP-NEXT:  {{^}}    L1: localloc{{$}}
; DUMP-NEXT:  {{^}}    L2: stloc.0{{$}}
; DUMP-NEXT:  {{^}}    L3: ldloc.0{{$}}
; DUMP-NEXT:  {{^}}    L4: call void(native int) '<Module>'::'sink'{{$}}
; DUMP-NEXT:  {{^}}    L5: ldnull{{$}}
; DUMP-NEXT:  {{^}}    L6: call object(object) '<Module>'::'frozen'{{$}}
; DUMP-NEXT:  {{^}}    L7: pop{{$}}
; DUMP-NEXT:  {{^}}    L8: ldnull{{$}}
; DUMP-NEXT:  {{^}}    L9: call object(object) '<Module>'::'frozen'{{$}}
; DUMP-NEXT:  {{^}}    L10: pop{{$}}
; DUMP-NEXT:  {{^}}    L11: call object() '<Module>'::'null_ref'{{$}}
; DUMP-NEXT:  {{^}}    L12: stloc.1{{$}}
; DUMP-NEXT:  {{^}}    L13: ldloc.0{{$}}
; DUMP-NEXT:  {{^}}    L14: call void(native int) '<Module>'::'sink'{{$}}
; DUMP-NEXT:  {{^}}    L15: ldloc.1{{$}}
; DUMP-NEXT:  {{^}}    L16: call object(object) '<Module>'::'frozen'{{$}}
; DUMP-NEXT:  {{^}}    L17: pop{{$}}
; DUMP-NEXT:  {{^}}    L18: ldloc.0{{$}}
; DUMP-NEXT:  {{^}}    L19: call void(native int) '<Module>'::'sink'{{$}}
; DUMP-NEXT:  {{^}}    L20: ldloc.1{{$}}
; DUMP-NEXT:  {{^}}    L21: call object(object) '<Module>'::'frozen'{{$}}
; DUMP-NEXT:  {{^}}    L22: pop{{$}}
; DUMP-NEXT:  {{^}}    L23: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L24: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'main' int32() flags=
; DUMP-NEXT:  {{^}}    .maxstack 6{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call native int(native int, object, object, int32) '<Module>'::'mixed'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call object(int32, object) '<Module>'::'falls'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'framed'{{$}}

; The link info: every reference function is an internal (LOCAL) symbol;
; only main is GLOBAL (the one allowed by the implicit checks below it).
; LINK:     {{^}}LinkInfo v1.{{[0-9]+}} triple={{.+}}{{$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT 'ident' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT 'pick' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT 'mixed' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT 'falls' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT 'framed' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION {{GLOBAL}} DEFAULT 'main' method=0x06{{[0-9A-F]+$}}
