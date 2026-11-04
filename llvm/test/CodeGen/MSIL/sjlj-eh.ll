; IL exception clauses for setjmp (ImplementationPlan.md 5.15.4 'IL shapes' and
; 'EH clauses in the backend', M27.a; recon p3/sjlj.md 4.2 and 4.3). The
; AsmPrinter gives every function with "msil-sjlj-invoke"="<i>" (a wrapper
; that MSILLowerSjLj makes, i the IR index of its native int status
; parameter) a whole-body '.try' with the fixed filter, and writes the body of
; the function with "msil-sjlj-throw" (IR body 'unreachable') itself:
; - the scoped text form, labels '@EH<f>_<k>' (f the function number of
;   '@BB<f>_<b>', k 0 TryBegin, 1 FilterBegin, 2-4 inside the filter, 5
;   HandlerBegin, 6 Done), '[core]' = '[mscorlib]' in whole-program text and
;   '[msil.bcl]' in object-form text, the status argument in its usual ldarg
;   form (ldarg.1, ldarg.2, 'ldarg.s 4');
; - every RETURN of the body becomes '[stloc R] leave @EH<f>_6' (the long form,
;   also in a body with two returns), and Done is '[ldloc R] ret'; R is an
;   extra local of the IL return type after the function's own locals (local 0
;   without other locals, local 1 after a byval copy's frame pointer or after
;   the -O0 local of multi_return);
; - .maxstack is at least 3 (the filter needs 3), and stays 4 for a body that
;   needs 4; a byval callee keeps its frame setup (localloc) inside the
;   '.try', an sret callee and an indirect wrapper (callee pointer first,
;   calli) get the same clause;
; - '__msil_sjlj_throw': '.maxstack 3', 'ldnull', 'ldarg.0', 'conv.i4', 'newobj
;   instance void [core]System.Runtime.InteropServices.ExternalException::.ctor(
;   string, int32)', 'throw';
; - no other method gets a clause ('.try', 'endfilter' and 'ExternalException'
;   appear nowhere else).
; wrappers.ll is hand-written IR with both attributes (the shape MSILLowerSjLj
; makes, sj 3.3), at -O0 and -O2 on both execution triples, in whole-program
; and object-form text. Under %if ilasm the whole-program text assembles, and
; the object-form text passes the object oracle of object-form-text.ll (5.12.6:
; ilasm's object and -filetype=obj's have the same canonical listing and link
; info, so the AsmPrinter-written instructions take the object token order of
; rule R6). llvm-cil-dump shows each method's one clause, 'try L0 to L<a>
; filter L<a> handler L<b> to L<c>': the try runs from the first instruction to
; the filter, the handler from the 'pop' after 'endfilter' to Done, which is
; also the target of every 'leave'. e2e.ll goes through MSILLowerSjLj: a
; setjmp caller, its wrapper (with the clause), '__msil_longjmp' and
; '__msil_sjlj_throw', and a function without a setjmp call that keeps its IL
; (no clause).
; branches.ll is a whole-body function whose compare chain branches forward
; over 18 return blocks, each '[ldc] stloc.0 leave' (7 or 8 bytes): the last
; two 'beq's span 141 bytes, so they are long only when branch selection
; counts every RETURN with its 'stloc R' and long 'leave' (at 1 byte per
; RETURN, or without the stloc byte, they would fit 127). The text keeps the
; long 'beq'/'brfalse' forms at -O2 on both execution triples, the
; whole-program text assembles, and the object-form text passes the object
; oracle.

; RUN: rm -rf %t && split-file %s %t

; DEFINE: %{nots} = --implicit-check-not=.try --implicit-check-not=endfilter \
; DEFINE:   --implicit-check-not=ExternalException --implicit-check-not="LLVM ERROR"
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{p} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/wrappers.ll \
; DEFINE:   -o %t/wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,%{p} -DCORE=mscorlib --input-file=%t/wp.%{t}%{o}.il %{nots} && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs \
; DEFINE:   < %t/wrappers.ll -o %t/of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,%{p} -DCORE=msil.bcl --input-file=%t/of.%{t}%{o}.il %{nots}

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; REDEFINE: %{p} = O2
; RUN: %{il}
; REDEFINE: %{o} = -O0
; REDEFINE: %{p} = O0
; RUN: %{il}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{il}
; REDEFINE: %{o} = -O2
; REDEFINE: %{p} = O2
; RUN: %{il}

; Whole-program text through ilasm.
; RUN: %if ilasm %{ %ilasm-verify -output=%t/wp.lnx.o2.dll %t/wp.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/wp.win.o0.dll %t/wp.x86_64-pc-windows-msvc-msil-O0.il %}

