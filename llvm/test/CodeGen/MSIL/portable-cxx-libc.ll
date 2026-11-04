; LLVM libc's C++ shapes on msil64-unknown-dotnet (ImplementationPlan.md
; 5.19.1 rule 5, 5.19.3 and 5.19.7, milestone P-1b; recon
; p3/portable-libc.md 4.2 and 6.8). c.dll is linked from llvm-libc's own
; objects, which clang compiles as freestanding C++ ('-ffreestanding
; -fno-exceptions -fno-rtti -fvisibility=hidden', a full build with
; LIBC_COPT_PUBLIC_PACKAGING), so the backend sees these shapes, taken from
; clang's IR for a mimic of src/__support/common.h and macros/config.h at -O0
; (allocas, optnone) and at -O2:
; - LLVM_LIBC_FUNCTION's alias pattern: the implementation is defined under
;   its C name ('strlen', through an asm label) and the namespaced C++ name
;   (_ZN22__llvm_libc_23_0_0_git6strlenEPKc) is a hidden alias of it;
; - header-only inline functions and an inline constexpr table are
;   linkonce_odr definitions in COMDAT groups (at -O2 only the address-taken
;   comparator and the table are left);
; - __llvm_libc_errno is declared inside the hidden LIBC_NAMESPACE_DECL
;   (src/__support/libc_errno.h), so every caller's object records it as a
;   HIDDEN undefined symbol (portable-libc D1: c.dll exports it only through
;   --export-list, lld/test/MSIL/portable-forced-export.test), and the
;   provider (OSUtil/dotnet/errno.cpp's shape: one cell behind the function)
;   defines it hidden too;
; - every symbol is hidden: no object symbol has DEFAULT visibility, so
;   c.dll's exports come only from its export list (5.19.1 rule 5);
; - nothing is imported: no ImplMap or ModuleRef row, no pinvokeimpl.
; Objects (the representation c.dll is linked from) at llc -O0 and -O2 of
; both IR shapes: the ALIAS symbols name their aliasees, the COMDAT groups
; name their WEAK HIDDEN members (a function, and the table's segment), and
; the hidden declaration is a HIDDEN UNDEFINED function. Whole-program text:
; the entry points alone are T-P3 for __llvm_libc_errno (standalone text
; imports nothing, 5.18.6 PL5), and with the provider in the module each
; alias becomes a forwarding method that calls its aliasee (M12), each COMDAT
; function is one method, the comparator's address is its '<msil>fn_' field
; (5.8.3 R2), abs stores ERANGE (34) through the provider's cell, and the
; text assembles.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{llc} = llc -mtriple=msil64-unknown-dotnet -verify-machineinstrs
; DEFINE: %{fc} = FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="LLVM ERROR"

; Objects.
; RUN: %if llvm-cil-dump %{ %{llc} -O0 -filetype=obj %t/entry-o0.ll -o %t/e0.o0.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ %{llc} -O2 -filetype=obj %t/entry-o0.ll -o %t/e0.o2.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ %{llc} -O0 -filetype=obj %t/entry-o2.ll -o %t/e2.o0.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ %{llc} -O2 -filetype=obj %t/entry-o2.ll -o %t/e2.o2.o 2>&1 | count 0 %}
; DEFINE: %{obj} = FileCheck %s --implicit-check-not="GLOBAL DEFAULT" --implicit-check-not="WEAK DEFAULT" \
; DEFINE:   --implicit-check-not=EXPLICIT_IMPORT
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/e0.o0.o | %{obj} --check-prefixes=OBJ,OBJ0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/e0.o2.o | %{obj} --check-prefixes=OBJ,OBJ0 %}
; At -O2 clang has inlined length and clamp_add into their callers.
; DEFINE: %{inlined} = --implicit-check-not=lengthEPKc --implicit-check-not=clamp_add
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/e2.o0.o | %{obj} --check-prefix=OBJ %{inlined} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/e2.o2.o | %{obj} --check-prefix=OBJ %{inlined} %}
; DEFINE: %{stats} = FileCheck %s --check-prefix=STATS --implicit-check-not="# table ImplMap" \
; DEFINE:   --implicit-check-not="# table ModuleRef"
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --stats-only %t/e0.o0.o | %{stats} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --stats-only %t/e2.o2.o | %{stats} %}
; STATS: {{^}}# table MethodDef rows={{[0-9]+}} {{.*$}}

