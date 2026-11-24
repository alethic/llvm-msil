; memset with a length known only at run time (ImplementationPlan.md FX-2;
; decision D15 as section 12's row 'D15 amended (FX-2)' amends it): the length
; is never cut to 32 bits. A native int length that the compiler cannot prove
; below 2^32 is tested by a guard; when it is not above 2^32-1, one initblk
; takes its low 32 bits as before; otherwise a loop fills blocks of 2^31 bytes
; (2 GiB) while the rest does not fit 32 bits, and then one initblk fills the
; rest, as a constant length is split. In the CIL:
;   guard: the length, 'ldc.i8 4294967295; conv.i', 'cgt.un', 'brfalse' to
;          the tail (the loop follows the guard)
;   loop:  initblk of 'ldc.i4 -2147483648' bytes (2^31 as an int32) at the
;          current address; the address plus and the length minus
;          'ldc.i8 2147483648; conv.i' ('add', 'sub'); 'cgt.un' of the rest
;          against 2^32-1 and 'brtrue' back to the loop
;   tail:  initblk of the rest through 'conv.i4', then the code after the memset
; Both initblks carry the memset's prefixes ('unaligned. N' for an alignment of
; 1, 2 or 4, then 'volatile.' for a volatile memset; plan 5.1 and D24). An
; int32 length (set_i32), a zext of an int32 (set_zext) and a length masked
; with 0xFFFFFFFF (set_mask) are below 2^32 and keep the single initblk with no
; guard; constant lengths keep their split (set_c1000, set_c4g5, as in
; memintrinsics.ll). The fill byte may be an argument (set_fill), the
; destination a stack array, that is a frame index (set_stack), and the length
; the address of a stack object (set_len_frame); two memsets in one block each
; get their own guard, loop and tail (set_two), and a memset inside a loop
; (set_in_loop) leaves the outer loop's branch after its tail.
;
; The hidden option -msil-memset-guard-bits=<N> (default 32) moves the guard to
; bits N and up and the block to 2^(N-1) bytes. With N = 4 the guard compares
; with 15 ('ldc.i4.s 15; conv.i') and the blocks are 8 bytes ('ldc.i4.8',
; 'ldc.i4.8; conv.i'); an int32 length, the zext and the masked length now
; have high bits too, so they are widened ('conv.u') and guarded, while the
; constant lengths stay split as before; -msil-memset-guard-bits=32 is the
; default's IL. The same CHECK lines serve both settings through the FileCheck
; variables LIM (the guard's constant), BLK (the block length) and STEP (the
; address and length step). The option refuses a value that is not a number
; from 4 to 32 when the options are parsed, before any input is read: llc
; prints the shared text T-FX2-1, '<N>' is not a number from 4 to 32, after
; LLVM's own prefix ('llc: for the --msil-memset-guard-bits option: ', 'llc.exe'
; on Windows) and exits with 1, with no other output, no 'LLVM ERROR' line and
; no crash report, even when the input file does not exist.
;
; The method bodies are the same on the four triples, at -O0 and -O2 (the
; portable msil64-unknown-dotnet output adds its width and byte-order guard,
; plan 5.18.7). The CHECK lines match the instructions that are the same at
; both levels: at -O0 the values wait in locals, at -O2 they are pushed where
; they are used and the loop keeps its address and rest in the argument slots.
; Every initblk, prefix, 'cgt.un', 'conv.i4' and localloc must be matched by a
; CHECK line (--implicit-check-not), so a guard where none belongs, a missing
; prefix or an extra block is an error even between the CHECK lines.
; memintrinsics.ll pins the whole body of one guarded memset (set_var) at
; both levels, and memset-guarded-loop.mir the blocks that the custom
; inserter builds. Each triple's output assembles at one level for each
; setting.
;
; The PRE lines check instruction selection at both levels (the MIR right
; before FinalizeISel): a length that may reach 2^N gives INITBLK_LONG with the
; memflags that MSILSetMemFlags filled (D24: 'unaligned. N' in bits 0-2,
; volatile in bit 3), the destination, the value, the native int length, N,
; and the memset's store memory operand (volatile when the memset is, of
; unknown size); a length below 2^N gives today's INITBLK with an int32
; length. Every INITBLK and INITBLK_LONG there must be matched. The MIR lines
; check that FinalizeISel, which runs after MSILSetMemFlags, leaves no
; INITBLK_LONG and copies the flags and the memory operand onto both INITBLKs.