; Objects: the clause tables, and the object oracle on the object-form text.
; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/wrappers.ll \
; DEFINE:   -o %t/w.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl %t/w.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=DUMP --implicit-check-not=".try L" --implicit-check-not=endfilter
; DEFINE: %{oracle} = %ilasm-verify -output=%t/of.%{t}%{o}.ilasm.o %t/of.%{t}%{o}.il && \
; DEFINE:   %{canon} %t/w.%{t}%{o}.o > %t/w.%{t}%{o}.canon1 && \
; DEFINE:   %{canon} %t/of.%{t}%{o}.ilasm.o > %t/w.%{t}%{o}.canon2 && \
; DEFINE:   diff %t/w.%{t}%{o}.canon1 %t/w.%{t}%{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t/w.%{t}%{o}.o > %t/w.%{t}%{o}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t/of.%{t}%{o}.ilasm.o > %t/w.%{t}%{o}.link2 && \
; DEFINE:   diff %t/w.%{t}%{o}.link1 %t/w.%{t}%{o}.link2
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}

; Through MSILLowerSjLj: whole-program text, ilasm, and the object's clause.
; DEFINE: %{e2e} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/e2e.ll \
; DEFINE:   -o %t/e2e.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=E2E -DCORE=mscorlib --input-file=%t/e2e.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=.try --implicit-check-not=endfilter && \
; DEFINE:   FileCheck %s --check-prefix=E2E-LJ --input-file=%t/e2e.%{t}%{o}.il && \
; DEFINE:   FileCheck %s --check-prefix=E2E-T --input-file=%t/e2e.%{t}%{o}.il
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{e2e}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/e2e.lnx.dll %t/e2e.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=%{t} -filetype=obj -verify-machineinstrs < %t/e2e.ll -o %t/e2e.lnx.o \
; RUN:   2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t/e2e.lnx.o \
; RUN:   | FileCheck %s --check-prefix=E2E-DUMP --implicit-check-not=".try L" %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{e2e}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/e2e.win.dll %t/e2e.x86_64-pc-windows-msvc-msil-O0.il %}