; OBJ:      {{^}}LinkInfo v{{[0-9.]+}} triple=msil64-unknown-dotnet {{.*}} datamodel=8,8,4,8{{$}}
; OBJ-DAG:  {{^}}Symbol [[#STRLEN:]] FUNCTION GLOBAL HIDDEN 'strlen' method=0x{{[0-9A-F]+}}{{$}}
; OBJ-DAG:  {{^}}Symbol [[#ABS:]] FUNCTION GLOBAL HIDDEN 'abs' method=0x{{[0-9A-F]+}}{{$}}
; OBJ-DAG:  {{^}}Symbol [[#LABS:]] FUNCTION GLOBAL HIDDEN 'labs_sat' method=0x{{[0-9A-F]+}}{{$}}
; OBJ-DAG:  {{^}}Symbol [[#DCMP:]] FUNCTION GLOBAL HIDDEN 'default_compare' method=0x{{[0-9A-F]+}}{{$}}
; OBJ-DAG:  {{^}}Symbol [[#CLASS:]] FUNCTION GLOBAL HIDDEN 'char_class' method=0x{{[0-9A-F]+}}{{$}}
; OBJ-DAG:  {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL HIDDEN UNDEFINED '__llvm_libc_errno'{{$}}
; OBJ-DAG:  {{^}}Symbol [[#CMP:]] FUNCTION WEAK HIDDEN '_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_' method=0x{{[0-9A-F]+}}{{$}}
; OBJ-DAG:  {{^}}Symbol {{[0-9]+}} DATA WEAK HIDDEN '_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE' field=0x{{[0-9A-F]+}} segment=[[#SEG:]] offset=0 size=8{{$}}
; OBJ-DAG:  {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL HIDDEN ALIAS '_ZN22__llvm_libc_23_0_0_git6strlenEPKc' aliasee=[[#STRLEN]]{{$}}
; OBJ-DAG:  {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL HIDDEN ALIAS '_ZN22__llvm_libc_23_0_0_git3absEi' aliasee=[[#ABS]]{{$}}
; OBJ-DAG:  {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL HIDDEN ALIAS '_ZN22__llvm_libc_23_0_0_git8labs_satEll' aliasee=[[#LABS]]{{$}}
; OBJ-DAG:  {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL HIDDEN ALIAS '_ZN22__llvm_libc_23_0_0_git15default_compareEv' aliasee=[[#DCMP]]{{$}}
; OBJ-DAG:  {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL HIDDEN ALIAS '_ZN22__llvm_libc_23_0_0_git10char_classEj' aliasee=[[#CLASS]]{{$}}
; OBJ0-DAG: {{^}}Symbol [[#LEN:]] FUNCTION WEAK HIDDEN '_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc' method=0x{{[0-9A-F]+}}{{$}}
; OBJ0-DAG: {{^}}Symbol [[#CLAMP:]] FUNCTION WEAK HIDDEN '_ZN22__llvm_libc_23_0_0_git8internal9clamp_addIlEET_S2_S2_S2_' method=0x{{[0-9A-F]+}}{{$}}
; OBJ-DAG:  {{^}}Segment [[#SEG]] '.rodata' align=1 size=8 flags=READONLY{{$}}
; OBJ-DAG:  {{^}}  01 02 04 08 10 20 40 80{{$}}
; OBJ-DAG:  {{^}}Comdat '_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_' ANY function=[[#CMP]]{{$}}
; OBJ-DAG:  {{^}}Comdat '_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE' ANY segment=[[#SEG]]{{$}}
; OBJ0-DAG: {{^}}Comdat '_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc' ANY function=[[#LEN]]{{$}}
; OBJ0-DAG: {{^}}Comdat '_ZN22__llvm_libc_23_0_0_git8internal9clamp_addIlEET_S2_S2_S2_' ANY function=[[#CLAMP]]{{$}}

; The provider's object: a hidden definition over a local cell.
; RUN: cat %t/errno-name.ll %t/provider.ll > %t/errno.ll
; RUN: %if llvm-cil-dump %{ %{llc} -O2 -filetype=obj %t/errno.ll -o %t/errno.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/errno.o | FileCheck %s --check-prefix=ERRNO \
; RUN:   --implicit-check-not="GLOBAL DEFAULT" --implicit-check-not=UNDEFINED %}
; ERRNO-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL HIDDEN '__llvm_libc_errno' method=0x{{[0-9A-F]+}}{{$}}
; ERRNO-DAG: {{^}}Symbol {{[0-9]+}} DATA LOCAL {{[A-Z]+}} '_ZN22__llvm_libc_23_0_0_git12_GLOBAL__N_14cellE' field=0x{{[0-9A-F]+}} segment={{[0-9]+}} offset=0 size=4{{$}}

; Whole-program text of the entry points alone: T-P3 for the hidden
; declaration, and nothing else.
; RUN: not %{llc} -O2 %t/entry-o2.ll -o /dev/null 2>&1 | %{fc} --check-prefix=TP3 --implicit-check-not=error:
; RUN: not %{llc} -O0 %t/entry-o0.ll -o /dev/null 2>&1 | %{fc} --check-prefix=TP3 --implicit-check-not=error:
; TP3: {{^}}{{.*}}error: function '__llvm_libc_errno' is not defined in this module: standalone text for msil64-unknown-dotnet imports nothing (link with msil-ld){{$}}

; Whole-program text with the provider in the module.
; RUN: grep -v "^declare hidden ptr @__llvm_libc_errno" %t/entry-o0.ll > %t/whole-o0.ll
; RUN: cat %t/provider.ll >> %t/whole-o0.ll
; RUN: grep -v "^declare hidden ptr @__llvm_libc_errno" %t/entry-o2.ll > %t/whole-o2.ll
; RUN: cat %t/provider.ll >> %t/whole-o2.ll
; DEFINE: %{txt} = %{llc} -asm-verbose=false
; RUN: %{txt} -O0 %t/whole-o0.ll -o %t/w0.o0.il 2>&1 | count 0
; RUN: %{txt} -O2 %t/whole-o0.ll -o %t/w0.o2.il 2>&1 | count 0
; RUN: %{txt} -O0 %t/whole-o2.ll -o %t/w2.o0.il 2>&1 | count 0
; RUN: %{txt} -O2 %t/whole-o2.ll -o %t/w2.o2.il 2>&1 | count 0
; DEFINE: %{text} = %{fc} --implicit-check-not=pinvokeimpl --implicit-check-not=moduleref \
; DEFINE:   --implicit-check-not=DllImport
; RUN: %{text} --check-prefixes=TXT,TXT0 --input-file=%t/w0.o0.il
; RUN: %{text} --check-prefixes=TXT,TXT0 --input-file=%t/w0.o2.il
; RUN: %{text} --check-prefixes=TXT,TXT2 --input-file=%t/w2.o0.il
; RUN: %{text} --check-prefixes=TXT,TXT2 --input-file=%t/w2.o2.il

; One method per COMDAT function, one forwarding method per alias.
; RUN: grep -c "^\.method .*'_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_'(" %t/w0.o0.il \
; RUN:   | FileCheck %s --check-prefix=ONE
; RUN: grep -c "^\.method .*'_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_'(" %t/w2.o2.il \
; RUN:   | FileCheck %s --check-prefix=ONE
; RUN: grep -c "^\.method .*'_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc'(" %t/w0.o2.il \
; RUN:   | FileCheck %s --check-prefix=ONE
; RUN: grep -c "^\.method .*'_ZN22__llvm_libc_23_0_0_git6strlenEPKc'(" %t/w2.o2.il \
; RUN:   | FileCheck %s --check-prefix=ONE
; ONE: {{^}}1{{$}}

; RUN: %if ilasm %{ %ilasm-verify -output=%t/w0.o0.dll %t/w0.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/w0.o2.dll %t/w0.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/w2.o0.dll %t/w2.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/w2.o2.dll %t/w2.o2.il %}

; TXT:         {{^}}// target triple   = "msil64-unknown-dotnet"{{$}}
; TXT:         {{^}}.assembly 'entry' { .ver 0:0:0:0 }{{$}}
; TXT-LABEL:   {{^}}.method public static native int 'strlen'(native int) cil managed{{$}}
; TXT0:        {{^}}  call native int '_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc'(native int){{$}}
; TXT2:        {{^}}  ldind.u1{{$}}
; TXT:         {{^}}} // end of method 'strlen'{{$}}
; TXT0-LABEL:  {{^}}.method public static native int '_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc'(native int) cil managed{{$}}
; TXT-LABEL:   {{^}}.method public static int32 'abs'(int32) cil managed{{$}}
; TXT:         {{^}}  call native int '__llvm_libc_errno'(){{$}}
; TXT-NEXT:    {{^}}  ldc.i4.s 34{{$}}
; TXT-NEXT:    {{^}}  stind.i4{{$}}
; TXT:         {{^}}} // end of method 'abs'{{$}}
; TXT-LABEL:   {{^}}.method public static native int 'labs_sat'(native int, native int) cil managed{{$}}
; TXT0:        {{^}}  call native int '_ZN22__llvm_libc_23_0_0_git8internal9clamp_addIlEET_S2_S2_S2_'(native int, native int, native int){{$}}
; TXT0-LABEL:  {{^}}.method public static native int '_ZN22__llvm_libc_23_0_0_git8internal9clamp_addIlEET_S2_S2_S2_'(native int, native int, native int) cil managed{{$}}
; TXT-LABEL:   {{^}}.method public static native int 'default_compare'() cil managed{{$}}
; TXT:         {{^}}  ldsfld native int '<msil>fn__ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_'{{$}}
; TXT:         {{^}}} // end of method 'default_compare'{{$}}
; TXT-LABEL:   {{^}}.method public static int32 '_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_'(native int, native int) cil managed{{$}}
; TXT-LABEL:   {{^}}.method public static int32 'char_class'(int32) cil managed{{$}}
; TXT:         {{^}}  ldsfld native int '_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE'{{$}}
; TXT:         {{^}}  ldind.u1{{$}}
; TXT:         {{^}}} // end of method 'char_class'{{$}}
; TXT-LABEL:   {{^}}.method public static native int '__llvm_libc_errno'() cil managed{{$}}
; TXT:         {{^}}  ldsfld native int '_ZN22__llvm_libc_23_0_0_git12_GLOBAL__N_14cellE'{{$}}
; TXT:         {{^}}} // end of method '__llvm_libc_errno'{{$}}
; TXT-LABEL:   {{^}}.method public static native int '_ZN22__llvm_libc_23_0_0_git6strlenEPKc'(native int) cil managed{{$}}
; TXT:         {{^}}  call native int 'strlen'(native int){{$}}
; TXT-NEXT:    {{^}}  ret{{$}}
; TXT-LABEL:   {{^}}.method public static int32 '_ZN22__llvm_libc_23_0_0_git3absEi'(int32) cil managed{{$}}
; TXT:         {{^}}  call int32 'abs'(int32){{$}}
; TXT-NEXT:    {{^}}  ret{{$}}
; TXT-LABEL:   {{^}}.method public static native int '_ZN22__llvm_libc_23_0_0_git8labs_satEll'(native int, native int) cil managed{{$}}
; TXT:         {{^}}  call native int 'labs_sat'(native int, native int){{$}}
; TXT-NEXT:    {{^}}  ret{{$}}
; TXT-LABEL:   {{^}}.method public static native int '_ZN22__llvm_libc_23_0_0_git15default_compareEv'() cil managed{{$}}
; TXT:         {{^}}  call native int 'default_compare'(){{$}}
; TXT-NEXT:    {{^}}  ret{{$}}
; TXT-LABEL:   {{^}}.method public static int32 '_ZN22__llvm_libc_23_0_0_git10char_classEj'(int32) cil managed{{$}}
; TXT:         {{^}}  call int32 'char_class'(int32){{$}}
; TXT-NEXT:    {{^}}  ret{{$}}
; TXT:         {{^}}  01 02 04 08 10 20 40 80 ){{.*$}}
; TXT:         {{^}}.field {{public|assembly}} static initonly native int '_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE'{{( // \+[0-9]+)?$}}
; TXT:         {{^}}.field {{public|assembly}} static initonly native int '_ZN22__llvm_libc_23_0_0_git12_GLOBAL__N_14cellE'{{( // \+[0-9]+)?$}}
; TXT:         {{^}}.field private static initonly native int '<msil>fn__ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_'{{$}}

;--- entry-o0.ll
source_filename = "entry.cpp"

$_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc = comdat any

$_ZN22__llvm_libc_23_0_0_git8internal9clamp_addIlEET_S2_S2_S2_ = comdat any

$_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_ = comdat any

$_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE = comdat any

@_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE = linkonce_odr hidden constant [8 x i8] c"\01\02\04\08\10 @\80", comdat, align 1

@_ZN22__llvm_libc_23_0_0_git6strlenEPKc = hidden alias i64 (ptr), ptr @strlen
@_ZN22__llvm_libc_23_0_0_git3absEi = hidden alias i32 (i32), ptr @abs
@_ZN22__llvm_libc_23_0_0_git8labs_satEll = hidden alias i64 (i64, i64), ptr @labs_sat
@_ZN22__llvm_libc_23_0_0_git15default_compareEv = hidden alias ptr (), ptr @default_compare
@_ZN22__llvm_libc_23_0_0_git10char_classEj = hidden alias i32 (i32), ptr @char_class

define hidden noundef i64 @strlen(ptr noundef %s) #0 {
entry:
  %s.addr = alloca ptr, align 8
  store ptr %s, ptr %s.addr, align 8
  %0 = load ptr, ptr %s.addr, align 8
  %call = call noundef i64 @_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc(ptr noundef %0) #2
  ret i64 %call
}

define linkonce_odr hidden noundef i64 @_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc(ptr noundef %s) #0 comdat {
entry:
  %s.addr = alloca ptr, align 8
  %n = alloca i64, align 8
  store ptr %s, ptr %s.addr, align 8
  store i64 0, ptr %n, align 8
  br label %while.cond

while.cond:                                       ; preds = %while.body, %entry
  %0 = load ptr, ptr %s.addr, align 8
  %1 = load i64, ptr %n, align 8
  %arrayidx = getelementptr inbounds nuw i8, ptr %0, i64 %1
  %2 = load i8, ptr %arrayidx, align 1
  %tobool = icmp ne i8 %2, 0
  br i1 %tobool, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %3 = load i64, ptr %n, align 8
  %inc = add i64 %3, 1
  store i64 %inc, ptr %n, align 8
  br label %while.cond

while.end:                                        ; preds = %while.cond
  %4 = load i64, ptr %n, align 8
  ret i64 %4
}

define hidden noundef i32 @abs(i32 noundef %x) #0 {
entry:
  %retval = alloca i32, align 4
  %x.addr = alloca i32, align 4
  store i32 %x, ptr %x.addr, align 4
  %0 = load i32, ptr %x.addr, align 4
  %cmp = icmp eq i32 %0, -2147483648
  br i1 %cmp, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %call = call ptr @__llvm_libc_errno() #3
  store i32 34, ptr %call, align 4
  %1 = load i32, ptr %x.addr, align 4
  store i32 %1, ptr %retval, align 4
  br label %return

if.end:                                           ; preds = %entry
  %2 = load i32, ptr %x.addr, align 4
  %cmp1 = icmp slt i32 %2, 0
  br i1 %cmp1, label %cond.true, label %cond.false

cond.true:                                        ; preds = %if.end
  %3 = load i32, ptr %x.addr, align 4
  %sub = sub nsw i32 0, %3
  br label %cond.end

cond.false:                                       ; preds = %if.end
  %4 = load i32, ptr %x.addr, align 4
  br label %cond.end

cond.end:                                         ; preds = %cond.false, %cond.true
  %cond = phi i32 [ %sub, %cond.true ], [ %4, %cond.false ]
  store i32 %cond, ptr %retval, align 4
  br label %return

return:                                           ; preds = %cond.end, %if.then
  %5 = load i32, ptr %retval, align 4
  ret i32 %5
}

declare hidden ptr @__llvm_libc_errno() #1

define hidden noundef i64 @labs_sat(i64 noundef %a, i64 noundef %b) #0 {
entry:
  %a.addr = alloca i64, align 8
  %b.addr = alloca i64, align 8
  store i64 %a, ptr %a.addr, align 8
  store i64 %b, ptr %b.addr, align 8
  %0 = load i64, ptr %a.addr, align 8
  %1 = load i64, ptr %b.addr, align 8
  %call = call noundef i64 @_ZN22__llvm_libc_23_0_0_git8internal9clamp_addIlEET_S2_S2_S2_(i64 noundef %0, i64 noundef %1, i64 noundef 1000) #2
  ret i64 %call
}

define linkonce_odr hidden noundef i64 @_ZN22__llvm_libc_23_0_0_git8internal9clamp_addIlEET_S2_S2_S2_(i64 noundef %a, i64 noundef %b, i64 noundef %hi) #0 comdat {
entry:
  %a.addr = alloca i64, align 8
  %b.addr = alloca i64, align 8
  %hi.addr = alloca i64, align 8
  %s = alloca i64, align 8
  store i64 %a, ptr %a.addr, align 8
  store i64 %b, ptr %b.addr, align 8
  store i64 %hi, ptr %hi.addr, align 8
  %0 = load i64, ptr %a.addr, align 8
  %1 = load i64, ptr %b.addr, align 8
  %add = add nsw i64 %0, %1
  store i64 %add, ptr %s, align 8
  %2 = load i64, ptr %s, align 8
  %3 = load i64, ptr %hi.addr, align 8
  %cmp = icmp sgt i64 %2, %3
  br i1 %cmp, label %cond.true, label %cond.false

cond.true:                                        ; preds = %entry
  %4 = load i64, ptr %hi.addr, align 8
  br label %cond.end

cond.false:                                       ; preds = %entry
  %5 = load i64, ptr %s, align 8
  br label %cond.end

cond.end:                                         ; preds = %cond.false, %cond.true
  %cond = phi i64 [ %4, %cond.true ], [ %5, %cond.false ]
  ret i64 %cond
}

define hidden noundef ptr @default_compare() #0 {
entry:
  ret ptr @_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_
}

define linkonce_odr hidden noundef i32 @_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_(ptr noundef %a, ptr noundef %b) #0 comdat {
entry:
  %a.addr = alloca ptr, align 8
  %b.addr = alloca ptr, align 8
  %x = alloca i32, align 4
  %y = alloca i32, align 4
  store ptr %a, ptr %a.addr, align 8
  store ptr %b, ptr %b.addr, align 8
  %0 = load ptr, ptr %a.addr, align 8
  %1 = load i32, ptr %0, align 4
  store i32 %1, ptr %x, align 4
  %2 = load ptr, ptr %b.addr, align 8
  %3 = load i32, ptr %2, align 4
  store i32 %3, ptr %y, align 4
  %4 = load i32, ptr %x, align 4
  %5 = load i32, ptr %y, align 4
  %cmp = icmp sgt i32 %4, %5
  %conv = zext i1 %cmp to i32
  %6 = load i32, ptr %x, align 4
  %7 = load i32, ptr %y, align 4
  %cmp1 = icmp slt i32 %6, %7
  %conv2 = zext i1 %cmp1 to i32
  %sub = sub nsw i32 %conv, %conv2
  ret i32 %sub
}

define hidden noundef i32 @char_class(i32 noundef %c) #0 {
entry:
  %c.addr = alloca i32, align 4
  store i32 %c, ptr %c.addr, align 4
  %0 = load i32, ptr %c.addr, align 4
  %and = and i32 %0, 7
  %idxprom = zext i32 %and to i64
  %arrayidx = getelementptr inbounds nuw [8 x i8], ptr @_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE, i64 0, i64 %idxprom
  %1 = load i8, ptr %arrayidx, align 1
  %conv = zext i8 %1 to i32
  ret i32 %conv
}

attributes #0 = { mustprogress noinline nounwind optnone "no-builtins" }
attributes #1 = { nounwind "no-builtins" }
attributes #2 = { nobuiltin "no-builtins" }
attributes #3 = { nobuiltin nounwind "no-builtins" }

;--- entry-o2.ll
source_filename = "entry.cpp"

$_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_ = comdat any

$_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE = comdat any

@_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE = linkonce_odr hidden local_unnamed_addr constant [8 x i8] c"\01\02\04\08\10 @\80", comdat, align 1

@_ZN22__llvm_libc_23_0_0_git6strlenEPKc = hidden alias i64 (ptr), ptr @strlen
@_ZN22__llvm_libc_23_0_0_git3absEi = hidden alias i32 (i32), ptr @abs
@_ZN22__llvm_libc_23_0_0_git8labs_satEll = hidden alias i64 (i64, i64), ptr @labs_sat
@_ZN22__llvm_libc_23_0_0_git15default_compareEv = hidden alias ptr (), ptr @default_compare
@_ZN22__llvm_libc_23_0_0_git10char_classEj = hidden alias i32 (i32), ptr @char_class

define hidden noundef i64 @strlen(ptr nofree noundef readonly captures(none) %s) #0 {
entry:
  br label %while.cond.i

while.cond.i:                                     ; preds = %while.cond.i, %entry
  %n.0.i = phi i64 [ 0, %entry ], [ %inc.i, %while.cond.i ]
  %arrayidx.i = getelementptr inbounds nuw i8, ptr %s, i64 %n.0.i
  %0 = load i8, ptr %arrayidx.i, align 1
  %tobool.not.i = icmp eq i8 %0, 0
  %inc.i = add i64 %n.0.i, 1
  br i1 %tobool.not.i, label %_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc.exit, label %while.cond.i

_ZN22__llvm_libc_23_0_0_git8internal6lengthEPKc.exit: ; preds = %while.cond.i
  ret i64 %n.0.i
}

define hidden noundef range(i32 0, -2147483647) i32 @abs(i32 noundef %x) #1 {
entry:
  %cmp = icmp eq i32 %x, -2147483648
  br i1 %cmp, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %call = tail call ptr @__llvm_libc_errno() #7
  store i32 34, ptr %call, align 4
  br label %return

if.end:                                           ; preds = %entry
  %cond = tail call i32 @llvm.abs.i32(i32 %x, i1 true)
  br label %return

return:                                           ; preds = %if.end, %if.then
  %retval.0 = phi i32 [ -2147483648, %if.then ], [ %cond, %if.end ]
  ret i32 %retval.0
}

declare hidden ptr @__llvm_libc_errno() local_unnamed_addr #2

define hidden noundef range(i64 -9223372036854775808, 1001) i64 @labs_sat(i64 noundef %a, i64 noundef %b) #3 {
entry:
  %add.i = add nsw i64 %b, %a
  %cond.i = tail call noundef i64 @llvm.smin.i64(i64 %add.i, i64 1000)
  ret i64 %cond.i
}

define hidden noundef nonnull ptr @default_compare() #3 {
entry:
  ret ptr @_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_
}

define linkonce_odr hidden noundef i32 @_ZN22__llvm_libc_23_0_0_git8internal7compareEPKvS2_(ptr noundef %a, ptr noundef %b) #4 comdat {
entry:
  %0 = load i32, ptr %a, align 4
  %1 = load i32, ptr %b, align 4
  %sub = tail call i32 @llvm.scmp.i32.i32(i32 %0, i32 %1)
  ret i32 %sub
}

define hidden noundef range(i32 0, 256) i32 @char_class(i32 noundef %c) #3 {
entry:
  %and = and i32 %c, 7
  %idxprom = zext nneg i32 %and to i64
  %arrayidx = getelementptr inbounds nuw i8, ptr @_ZN22__llvm_libc_23_0_0_git8internal11CLASS_TABLEE, i64 %idxprom
  %0 = load i8, ptr %arrayidx, align 1
  %conv = zext i8 %0 to i32
  ret i32 %conv
}

declare i32 @llvm.abs.i32(i32, i1 immarg) #5

declare i64 @llvm.smin.i64(i64, i64) #6

declare range(i32 -1, 2) i32 @llvm.scmp.i32.i32(i32, i32) #6

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: read) "no-builtins" }
attributes #1 = { mustprogress nounwind "no-builtins" }
attributes #2 = { nounwind "no-builtins" }
attributes #3 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "no-builtins" }
attributes #4 = { inlinehint mustprogress nounwind "no-builtins" }
attributes #5 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #6 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #7 = { nobuiltin nounwind "no-builtins" }

;--- errno-name.ll
source_filename = "errno.cpp"

;--- provider.ll
@_ZN22__llvm_libc_23_0_0_git12_GLOBAL__N_14cellE = internal global i32 0, align 4

define hidden ptr @__llvm_libc_errno() #20 {
  ret ptr @_ZN22__llvm_libc_23_0_0_git12_GLOBAL__N_14cellE
}

attributes #20 = { nounwind "no-builtins" }
