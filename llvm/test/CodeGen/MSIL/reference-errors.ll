; The back end's backstops for .NET references (ImplementationPlan.md 5.21.7
; RB2, RB11, RB12; texts T-RB1 to T-RB12 of 5.21.12, each pinned whole;
; milestone R-a, RV24). clang's Sema refuses every C form these cases stand
; for (5.21.4), so the IR here is hand-written: MSILPrepareIR (P1) refuses
; what no reference may reach, erases the diagnosed definition's body and
; turns the uses of its address into poison; MSILFixFunctionBitcasts (P5)
; refuses a mismatched call involving a reference (T-RB5) instead of
; converting it or trapping; instruction selection refuses a load or store
; of a reference that reaches it (T-RB6). llc reports each error, exits with
; 1 and never crashes, in whole-program text and in object mode
; (-filetype=obj), at -O0 and -O2, on both host x86_64 triples,
; msil64-unknown-dotnet and aarch64. Function-level texts carry
; DiagnosticInfoUnsupported's prefix 'in function <name> <IR type>', and
; each text is reported once per function. The errors are plain CHECK lines
; in the order llc reports them (P1 in module order, its declaration loop
; after the definitions; P5 callee by callee in module order, a callee's
; later call first, as LLVM's use lists hold them): FileCheck applies the
; implicit check-nots only before a CHECK-DAG group, not between or after
; its matches, so with CHECK-DAG a second or extra report would pass.
; P1 (RB11's rules):
; - aggregate.ll, rule 1: 'a .NET reference cannot be held in C memory: the
;   aggregate type '<T>'' for a structure in a signature (and in the
;   caller's call result) and an array slot;
; - params.ll, rule 7: the same text with '<a|an> '<attribute>' parameter',
;   with the article that LLVM writes before each name (section 12's row
;   '5.21.12 T-R21, T-RB10 and T-RB1 amended' of 2026-11-21): a 'byval', an
;   'sret', an 'inalloca', a 'preallocated' and a 'byref' parameter;
;   eltype.ll an 'elementtype' parameter, a call site's elementtype (the
;   verifier accepts elementtype only on intrinsic and asm call sites, so it
;   runs with -disable-verify);
; - slots.ll, rule 9: 'a stack slot of more than one element' and 'a stack
;   slot outside the entry block';
; - linkage.ll, rule 3: 'a function with a .NET reference in its signature
;   <reason> is not supported for MSIL yet' with 'that is not 'static'' (an
;   external definition and an external declaration) and 'that ThinLTO or
;   --lto-partitions made external (link with full LTO and
;   --lto-partitions=1)' (a hidden definition);
; - forwarder.ll: an MC-1 dllexport forwarder (5.20.4 as amended) of a
;   managed call with references is such an external definition: T-RB2's
;   first reason, its body erased; the stub itself is no error (P0's stubs
;   and the helper are exempt from rule 3);
; - address.ll, rule 4: '... whose address is taken ...' for an address
;   returned, an address in a global initializer and an
;   llvm.global.annotations entry; the uses become poison, the definitions
;   lose their bodies and their direct calls are erased (after P1,
;   -stop-after=msil-prepare-ir);
;   used.ll and compiler-used.ll: an llvm.used or an llvm.compiler.used
;   listing takes no address, so they compile, in text and objects;
;   metadata-list.ll, their control: the same function listed in another
;   llvm.metadata array has its address taken;
; - alias.ll, rule 4: an alias of a reference function (one of its own type
;   and one of another type) takes its address, in object mode and in
;   object-form text; in whole-program text P1 lowers the aliases (decision
;   D26 as amended): the one of another type still takes the function's
;   address, and the external one of its own type becomes a function of
;   that name, which rule 3 refuses as well;
; - calls.ll, rules 4 and 5: '... called through a pointer or with another
;   function type ...' for an indirect call and a direct call with another
;   function type, whose callee also has its address taken;
; - variadic.ll, rule 6: '... that is variadic ...' and T-RB12 'a .NET
;   reference as a variadic argument or as the type of 'va_arg' is not
;   supported for MSIL' (a variadic argument and a va_arg); on the Linux
;   triples (LNX), whose va_list is a structure, P1 reports every va_arg
;   instruction with M26's 'the va_arg instruction is not supported for MSIL
;   on this triple; clang lowers C's va_arg itself' (va-intrinsics.ll), so
;   the va_arg gets that text too, before T-RB12;
; - setjmp.ll, rule 8: T-RB3 'a .NET reference in a function that calls
;   setjmp is not supported for MSIL yet' for a call of a returns_twice
;   'setjmp'; setjmp-names.ll the same text for direct calls of 'setjmp' and
;   '_setjmp3' declared without returns_twice, since rule 8 also fences the
;   direct callers of a declaration whose name MSIL::isSetjmpName accepts,
;   which P8 lowers as setjmp sites (section 12's row '5.21.7 RB11 rule 8
;   amended' of 2026-11-21); setjmp-knr.ll the same text for a call of
;   '_setjmp3' declared without a prototype ("no-prototype": the call has a
;   function type of its own, so rule 8 takes the callee from the called
;   operand, as P8 does) and for an invoke of '_setjmp', neither declared
;   returns_twice; setjmp-uses.ll the same text for setjmp callers with no
;   reference parameter: an unused reference call result before a call of
;   a returns_twice 'setjmp' and before a call of '_setjmp3' (by name), a
;   reference result, and a null reference passed to a call;
; - reserved.ll, rule 11: T-RB7 ''<name>' is reserved for the MSIL
;   toolchain' for a definition of '__msil_ref.eq', a declaration of another
;   '__msil_ref.' name and (reserved-type.ll) a declaration of
;   '__msil_ref.eq' with another type;
; - global.ll, rule 2: 'global variable @<g>: a .NET reference in a global
;   variable is not supported for MSIL yet' for a reference and an array of
;   references, with -disable-verify (the IR verifier refuses such globals
;   first: reference-ir.ll);
; - asm.ll, rule 10: T-RB9 'a .NET reference in an inline asm operand is not
;   supported for MSIL yet' for an operand and for a result;
; - volatile.ll and atomic.ll, rule 9: T-RB10 '<a volatile|an atomic> access
;   of a .NET reference is not supported for MSIL' (as section 12's row
;   '5.21.12 T-R21, T-RB10 and T-RB1 amended' of 2026-11-21 reads it): 'a
;   volatile access ...' for a volatile store and a volatile load, and 'an
;   atomic access ...' for an atomic load (the verifier refuses an atomic
;   load of a target type, so -disable-verify);
; - intrinsic.ll, rule 12: T-RB11 'a .NET reference as an operand or result
;   of '<intrinsic>' is not supported for MSIL' for llvm.ssa.copy;
;   bundle.ll the same text for 'call' when a reference is an operand of an
;   operand bundle (a custom "foo" bundle and "gc-transition"), each
;   function's only error;
; - fakeuse.ll: llvm.fake.use of a reference (clang writes it at -Og,
;   5.21.6 RI4) is accepted and compiles.
; P5: bitcast-calls.ll, which P1 refuses (rules 4 and 5) before P5 runs, so
; it starts after P1 (-start-after=msil-prepare-ir): T-RB5 'call of '<g>'
; passes '<A>' where it takes '<P>': a .NET reference cannot be converted'
; for a pointer passed for a reference, a reference result taken as a
; pointer, a missing argument ('void') and, when no reference position
; changes, the two function types.
; ISel: isel.ll starts after P15 (-start-after=msil-promote-references) with
; a slot of the type: T-RB6 'a .NET reference is loaded from or stored to
; memory' (at least once: the store and the load may be reported apart).

