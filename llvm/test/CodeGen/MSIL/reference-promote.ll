; Pass P15, MSILPromoteReferences (ImplementationPlan.md 5.21.7 RB10, 5.13.6
; as amended in section 12 on 2026-11-21; milestone R-a, RV23): after
; AtomicExpand and before the generic ISel preparation (pipeline.ll pins the
; slot), at every optimization level and under optnone, it promotes with
; PromoteMemToReg each static single-element stack slot of exactly the
; reference type whose loads and stores all have that type, and no other
; slot (LLVM issue 81575); clang writes such slots for every reference
; parameter and local at -O0 (5.21.5 RG1).
; - promote.ll, after P15 (-stop-after=msil-promote-references) at -O0 and
;   -O2: the reference slots of 'framed', of the optnone function 'kept' and
;   of 'swap' are gone, their loads replaced by the stored values (a phi
;   where two stores meet); the int32 slots stay, in 'framed', whose address
;   escapes, and in 'ints_only', which has no reference slot and stays as it
;   was, its slot included.
; - promote.ll through the whole pipeline: 'framed' keeps its C frame (one
;   'localloc' at entry, the frame pointer a native int local, 'stind.i4'
;   and 'ldind.i4' into it) with an object local beside it: the object held
;   across the call that writes the frame is an 'object' local, never frame
;   memory (no 'ldind.ref', 'stind.ref', 'ldind.i' or 'stind.i' anywhere);
;   the text assembles, and the objects list the same locals.
; - lifetime.ll, clang's slots with lifetime markers and debug records (RI4:
;   P15 removes both): 'marked' is an optnone function as clang -O2 -g
;   writes it, every reference slot with llvm.lifetime.start/end and a
;   #dbg_declare record, and an int32 slot (#dbg_declare only), which stays
;   in the C frame; 'declared' is a function as clang -O0 -g writes it
;   (#dbg_declare records only). After P15, at -O0 and at -O2 (where
;   'marked' is still optnone): no slot of the type and no lifetime marker
;   is left (implicit checks), each #dbg_declare of a reference slot has
;   become #dbg_value records of the reference with the slot's
;   DILocalVariable (a phi where two stores meet), and the int32 slot keeps
;   its #dbg_declare. Through the whole pipeline, with -verify-machineinstrs
;   (the DBG_VALUEs of object registers): the bodies pinned whole, the
;   references in object locals and arguments beside the frame (no
;   'ldind.ref', 'stind.ref', 'ldind.i' or 'stind.i'); the text assembles.
; - the T-RB4 errors, each text of 5.21.12 pinned whole ('a .NET reference
;   that cannot become an IL local is not supported for MSIL: <reason>'),
;   in text and object mode at -O0 and -O2: 'the slot's address escapes'
;   (escape.ll: the slot passed to a function), 'the slot is copied as
;   memory' (copy.ll: llvm.memcpy from one slot to another), 'a load through
;   a pointer' and 'a store through a pointer' (through.ll: a load and a
;   store of the type through a pointer parameter; one error per function,
;   plain CHECK lines in module order, so that the implicit check-nots also
;   run between and after them, which they do not for a CHECK-DAG group).
;   The diagnosed body becomes a lone 'unreachable' (after P15) and llc
;   exits with 1 without a crash.
; - T-RB8 'a .NET reference live across an exception handler is not
;   supported for MSIL yet': no exception pad survives P3/P4 today, so
;   pad.ll starts at P15 (-start-before=msil-promote-references) with a slot
;   that a landing pad reads; the body becomes 'unreachable'. padok.ll's
;   slot, which no pad uses or reaches, is promoted.

; RUN: rm -rf %t && split-file %s %t

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{ir} = llc -mtriple=%{t} %{o} -stop-after=msil-promote-references < %t/promote.ll -o - \
; DEFINE:   | FileCheck %t/promote.ll --check-prefix=IR
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{ir}
; REDEFINE: %{o} = -O2
; RUN: %{ir}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{ir}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %{ir}

; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/promote.ll \
; DEFINE:   -o %t/promote.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %t/promote.ll --check-prefix=IL --input-file=%t/promote.%{t}%{o}.il \
; DEFINE:     --implicit-check-not="{{(ld|st)ind\.(ref|i)$}}"
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{il}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{il}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/promote0.dll %t/promote.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/promote2.dll %t/promote.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj < %t/promote.ll \
; RUN:   -o %t/promote0.o && llvm-cil-dump --canonical --order=decl --params=drop %t/promote0.o \
; RUN:   | FileCheck %t/promote.ll --check-prefix=DUMP %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=msil64-unknown-dotnet -O2 -filetype=obj < %t/promote.ll \
; RUN:   -o %t/promote2.o && llvm-cil-dump --canonical --order=decl --params=drop %t/promote2.o \
; RUN:   | FileCheck %t/promote.ll --check-prefix=DUMP %}

; DEFINE: %{lir} = llc -mtriple=%{t} %{o} -stop-after=msil-promote-references < %t/lifetime.ll -o - \
; DEFINE:   | FileCheck %t/lifetime.ll --check-prefix=IR --implicit-check-not="call void @llvm.lifetime" \
; DEFINE:     --implicit-check-not="{{alloca target\(.msil\.ref.\)}}"
; DEFINE: %{lil} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/lifetime.ll \
; DEFINE:   -o %t/lifetime.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %t/lifetime.ll --check-prefixes=IL,IL%{o} --input-file=%t/lifetime.%{t}%{o}.il \
; DEFINE:     --implicit-check-not="{{(ld|st)ind\.(ref|i)$}}"
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{lir}
; RUN: %{lil}
; REDEFINE: %{o} = -O2
; RUN: %{lir}
; RUN: %{lil}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{lir}
; RUN: %{lil}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %{lir}
; RUN: %{lil}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{lil}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/lifetime0.dll %t/lifetime.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/lifetime2.dll %t/lifetime.x86_64-unknown-linux-gnu-msil-O2.il %}

; DEFINE: %{f} =
; DEFINE: %{rb4} = not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/%{f}.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %t/%{f}.ll --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:     --implicit-check-not=error: --implicit-check-not=warning: && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj < %t/%{f}.ll -o %t/%{f}.o 2>&1 \
; DEFINE:   | FileCheck %t/%{f}.ll --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:     --implicit-check-not=error: --implicit-check-not=warning: && \
; DEFINE:   not llc -mtriple=%{t} %{o} -stop-after=msil-promote-references < %t/%{f}.ll -o - 2>/dev/null \
; DEFINE:   | FileCheck %t/%{f}.ll --check-prefix=AFTER
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; REDEFINE: %{f} = escape
; RUN: %{rb4}
; REDEFINE: %{f} = copy
; RUN: %{rb4}
; REDEFINE: %{f} = through
; RUN: %{rb4}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; REDEFINE: %{f} = escape
; RUN: %{rb4}
; REDEFINE: %{f} = copy
; RUN: %{rb4}
; REDEFINE: %{f} = through
; RUN: %{rb4}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{f} = escape
; RUN: %{rb4}
; REDEFINE: %{o} = -O0
; REDEFINE: %{f} = through
; RUN: %{rb4}

; DEFINE: %{pad} = not llc -mtriple=%{t} %{o} -start-before=msil-promote-references -verify-machineinstrs \
; DEFINE:   < %t/pad.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %t/pad.ll --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:     --implicit-check-not=error: --implicit-check-not=warning: && \
; DEFINE:   not llc -mtriple=%{t} %{o} -start-before=msil-promote-references -stop-after=msil-promote-references \
; DEFINE:   < %t/pad.ll -o - 2>/dev/null | FileCheck %t/pad.ll --check-prefix=AFTER && \
; DEFINE:   llc -mtriple=%{t} %{o} -start-before=msil-promote-references -stop-after=msil-promote-references \
; DEFINE:   < %t/padok.ll -o - 2>&1 | FileCheck %t/padok.ll
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{pad}
; REDEFINE: %{o} = -O2
; RUN: %{pad}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{pad}

;--- promote.ll
source_filename = "reference-promote.c"

declare i1 @"__msil_ref.eq"(target("msil.ref"), target("msil.ref")) memory(none) nounwind willreturn speculatable

define internal target("msil.ref") @make() {
  ret target("msil.ref") zeroinitializer
}

define internal void @poke(ptr %p) {
  store i32 9, ptr %p, align 4
  ret void
}