; Branch forms over the return-as-leave tails.
; DEFINE: %{bf} = llc -mtriple=%{t} -O2 -asm-verbose=false -verify-machineinstrs < %t/branches.ll \
; DEFINE:   -o %t/bf.%{t}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=BF --input-file=%t/bf.%{t}.il && \
; DEFINE:   llc -mtriple=%{t} -O2 -msil-object-form -asm-verbose=false -verify-machineinstrs \
; DEFINE:   < %t/branches.ll -o %t/bfo.%{t}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=BF --input-file=%t/bfo.%{t}.il
; DEFINE: %{bfobj} = llc -mtriple=%{t} -O2 -filetype=obj -verify-machineinstrs < %t/branches.ll \
; DEFINE:   -o %t/bf.%{t}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl %t/bf.%{t}.o | FileCheck %s --check-prefix=BF-DUMP
; DEFINE: %{bforacle} = %ilasm-verify -output=%t/bfo.%{t}.ilasm.o %t/bfo.%{t}.il && \
; DEFINE:   %{canon} %t/bf.%{t}.o > %t/bf.%{t}.canon1 && \
; DEFINE:   %{canon} %t/bfo.%{t}.ilasm.o > %t/bf.%{t}.canon2 && \
; DEFINE:   diff %t/bf.%{t}.canon1 %t/bf.%{t}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t/bf.%{t}.o > %t/bf.%{t}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t/bfo.%{t}.ilasm.o > %t/bf.%{t}.link2 && \
; DEFINE:   diff %t/bf.%{t}.link1 %t/bf.%{t}.link2
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; RUN: %{bf}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/bf.lnx.dll %t/bf.x86_64-unknown-linux-gnu-msil.il %}
; RUN: %if llvm-cil-dump %{ %{bfobj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{bforacle} %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{bf}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/bf.win.dll %t/bf.x86_64-pc-windows-msvc-msil.il %}
; RUN: %if llvm-cil-dump %{ %{bfobj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{bforacle} %}

; Whole-program and object-form text of each wrapper.
; CHECK-LABEL: .method assembly static void '__msil_sjlj_invoke.0'(int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^  \.try \{$}}
; CHECK-NEXT:  {{^}}@EH[[F:[0-9]+]]_0:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'cv'(int32){{$}}
; CHECK-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \} filter \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  isinst {{\[}}[[CORE]]]System.Runtime.InteropServices.ExternalException{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @EH[[F]]_2{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_2:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Exception::get_HResult(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @EH[[F]]_3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_3:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_4:{{$}}
; CHECK-NEXT:  {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_5:{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  leave.s @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_6:{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_sjlj_invoke.0'{{$}}

; CHECK-LABEL: .method assembly static int32 '__msil_sjlj_invoke.1'(native int, int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^  \.try \{$}}
; CHECK-NEXT:  {{^}}@EH[[F:[0-9]+]]_0:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'ci'(native int, int32){{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \} filter \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  isinst {{\[}}[[CORE]]]System.Runtime.InteropServices.ExternalException{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @EH[[F]]_2{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_2:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Exception::get_HResult(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @EH[[F]]_3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_3:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_4:{{$}}
; CHECK-NEXT:  {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_5:{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  leave.s @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_6:{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_sjlj_invoke.1'{{$}}

; CHECK-LABEL: .method assembly static int32 '__msil_sjlj_invoke.2'(native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int, int32){{$}}
; CHECK-NEXT:  {{^  \.try \{$}}
; CHECK-NEXT:  {{^}}@EH[[F:[0-9]+]]_0:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'cbv'(native int){{$}}
; CHECK-NEXT:  {{^}}  stloc.1{{$}}
; CHECK-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \} filter \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  isinst {{\[}}[[CORE]]]System.Runtime.InteropServices.ExternalException{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @EH[[F]]_2{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_2:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Exception::get_HResult(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @EH[[F]]_3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_3:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_4:{{$}}
; CHECK-NEXT:  {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_5:{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  leave.s @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_6:{{$}}
; CHECK-NEXT:  {{^}}  ldloc.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_sjlj_invoke.2'{{$}}

; CHECK-LABEL: .method assembly static void '__msil_sjlj_invoke.3'(native int, int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^  \.try \{$}}
; CHECK-NEXT:  {{^}}@EH[[F:[0-9]+]]_0:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call void 'csr'(native int, int32){{$}}
; CHECK-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \} filter \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  isinst {{\[}}[[CORE]]]System.Runtime.InteropServices.ExternalException{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @EH[[F]]_2{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_2:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Exception::get_HResult(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @EH[[F]]_3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_3:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_4:{{$}}
; CHECK-NEXT:  {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_5:{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  leave.s @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_6:{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_sjlj_invoke.3'{{$}}

; CHECK-LABEL: .method assembly static float64 '__msil_sjlj_invoke.4'(int32, int32, int32, int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  .locals (float64){{$}}
; CHECK-NEXT:  {{^  \.try \{$}}
; CHECK-NEXT:  {{^}}@EH[[F:[0-9]+]]_0:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  call float64 'c4'(int32, int32, int32, int32){{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \} filter \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  isinst {{\[}}[[CORE]]]System.Runtime.InteropServices.ExternalException{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @EH[[F]]_2{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_2:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Exception::get_HResult(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @EH[[F]]_3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_3:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_4:{{$}}
; CHECK-NEXT:  {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_5:{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  leave.s @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_6:{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_sjlj_invoke.4'{{$}}

; CHECK-LABEL: .method assembly static int32 '__msil_sjlj_invoke_ind.0'(native int, int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^  \.try \{$}}
; CHECK-NEXT:  {{^}}@EH[[F:[0-9]+]]_0:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32){{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \} filter \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  isinst {{\[}}[[CORE]]]System.Runtime.InteropServices.ExternalException{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @EH[[F]]_2{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_2:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Exception::get_HResult(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @EH[[F]]_3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_3:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_4:{{$}}
; CHECK-NEXT:  {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_5:{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  leave.s @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_6:{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_sjlj_invoke_ind.0'{{$}}

; CHECK-LABEL: .method assembly static int32 'multi_return'(int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  .locals (int32, int32){{$}}
; CHECK-NEXT:  {{^  \.try \{$}}
; CHECK-NEXT:  {{^}}@EH[[F:[0-9]+]]_0:{{$}}
; CHECK-NOT:   {{^  ret$}}
; O2:          {{^}}  stloc.0{{$}}
; O0:          {{^}}  stloc.1{{$}}
; CHECK-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; CHECK-NOT:   {{^  ret$}}
; O2:          {{^}}  stloc.0{{$}}
; O0:          {{^}}  stloc.1{{$}}
; CHECK-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; CHECK-NOT:   {{^  (ret|leave)}}
; CHECK:       {{^  \} filter \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  isinst {{\[}}[[CORE]]]System.Runtime.InteropServices.ExternalException{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @EH[[F]]_2{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_2:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Exception::get_HResult(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @EH[[F]]_3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  br.s @EH[[F]]_4{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_3:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_4:{{$}}
; CHECK-NEXT:  {{^}}  endfilter{{$}}
; CHECK-NEXT:  {{^  \} \{$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_5:{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  leave.s @EH[[F]]_6{{$}}
; CHECK-NEXT:  {{^  \}$}}
; CHECK-NEXT:  {{^}}@EH[[F]]_6:{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'multi_return'{{$}}

; The throw helper's body is written by the AsmPrinter.
; CHECK-LABEL: .method {{.*}} void '__msil_sjlj_throw'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.Runtime.InteropServices.ExternalException::.ctor(string, int32){{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_sjlj_throw'{{$}}

; The callers keep their IL: no clause, the usual calls.
; CHECK-LABEL: .method {{.*}} int32 'caller'(native int, int32) cil managed{{$}}
; CHECK:       {{^}}  call void '__msil_sjlj_invoke.0'(int32, native int){{$}}
; CHECK:       {{^}}  call int32 '__msil_sjlj_invoke_ind.0'(native int, int32, native int){{$}}
; CHECK:       {{^}}  call int32 'multi_return'(int32, native int){{$}}
; CHECK-LABEL: .method {{.*}} void 'thrower'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void '__msil_sjlj_throw'(native int){{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'thrower'{{$}}

; The clause table of each wrapper in the object, and the throw helper.
; DUMP-LABEL: {{^}}  .method '__msil_sjlj_invoke.0' void(int32, native int) flags=
; DUMP-NEXT:   {{^}}    .maxstack 3{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: leave L[[D:[0-9]+]]{{$}}
; DUMP-NEXT:   {{^}}    L[[FB:[0-9]+]]: isinst {{.*}}System.Runtime.InteropServices.ExternalException{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: callvirt {{.*}}System.Exception::'get_HResult'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:   {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: leave.s L[[D]]{{$}}
; DUMP-NEXT:   {{^}}    L[[D]]: ret{{$}}
; DUMP-NEXT:   {{^}}    .try L0 to L[[FB]] filter L[[FB]] handler L[[HB]] to L[[D]]{{$}}

; DUMP-LABEL: {{^}}  .method '__msil_sjlj_invoke.1' int32(native int, int32, native int) flags=
; DUMP-NEXT:   {{^}}    .maxstack 3{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: leave L[[D:[0-9]+]]{{$}}
; DUMP-NEXT:   {{^}}    L[[FB:[0-9]+]]: isinst {{.*}}System.Runtime.InteropServices.ExternalException{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: callvirt {{.*}}System.Exception::'get_HResult'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:   {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: leave.s L[[D]]{{$}}
; DUMP-NEXT:   {{^}}    L[[D]]: ldloc.{{[0-9]}}{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-NEXT:   {{^}}    .try L0 to L[[FB]] filter L[[FB]] handler L[[HB]] to L[[D]]{{$}}

; DUMP-LABEL: {{^}}  .method '__msil_sjlj_invoke.2' int32(native int, native int) flags=
; DUMP-NEXT:   {{^}}    .maxstack 3{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: leave L[[D:[0-9]+]]{{$}}
; DUMP-NEXT:   {{^}}    L[[FB:[0-9]+]]: isinst {{.*}}System.Runtime.InteropServices.ExternalException{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: callvirt {{.*}}System.Exception::'get_HResult'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:   {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: leave.s L[[D]]{{$}}
; DUMP-NEXT:   {{^}}    L[[D]]: ldloc.{{[0-9]}}{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-NEXT:   {{^}}    .try L0 to L[[FB]] filter L[[FB]] handler L[[HB]] to L[[D]]{{$}}

; DUMP-LABEL: {{^}}  .method '__msil_sjlj_invoke.3' void(native int, int32, native int) flags=
; DUMP-NEXT:   {{^}}    .maxstack 3{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: leave L[[D:[0-9]+]]{{$}}
; DUMP-NEXT:   {{^}}    L[[FB:[0-9]+]]: isinst {{.*}}System.Runtime.InteropServices.ExternalException{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: callvirt {{.*}}System.Exception::'get_HResult'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:   {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: leave.s L[[D]]{{$}}
; DUMP-NEXT:   {{^}}    L[[D]]: ret{{$}}
; DUMP-NEXT:   {{^}}    .try L0 to L[[FB]] filter L[[FB]] handler L[[HB]] to L[[D]]{{$}}

; DUMP-LABEL: {{^}}  .method '__msil_sjlj_invoke.4' float64(int32, int32, int32, int32, native int) flags=
; DUMP-NEXT:   {{^}}    .maxstack 4{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: leave L[[D:[0-9]+]]{{$}}
; DUMP-NEXT:   {{^}}    L[[FB:[0-9]+]]: isinst {{.*}}System.Runtime.InteropServices.ExternalException{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: callvirt {{.*}}System.Exception::'get_HResult'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:   {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: leave.s L[[D]]{{$}}
; DUMP-NEXT:   {{^}}    L[[D]]: ldloc.{{[0-9]}}{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-NEXT:   {{^}}    .try L0 to L[[FB]] filter L[[FB]] handler L[[HB]] to L[[D]]{{$}}

; DUMP-LABEL: {{^}}  .method '__msil_sjlj_invoke_ind.0' int32(native int, int32, native int) flags=
; DUMP-NEXT:   {{^}}    .maxstack 3{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: leave L[[D:[0-9]+]]{{$}}
; DUMP-NEXT:   {{^}}    L[[FB:[0-9]+]]: isinst {{.*}}System.Runtime.InteropServices.ExternalException{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: callvirt {{.*}}System.Exception::'get_HResult'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:   {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: leave.s L[[D]]{{$}}
; DUMP-NEXT:   {{^}}    L[[D]]: ldloc.{{[0-9]}}{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-NEXT:   {{^}}    .try L0 to L[[FB]] filter L[[FB]] handler L[[HB]] to L[[D]]{{$}}

; DUMP-LABEL: {{^}}  .method 'multi_return' int32(int32, native int) flags=
; DUMP-NEXT:   {{^}}    .maxstack 3{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: leave L[[D:[0-9]+]]{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: leave L[[D]]{{$}}
; DUMP-NEXT:   {{^}}    L[[FB:[0-9]+]]: isinst {{.*}}System.Runtime.InteropServices.ExternalException{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: callvirt {{.*}}System.Exception::'get_HResult'{{$}}
; DUMP:        {{^}}    L{{[0-9]+}}: endfilter{{$}}
; DUMP-NEXT:   {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: leave.s L[[D]]{{$}}
; DUMP-NEXT:   {{^}}    L[[D]]: ldloc.{{[0-9]}}{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: ret{{$}}
; DUMP-NEXT:   {{^}}    .try L0 to L[[FB]] filter L[[FB]] handler L[[HB]] to L[[D]]{{$}}

; DUMP-LABEL: {{^}}  .method '__msil_sjlj_throw' void(native int) flags=
; DUMP-NEXT:   {{^}}    .maxstack 3{{$}}
; DUMP-NEXT:   {{^}}    L0: ldnull{{$}}
; DUMP-NEXT:   {{^}}    L1: ldarg.0{{$}}
; DUMP-NEXT:   {{^}}    L2: conv.i4{{$}}
; DUMP-NEXT:   {{^}}    L3: newobj {{.*}}System.Runtime.InteropServices.ExternalException::'.ctor'{{$}}
; DUMP-NEXT:   {{^}}    L4: throw{{$}}
; DUMP-NEXT:   {{^}}  .method 'caller' int32(native int, int32) flags=

; Through the pass: the function without a setjmp call keeps its plain call
; of '__msil_longjmp'; the setjmp caller calls its wrapper, which carries the
; clause (its status argument is argument 2). The helpers come in any order
; after the functions of the module, so each has its own FileCheck run.
; E2E-LABEL:  .method {{.*}} void 'thrower'(native int, int32) cil managed{{$}}
; E2E-NEXT:   {{^\{$}}
; E2E-NEXT:   {{^}}  .maxstack 2{{$}}
; E2E-NEXT:   {{^}}  ldarg.0{{$}}
; E2E-NEXT:   {{^}}  ldarg.1{{$}}
; E2E-NEXT:   {{^}}  call void '__msil_longjmp'(native int, int32){{$}}
; E2E-NEXT:   {{^}}  ldnull{{$}}
; E2E-NEXT:   {{^}}  throw{{$}}
; E2E-NEXT:   {{^}}} // end of method 'thrower'{{$}}
; E2E-LABEL:  .method {{.*}} int32 'run'(native int) cil managed{{$}}
; E2E:        {{^}}  call void '__msil_sjlj_invoke.0'(native int, int32, native int){{$}}
; E2E-LABEL:  .method assembly static void '__msil_sjlj_invoke.0'(native int, int32, native int) cil managed{{$}}
; E2E-NEXT:   {{^\{$}}
; E2E-NEXT:   {{^}}  .maxstack 3{{$}}
; E2E-NEXT:   {{^  \.try \{$}}
; E2E-NEXT:   {{^}}@EH[[F:[0-9]+]]_0:{{$}}
; E2E-NEXT:   {{^}}  ldarg.0{{$}}
; E2E-NEXT:   {{^}}  ldarg.1{{$}}
; E2E-NEXT:   {{^}}  call void 'thrower'(native int, int32){{$}}
; E2E-NEXT:   {{^}}  leave @EH[[F]]_6{{$}}
; E2E-NEXT:   {{^  \} filter \{$}}
; E2E-NEXT:   {{^}}@EH[[F]]_1:{{$}}
; E2E-NEXT:   {{^}}  isinst {{\[}}[[CORE]]]System.Runtime.InteropServices.ExternalException{{$}}
; E2E-NEXT:   {{^}}  dup{{$}}
; E2E-NEXT:   {{^}}  brtrue.s @EH[[F]]_2{{$}}
; E2E-NEXT:   {{^}}  pop{{$}}
; E2E-NEXT:   {{^}}  ldc.i4.0{{$}}
; E2E-NEXT:   {{^}}  br.s @EH[[F]]_4{{$}}
; E2E-NEXT:   {{^}}@EH[[F]]_2:{{$}}
; E2E-NEXT:   {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Exception::get_HResult(){{$}}
; E2E-NEXT:   {{^}}  ldarg.2{{$}}
; E2E-NEXT:   {{^}}  conv.i4{{$}}
; E2E-NEXT:   {{^}}  bne.un.s @EH[[F]]_3{{$}}
; E2E-NEXT:   {{^}}  ldarg.2{{$}}
; E2E-NEXT:   {{^}}  ldind.i4{{$}}
; E2E-NEXT:   {{^}}  ldc.i4.0{{$}}
; E2E-NEXT:   {{^}}  cgt.un{{$}}
; E2E-NEXT:   {{^}}  br.s @EH[[F]]_4{{$}}
; E2E-NEXT:   {{^}}@EH[[F]]_3:{{$}}
; E2E-NEXT:   {{^}}  ldc.i4.0{{$}}
; E2E-NEXT:   {{^}}@EH[[F]]_4:{{$}}
; E2E-NEXT:   {{^}}  endfilter{{$}}
; E2E-NEXT:   {{^  \} \{$}}
; E2E-NEXT:   {{^}}@EH[[F]]_5:{{$}}
; E2E-NEXT:   {{^}}  pop{{$}}
; E2E-NEXT:   {{^}}  leave.s @EH[[F]]_6{{$}}
; E2E-NEXT:   {{^  \}$}}
; E2E-NEXT:   {{^}}@EH[[F]]_6:{{$}}
; E2E-NEXT:   {{^}}  ret{{$}}
; E2E-NEXT:   {{^}}} // end of method '__msil_sjlj_invoke.0'{{$}}

; E2E-LJ-LABEL: .method {{.*}} void '__msil_longjmp'(native int, int32) cil managed{{$}}
; E2E-LJ-NOT:   .try
; E2E-LJ:       {{^}}  ldc.i4 826954573{{$}}
; E2E-LJ:       {{^}}  call void '__msil_sjlj_throw'(native int){{$}}
; E2E-LJ:       {{^}}} // end of method '__msil_longjmp'{{$}}

; E2E-T-LABEL:  .method {{.*}} void '__msil_sjlj_throw'(native int) cil managed{{$}}
; E2E-T-NEXT:   {{^\{$}}
; E2E-T-NEXT:   {{^}}  .maxstack 3{{$}}
; E2E-T-NEXT:   {{^}}  ldnull{{$}}
; E2E-T-NEXT:   {{^}}  ldarg.0{{$}}
; E2E-T-NEXT:   {{^}}  conv.i4{{$}}
; E2E-T-NEXT:   {{^}}  newobj instance void [mscorlib]System.Runtime.InteropServices.ExternalException::.ctor(string, int32){{$}}
; E2E-T-NEXT:   {{^}}  throw{{$}}
; E2E-T-NEXT:   {{^}}} // end of method '__msil_sjlj_throw'{{$}}

; E2E-DUMP-LABEL: {{^}}  .method '__msil_sjlj_invoke.0' void(native int, int32, native int) flags=
; E2E-DUMP-NEXT:  {{^}}    .maxstack 3{{$}}
; E2E-DUMP:       {{^}}    L{{[0-9]+}}: leave L[[D:[0-9]+]]{{$}}
; E2E-DUMP-NEXT:  {{^}}    L[[FB:[0-9]+]]: isinst {{.*}}System.Runtime.InteropServices.ExternalException{{$}}
; E2E-DUMP:       {{^}}    L{{[0-9]+}}: endfilter{{$}}
; E2E-DUMP-NEXT:  {{^}}    L[[HB:[0-9]+]]: pop{{$}}
; E2E-DUMP-NEXT:  {{^}}    L{{[0-9]+}}: leave.s L[[D]]{{$}}
; E2E-DUMP-NEXT:  {{^}}    L[[D]]: ret{{$}}
; E2E-DUMP-NEXT:  {{^}}    .try L0 to L[[FB]] filter L[[FB]] handler L[[HB]] to L[[D]]{{$}}

; The branch forms (branches.ll): the compare chain's branches are all long,
; the last two over 141 bytes of return blocks and the fall-through and 'far'
; tails.
; BF-LABEL: .method assembly static int32 'branch_forms'(int32, int32, native int) cil managed{{$}}
; BF:       {{^  \.try \{$}}
; BF-NEXT:  {{^}}@EH[[F:[0-9]+]]_0:{{$}}
; BF-NEXT:  {{^}}  ldarg.1{{$}}
; BF-NEXT:  {{^}}  ldc.i4.s 42{{$}}
; BF-NEXT:  {{^}}  beq @BB[[F]]_{{[0-9]+}}{{$}}
; BF-NEXT:  {{^}}  ldarg.0{{$}}
; BF-NEXT:  {{^}}  brfalse @BB[[F]]_{{[0-9]+}}{{$}}
; BF-NOT:   {{\.s @BB}}
; BF:       {{^}}  ldc.i4.s 15{{$}}
; BF-NEXT:  {{^}}  beq @BB[[F]]_[[R15:[0-9]+]]{{$}}
; BF-NEXT:  {{^}}  ldarg.0{{$}}
; BF-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; BF-NEXT:  {{^}}  beq @BB[[F]]_[[R16:[0-9]+]]{{$}}
; BF-NEXT:  {{^}}  ldc.i4.s 77{{$}}
; BF-NEXT:  {{^}}  stloc.0{{$}}
; BF-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; BF-NOT:   {{\.s @BB}}
; BF:       {{^}}@BB[[F]]_[[R15]]:{{$}}
; BF-NEXT:  {{^}}  ldc.i4.s 15{{$}}
; BF-NEXT:  {{^}}  stloc.0{{$}}
; BF-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; BF-NEXT:  {{^}}@BB[[F]]_[[R16]]:{{$}}
; BF-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; BF-NEXT:  {{^}}  stloc.0{{$}}
; BF-NEXT:  {{^}}  leave @EH[[F]]_6{{$}}
; BF-NEXT:  {{^  \} filter \{$}}
; BF:       {{^}}@EH[[F]]_6:{{$}}
; BF-NEXT:  {{^}}  ldloc.0{{$}}
; BF-NEXT:  {{^}}  ret{{$}}
; BF-NEXT:  {{^}}} // end of method 'branch_forms'{{$}}

; BF-DUMP-LABEL: {{^}}  .method 'branch_forms' int32(int32, int32, native int) flags=
; BF-DUMP:       {{^}}    L{{[0-9]+}}: ldc.i4.s 16{{$}}
; BF-DUMP-NEXT:  {{^}}    L{{[0-9]+}}: beq L[[T:[0-9]+]]{{$}}
; BF-DUMP:       {{^}}    L[[T]]: ldc.i4.s 16{{$}}
; BF-DUMP:       {{^}}    .try L0 to L{{[0-9]+}} filter L{{[0-9]+}} handler L{{[0-9]+}} to L{{[0-9]+}}{{$}}

;--- wrappers.ll
source_filename = "sjlj-eh.c"

%struct.S = type { i32, i32, i64 }

$__msil_sjlj_throw = comdat any

define void @cv(i32 %a) {
  ret void
}

define i32 @ci(ptr %p, i32 %a) {
  ret i32 %a
}

define i32 @cbv(ptr byval(%struct.S) align 8 %s) {
  %v = load i32, ptr %s, align 8
  ret i32 %v
}

define void @csr(ptr sret(%struct.S) align 8 %r, i32 %a) {
  store i32 %a, ptr %r, align 8
  ret void
}

define double @c4(i32 %a, i32 %b, i32 %c, i32 %d) {
  %s = add i32 %a, %b
  %t = add i32 %c, %d
  %u = add i32 %s, %t
  %f = sitofp i32 %u to double
  ret double %f
}

; Direct wrappers: void, value-returning, byval and sret callees, and a
; status argument at index 4.
define internal void @__msil_sjlj_invoke.0(i32 %a, ptr %st) #0 {
  call void @cv(i32 %a)
  ret void
}

define internal i32 @__msil_sjlj_invoke.1(ptr %p, i32 %a, ptr %st) #1 {
  %r = call i32 @ci(ptr %p, i32 %a)
  ret i32 %r
}

define internal i32 @__msil_sjlj_invoke.2(ptr byval(%struct.S) align 8 %s, ptr %st) #0 {
  %r = call i32 @cbv(ptr byval(%struct.S) align 8 %s)
  ret i32 %r
}

define internal void @__msil_sjlj_invoke.3(ptr sret(%struct.S) align 8 %r, i32 %a, ptr %st) #1 {
  call void @csr(ptr sret(%struct.S) align 8 %r, i32 %a)
  ret void
}

define internal double @__msil_sjlj_invoke.4(i32 %a, i32 %b, i32 %c, i32 %d, ptr %st) #2 {
  %r = call double @c4(i32 %a, i32 %b, i32 %c, i32 %d)
  ret double %r
}

; An indirect wrapper: the callee pointer first.
define internal i32 @__msil_sjlj_invoke_ind.0(ptr %fn, i32 %a, ptr %st) #1 {
  %r = call i32 %fn(i32 %a)
  ret i32 %r
}

; Two returns (MSILLowerSjLj makes none, M32's whole-body clauses will).
define internal i32 @multi_return(i32 %c, ptr %st) #0 {
entry:
  %z = icmp eq i32 %c, 0
  br i1 %z, label %zero, label %other
zero:
  call void @cv(i32 1)
  ret i32 1
other:
  call void @cv(i32 %c)
  ret i32 2
}

define linkonce_odr hidden void @__msil_sjlj_throw(ptr %st) unnamed_addr #3 comdat {
  unreachable
}

define i32 @caller(ptr %fn, i32 %a) {
  %st = alloca { i32, i32 }, align 8
  %s = alloca %struct.S, align 8
  store i32 0, ptr %st, align 8
  call void @__msil_sjlj_invoke.0(i32 %a, ptr %st)
  %r1 = call i32 @__msil_sjlj_invoke.1(ptr %s, i32 %a, ptr %st)
  %r2 = call i32 @__msil_sjlj_invoke.2(ptr byval(%struct.S) align 8 %s, ptr %st)
  call void @__msil_sjlj_invoke.3(ptr sret(%struct.S) align 8 %s, i32 %a, ptr %st)
  %d = call double @__msil_sjlj_invoke.4(i32 1, i32 2, i32 3, i32 %a, ptr %st)
  %r3 = call i32 @__msil_sjlj_invoke_ind.0(ptr %fn, i32 %a, ptr %st)
  %r4 = call i32 @multi_return(i32 %a, ptr %st)
  %x1 = add i32 %r1, %r2
  %x2 = add i32 %r3, %r4
  %x3 = fptosi double %d to i32
  %x4 = add i32 %x1, %x2
  %x5 = add i32 %x4, %x3
  ret i32 %x5
}

define void @thrower(ptr %st) {
  call void @__msil_sjlj_throw(ptr %st)
  unreachable
}

attributes #0 = { noinline "msil-sjlj-invoke"="1" }
attributes #1 = { noinline "msil-sjlj-invoke"="2" }
attributes #2 = { noinline "msil-sjlj-invoke"="4" }
attributes #3 = { noreturn nounwind "msil-sjlj-throw" }

;--- e2e.ll
source_filename = "sjlj-e2e.c"

declare i32 @_setjmp(ptr) #0
declare void @longjmp(ptr, i32) #1

define void @thrower(ptr %b, i32 %v) {
  call void @longjmp(ptr %b, i32 %v) #3
  unreachable
}

define i32 @run(ptr %b) {
entry:
  %r = call i32 @_setjmp(ptr %b) #2
  %c = icmp eq i32 %r, 0
  br i1 %c, label %go, label %out
go:
  call void @thrower(ptr %b, i32 3)
  br label %out
out:
  %v = phi i32 [ %r, %entry ], [ 0, %go ]
  ret i32 %v
}

attributes #0 = { nounwind returns_twice }
attributes #1 = { noreturn nounwind }
attributes #2 = { returns_twice }
attributes #3 = { noreturn }

;--- branches.ll
source_filename = "sjlj-branches.c"

define void @bext(i32 %v) {
  ret void
}

; 17 compares of %x, each with its own return block, after a test of %y
; that branches to the last block.
define internal i32 @branch_forms(i32 %x, i32 %y, ptr %st) #0 {
entry:
  %e = icmp eq i32 %y, 42
  br i1 %e, label %far, label %t0
t0:
  %c0 = icmp eq i32 %x, 0
  br i1 %c0, label %r0, label %t1
r0:
  ret i32 0
t1:
  %c1 = icmp eq i32 %x, 1
  br i1 %c1, label %r1, label %t2
r1:
  ret i32 1
t2:
  %c2 = icmp eq i32 %x, 2
  br i1 %c2, label %r2, label %t3
r2:
  ret i32 2
t3:
  %c3 = icmp eq i32 %x, 3
  br i1 %c3, label %r3, label %t4
r3:
  ret i32 3
t4:
  %c4 = icmp eq i32 %x, 4
  br i1 %c4, label %r4, label %t5
r4:
  ret i32 4
t5:
  %c5 = icmp eq i32 %x, 5
  br i1 %c5, label %r5, label %t6
r5:
  ret i32 5
t6:
  %c6 = icmp eq i32 %x, 6
  br i1 %c6, label %r6, label %t7
r6:
  ret i32 6
t7:
  %c7 = icmp eq i32 %x, 7
  br i1 %c7, label %r7, label %t8
r7:
  ret i32 7
t8:
  %c8 = icmp eq i32 %x, 8
  br i1 %c8, label %r8, label %t9
r8:
  ret i32 8
t9:
  %c9 = icmp eq i32 %x, 9
  br i1 %c9, label %r9, label %t10
r9:
  ret i32 9
t10:
  %c10 = icmp eq i32 %x, 10
  br i1 %c10, label %r10, label %t11
r10:
  ret i32 10
t11:
  %c11 = icmp eq i32 %x, 11
  br i1 %c11, label %r11, label %t12
r11:
  ret i32 11
t12:
  %c12 = icmp eq i32 %x, 12
  br i1 %c12, label %r12, label %t13
r12:
  ret i32 12
t13:
  %c13 = icmp eq i32 %x, 13
  br i1 %c13, label %r13, label %t14
r13:
  ret i32 13
t14:
  %c14 = icmp eq i32 %x, 14
  br i1 %c14, label %r14, label %t15
r14:
  ret i32 14
t15:
  %c15 = icmp eq i32 %x, 15
  br i1 %c15, label %r15, label %t16
r15:
  ret i32 15
t16:
  %c16 = icmp eq i32 %x, 16
  br i1 %c16, label %r16, label %t17
r16:
  ret i32 16
t17:
  ret i32 77
far:
  call void @bext(i32 %y)
  ret i32 99
}

define i32 @bf_caller(i32 %x, i32 %y) {
  %st = alloca { i32, i32 }, align 8
  store i32 0, ptr %st, align 8
  %r = call i32 @branch_forms(i32 %x, i32 %y, ptr %st)
  ret i32 %r
}

attributes #0 = { noinline "msil-sjlj-invoke"="2" }