; RUN: rm -rf %t && split-file %s %t

; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="Stack dump"
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{f} =
; DEFINE: %{x} =
; DEFINE: %{p} =
; DEFINE: %{wp} = not llc -mtriple=%{t} %{o} %{x} -verify-machineinstrs < %t/%{f}.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %t/%{f}.ll %{p} %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:
; DEFINE: %{obj} = not llc -mtriple=%{t} %{o} %{x} -filetype=obj < %t/%{f}.ll -o %t/%{f}.o 2>&1 \
; DEFINE:   | FileCheck %t/%{f}.ll %{p} %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:
; DEFINE: %{both} = %{wp} && %{obj}

; REDEFINE: %{f} = aggregate
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{both}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O2
; RUN: %{both}

; REDEFINE: %{f} = params
; RUN: %{both}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{both}

; REDEFINE: %{f} = eltype
; REDEFINE: %{x} = -disable-verify
; RUN: %{both}
; REDEFINE: %{x} =

; REDEFINE: %{f} = slots
; RUN: %{both}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}

; REDEFINE: %{f} = linkage
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %{both}

; REDEFINE: %{f} = forwarder
; RUN: %{both}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil < %t/forwarder.ll -o - 2>/dev/null \
; RUN:   | FileCheck %t/forwarder.ll --check-prefix=STUB