; IR-LABEL: define internal i32 @framed(i32 %n) {
; IR:         %x = alloca i32, align 4
; IR-NOT:     alloca
; IR-NOT:     target("msil.ref"){{.*}}, ptr
; IR:         %c0 = call target("msil.ref") @make()
; IR:         call void @poke(ptr %x)
; IR:         call i1 @__msil_ref.eq(target("msil.ref") %c0, target("msil.ref") zeroinitializer)
; IR:       }
; IL-LABEL: .method assembly static int32 'framed'(int32) cil managed
; IL:         .locals (native int{{(, [a-z0-9 ]+)*}}, object{{(, [a-z0-9 ]+)*}})
; IL-NEXT:    {{^  ldc\.i4\.s [0-9]+$}}
; IL-NEXT:    {{^  localloc$}}
; IL:         call object 'make'()
; IL-NEXT:    {{^  stloc(\.[0-3]| [0-9]+| \.s [0-9]+)$}}
; IL:         stind.i4
; IL:         call void 'poke'(native int)
; IL:         ldind.i4
; IL:       } // end of method 'framed'
; DUMP-LABEL: .method 'framed' int32(int32) flags=
; DUMP:       .locals (native int{{(, [a-z0-9 ]+)*}}, object{{(, [a-z0-9 ]+)*}})
; DUMP:       localloc
define internal i32 @framed(i32 %n) {
entry:
  %n.addr = alloca i32, align 4
  %x = alloca i32, align 4
  %c.slot = alloca target("msil.ref"), align 8
  store i32 %n, ptr %n.addr, align 4
  store target("msil.ref") zeroinitializer, ptr %c.slot, align 8
  %c0 = call target("msil.ref") @make()
  store target("msil.ref") %c0, ptr %c.slot, align 8
  %nv = load i32, ptr %n.addr, align 4
  store i32 %nv, ptr %x, align 4
  call void @poke(ptr %x)
  %c = load target("msil.ref"), ptr %c.slot, align 8
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %c, target("msil.ref") zeroinitializer)
  %xv = load i32, ptr %x, align 4
  %ez = zext i1 %e to i32
  %r = add i32 %xv, %ez
  ret i32 %r
}

; optnone: promoted all the same.
; IR-LABEL: define internal target("msil.ref") @kept(target("msil.ref") %a) #{{[0-9]+}} {
; IR-NEXT:  entry:
; IR-NEXT:    ret target("msil.ref") %a
; IL-LABEL: .method assembly static object 'kept'(object) cil managed
; IL-NOT:     .locals
; IL:         ldarg.0
; IL-NEXT:    ret
define internal target("msil.ref") @kept(target("msil.ref") %a) noinline optnone {
entry:
  %a.addr = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %a.addr, align 8
  %v = load target("msil.ref"), ptr %a.addr, align 8
  ret target("msil.ref") %v
}

; Two stores that meet: a phi of the two references.
; IR-LABEL: define internal target("msil.ref") @swap(i32 %c, target("msil.ref") %a, target("msil.ref") %b) {
; IR-NOT:     alloca
; IR:         phi target("msil.ref") {{\[ %a, %entry \], \[ %b, %then \]|\[ %b, %then \], \[ %a, %entry \]}}
; IL-LABEL: .method assembly static object 'swap'(int32, object, object) cil managed
; IL-NOT:     localloc
; IL:       } // end of method 'swap'
define internal target("msil.ref") @swap(i32 %c, target("msil.ref") %a, target("msil.ref") %b) {
entry:
  %r = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %r, align 8
  %t = icmp ne i32 %c, 0
  br i1 %t, label %then, label %done
then:
  store target("msil.ref") %b, ptr %r, align 8
  br label %done
done:
  %v = load target("msil.ref"), ptr %r, align 8
  ret target("msil.ref") %v
}

; No reference slot: P15 leaves the function as it is, its int32 slot too.
; IR-LABEL: define internal i32 @ints_only(i32 %n) {
; IR-NEXT:    %s = alloca i32, align 4
; IR-NEXT:    store i32 %n, ptr %s, align 4
; IR-NEXT:    %v = load i32, ptr %s, align 4
; IR-NEXT:    ret i32 %v
define internal i32 @ints_only(i32 %n) {
  %s = alloca i32, align 4
  store i32 %n, ptr %s, align 4
  %v = load i32, ptr %s, align 4
  ret i32 %v
}

define i32 @main() {
  %f = call i32 @framed(i32 3)
  %k = call target("msil.ref") @kept(target("msil.ref") zeroinitializer)
  %s = call target("msil.ref") @swap(i32 %f, target("msil.ref") %k, target("msil.ref") zeroinitializer)
  %i = call i32 @ints_only(i32 %f)
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %s, target("msil.ref") zeroinitializer)
  %z = zext i1 %e to i32
  %r = add i32 %i, %z
  ret i32 %r
}