; DEFINE: %{llc} = llc -asm-verbose=false -verify-machineinstrs
; DEFINE: %{nots} = --implicit-check-not=initblk --implicit-check-not=unaligned. \
; DEFINE:   --implicit-check-not=volatile. --implicit-check-not=cgt.un --implicit-check-not=conv.i4 \
; DEFINE:   --implicit-check-not=localloc --implicit-check-not=pinvokeimpl --implicit-check-not="'memset'("
; DEFINE: %{check32} = FileCheck %s %{nots} -DLIM="ldc.i8 4294967295" -DBLK="ldc.i4 -2147483648" \
; DEFINE:   -DSTEP="ldc.i8 2147483648"
; DEFINE: %{check4} = FileCheck %s %{nots} -DLIM="ldc.i4.s 15" -DBLK=ldc.i4.8 -DSTEP=ldc.i4.8
; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="{{.}}"

; The default (N = 32): every triple at -O2 and -O0.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.lnx.o2.il
; RUN: %{check32} --check-prefixes=CHECK,O2,N32,N32O2 --input-file=%t.lnx.o2.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o2.dll %t.lnx.o2.il %}
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.lnx.o0.il
; RUN: %{check32} --check-prefixes=CHECK,O0,N32,N32O0 --input-file=%t.lnx.o0.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.win.o2.il
; RUN: %{check32} --check-prefixes=CHECK,O2,N32,N32O2 --input-file=%t.win.o2.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %s -o %t.win.o0.il
; RUN: %{check32} --check-prefixes=CHECK,O0,N32,N32O0 --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil < %s -o %t.a64.o2.il
; RUN: %{check32} --check-prefixes=CHECK,O2,N32,N32O2 --input-file=%t.a64.o2.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.a64.o2.dll %t.a64.o2.il %}
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %s -o %t.a64.o0.il
; RUN: %{check32} --check-prefixes=CHECK,O0,N32,N32O0 --input-file=%t.a64.o0.il
; RUN: %{llc} -mtriple=msil64-unknown-dotnet < %s -o %t.net.o2.il
; RUN: %{check32} --check-prefixes=CHECK,O2,N32,N32O2 --input-file=%t.net.o2.il
; RUN: %{llc} -mtriple=msil64-unknown-dotnet -O0 < %s -o %t.net.o0.il
; RUN: %{check32} --check-prefixes=CHECK,O0,N32,N32O0 --input-file=%t.net.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.net.o0.dll %t.net.o0.il %}

; -msil-memset-guard-bits=32 is the default.
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -msil-memset-guard-bits=32 < %s -o %t.win.o2.b32.il
; RUN: diff %t.win.o2.il %t.win.o2.b32.il
; RUN: %{llc} -mtriple=msil64-unknown-dotnet -O0 -msil-memset-guard-bits=32 < %s -o %t.net.o0.b32.il
; RUN: diff %t.net.o0.il %t.net.o0.b32.il

; N = 4: every triple at -O2 and -O0.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -msil-memset-guard-bits=4 < %s -o %t.lnx.o2.b4.il
; RUN: %{check4} --check-prefixes=CHECK,O2,N4 --input-file=%t.lnx.o2.b4.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 -msil-memset-guard-bits=4 < %s -o %t.lnx.o0.b4.il
; RUN: %{check4} --check-prefixes=CHECK,O0,N4 --input-file=%t.lnx.o0.b4.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o0.b4.dll %t.lnx.o0.b4.il %}
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -msil-memset-guard-bits=4 < %s -o %t.win.o2.b4.il
; RUN: %{check4} --check-prefixes=CHECK,O2,N4 --input-file=%t.win.o2.b4.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o2.b4.dll %t.win.o2.b4.il %}
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-memset-guard-bits=4 < %s -o %t.win.o0.b4.il
; RUN: %{check4} --check-prefixes=CHECK,O0,N4 --input-file=%t.win.o0.b4.il
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -msil-memset-guard-bits=4 < %s -o %t.a64.o2.b4.il
; RUN: %{check4} --check-prefixes=CHECK,O2,N4 --input-file=%t.a64.o2.b4.il
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O0 -msil-memset-guard-bits=4 < %s -o %t.a64.o0.b4.il
; RUN: %{check4} --check-prefixes=CHECK,O0,N4 --input-file=%t.a64.o0.b4.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.a64.o0.b4.dll %t.a64.o0.b4.il %}
; RUN: %{llc} -mtriple=msil64-unknown-dotnet -msil-memset-guard-bits=4 < %s -o %t.net.o2.b4.il
; RUN: %{check4} --check-prefixes=CHECK,O2,N4 --input-file=%t.net.o2.b4.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.net.o2.b4.dll %t.net.o2.b4.il %}
; RUN: %{llc} -mtriple=msil64-unknown-dotnet -O0 -msil-memset-guard-bits=4 < %s -o %t.net.o0.b4.il
; RUN: %{check4} --check-prefixes=CHECK,O0,N4 --input-file=%t.net.o0.b4.il