; REDEFINE: %{f} = address
; RUN: %{both}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{both}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/address.ll -o - 2>/dev/null \
; RUN:   | FileCheck %t/address.ll --check-prefix=AFTER
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/used.ll -o /dev/null 2>&1 | count 0
; RUN: llc -mtriple=msil64-unknown-dotnet -O2 -filetype=obj < %t/used.ll -o %t/used.o 2>&1 | count 0
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -O2 -verify-machineinstrs < %t/compiler-used.ll \
; RUN:   -o /dev/null 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj < %t/compiler-used.ll \
; RUN:   -o %t/compiler-used.o 2>&1 | count 0
; REDEFINE: %{f} = metadata-list
; RUN: %{both}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}

; REDEFINE: %{f} = alias
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{obj}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form < %t/alias.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %t/alias.ll %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O2
; RUN: %{obj}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 < %t/alias.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %t/alias.ll --check-prefixes=CHECK,WP %{nocrash} --implicit-check-not=error: \
; RUN:     --implicit-check-not=warning:

; REDEFINE: %{f} = calls
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{both}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}

; REDEFINE: %{f} = variadic
; REDEFINE: %{p} = --check-prefixes=CHECK,LNX
; RUN: %{both}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{both}
; REDEFINE: %{p} =
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{both}

; REDEFINE: %{f} = setjmp
; RUN: %{both}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}
; REDEFINE: %{f} = setjmp-names
; RUN: %{both}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{both}
; REDEFINE: %{f} = setjmp-knr
; RUN: %{both}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}
; REDEFINE: %{f} = setjmp-uses
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; RUN: %{both}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %{both}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2

; REDEFINE: %{f} = reserved
; RUN: %{both}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %{both}
; REDEFINE: %{f} = reserved-type
; RUN: %{both}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}

; REDEFINE: %{f} = global
; REDEFINE: %{x} = -disable-verify
; RUN: %{both}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{both}
; REDEFINE: %{x} =

; REDEFINE: %{f} = asm
; RUN: %{both}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}

; REDEFINE: %{f} = volatile
; RUN: %{both}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O0
; RUN: %{both}
; REDEFINE: %{f} = atomic
; REDEFINE: %{x} = -disable-verify
; RUN: %{both}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}
; REDEFINE: %{x} =

; REDEFINE: %{f} = intrinsic
; RUN: %{both}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{both}
; REDEFINE: %{f} = bundle
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{both}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/fakeuse.ll -o %t/fakeuse.il 2>&1 \
; RUN:   | count 0
; RUN: FileCheck %t/fakeuse.ll --input-file=%t/fakeuse.il
; RUN: llc -mtriple=msil64-unknown-dotnet -O2 -filetype=obj < %t/fakeuse.ll -o %t/fakeuse.o 2>&1 | count 0
; RUN: %if ilasm %{ %ilasm-verify -output=%t/fakeuse.dll %t/fakeuse.il %}

; REDEFINE: %{f} = bitcast-calls
; REDEFINE: %{x} = -start-after=msil-prepare-ir
; RUN: %{both}
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{both}
; REDEFINE: %{x} =

; DEFINE: %{isel} = not llc -mtriple=%{t} %{o} -start-after=msil-promote-references < %t/isel.ll \
; DEFINE:   -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %t/isel.ll %{nocrash} --implicit-check-not="not supported" && \
; DEFINE:   not llc -mtriple=%{t} %{o} -start-after=msil-promote-references -filetype=obj < %t/isel.ll \
; DEFINE:   -o %t/isel.o 2>&1 \
; DEFINE:   | FileCheck %t/isel.ll %{nocrash} --implicit-check-not="not supported"
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{isel}
; REDEFINE: %{o} = -O2
; RUN: %{isel}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{isel}