;--- lifetime.ll
source_filename = "reference-lifetime.c"

declare i1 @"__msil_ref.eq"(target("msil.ref"), target("msil.ref")) memory(none) nounwind willreturn speculatable
declare void @llvm.lifetime.start.p0(ptr captures(none))
declare void @llvm.lifetime.end.p0(ptr captures(none))

; IR-LABEL:   define internal target("msil.ref") @marked(target("msil.ref") %a, i32 %c) #{{[0-9]+}} !dbg !{{[0-9]+}} {
; IR-NEXT:    entry:
; IR-NEXT:      %c.addr = alloca i32, align 4
; IR-NEXT:      store i32 %c, ptr %c.addr, align 4
; IR-NEXT:      #dbg_declare(ptr %c.addr, ![[C:[0-9]+]], !DIExpression(),
; IR-NEXT:      #dbg_value(target("msil.ref") %a, ![[A:[0-9]+]], !DIExpression(),
; IR-NEXT:      #dbg_value(target("msil.ref") zeroinitializer, ![[S:[0-9]+]], !DIExpression(),
; IR-NEXT:      #dbg_value(target("msil.ref") %a, ![[T:[0-9]+]], !DIExpression(),
; IR:           br i1 %tobool, label %if.then, label %if.end
; IR:         if.then:
; IR-NEXT:      #dbg_value(target("msil.ref") %a, ![[S]], !DIExpression(),
; IR-NEXT:      br label %if.end
; IR:         if.end:
; IR-NEXT:      [[P:%[0-9A-Za-z.]+]] = phi target("msil.ref") {{\[ %a, %if.then \], \[ zeroinitializer, %entry \]|\[ zeroinitializer, %entry \], \[ %a, %if.then \]}}
; IR-NEXT:      #dbg_value(target("msil.ref") [[P]], ![[S]], !DIExpression(),
; IR-NEXT:      ret target("msil.ref") [[P]]
; IR-NEXT:    }
; IL-LABEL:   .method assembly static object 'marked'(object, int32) cil managed
; IL-NEXT:    {{^\{$}}
; IL-NEXT:    {{^@func_begin[0-9]+:$}}
; IL-NEXT:    {{^  \.maxstack 2$}}
; IL-O0-NEXT: {{^  \.locals \(native int, int32, object, object, object\)$}}
; IL-O2-NEXT: {{^  \.locals \(native int, object\)$}}
; IL-NEXT:    {{^  ldc\.i4\.s 16$}}
; IL-NEXT:    {{^  localloc$}}
; IL-NEXT:    {{^  stloc\.0$}}
; IL-NEXT:    {{^  ldloc\.0$}}
; IL-NEXT:    {{^  ldc\.i4\.s 12$}}
; IL-NEXT:    {{^  conv\.i$}}
; IL-NEXT:    {{^  add$}}
; IL-NEXT:    {{^  ldarg\.1$}}
; IL-NEXT:    {{^  stind\.i4$}}
; IL-O0-NEXT: {{^  ldloc\.0$}}
; IL-O0-NEXT: {{^  ldc\.i4\.s 12$}}
; IL-O0-NEXT: {{^  conv\.i$}}
; IL-O0-NEXT: {{^  add$}}
; IL-O0-NEXT: {{^  ldind\.i4$}}
; IL-O0-NEXT: {{^  stloc\.1$}}
; IL-O0-NEXT: {{^  ldnull$}}
; IL-O0-NEXT: {{^  stloc\.2$}}
; IL-O0-NEXT: {{^  ldloc\.2$}}
; IL-O0-NEXT: {{^  stloc\.3$}}
; IL-O0-NEXT: {{^  ldloc\.1$}}
; IL-O2-NEXT: {{^  ldnull$}}
; IL-O2-NEXT: {{^  stloc\.1$}}
; IL-O2-NEXT: {{^  ldloc\.0$}}
; IL-O2-NEXT: {{^  ldc\.i4\.s 12$}}
; IL-O2-NEXT: {{^  conv\.i$}}
; IL-O2-NEXT: {{^  add$}}
; IL-O2-NEXT: {{^  ldind\.i4$}}
; IL-NEXT:    {{^  }}brfalse.s [[END:@BB[0-9]+_[0-9]+]]{{$}}
; IL-NEXT:    {{^  }}br.s [[THEN:@BB[0-9]+_[0-9]+]]{{$}}
; IL-NEXT:    {{^}}[[THEN]]:{{$}}
; IL-NEXT:    {{^  ldarg\.0$}}
; IL-O0-NEXT: {{^  stloc\.3$}}
; IL-O2-NEXT: {{^  stloc\.1$}}
; IL-NEXT:    {{^  }}br.s [[END]]{{$}}
; IL-NEXT:    {{^}}[[END]]:{{$}}
; IL-O0-NEXT: {{^  ldloc\.3$}}
; IL-O0-NEXT: {{^  stloc\.s 4$}}
; IL-O0-NEXT: {{^  ldloc\.s 4$}}
; IL-O2-NEXT: {{^  ldloc\.1$}}
; IL-NEXT:    {{^  ret$}}
; IL-NEXT:    {{^@func_end[0-9]+:$}}
; IL-NEXT:    {{^}}} // end of method 'marked'
define internal target("msil.ref") @marked(target("msil.ref") %a, i32 %c) noinline optnone !dbg !10 {
entry:
  %c.addr = alloca i32, align 4
  %a.addr = alloca target("msil.ref"), align 8
  %s = alloca target("msil.ref"), align 8
  %t = alloca target("msil.ref"), align 8
  store i32 %c, ptr %c.addr, align 4
    #dbg_declare(ptr %c.addr, !14, !DIExpression(), !19)
  store target("msil.ref") %a, ptr %a.addr, align 8
    #dbg_declare(ptr %a.addr, !15, !DIExpression(), !19)
  call void @llvm.lifetime.start.p0(ptr %s), !dbg !19
    #dbg_declare(ptr %s, !16, !DIExpression(), !19)
  store target("msil.ref") zeroinitializer, ptr %s, align 8, !dbg !19
  call void @llvm.lifetime.start.p0(ptr %t), !dbg !20
    #dbg_declare(ptr %t, !17, !DIExpression(), !20)
  %0 = load target("msil.ref"), ptr %a.addr, align 8, !dbg !20
  store target("msil.ref") %0, ptr %t, align 8, !dbg !20
  %1 = load i32, ptr %c.addr, align 4, !dbg !20
  %tobool = icmp ne i32 %1, 0, !dbg !20
  br i1 %tobool, label %if.then, label %if.end, !dbg !20

if.then:
  %2 = load target("msil.ref"), ptr %t, align 8, !dbg !20
  store target("msil.ref") %2, ptr %s, align 8, !dbg !20
  br label %if.end, !dbg !20

if.end:
  call void @llvm.lifetime.end.p0(ptr %t), !dbg !20
  %3 = load target("msil.ref"), ptr %s, align 8, !dbg !19
  call void @llvm.lifetime.end.p0(ptr %s), !dbg !19
  ret target("msil.ref") %3, !dbg !19
}