; Before and after FinalizeISel.
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-before=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefixes=PRE,PRE32 -DN=32 --implicit-check-not=INITBLK
; RUN: llc -mtriple=msil64-unknown-dotnet -O0 -verify-machineinstrs -stop-before=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefixes=PRE,PRE32 -DN=32 --implicit-check-not=INITBLK
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -msil-memset-guard-bits=4 -verify-machineinstrs \
; RUN:   -stop-before=finalize-isel < %s | FileCheck %s --check-prefixes=PRE,PRE4 -DN=4 --implicit-check-not=INITBLK
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -msil-memset-guard-bits=4 -verify-machineinstrs \
; RUN:   -stop-before=finalize-isel < %s | FileCheck %s --check-prefixes=PRE,PRE4 -DN=4 --implicit-check-not=INITBLK
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR --implicit-check-not=INITBLK_LONG
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR --implicit-check-not=INITBLK_LONG

; The shared text T-FX2-1 for values outside 4 to 32, whole, and nothing else.
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -msil-memset-guard-bits=3 < %s -o %t.bits3.il 2>&1 \
; RUN:   | %{diag} --check-prefix=BITS3
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -msil-memset-guard-bits=33 < %s -o %t.bits33.il 2>&1 \
; RUN:   | %{diag} --check-prefix=BITS33
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -msil-memset-guard-bits=four < %s -o %t.four.il 2>&1 \
; RUN:   | %{diag} --check-prefix=FOUR
; Before any input is read: a missing input file is never opened.
; RUN: rm -f %t.none.ll
; RUN: not llc -mtriple=msil64-unknown-dotnet -msil-memset-guard-bits=33 %t.none.ll -o %t.none.il 2>&1 \
; RUN:   | %{diag} --check-prefix=BITS33
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-memset-guard-bits=3 %t.none.ll -o %t.none.il 2>&1 \
; RUN:   | %{diag} --check-prefix=BITS3

; BITS3:  {{^}}llc{{(\.exe)?}}: for the --msil-memset-guard-bits option: '3' is not a number from 4 to 32{{$}}
; BITS33: {{^}}llc{{(\.exe)?}}: for the --msil-memset-guard-bits option: '33' is not a number from 4 to 32{{$}}
; FOUR:   {{^}}llc{{(\.exe)?}}: for the --msil-memset-guard-bits option: 'four' is not a number from 4 to 32{{$}}

source_filename = "memset-guarded-loop.c"

declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
declare void @llvm.memset.p0.i32(ptr, i8, i32, i1)

; ---- Run-time native int lengths: the guard, the loop and the tail ----