;--- aggregate.ll
; CHECK: {{^}}error: <unknown>:0:0: in function agg_ret { target("msil.ref"), i32 } (target("msil.ref")): a .NET reference cannot be held in C memory: the aggregate type '{ target("msil.ref"), i32 }'{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function agg_slot void (target("msil.ref")): a .NET reference cannot be held in C memory: the aggregate type '[2 x target("msil.ref")]'{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function main i32 (): a .NET reference cannot be held in C memory: the aggregate type '{ target("msil.ref"), i32 }'{{$}}
define internal { target("msil.ref"), i32 } @agg_ret(target("msil.ref") %a) {
  %s = insertvalue { target("msil.ref"), i32 } poison, target("msil.ref") %a, 0
  ret { target("msil.ref"), i32 } %s
}

define internal void @agg_slot(target("msil.ref") %a) {
  %s = alloca [2 x target("msil.ref")], align 8
  ret void
}

define i32 @main() {
  %r = call { target("msil.ref"), i32 } @agg_ret(target("msil.ref") zeroinitializer)
  call void @agg_slot(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- params.ll
; CHECK: {{^}}error: <unknown>:0:0: in function by_val void (ptr): a .NET reference cannot be held in C memory: a 'byval' parameter{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function s_ret void (ptr): a .NET reference cannot be held in C memory: an 'sret' parameter{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function in_alloca void (ptr): a .NET reference cannot be held in C memory: an 'inalloca' parameter{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function pre_alloc void (ptr): a .NET reference cannot be held in C memory: a 'preallocated' parameter{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function by_ref void (ptr): a .NET reference cannot be held in C memory: a 'byref' parameter{{$}}
define internal void @by_val(ptr byval(target("msil.ref")) %p) {
  ret void
}

define internal void @s_ret(ptr sret(target("msil.ref")) %p) {
  ret void
}

define internal void @in_alloca(ptr inalloca(target("msil.ref")) %p) {
  ret void
}

define internal void @pre_alloc(ptr preallocated(target("msil.ref")) %p) {
  ret void
}

define internal void @by_ref(ptr byref(target("msil.ref")) %p) {
  ret void
}

define i32 @main() {
  ret i32 0
}

;--- eltype.ll
; CHECK: {{^}}error: <unknown>:0:0: in function elt void (ptr): a .NET reference cannot be held in C memory: an 'elementtype' parameter{{$}}
define internal void @take(ptr %p) {
  ret void
}

define internal void @elt(ptr %p) {
  call void @take(ptr elementtype(target("msil.ref")) %p)
  ret void
}

define i32 @main() {
  %x = alloca i64, align 8
  call void @elt(ptr %x)
  ret i32 0
}

;--- slots.ll
; CHECK: {{^}}error: <unknown>:0:0: in function two void (target("msil.ref")): a .NET reference cannot be held in C memory: a stack slot of more than one element{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function late void (i1, target("msil.ref")): a .NET reference cannot be held in C memory: a stack slot outside the entry block{{$}}
define internal void @two(target("msil.ref") %a) {
  %s = alloca target("msil.ref"), i32 2, align 8
  store target("msil.ref") %a, ptr %s, align 8
  ret void
}

define internal void @late(i1 %c, target("msil.ref") %a) {
entry:
  br i1 %c, label %b, label %e
b:
  %s = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %s, align 8
  br label %e
e:
  ret void
}

define i32 @main() {
  call void @two(target("msil.ref") zeroinitializer)
  call void @late(i1 true, target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- linkage.ll
; CHECK: {{^}}error: <unknown>:0:0: in function ext void (target("msil.ref")): a function with a .NET reference in its signature that is not 'static' is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function hid target("msil.ref") (): a function with a .NET reference in its signature that ThinLTO or --lto-partitions made external (link with full LTO and --lto-partitions=1) is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function ext_decl void (target("msil.ref")): a function with a .NET reference in its signature that is not 'static' is not supported for MSIL yet{{$}}
define void @ext(target("msil.ref") %a) {
  ret void
}

define hidden target("msil.ref") @hid() {
  ret target("msil.ref") zeroinitializer
}

declare void @ext_decl(target("msil.ref"))

define i32 @main() {
  call void @ext(target("msil.ref") zeroinitializer)
  %h = call target("msil.ref") @hid()
  call void @ext_decl(target("msil.ref") %h)
  ret i32 0
}

;--- forwarder.ll
; CHECK: {{^}}error: <unknown>:0:0: in function fx_id target("msil.ref") (target("msil.ref")): a function with a .NET reference in its signature that is not 'static' is not supported for MSIL yet{{$}}
; STUB-NOT:   'fx_id'
; STUB-LABEL: .method assembly static object '__msil_mc.fx_id'(object) cil managed
; STUB:         ldarg.0
; STUB-NEXT:    call object [mscorlib]Ns.Ids::Identity(object)
; STUB-NEXT:    ret
; STUB-NOT:   'fx_id'
$fx_id = comdat any

define weak_odr dllexport target("msil.ref") @fx_id(target("msil.ref") %o) #1 comdat {
  %r = call target("msil.ref") @__msil_mc.fx_id(target("msil.ref") %o)
  ret target("msil.ref") %r
}

declare target("msil.ref") @__msil_mc.fx_id(target("msil.ref")) #0

define i32 @main() {
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="ref;ref" "msil-managed-member"="Identity" "msil-managed-sig"="object(object)" "msil-managed-type"="Ns.Ids" }
attributes #1 = { "msil-managed-forwarder"="fx_id" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.fx_id", !"Ns.Ids", !"Identity", !"object(object)", !"", !"ref;ref", !"", !"dllexport"}

;--- address.ll
; CHECK: {{^}}error: <unknown>:0:0: in function f void (target("msil.ref")): a function with a .NET reference in its signature whose address is taken is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function g target("msil.ref") (): a function with a .NET reference in its signature whose address is taken is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function h void (target("msil.ref"), i32): a function with a .NET reference in its signature whose address is taken is not supported for MSIL yet{{$}}
; AFTER:      {{^ *}}@tbl = internal global ptr poison{{$}}
; AFTER:      {{^ *}}declare {{.*}}void @f(target("msil.ref")){{$}}
; AFTER:      {{^ *}}declare {{.*}}target("msil.ref") @g(){{$}}
; AFTER:      {{^ *}}declare {{.*}}void @h(target("msil.ref"), i32){{$}}
; AFTER:      {{^ *}}define internal ptr @take() {
; AFTER-NEXT: {{^ *}}ret ptr poison{{$}}
; AFTER:      {{^ *}}define i32 @main() {
; AFTER-NOT:  call {{.*}}@{{[fgh]}}(
; AFTER:      {{^ *}}ret i32 0{{$}}
@tbl = internal global ptr @g
@.str = private unnamed_addr constant [4 x i8] c"ann\00", section "llvm.metadata"
@llvm.global.annotations = appending global [1 x { ptr, ptr, ptr, i32, ptr }] [{ ptr, ptr, ptr, i32, ptr } { ptr @h, ptr @.str, ptr null, i32 1, ptr null }], section "llvm.metadata"

define internal void @f(target("msil.ref") %a) {
  ret void
}

define internal target("msil.ref") @g() {
  ret target("msil.ref") zeroinitializer
}

define internal void @h(target("msil.ref") %a, i32 %n) {
  ret void
}

define internal ptr @take() {
  ret ptr @f
}

define i32 @main() {
  %p = call ptr @take()
  %t = load ptr, ptr @tbl, align 8
  call void @f(target("msil.ref") zeroinitializer)
  %r = call target("msil.ref") @g()
  call void @h(target("msil.ref") %r, i32 1)
  ret i32 0
}

;--- used.ll
@llvm.used = appending global [1 x ptr] [ptr @f], section "llvm.metadata"

define internal void @f(target("msil.ref") %a) {
  ret void
}

define i32 @main() {
  call void @f(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- compiler-used.ll
@llvm.compiler.used = appending global [1 x ptr] [ptr @f], section "llvm.metadata"

define internal void @f(target("msil.ref") %a) {
  ret void
}

define i32 @main() {
  call void @f(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- metadata-list.ll
; CHECK: {{^}}error: <unknown>:0:0: in function f void (target("msil.ref")): a function with a .NET reference in its signature whose address is taken is not supported for MSIL yet{{$}}
@list = appending global [1 x ptr] [ptr @f], section "llvm.metadata"

define internal void @f(target("msil.ref") %a) {
  ret void
}

define i32 @main() {
  call void @f(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- alias.ll
; CHECK: {{^}}error: <unknown>:0:0: in function f void (target("msil.ref")): a function with a .NET reference in its signature whose address is taken is not supported for MSIL yet{{$}}
; WP:    {{^}}error: <unknown>:0:0: in function al void (target("msil.ref")): a function with a .NET reference in its signature that is not 'static' is not supported for MSIL yet{{$}}
@al = alias void (target("msil.ref")), ptr @f
@other = alias void (), ptr @f

define internal void @f(target("msil.ref") %a) {
  ret void
}

define i32 @main() {
  call void @f(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- calls.ll
; CHECK: {{^}}error: <unknown>:0:0: in function f void (target("msil.ref")): a function with a .NET reference in its signature whose address is taken is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function callp void (ptr, target("msil.ref")): a function with a .NET reference in its signature called through a pointer or with another function type is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function callt void (target("msil.ref")): a function with a .NET reference in its signature called through a pointer or with another function type is not supported for MSIL yet{{$}}
define internal void @f(target("msil.ref") %a) {
  ret void
}

define internal void @callp(ptr %fp, target("msil.ref") %a) {
  call void %fp(target("msil.ref") %a)
  ret void
}

define internal void @callt(target("msil.ref") %a) {
  call void @f(target("msil.ref") %a, i32 1)
  ret void
}

define i32 @main() {
  call void @callp(ptr null, target("msil.ref") zeroinitializer)
  call void @callt(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- variadic.ll
; CHECK: {{^}}error: <unknown>:0:0: in function v void (target("msil.ref"), ...): a function with a .NET reference in its signature that is variadic is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function pass void (target("msil.ref")): a .NET reference as a variadic argument or as the type of 'va_arg' is not supported for MSIL{{$}}
; LNX:   {{^}}error: <unknown>:0:0: in function va target("msil.ref") (ptr): the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function va target("msil.ref") (ptr): a .NET reference as a variadic argument or as the type of 'va_arg' is not supported for MSIL{{$}}
define internal void @v(target("msil.ref") %a, ...) {
  ret void
}

define internal void @vi(i32 %n, ...) {
  ret void
}

define internal void @pass(target("msil.ref") %a) {
  call void (i32, ...) @vi(i32 1, target("msil.ref") %a)
  ret void
}

define internal target("msil.ref") @va(ptr %ap) {
  %r = va_arg ptr %ap, target("msil.ref")
  ret target("msil.ref") %r
}

define i32 @main() {
  call void (target("msil.ref"), ...) @v(target("msil.ref") zeroinitializer)
  call void @pass(target("msil.ref") zeroinitializer)
  %x = alloca ptr, align 8
  %r = call target("msil.ref") @va(ptr %x)
  ret i32 0
}

;--- setjmp.ll
; CHECK: {{^}}error: <unknown>:0:0: in function jumper i32 (target("msil.ref")): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
declare i32 @setjmp(ptr) returns_twice

define internal i32 @jumper(target("msil.ref") %a) {
  %buf = alloca [64 x i8], align 16
  %r = call i32 @setjmp(ptr %buf)
  ret i32 %r
}

define i32 @main() {
  %r = call i32 @jumper(target("msil.ref") zeroinitializer)
  ret i32 %r
}

;--- setjmp-names.ll
; CHECK: {{^}}error: <unknown>:0:0: in function jumper i32 (target("msil.ref")): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function jumper3 i32 (target("msil.ref")): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
declare i32 @setjmp(ptr)

declare i32 @_setjmp3(ptr, i32, ...)

define internal i32 @jumper(target("msil.ref") %a) {
  %buf = alloca [64 x i8], align 16
  %r = call i32 @setjmp(ptr %buf)
  ret i32 %r
}

define internal i32 @jumper3(target("msil.ref") %a) {
  %buf = alloca [64 x i8], align 16
  %r = call i32 (ptr, i32, ...) @_setjmp3(ptr %buf, i32 0)
  ret i32 %r
}

define i32 @main() {
  %r = call i32 @jumper(target("msil.ref") zeroinitializer)
  %s = call i32 @jumper3(target("msil.ref") zeroinitializer)
  %t = add i32 %r, %s
  ret i32 %t
}

;--- setjmp-knr.ll
; CHECK: {{^}}error: <unknown>:0:0: in function jumper_knr i32 (target("msil.ref")): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function jumper_invoke i32 (target("msil.ref")): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
declare i32 @_setjmp3(...) #0

declare i32 @_setjmp(ptr)

declare i32 @__gxx_personality_v0(...)

define internal i32 @jumper_knr(target("msil.ref") %a) {
  %buf = alloca [64 x i8], align 16
  %r = call i32 @_setjmp3(ptr %buf, i32 0)
  ret i32 %r
}

define internal i32 @jumper_invoke(target("msil.ref") %a) personality ptr @__gxx_personality_v0 {
  %buf = alloca [64 x i8], align 16
  %r = invoke i32 @_setjmp(ptr %buf) to label %ok unwind label %lp
ok:
  ret i32 %r
lp:
  %l = landingpad { ptr, i32 } cleanup
  ret i32 1
}

define i32 @main() {
  %r = call i32 @jumper_knr(target("msil.ref") zeroinitializer)
  %s = call i32 @jumper_invoke(target("msil.ref") zeroinitializer)
  %t = add i32 %r, %s
  ret i32 %t
}

attributes #0 = { "no-prototype" }

;--- setjmp-uses.ll
; CHECK: {{^}}error: <unknown>:0:0: in function result_twice i32 (): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function result_named i32 (): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function gives target("msil.ref") (): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function operand i32 (): a .NET reference in a function that calls setjmp is not supported for MSIL yet{{$}}
declare i32 @setjmp(ptr) returns_twice

declare i32 @_setjmp3(ptr, i32, ...)

define internal target("msil.ref") @make() {
  ret target("msil.ref") zeroinitializer
}

define internal void @take(target("msil.ref") %a) {
  ret void
}

define internal i32 @result_twice() {
  %buf = alloca [64 x i8], align 16
  %unused = call target("msil.ref") @make()
  %r = call i32 @setjmp(ptr %buf)
  ret i32 %r
}

define internal i32 @result_named() {
  %buf = alloca [64 x i8], align 16
  %unused = call target("msil.ref") @make()
  %r = call i32 (ptr, i32, ...) @_setjmp3(ptr %buf, i32 0)
  ret i32 %r
}

define internal target("msil.ref") @gives() {
  %buf = alloca [64 x i8], align 16
  %r = call i32 @setjmp(ptr %buf)
  ret target("msil.ref") zeroinitializer
}

define internal i32 @operand() {
  %buf = alloca [64 x i8], align 16
  call void @take(target("msil.ref") zeroinitializer)
  %r = call i32 @setjmp(ptr %buf)
  ret i32 %r
}

define i32 @main() {
  %a = call i32 @result_twice()
  %b = call i32 @result_named()
  %g = call target("msil.ref") @gives()
  %c = call i32 @operand()
  %s = add i32 %a, %b
  %t = add i32 %s, %c
  ret i32 %t
}

;--- reserved.ll
; CHECK: {{^}}error: <unknown>:0:0: in function __msil_ref.eq i1 (target("msil.ref"), target("msil.ref")): '__msil_ref.eq' is reserved for the MSIL toolchain{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function __msil_ref.other i32 (i32): '__msil_ref.other' is reserved for the MSIL toolchain{{$}}
define internal i1 @"__msil_ref.eq"(target("msil.ref") %a, target("msil.ref") %b) {
  ret i1 false
}

declare i32 @"__msil_ref.other"(i32)

define i32 @main() {
  %e = call i1 @"__msil_ref.eq"(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  %o = call i32 @"__msil_ref.other"(i32 1)
  ret i32 %o
}

;--- reserved-type.ll
; CHECK: {{^}}error: <unknown>:0:0: in function __msil_ref.eq i32 (target("msil.ref"), target("msil.ref")): '__msil_ref.eq' is reserved for the MSIL toolchain{{$}}
declare i32 @"__msil_ref.eq"(target("msil.ref"), target("msil.ref"))

define i32 @main() {
  %e = call i32 @"__msil_ref.eq"(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
  ret i32 %e
}

;--- global.ll
; CHECK: {{^}}error: global variable @g: a .NET reference in a global variable is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: global variable @ga: a .NET reference in a global variable is not supported for MSIL yet{{$}}
@g = internal global target("msil.ref") zeroinitializer
@ga = internal global [2 x target("msil.ref")] zeroinitializer

define i32 @main() {
  ret i32 0
}

;--- asm.ll
; CHECK: {{^}}error: <unknown>:0:0: in function asm_in void (target("msil.ref")): a .NET reference in an inline asm operand is not supported for MSIL yet{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function asm_out target("msil.ref") (): a .NET reference in an inline asm operand is not supported for MSIL yet{{$}}
define internal void @asm_in(target("msil.ref") %a) {
  call void asm sideeffect "", "r"(target("msil.ref") %a)
  ret void
}

define internal target("msil.ref") @asm_out() {
  %r = call target("msil.ref") asm sideeffect "", "=r"()
  ret target("msil.ref") %r
}

define i32 @main() {
  call void @asm_in(target("msil.ref") zeroinitializer)
  %r = call target("msil.ref") @asm_out()
  ret i32 0
}

;--- volatile.ll
; CHECK: {{^}}error: <unknown>:0:0: in function vstore target("msil.ref") (target("msil.ref")): a volatile access of a .NET reference is not supported for MSIL{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function vload target("msil.ref") (target("msil.ref")): a volatile access of a .NET reference is not supported for MSIL{{$}}
define internal target("msil.ref") @vstore(target("msil.ref") %a) {
  %s = alloca target("msil.ref"), align 8
  store volatile target("msil.ref") %a, ptr %s, align 8
  %v = load target("msil.ref"), ptr %s, align 8
  ret target("msil.ref") %v
}

define internal target("msil.ref") @vload(target("msil.ref") %a) {
  %s = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %s, align 8
  %v = load volatile target("msil.ref"), ptr %s, align 8
  ret target("msil.ref") %v
}

define i32 @main() {
  %v = call target("msil.ref") @vstore(target("msil.ref") zeroinitializer)
  %w = call target("msil.ref") @vload(target("msil.ref") %v)
  ret i32 0
}

;--- atomic.ll
; CHECK: {{^}}error: <unknown>:0:0: in function atom target("msil.ref") (target("msil.ref")): an atomic access of a .NET reference is not supported for MSIL{{$}}
define internal target("msil.ref") @atom(target("msil.ref") %a) {
  %s = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %s, align 8
  %v = load atomic target("msil.ref"), ptr %s seq_cst, align 8
  ret target("msil.ref") %v
}

define i32 @main() {
  %a = call target("msil.ref") @atom(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- intrinsic.ll
; CHECK: {{^}}error: <unknown>:0:0: in function copyit target("msil.ref") (target("msil.ref")): a .NET reference as an operand or result of 'llvm.ssa.copy' is not supported for MSIL{{$}}
declare target("msil.ref") @llvm.ssa.copy.tmsil.reft(target("msil.ref") returned)

define internal target("msil.ref") @copyit(target("msil.ref") %a) {
  %c = call target("msil.ref") @llvm.ssa.copy.tmsil.reft(target("msil.ref") %a)
  ret target("msil.ref") %c
}

define i32 @main() {
  %c = call target("msil.ref") @copyit(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- bundle.ll
; CHECK: {{^}}error: <unknown>:0:0: in function custom void (target("msil.ref")): a .NET reference as an operand or result of 'call' is not supported for MSIL{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function transition void (target("msil.ref")): a .NET reference as an operand or result of 'call' is not supported for MSIL{{$}}
define internal void @g() {
  ret void
}

define internal void @custom(target("msil.ref") %a) {
  call void @g() [ "foo"(target("msil.ref") %a) ]
  ret void
}

define internal void @transition(target("msil.ref") %a) {
  call void @g() [ "gc-transition"(target("msil.ref") %a) ]
  ret void
}

define i32 @main() {
  call void @custom(target("msil.ref") zeroinitializer)
  call void @transition(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- fakeuse.ll
; CHECK-LABEL: .method assembly static void 'fu'(object) cil managed
; CHECK:         ret
; CHECK-LABEL: .method public static int32 'main'() cil managed
; CHECK:         call void 'fu'(object)
define internal void @fu(target("msil.ref") %a) {
  call void (...) @llvm.fake.use(target("msil.ref") %a)
  ret void
}

define i32 @main() {
  call void @fu(target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- bitcast-calls.ll
; CHECK: {{^}}error: <unknown>:0:0: in function caller void (ptr, target("msil.ref")): call of 'takes_ref' passes 'void' where it takes 'target("msil.ref")': a .NET reference cannot be converted{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function caller void (ptr, target("msil.ref")): call of 'takes_ref' passes 'ptr' where it takes 'target("msil.ref")': a .NET reference cannot be converted{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function caller void (ptr, target("msil.ref")): call of 'gives_ref' passes 'target("msil.ref")' where it takes 'ptr': a .NET reference cannot be converted{{$}}
; CHECK: {{^}}error: <unknown>:0:0: in function caller void (ptr, target("msil.ref")): call of 'takes_two' passes 'void (target("msil.ref"), i32)' where it takes 'void (target("msil.ref"), i64)': a .NET reference cannot be converted{{$}}
define internal void @takes_ref(target("msil.ref") %a) {
  ret void
}

define internal target("msil.ref") @gives_ref() {
  ret target("msil.ref") zeroinitializer
}

define internal void @takes_two(target("msil.ref") %a, i64 %n) {
  ret void
}

define internal void @caller(ptr %p, target("msil.ref") %r) {
  call void @takes_ref(ptr %p)
  %x = call ptr @gives_ref()
  call void @takes_ref()
  call void @takes_two(target("msil.ref") %r, i32 1)
  ret void
}

define i32 @main() {
  call void @caller(ptr null, target("msil.ref") zeroinitializer)
  ret i32 0
}

;--- isel.ll
; CHECK: {{^}}error: <unknown>:0:0: in function mem target("msil.ref") (target("msil.ref")): a .NET reference is loaded from or stored to memory{{$}}
define internal target("msil.ref") @mem(target("msil.ref") %a) {
  %s = alloca target("msil.ref"), align 8
  store target("msil.ref") %a, ptr %s, align 8
  %v = load target("msil.ref"), ptr %s, align 8
  ret target("msil.ref") %v
}

define i32 @main() {
  %r = call target("msil.ref") @mem(target("msil.ref") zeroinitializer)
  ret i32 0
}