; IR-LABEL:   define internal i32 @declared(target("msil.ref") %a, target("msil.ref") %b) #{{[0-9]+}} !dbg !{{[0-9]+}} {
; IR-NEXT:    entry:
; IR-NEXT:      #dbg_value(target("msil.ref") %b, ![[DB:[0-9]+]], !DIExpression(),
; IR-NEXT:      #dbg_value(target("msil.ref") %a, ![[DA:[0-9]+]], !DIExpression(),
; IR-NEXT:      %e = call i1 @__msil_ref.eq(target("msil.ref") %a, target("msil.ref") %b)
; IR-NEXT:      %z = zext i1 %e to i32
; IR-NEXT:      ret i32 %z
; IR-NEXT:    }
; IL-LABEL:   .method assembly static int32 'declared'(object, object) cil managed
; IL-NEXT:    {{^\{$}}
; IL-NEXT:    {{^@func_begin[0-9]+:$}}
; IL-NEXT:    {{^  \.maxstack 2$}}
; IL-NEXT:    {{^  ldarg\.0$}}
; IL-NEXT:    {{^  ldarg\.1$}}
; IL-NEXT:    {{^  ceq$}}
; IL-NEXT:    {{^  ret$}}
; IL-NEXT:    {{^@func_end[0-9]+:$}}
; IL-NEXT:    {{^}}} // end of method 'declared'
define internal i32 @declared(target("msil.ref") %a, target("msil.ref") %b) noinline optnone !dbg !21 {
entry:
  %b.addr = alloca target("msil.ref"), align 8
  %a.addr = alloca target("msil.ref"), align 8
  store target("msil.ref") %b, ptr %b.addr, align 8
    #dbg_declare(ptr %b.addr, !24, !DIExpression(), !25)
  store target("msil.ref") %a, ptr %a.addr, align 8
    #dbg_declare(ptr %a.addr, !23, !DIExpression(), !25)
  %0 = load target("msil.ref"), ptr %a.addr, align 8, !dbg !25
  %1 = load target("msil.ref"), ptr %b.addr, align 8, !dbg !25
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") %0, target("msil.ref") %1), !dbg !25
  %z = zext i1 %e to i32, !dbg !25
  ret i32 %z, !dbg !25
}