; 1-aligned: 'unaligned. 1' on both initblks.
; CHECK-LABEL: .method {{.*}}'set_a1'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  [[LIM]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK:       {{^}}  [[BLK]]{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_a1'{{$}}
; PRE-LABEL: name: set_a1{{$}}
; PRE-DAG:   [[D:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; PRE-DAG:   [[L:%[0-9]+]]:i64 = ARGUMENT_I64 1, implicit $arguments
; PRE-DAG:   [[V:%[0-9]+]]:i32 = CONST_I32 0, implicit-def dead $arguments
; PRE:       INITBLK_LONG 1, [[D]], [[V]], [[L]], [[N]], implicit-def dead $arguments :: (store unknown-size into %ir.d, align 1){{$}}
define void @set_a1(ptr %d, i64 %n) {
  call void @llvm.memset.p0.i64(ptr align 1 %d, i8 0, i64 %n, i1 false)
  ret void
}

; 2-aligned and volatile: 'unaligned. 2', then 'volatile.', on both initblks.
; CHECK-LABEL: .method {{.*}}'set_a2_volatile'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  [[LIM]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK:       {{^}}  [[BLK]]{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_a2_volatile'{{$}}
; PRE-LABEL: name: set_a2_volatile{{$}}
; PRE:       INITBLK_LONG 10, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, [[N]], implicit-def dead $arguments :: (volatile store unknown-size into %ir.d, align 2){{$}}
; MIR-LABEL: name: set_a2_volatile{{$}}
; MIR:       [[LEN:%[0-9]+]]:i64 = ARGUMENT_I64 1, implicit $arguments
; MIR:       [[LIM:%[0-9]+]]:i64 = CONST_I64 4294967295, implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  [[BIG:%[0-9]+]]:i32 = CGT_UN_I64 [[LEN]], [[LIM]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  BR_UNLESS %bb.[[TAIL:[0-9]+]], [[BIG]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR:       {{^ +}}bb.[[LOOP:[0-9]+]]{{[ .:]}}
; MIR:       CONST_I32 -2147483648, implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  INITBLK 10, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, implicit-def {{(dead )?}}$arguments :: (volatile store unknown-size into %ir.d, align 2){{$}}
; MIR:       BR_IF %bb.[[LOOP]], %{{[0-9]+}}, implicit-def {{(dead )?}}$arguments{{$}}
; MIR:       {{^ +}}bb.[[TAIL]]{{[ .:]}}
; MIR:       CONV_I4_I64 %{{[0-9]+}}, implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  INITBLK 10, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, implicit-def {{(dead )?}}$arguments :: (volatile store unknown-size into %ir.d, align 2){{$}}
; MIR-NEXT:  RETURN implicit-def dead $arguments
define void @set_a2_volatile(ptr %d, i64 %n) {
  call void @llvm.memset.p0.i64(ptr align 2 %d, i8 0, i64 %n, i1 true)
  ret void
}

; Volatile and 8-aligned: 'volatile.' alone on both initblks.
; CHECK-LABEL: .method {{.*}}'set_volatile'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  [[LIM]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK:       {{^}}  [[BLK]]{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_volatile'{{$}}
; PRE-LABEL: name: set_volatile{{$}}
; PRE:       INITBLK_LONG 8, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, [[N]], implicit-def dead $arguments :: (volatile store unknown-size into %ir.d, align 8){{$}}
define void @set_volatile(ptr %d, i64 %n) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 0, i64 %n, i1 true)
  ret void
}

; A fill byte from an argument: both initblks take it (initblk's operands are
; the address, the value, then the length). 16-aligned blocks need no prefix,
; and with N = 4 the 8-byte blocks keep the natural alignment of 8.
; CHECK-LABEL: .method {{.*}}'set_fill'(
; CHECK-SAME:  native int, int32, native int) cil managed{{$}}
; CHECK:       {{^}}  [[LIM]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  [[BLK]]{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^  ld(arg|loc)(\.[0-3]| [0-9]+|\.s [0-9]+)$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_fill'{{$}}
; PRE-LABEL: name: set_fill{{$}}
; PRE-DAG:   [[D:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; PRE-DAG:   [[C:%[0-9]+]]:i32 = ARGUMENT_I32 1, implicit $arguments
; PRE-DAG:   [[L:%[0-9]+]]:i64 = ARGUMENT_I64 2, implicit $arguments
; PRE:       INITBLK_LONG 0, [[D]], [[C]], [[L]], [[N]], implicit-def dead $arguments :: (store unknown-size into %ir.d, align 16){{$}}
define void @set_fill(ptr %d, i8 %c, i64 %n) {
  call void @llvm.memset.p0.i64(ptr align 16 %d, i8 %c, i64 %n, i1 false)
  ret void
}

; A stack array as the destination: its address comes from the frame (the
; entry's localloc, D13), the loop advances a copy of it, and the byte read
; after the memset uses the array's own address. (8-aligned objects keep the
; frame free of the realignment constants, so LIM and STEP match only the
; guard and the loop.)
; CHECK-LABEL: .method {{.*}}'set_stack'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK:       {{^}}  [[LIM]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK:       {{^}}  [[BLK]]{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  ldind.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_stack'{{$}}
; PRE-LABEL: name: set_stack{{$}}
; PRE:       [[L:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; PRE:       INITBLK_LONG 0, %stack.0.buf, %{{[0-9]+}}, [[L]], [[N]], implicit-def dead $arguments :: (store unknown-size into %ir.buf, align 8){{$}}
; MIR-LABEL: name: set_stack{{$}}
; MIR:       [[DST:%[0-9]+]]:i64 = COPY_I64 %stack.0.buf, implicit-def {{(dead )?}}$arguments{{$}}
; MIR:       BR_UNLESS %bb.[[TAIL:[0-9]+]], %{{[0-9]+}}, implicit-def {{(dead )?}}$arguments{{$}}
; MIR:       {{^ +}}bb.[[LOOP:[0-9]+]]{{[ .:]}}
; MIR:       PHI [[DST]], %bb.0, %{{[0-9]+}}, %bb.[[LOOP]]{{$}}
; MIR:       INITBLK 0, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, implicit-def {{(dead )?}}$arguments :: (store unknown-size into %ir.buf, align 8){{$}}
; MIR:       {{^ +}}bb.[[TAIL]]{{[ .:]}}
; MIR-NEXT:  PHI [[DST]], %bb.0, %{{[0-9]+}}, %bb.[[LOOP]]{{$}}
; MIR:       INITBLK 0, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, implicit-def {{(dead )?}}$arguments :: (store unknown-size into %ir.buf, align 8){{$}}
; MIR-NEXT:  ADD_I64 %stack.0.buf, %{{[0-9]+}}, implicit-def dead $arguments
define i8 @set_stack(i64 %n, i64 %i) {
  %buf = alloca [64 x i8], align 8
  call void @llvm.memset.p0.i64(ptr align 8 %buf, i8 42, i64 %n, i1 false)
  %p = getelementptr inbounds i8, ptr %buf, i64 %i
  %v = load i8, ptr %p
  ret i8 %v
}

; The address of a stack object as the length (a frame index, which selection
; leaves in the pseudo's length operand): the PHIs of the length take a
; register copy of it, so every block is valid MIR (-verify-machineinstrs
; checks each PHI; memset-guarded-loop.mir checks the copy).
; CHECK-LABEL: .method {{.*}}'set_len_frame'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK:       {{^}}  [[LIM]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK:       {{^}}  [[BLK]]{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  ldind.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_len_frame'{{$}}
; PRE-LABEL: name: set_len_frame{{$}}
; PRE:       [[D:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; PRE:       INITBLK_LONG 1, [[D]], %{{[0-9]+}}, %stack.0.obj, [[N]], implicit-def dead $arguments :: (store unknown-size into %ir.d, align 1){{$}}
define i8 @set_len_frame(ptr %d) {
  %obj = alloca [64 x i8], align 8
  %n = ptrtoint ptr %obj to i64
  call void @llvm.memset.p0.i64(ptr align 1 %d, i8 0, i64 %n, i1 false)
  %v = load i8, ptr %obj
  ret i8 %v
}

; Two memsets in one block: the first tail is followed by the second guard.
; The first is 4-aligned with the fill byte 1, the second 8-aligned with 2
; (at -O0 the constant fill bytes wait in locals).
; CHECK-LABEL: .method {{.*}}'set_two'(
; CHECK-SAME:  native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}  [[LIM]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL1:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[LOOP1:[0-9]+]]:{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; CHECK:       {{^}}  [[BLK]]{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[LOOP1]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[TAIL1]]:{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[LIM]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brfalse.s @BB[[F]]_[[TAIL2:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[LOOP2:[0-9]+]]:{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.2{{$}}
; CHECK:       {{^}}  [[BLK]]{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[LOOP2]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[TAIL2]]:{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_two'{{$}}
; PRE-LABEL: name: set_two{{$}}
; PRE:       INITBLK_LONG 4, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, [[N]], implicit-def dead $arguments :: (store unknown-size into %ir.d, align 4){{$}}
; PRE:       INITBLK_LONG 0, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, [[N]], implicit-def dead $arguments :: (store unknown-size into %ir.e, align 8){{$}}
define void @set_two(ptr %d, ptr %e, i64 %n, i64 %m) {
  call void @llvm.memset.p0.i64(ptr align 4 %d, i8 1, i64 %n, i1 false)
  call void @llvm.memset.p0.i64(ptr align 8 %e, i8 2, i64 %m, i1 false)
  ret void
}

; A memset inside a loop: the outer loop's header comes before the guard, and
; its increment and branch back follow the tail.
; CHECK-LABEL: .method {{.*}}'set_in_loop'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}@BB[[F:[0-9]+]]_[[OUTER:[0-9]+]]:{{$}}
; CHECK:       {{^}}  [[LIM]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brfalse.s @BB[[F]]_[[TAIL:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK:       {{^}}  [[BLK]]{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  [[STEP]]{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK:       {{^}}  {{blt\.un(\.s)?}} @BB[[F]]_[[OUTER]]{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_in_loop'{{$}}
; PRE-LABEL: name: set_in_loop{{$}}
; PRE:       INITBLK_LONG 1, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, [[N]], implicit-def dead $arguments :: (store unknown-size into %ir.{{[a-z.]+}}, align 1){{$}}
define void @set_in_loop(ptr %d, i64 %n, i32 %k) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  %o = zext i32 %i to i64
  %p = getelementptr i8, ptr %d, i64 %o
  call void @llvm.memset.p0.i64(ptr align 1 %p, i8 0, i64 %n, i1 false)
  %i1 = add i32 %i, 1
  %c = icmp ult i32 %i1, %k
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; ---- Lengths below 2^32: no guard at the default ----

; An int32 length is initblk's size operand as it is. With N = 4 it may have
; bits 4 and up, so it is widened to native int (conv.u) and guarded.
; CHECK-LABEL: .method {{.*}}'set_i32'(
; CHECK-SAME:  native int, int32, int32) cil managed{{$}}
; N32-NEXT:    {{^\{$}}
; N32-NEXT:    {{^}}  .maxstack 3{{$}}
; N32-NEXT:    {{^}}  ldarg.0{{$}}
; N32-NEXT:    {{^}}  ldarg.1{{$}}
; N32-NEXT:    {{^}}  ldarg.2{{$}}
; N32-NEXT:    {{^}}  unaligned. 1{{$}}
; N32-NEXT:    {{^}}  initblk{{$}}
; N4:          {{^}}  ldarg.2{{$}}
; N4-NEXT:     {{^}}  conv.u{{$}}
; N4:          {{^}}  [[LIM]]{{$}}
; N4-NEXT:     {{^}}  conv.i{{$}}
; N4:          {{^}}  cgt.un{{$}}
; N4:          {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL:[0-9]+]]{{$}}
; N4-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; N4:          {{^}}  ldarg.1{{$}}
; N4-NEXT:     {{^}}  [[BLK]]{{$}}
; N4-NEXT:     {{^}}  unaligned. 1{{$}}
; N4-NEXT:     {{^}}  initblk{{$}}
; N4:          {{^}}  [[STEP]]{{$}}
; N4-NEXT:     {{^}}  conv.i{{$}}
; N4-NEXT:     {{^}}  add{{$}}
; N4:          {{^}}  [[STEP]]{{$}}
; N4-NEXT:     {{^}}  conv.i{{$}}
; N4-NEXT:     {{^}}  sub{{$}}
; N4:          {{^}}  cgt.un{{$}}
; N4:          {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; N4-NEXT:     {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; N4:          {{^}}  conv.i4{{$}}
; N4-NEXT:     {{^}}  unaligned. 1{{$}}
; N4-NEXT:     {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_i32'{{$}}
; PRE-LABEL: name: set_i32{{$}}
; PRE-DAG:   [[D:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; PRE-DAG:   [[C:%[0-9]+]]:i32 = ARGUMENT_I32 1, implicit $arguments
; PRE-DAG:   [[L:%[0-9]+]]:i32 = ARGUMENT_I32 2, implicit $arguments
; PRE32:     INITBLK 1, [[D]], [[C]], [[L]], implicit-def dead $arguments :: (store unknown-size into %ir.d, align 1){{$}}
; PRE4:      [[W:%[0-9]+]]:i64 = CONV_U_I32 [[L]], implicit-def dead $arguments
; PRE4:      INITBLK_LONG 1, [[D]], [[C]], [[W]], 4, implicit-def dead $arguments :: (store unknown-size into %ir.d, align 1){{$}}
define void @set_i32(ptr %d, i8 %c, i32 %n) {
  call void @llvm.memset.p0.i32(ptr align 1 %d, i8 %c, i32 %n, i1 false)
  ret void
}

; A zext of an int32: the int32 itself is the size operand.
; CHECK-LABEL: .method {{.*}}'set_zext'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; N32-NEXT:    {{^\{$}}
; N32-NEXT:    {{^}}  .maxstack 3{{$}}
; N32-NEXT:    {{^}}  ldarg.0{{$}}
; N32-NEXT:    {{^}}  ldc.i4.0{{$}}
; N32-NEXT:    {{^}}  ldarg.1{{$}}
; N32-NEXT:    {{^}}  unaligned. 1{{$}}
; N32-NEXT:    {{^}}  initblk{{$}}
; N4:          {{^}}  ldarg.1{{$}}
; N4-NEXT:     {{^}}  conv.u{{$}}
; N4:          {{^}}  [[LIM]]{{$}}
; N4-NEXT:     {{^}}  conv.i{{$}}
; N4:          {{^}}  cgt.un{{$}}
; N4:          {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL:[0-9]+]]{{$}}
; N4-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; N4:          {{^}}  [[BLK]]{{$}}
; N4-NEXT:     {{^}}  unaligned. 1{{$}}
; N4-NEXT:     {{^}}  initblk{{$}}
; N4:          {{^}}  [[STEP]]{{$}}
; N4-NEXT:     {{^}}  conv.i{{$}}
; N4-NEXT:     {{^}}  add{{$}}
; N4:          {{^}}  [[STEP]]{{$}}
; N4-NEXT:     {{^}}  conv.i{{$}}
; N4-NEXT:     {{^}}  sub{{$}}
; N4:          {{^}}  cgt.un{{$}}
; N4:          {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; N4-NEXT:     {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; N4:          {{^}}  conv.i4{{$}}
; N4-NEXT:     {{^}}  unaligned. 1{{$}}
; N4-NEXT:     {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_zext'{{$}}
; PRE-LABEL: name: set_zext{{$}}
; PRE-DAG:   [[D:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; PRE-DAG:   [[L:%[0-9]+]]:i32 = ARGUMENT_I32 1, implicit $arguments
; PRE32:     INITBLK 1, [[D]], %{{[0-9]+}}, [[L]], implicit-def dead $arguments :: (store unknown-size into %ir.d, align 1){{$}}
; PRE4:      [[W:%[0-9]+]]:i64 = CONV_U_I32 [[L]], implicit-def dead $arguments
; PRE4:      INITBLK_LONG 1, [[D]], %{{[0-9]+}}, [[W]], 4, implicit-def dead $arguments :: (store unknown-size into %ir.d, align 1){{$}}
define void @set_zext(ptr %d, i32 %n) {
  %z = zext i32 %n to i64
  call void @llvm.memset.p0.i64(ptr align 1 %d, i8 0, i64 %z, i1 false)
  ret void
}

; A length masked with 0xFFFFFFFF: its low 32 bits (conv.i4). With N = 4 the
; masked value is the native int that the guard tests ('conv.i4; conv.u').
; CHECK-LABEL: .method {{.*}}'set_mask'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; N32-NEXT:    {{^\{$}}
; N32-NEXT:    {{^}}  .maxstack 3{{$}}
; N32O0-NEXT:  {{^}}  .locals (int32){{$}}
; N32O0-NEXT:  {{^}}  ldarg.1{{$}}
; N32O0-NEXT:  {{^}}  conv.i4{{$}}
; N32O0-NEXT:  {{^}}  stloc.0{{$}}
; N32-NEXT:    {{^}}  ldarg.0{{$}}
; N32-NEXT:    {{^}}  ldc.i4.0{{$}}
; N32O2-NEXT:  {{^}}  ldarg.1{{$}}
; N32O2-NEXT:  {{^}}  conv.i4{{$}}
; N32O0-NEXT:  {{^}}  ldloc.0{{$}}
; N32-NEXT:    {{^}}  unaligned. 1{{$}}
; N32-NEXT:    {{^}}  initblk{{$}}
; N4:          {{^}}  ldarg.1{{$}}
; N4-NEXT:     {{^}}  conv.i4{{$}}
; N4-NEXT:     {{^}}  conv.u{{$}}
; N4:          {{^}}  [[LIM]]{{$}}
; N4-NEXT:     {{^}}  conv.i{{$}}
; N4:          {{^}}  cgt.un{{$}}
; N4:          {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[TAIL:[0-9]+]]{{$}}
; N4-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; N4:          {{^}}  [[BLK]]{{$}}
; N4-NEXT:     {{^}}  unaligned. 1{{$}}
; N4-NEXT:     {{^}}  initblk{{$}}
; N4:          {{^}}  [[STEP]]{{$}}
; N4-NEXT:     {{^}}  conv.i{{$}}
; N4-NEXT:     {{^}}  add{{$}}
; N4:          {{^}}  [[STEP]]{{$}}
; N4-NEXT:     {{^}}  conv.i{{$}}
; N4-NEXT:     {{^}}  sub{{$}}
; N4:          {{^}}  cgt.un{{$}}
; N4:          {{^}}  brtrue.s @BB[[F]]_[[LOOP]]{{$}}
; N4-NEXT:     {{^}}@BB[[F]]_[[TAIL]]:{{$}}
; N4:          {{^}}  conv.i4{{$}}
; N4-NEXT:     {{^}}  unaligned. 1{{$}}
; N4-NEXT:     {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_mask'{{$}}
; PRE-LABEL: name: set_mask{{$}}
; PRE-DAG:   [[D:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; PRE-DAG:   [[L:%[0-9]+]]:i64 = ARGUMENT_I64 1, implicit $arguments
; PRE:       [[T:%[0-9]+]]:i32 = CONV_I4_I64 [[L]], implicit-def dead $arguments
; PRE32:     INITBLK 1, [[D]], %{{[0-9]+}}, [[T]], implicit-def dead $arguments :: (store unknown-size into %ir.d, align 1){{$}}
; PRE4:      [[W:%[0-9]+]]:i64 = CONV_U_I32 [[T]], implicit-def dead $arguments
; PRE4:      INITBLK_LONG 1, [[D]], %{{[0-9]+}}, [[W]], 4, implicit-def dead $arguments :: (store unknown-size into %ir.d, align 1){{$}}
define void @set_mask(ptr %d, i64 %n) {
  %z = and i64 %n, 4294967295
  call void @llvm.memset.p0.i64(ptr align 1 %d, i8 0, i64 %z, i1 false)
  ret void
}

; ---- Constant lengths: unchanged, whatever N is ----

; CHECK-LABEL: .method {{.*}}'set_c1000'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_c1000'{{$}}
; PRE-LABEL: name: set_c1000{{$}}
; PRE:       {{^ +}}INITBLK 1, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, implicit-def dead $arguments ::
define void @set_c1000(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 1 %d, i8 0, i64 1000, i1 false)
  ret void
}

; 4294967301 = 2^31 + 2147483653 (2 GiB and more): the constant split, two
; blocks with no guard and no loop, the second at +2^31.
; CHECK-LABEL: .method {{.*}}'set_c4g5'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32, int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 -2147483648{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldc.i4.7{{$}}
; O0-NEXT:     {{^}}  stloc.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O2-NEXT:     {{^}}  ldc.i4 -2147483648{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 2147483648{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 -2147483643{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_c4g5'{{$}}
; PRE-LABEL: name: set_c4g5{{$}}
; PRE:       {{^ +}}INITBLK 0, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, implicit-def dead $arguments ::
; PRE:       {{^ +}}INITBLK 0, %{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, implicit-def dead $arguments ::
define void @set_c4g5(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 7, i64 4294967301, i1 false)
  ret void
}