; IL-LABEL: .method public static int32 'main'() cil managed
; IL:         call object 'marked'(object, int32)
; IL:         call int32 'declared'(object, object)
; IL:       } // end of method 'main'
define i32 @main() {
  %p = call target("msil.ref") @marked(target("msil.ref") zeroinitializer, i32 1)
  %s = call i32 @declared(target("msil.ref") %p, target("msil.ref") zeroinitializer)
  ret i32 %s
}

; The DILocalVariables of the promoted slots and of the int32 slot.
; IR-DAG:   ![[C]] = !DILocalVariable(name: "c", arg: 2, scope:
; IR-DAG:   ![[A]] = !DILocalVariable(name: "a", arg: 1, scope:
; IR-DAG:   ![[S]] = !DILocalVariable(name: "s", scope:
; IR-DAG:   ![[T]] = !DILocalVariable(name: "t", scope:
; IR-DAG:   ![[DA]] = !DILocalVariable(name: "a", arg: 1, scope:
; IR-DAG:   ![[DB]] = !DILocalVariable(name: "b", arg: 2, scope:
!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "reference-lifetime.c", directory: "")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!4 = !DICompositeType(tag: DW_TAG_structure_type, name: "System.String", file: !1, flags: DIFlagFwdDecl)
!5 = !DISubroutineType(types: !6)
!6 = !{!4, !4, !3}
!10 = distinct !DISubprogram(name: "marked", scope: !1, file: !1, line: 7, type: !5, scopeLine: 7, flags: DIFlagPrototyped, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !13)
!11 = distinct !DILexicalBlock(scope: !10, file: !1, line: 9, column: 3)
!13 = !{!14, !15, !16, !17}
!14 = !DILocalVariable(name: "c", arg: 2, scope: !10, file: !1, line: 7, type: !3)
!15 = !DILocalVariable(name: "a", arg: 1, scope: !10, file: !1, line: 7, type: !4)
!16 = !DILocalVariable(name: "s", scope: !10, file: !1, line: 8, type: !4)
!17 = !DILocalVariable(name: "t", scope: !11, file: !1, line: 10, type: !4)
!19 = !DILocation(line: 8, column: 3, scope: !10)
!20 = !DILocation(line: 10, column: 5, scope: !11)
!21 = distinct !DISubprogram(name: "declared", scope: !1, file: !1, line: 14, type: !22, scopeLine: 14, flags: DIFlagPrototyped, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0, retainedNodes: !26)
!22 = !DISubroutineType(types: !27)
!23 = !DILocalVariable(name: "a", arg: 1, scope: !21, file: !1, line: 14, type: !4)
!24 = !DILocalVariable(name: "b", arg: 2, scope: !21, file: !1, line: 14, type: !4)
!25 = !DILocation(line: 14, column: 40, scope: !21)
!26 = !{!23, !24}
!27 = !{!3, !4, !4}

;--- escape.ll
; CHECK: {{^}}error: <unknown>:0:0: in function escape void (target("msil.ref")): a .NET reference that cannot become an IL local is not supported for MSIL: the slot's address escapes{{$}}
; AFTER-LABEL: define internal void @escape(target("msil.ref") %a) {
; AFTER-NOT:     {{= |store |call |ret |br |invoke |landingpad }}
; AFTER:         {{^ +}}unreachable
; AFTER-NEXT:  }
define internal void @sink(ptr %p) {
  ret void
}

define internal void @escape(target("msil.ref") %a) {
  %s = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %s, align 8
  call void @sink(ptr %s)
  ret void
}

define i32 @main() {
  call void @escape(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- copy.ll
; CHECK: {{^}}error: <unknown>:0:0: in function copy target("msil.ref") (target("msil.ref")): a .NET reference that cannot become an IL local is not supported for MSIL: the slot is copied as memory{{$}}
; AFTER-LABEL: define internal target("msil.ref") @copy(target("msil.ref") %a) {
; AFTER-NOT:     {{= |store |call |ret |br |invoke |landingpad }}
; AFTER:         {{^ +}}unreachable
; AFTER-NEXT:  }
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

define internal target("msil.ref") @copy(target("msil.ref") %a) {
  %s = alloca target("msil.ref"), align 8
  %t = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %s, align 8
  call void @llvm.memcpy.p0.p0.i64(ptr %t, ptr %s, i64 8, i1 false)
  %v = load target("msil.ref"), ptr %t, align 8
  ret target("msil.ref") %v
}

define i32 @main() {
  %r = call target("msil.ref") @copy(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- through.ll
; CHECK: {{^}}error: <unknown>:0:0: in function loadp target("msil.ref") (ptr): a .NET reference that cannot become an IL local is not supported for MSIL: a load through a pointer{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function storep void (ptr, target("msil.ref")): a .NET reference that cannot become an IL local is not supported for MSIL: a store through a pointer{{$}}
; AFTER-LABEL: define internal target("msil.ref") @loadp(ptr %p) {
; AFTER-NOT:     {{= |store |call |ret |br |invoke |landingpad }}
; AFTER:         {{^ +}}unreachable
; AFTER-NEXT:  }
; AFTER-LABEL: define internal void @storep(ptr %p, target("msil.ref") %a) {
; AFTER-NOT:     {{= |store |call |ret |br |invoke |landingpad }}
; AFTER:         {{^ +}}unreachable
; AFTER-NEXT:  }
define internal target("msil.ref") @loadp(ptr %p) {
  %v = load target("msil.ref"), ptr %p, align 8
  ret target("msil.ref") %v
}

define internal void @storep(ptr %p, target("msil.ref") %a) {
  store target("msil.ref") %a, ptr %p, align 8
  ret void
}

define i32 @main() {
  %x = alloca i64, align 8
  %r = call target("msil.ref") @loadp(ptr %x)
  call void @storep(ptr %x, target("msil.ref") %r)
  ret i32 0
}

;--- pad.ll
; CHECK: {{^}}error: <unknown>:0:0: in function pad void (target("msil.ref")): a .NET reference live across an exception handler is not supported for MSIL yet{{$}}
; AFTER-LABEL: define internal void @pad(target("msil.ref") %a) personality ptr @__gxx_personality_v0 {
; AFTER-NOT:     {{= |store |call |ret |br |invoke |landingpad }}
; AFTER:         {{^ +}}unreachable
; AFTER-NEXT:  }
declare i32 @__gxx_personality_v0(...)

define internal void @may_throw() {
  ret void
}

define internal void @use(target("msil.ref") %a) {
  ret void
}

define internal void @pad(target("msil.ref") %a) personality ptr @__gxx_personality_v0 {
entry:
  %s = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %s, align 8
  invoke void @may_throw() to label %ok unwind label %lp
ok:
  ret void
lp:
  %l = landingpad { ptr, i32 } cleanup
  %v = load target("msil.ref"), ptr %s, align 8
  call void @use(target("msil.ref") %v)
  resume { ptr, i32 } %l
}

define i32 @main() {
  call void @pad(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- padok.ll
; CHECK-NOT:   error:
; CHECK-LABEL: define internal void @pad(target("msil.ref") %a) personality ptr @__gxx_personality_v0 {
; CHECK-NOT:     alloca
; CHECK:         call void @use(target("msil.ref") %a)
; CHECK:         landingpad
; CHECK-NOT:     target("msil.ref")
; CHECK:       }
declare i32 @__gxx_personality_v0(...)

define internal void @may_throw() {
  ret void
}

define internal void @use(target("msil.ref") %a) {
  ret void
}

define internal void @pad(target("msil.ref") %a) personality ptr @__gxx_personality_v0 {
entry:
  %s = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %s, align 8
  invoke void @may_throw() to label %ok unwind label %lp
ok:
  %v = load target("msil.ref"), ptr %s, align 8
  call void @use(target("msil.ref") %v)
  ret void
lp:
  %l = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %l
}

define i32 @main() {
  call void @pad(target("msil.ref") zeroinitializer)
  ret i32 0
}
