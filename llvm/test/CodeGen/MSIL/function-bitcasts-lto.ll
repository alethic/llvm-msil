; MSILFixFunctionBitcasts under the module flag "msil-lto" (ImplementationPlan.md
; 5.9.8, Q8, M20; the D30 row of 2026-10-21 for M20). msil-ld sets the flag on
; the merged LTO module (5.9.13), and the pass then applies the adapter table
; of cli::classifyCall to every call of a definition whose call-site type
; differs, so the LTO path agrees with msil-ld's object path:
; - an adaptable difference gets an IR wrapper that keeps Phase 1's name
;   '<f>_bitcast' (objects may not carry '<msil>' names) and does what the
;   table says: drop extra trailing arguments, pass zero for missing ones,
;   trunc (conv.i4) a native int argument for an int32 parameter, sext (conv.i)
;   an int32 argument for a native int parameter, return zero for a void
;   definition, drop a result the call discards, convert an int32/native int
;   result. Each adaptation is a warning with 5.9.8's text minus the
;   ' in <file>' parts (the merged module has no files):
;     warning: function signature mismatch: <f>
;     >>> defined as <definition's shape>
;     >>> called as <call's shape>
;     >>> adapter: <what it does>
; - integer versus floating point and float32 versus float64 are errors with
;   the same first three lines and '>>> <why>'; llc exits with 1 and writes
;   no 'LLVM ERROR' (the output keeps Phase 1's trapping wrapper).
; The module of objfmt-binding-files/sigmismatch (callee.c 'int f(int a)',
; caller.c 'int f(int, int); ... f(40, 2)') as the IR linker merges it is the
; first case; one module has a mismatch inside itself, which warns too
; (the pass sees the merged module). Narrow results: the CIL method of a
; 'zeroext i8', 'zeroext i16' or 'signext i8' definition returns int32,
; already extended by its attribute, so a call as 'i32 ()' is a direct match
; (no warning) whose wrapper reproduces that int32: it zero-extends for
; zeroext and sign-extends for signext, which instruction selection folds
; into the call (200 stays 200 for 'unsigned char'). A call as 'i64 ()' is
; the table's int32 -> native int row: the same int32, then conv.i (printed
; as conv.u for a zero-extended value, which is the same number), with a
; warning. Narrow arguments (the 5.9.8 row of 2026-10-25 for M20): a call
; passes an 'i8' argument as the int32 its call site's attribute makes of it,
; zero-extended for 'zeroext' or no attribute and sign-extended for
; 'signext', as the object path's call site materializes it. So against an
; 'i32' parameter the call is a direct match (no warning) whose wrapper does
; that extension, and against an 'i64' parameter it is the int32 -> native
; int row: the same extension to int32, then conv.i, with a warning. No
; wrapper extends the i8 straight to i64, which would turn 'unsigned char'
; 200 into -56. The attribute is each call site's own: two calls of one
; definition with one IR type, one 'zeroext' and one 'signext', are each
; widened by their own attribute whichever comes first, so no wrapper serves
; both (narrowkey.ll). Attributes alone can make a mismatch: a call of the
; definition's IR type without its sret, or with a byval of another size, is
; 5.9.8's sret or byval error (sret.ll, byvalsize.ll), and a definition
; called once with byval and once without gets both diagnostics, the byval
; error and the missing-argument warning, in either order (byvalmix.ll).
; Their reason lines are the object path's (the sr and bv errors of
; lld/test/MSIL/obj-signatures.test). A same-type call with the definition's
; attributes gets no wrapper and no diagnostic (same.ll). Without the flag
; the pass is Phase 1's (function-bitcasts.ll): the same modules compile
; silently, with poison for missing values and '_bitcast_invalid' wrappers
; that trap for every difference that is not a bit or pointer cast,
; int/float included where the cast is a bitcast, and a call of the
; definition's IR type stays a direct call whatever its sret and byval
; attributes (D30). Both x86_64 triples; -O2 and -O0.

; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; DEFINE: %{direct} = --implicit-check-not="mismatch: h{{[zsn]}}"
; RUN: split-file %s %t
; RUN: cat %t/cases.ll %t/flag.ll > %t/cases-lto.ll
; RUN: cat %t/errors.ll %t/flag.ll > %t/errors-lto.ll

; Adapters, with one warning each, and the narrow arguments' direct matches,
; with none.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/cases-lto.ll -o %t/cases.lnx.il 2> %t/cases.lnx.err
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/cases-lto.ll -o %t/cases.win.il 2> %t/cases.win.err
; RUN: FileCheck %s --check-prefix=WF %{nocrash} %{direct} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WTWO %{nocrash} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WH %{nocrash} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WW %{nocrash} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WV %{nocrash} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WK %{nocrash} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WN %{nocrash} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WWZ %{nocrash} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WWS %{nocrash} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WWN %{nocrash} --input-file=%t/cases.lnx.err
; RUN: FileCheck %s --check-prefix=WF %{nocrash} %{direct} --input-file=%t/cases.win.err
; RUN: FileCheck %s --check-prefix=WN %{nocrash} --input-file=%t/cases.win.err
; RUN: FileCheck %s --check-prefix=WWZ %{nocrash} --input-file=%t/cases.win.err
; RUN: FileCheck %s --check-prefix=WWS %{nocrash} --input-file=%t/cases.win.err
; RUN: FileCheck %s --check-prefix=WWN %{nocrash} --input-file=%t/cases.win.err
; Ten warnings (none for the direct matches hz, hs and hn, which the WF runs'
; %{direct} rejects), no error, and never a file name or Phase 1's silence.
; RUN: grep -c "^warning: .*function signature mismatch: " %t/cases.lnx.err | FileCheck %s --check-prefix=TEN
; RUN: grep -c "^warning: .*function signature mismatch: " %t/cases.win.err | FileCheck %s --check-prefix=TEN
; RUN: FileCheck %s --check-prefix=WBAN --implicit-check-not=error: --implicit-check-not=ld-temp \
; RUN:   %{nocrash} --input-file=%t/cases.lnx.err
; TEN:   {{^}}10{{$}}
; WBAN:  {{^}}warning: {{.*}}function signature mismatch: {{.*}}

; The adapters' IL: the calls (-O2 and -O0) and the adapter bodies (-O2),
; and their IR.
; DEFINE: %{ad} = FileCheck %s --implicit-check-not=_bitcast_invalid --implicit-check-not="<msil>adapt"
; RUN: %{ad} --check-prefix=AD --input-file=%t/cases.lnx.il
; RUN: %{ad} --check-prefix=AD --input-file=%t/cases.win.il
; RUN: %{ad} --check-prefix=ADW --input-file=%t/cases.lnx.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/cases-lto.ll \
; RUN:   -o %t/cases.mir 2>/dev/null
; RUN: FileCheck %s --check-prefix=IRF --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRTWO --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRH --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRW --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRV --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRK --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRN --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRWZ --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRWS --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRWN --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRHZ --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRHS --input-file=%t/cases.mir
; RUN: FileCheck %s --check-prefix=IRHN --input-file=%t/cases.mir
; RUN: not grep -E "(sext|zext) i8 .* to i64" %t/cases.mir
; The Windows triple, whose C calls carry no extension attribute, at -O0.
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=msil-fix-function-bitcasts < %t/cases-lto.ll \
; RUN:   -o %t/cases.win.mir 2>/dev/null
; RUN: FileCheck %s --check-prefix=IRWN --input-file=%t/cases.win.mir
; RUN: FileCheck %s --check-prefix=IRHN --input-file=%t/cases.win.mir
; RUN: not grep -E "(sext|zext) i8 .* to i64" %t/cases.win.mir
; RUN: %if ilasm %{ %ilasm-verify -output=%t/cases.lnx.dll %t/cases.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/cases.win.dll %t/cases.win.il %}

; Errors.
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/errors-lto.ll -o /dev/null 2> %t/errors.lnx.err
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/errors-lto.ll -o /dev/null 2> %t/errors.win.err
; RUN: FileCheck %s --check-prefix=EG %{nocrash} --input-file=%t/errors.lnx.err
; RUN: FileCheck %s --check-prefix=ES %{nocrash} --input-file=%t/errors.lnx.err
; RUN: FileCheck %s --check-prefix=EFL %{nocrash} --input-file=%t/errors.lnx.err
; RUN: FileCheck %s --check-prefix=EG %{nocrash} --input-file=%t/errors.win.err
; RUN: FileCheck %s --check-prefix=ES %{nocrash} --input-file=%t/errors.win.err
; RUN: FileCheck %s --check-prefix=EFL %{nocrash} --input-file=%t/errors.win.err
; RUN: grep -c "^error: .*function signature mismatch: " %t/errors.lnx.err | FileCheck %s --check-prefix=THREE
; RUN: FileCheck %s --check-prefix=EBAN --implicit-check-not=warning: --implicit-check-not="adapter:" \
; RUN:   --implicit-check-not=ld-temp %{nocrash} --input-file=%t/errors.lnx.err
; THREE: {{^}}3{{$}}
; EBAN:  {{^}}error: {{.*}}function signature mismatch: {{.*}}

; Narrow results called as int32: direct matches, so no output at all.
; RUN: cat %t/narrow.ll %t/flag.ll > %t/narrow-lto.ll
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/narrow-lto.ll -o %t/narrow.lnx.il 2>&1 | count 0
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/narrow-lto.ll -o %t/narrow.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NR --implicit-check-not=_bitcast_invalid --input-file=%t/narrow.lnx.il
; RUN: FileCheck %s --check-prefix=NR --implicit-check-not=_bitcast_invalid --input-file=%t/narrow.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/narrow-lto.ll \
; RUN:   -o %t/narrow.mir 2>/dev/null
; RUN: FileCheck %s --check-prefix=IRNR --input-file=%t/narrow.mir
; RUN: %if ilasm %{ %ilasm-verify -output=%t/narrow.lnx.dll %t/narrow.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/narrow.win.dll %t/narrow.win.il %}

; Narrow results called as native int: the int32 -> native int adapter,
; with one warning each.
; RUN: cat %t/narrow64.ll %t/flag.ll > %t/narrow64-lto.ll
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/narrow64-lto.ll -o %t/narrow64.lnx.il \
; RUN:   2> %t/narrow64.lnx.err
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/narrow64-lto.ll -o %t/narrow64.win.il \
; RUN:   2> %t/narrow64.win.err
; RUN: FileCheck %s --check-prefix=WUL %{nocrash} --input-file=%t/narrow64.lnx.err
; RUN: FileCheck %s --check-prefix=WSL %{nocrash} --input-file=%t/narrow64.lnx.err
; RUN: FileCheck %s --check-prefix=WUL %{nocrash} --input-file=%t/narrow64.win.err
; RUN: FileCheck %s --check-prefix=WSL %{nocrash} --input-file=%t/narrow64.win.err
; RUN: grep -c "^warning: .*function signature mismatch: " %t/narrow64.lnx.err | FileCheck %s --check-prefix=TWO
; RUN: FileCheck %s --check-prefix=WBAN --implicit-check-not=error: --implicit-check-not=ld-temp \
; RUN:   %{nocrash} --input-file=%t/narrow64.lnx.err
; RUN: FileCheck %s --check-prefix=NR64 --implicit-check-not=_bitcast_invalid --input-file=%t/narrow64.lnx.il
; RUN: FileCheck %s --check-prefix=NR64 --implicit-check-not=_bitcast_invalid --input-file=%t/narrow64.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/narrow64-lto.ll \
; RUN:   -o %t/narrow64.mir 2>/dev/null
; RUN: FileCheck %s --check-prefix=IRUL --input-file=%t/narrow64.mir
; RUN: FileCheck %s --check-prefix=IRSL --input-file=%t/narrow64.mir
; RUN: %if ilasm %{ %ilasm-verify -output=%t/narrow64.lnx.dll %t/narrow64.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/narrow64.win.dll %t/narrow64.win.il %}
; TWO: {{^}}2{{$}}

; Two calls of one definition with one IR type, 'i32 (i8)', one 'zeroext 200'
; and one 'signext -56', in both orders (hzs: zeroext first, hsz: signext
; first): int32 on both sides, so no diagnostic, and each call's wrapper
; extends by that call's own attribute, so each definition has two wrappers.
; RUN: cat %t/narrowkey.ll %t/flag.ll > %t/narrowkey-lto.ll
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/narrowkey-lto.ll -o %t/narrowkey.lnx.il 2>&1 | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-fix-function-bitcasts < %t/narrowkey-lto.ll \
; RUN:   -o %t/narrowkey.mir 2>/dev/null
; RUN: FileCheck %s --check-prefix=NKZ -DCALLER=hzs_z -DDEF=hzs --input-file=%t/narrowkey.mir
; RUN: FileCheck %s --check-prefix=NKS -DCALLER=hzs_s -DDEF=hzs --input-file=%t/narrowkey.mir
; RUN: FileCheck %s --check-prefix=NKS -DCALLER=hsz_s -DDEF=hsz --input-file=%t/narrowkey.mir
; RUN: FileCheck %s --check-prefix=NKZ -DCALLER=hsz_z -DDEF=hsz --input-file=%t/narrowkey.mir
; RUN: grep -c "^  call int32 'hzs'(int32)" %t/narrowkey.lnx.il | FileCheck %s --check-prefix=TWO
; RUN: grep -c "^  call int32 'hsz'(int32)" %t/narrowkey.lnx.il | FileCheck %s --check-prefix=TWO
; RUN: %if ilasm %{ %ilasm-verify -output=%t/narrowkey.lnx.dll %t/narrowkey.lnx.il %}

; One definition, 'i64 f(ptr, i32)', called as 'i64 (ptr)' once with
; 'byval(%struct.S)' and once without, in both orders (fbp: byval first, fpb:
; plain first): each call is classified on its own, so each definition gets
; the byval error and the missing-argument adapter warning, and nothing else.
; RUN: cat %t/byvalmix.ll %t/flag.ll > %t/byvalmix-lto.ll
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/byvalmix-lto.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=BME -DNAME=fbp %{nocrash}
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/byvalmix-lto.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=BME -DNAME=fpb %{nocrash}
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/byvalmix-lto.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=BMW -DNAME=fbp %{nocrash}
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/byvalmix-lto.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=BMW -DNAME=fpb %{nocrash}
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/byvalmix-lto.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=BMN --implicit-check-not=error: --implicit-check-not=warning: %{nocrash}

; Calls of the definition's own IR type that differ only in their
; attributes: no sret where the definition has one (both triples), and a
; 24-byte byval for a 32-byte byval parameter (Linux).
; RUN: cat %t/sret.ll %t/flag.ll > %t/sret-lto.ll
; RUN: cat %t/byvalsize.ll %t/flag.ll > %t/byvalsize-lto.ll
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/sret-lto.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SRE --implicit-check-not=error: --implicit-check-not=warning: %{nocrash}
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/sret-lto.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SRE --implicit-check-not=error: --implicit-check-not=warning: %{nocrash}
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/byvalsize-lto.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=BVE --implicit-check-not=error: --implicit-check-not=warning: %{nocrash}

; Same-type calls with the definition's attributes (sret, byval, zeroext):
; direct calls, no wrapper, no diagnostic.
; RUN: cat %t/same.ll %t/flag.ll > %t/same-lto.ll
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/same-lto.ll -o %t/same.lnx.il 2>&1 | count 0
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/same-lto.ll -o %t/same.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=SAMEOK --implicit-check-not=_bitcast --input-file=%t/same.lnx.il
; RUN: FileCheck %s --check-prefix=SAMEOK --implicit-check-not=_bitcast --input-file=%t/same.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/same.lnx.dll %t/same.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/same.win.dll %t/same.win.il %}

; Without the flag: Phase 1's silent wrappers.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/cases.ll -o %t/p1.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=P1 --input-file=%t/p1.lnx.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/cases.ll -o %t/p1.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=P1 --input-file=%t/p1.win.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/errors.ll -o %t/p1e.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=P1E --input-file=%t/p1e.lnx.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/errors.ll -o %t/p1e.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=P1E --input-file=%t/p1e.win.il
; The attribute-only differences: direct calls, no wrapper, no diagnostic.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/sret.ll -o %t/p1sr.lnx.il 2>&1 | count 0
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/sret.ll -o %t/p1sr.win.il 2>&1 | count 0
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/byvalsize.ll -o %t/p1bv.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=P1SR --implicit-check-not=_bitcast --input-file=%t/p1sr.lnx.il
; RUN: FileCheck %s --check-prefix=P1SR --implicit-check-not=_bitcast --input-file=%t/p1sr.win.il
; RUN: FileCheck %s --check-prefix=P1BV --implicit-check-not=_bitcast --input-file=%t/p1bv.lnx.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/p1sr.lnx.dll %t/p1sr.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/p1sr.win.dll %t/p1sr.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/p1bv.lnx.dll %t/p1bv.lnx.il %}

;--- flag.ll
!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-lto", i32 1}

;--- cases.ll
; The merged module: sigmismatch's f, then one definition per row of the
; table and a caller each.
source_filename = "ld-temp.o"

define i32 @f(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

define i32 @two(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

define i32 @h(i32 %v) {
  ret i32 %v
}

define i64 @w(i64 %x) {
  ret i64 %x
}

define void @v() {
  ret void
}

define i32 @k() {
  ret i32 5
}

define i64 @n() {
  ret i64 9
}

; Narrow arguments: 'long long w?(long long c) { return c; }' and
; 'int h?(int c) { return c; }', each called through a prototype with an
; 'unsigned char' (zeroext), a 'signed char' (signext) or an unattributed
; (the Windows triple's) 'char' parameter.
define i64 @wz(i64 %c) {
  ret i64 %c
}

define i64 @ws(i64 %c) {
  ret i64 %c
}

define i64 @wn(i64 %c) {
  ret i64 %c
}

define i32 @hz(i32 %c) {
  ret i32 %c
}

define i32 @hs(i32 %c) {
  ret i32 %c
}

define i32 @hn(i32 %c) {
  ret i32 %c
}

; caller.c: int f(int, int); return f(40, 2) & 0xff.
define i32 @main() {
  %r = call i32 @f(i32 40, i32 2)
  %m = and i32 %r, 255
  ret i32 %m
}

define i32 @call_two(i32 %x) {
  %r = call i32 @two(i32 %x)
  ret i32 %r
}

define i32 @call_h(i64 %x) {
  %r = call i32 @h(i64 %x)
  ret i32 %r
}

define i64 @call_w(i32 %x) {
  %r = call i64 @w(i32 %x)
  ret i64 %r
}

define i32 @call_v() {
  %r = call i32 @v()
  ret i32 %r
}

define void @call_k() {
  call void @k()
  ret void
}

define i32 @call_n() {
  %r = call i32 @n()
  ret i32 %r
}

; 200 must arrive as 200 for the zeroext and unattributed calls, and -56 as
; -56 for the signext call.
define i64 @call_wz() {
  %r = call i64 @wz(i8 zeroext 200)
  ret i64 %r
}

define i64 @call_ws() {
  %r = call i64 @ws(i8 signext -56)
  ret i64 %r
}

define i64 @call_wn() {
  %r = call i64 @wn(i8 -56)
  ret i64 %r
}

define i32 @call_hz() {
  %r = call i32 @hz(i8 zeroext 200)
  ret i32 %r
}

define i32 @call_hs() {
  %r = call i32 @hs(i8 signext -56)
  ret i32 %r
}

define i32 @call_hn() {
  %r = call i32 @hn(i8 -56)
  ret i32 %r
}

; WF:        {{^}}warning: {{.*}}function signature mismatch: f{{$}}
; WF-NEXT:   {{^}}>>> defined as int32(int32){{$}}
; WF-NEXT:   {{^}}>>> called as int32(int32, int32){{$}}
; WF-NEXT:   {{^}}>>> adapter: {{.+}}{{$}}
; WTWO:      {{^}}warning: {{.*}}function signature mismatch: two{{$}}
; WTWO-NEXT: {{^}}>>> defined as int32(int32, int32){{$}}
; WTWO-NEXT: {{^}}>>> called as int32(int32){{$}}
; WTWO-NEXT: {{^}}>>> adapter: {{.+}}{{$}}
; WH:        {{^}}warning: {{.*}}function signature mismatch: h{{$}}
; WH-NEXT:   {{^}}>>> defined as int32(int32){{$}}
; WH-NEXT:   {{^}}>>> called as int32(native int){{$}}
; WH-NEXT:   {{^}}>>> adapter: {{.+}}{{$}}
; WW:        {{^}}warning: {{.*}}function signature mismatch: w{{$}}
; WW-NEXT:   {{^}}>>> defined as native int(native int){{$}}
; WW-NEXT:   {{^}}>>> called as native int(int32){{$}}
; WW-NEXT:   {{^}}>>> adapter: {{.+}}{{$}}
; WV:        {{^}}warning: {{.*}}function signature mismatch: v{{$}}
; WV-NEXT:   {{^}}>>> defined as void(){{$}}
; WV-NEXT:   {{^}}>>> called as int32(){{$}}
; WV-NEXT:   {{^}}>>> adapter: {{.+}}{{$}}
; WK:        {{^}}warning: {{.*}}function signature mismatch: k{{$}}
; WK-NEXT:   {{^}}>>> defined as int32(){{$}}
; WK-NEXT:   {{^}}>>> called as void(){{$}}
; WK-NEXT:   {{^}}>>> adapter: {{.+}}{{$}}
; WN:        {{^}}warning: {{.*}}function signature mismatch: n{{$}}
; WN-NEXT:   {{^}}>>> defined as native int(){{$}}
; WN-NEXT:   {{^}}>>> called as int32(){{$}}
; WN-NEXT:   {{^}}>>> adapter: {{.+}}{{$}}
; An i8 argument is int32 in the table: native int parameters get the int32 ->
; native int adapter, int32 parameters (hz, hs, hn) match directly.
; WWZ:       {{^}}warning: {{.*}}function signature mismatch: wz{{$}}
; WWZ-NEXT:  {{^}}>>> defined as native int(native int){{$}}
; WWZ-NEXT:  {{^}}>>> called as native int(int32){{$}}
; WWZ-NEXT:  {{^}}>>> adapter: {{.+}}{{$}}
; WWS:       {{^}}warning: {{.*}}function signature mismatch: ws{{$}}
; WWS-NEXT:  {{^}}>>> defined as native int(native int){{$}}
; WWS-NEXT:  {{^}}>>> called as native int(int32){{$}}
; WWS-NEXT:  {{^}}>>> adapter: {{.+}}{{$}}
; WWN:       {{^}}warning: {{.*}}function signature mismatch: wn{{$}}
; WWN-NEXT:  {{^}}>>> defined as native int(native int){{$}}
; WWN-NEXT:  {{^}}>>> called as native int(int32){{$}}
; WWN-NEXT:  {{^}}>>> adapter: {{.+}}{{$}}

; The callers call the adapters, which call the definitions.
; AD-LABEL: {{^}}.method public static int32 'main'() cil managed{{$}}
; AD:       {{^}}  call int32 'f_bitcast'(int32, int32){{$}}
; AD-LABEL: {{^}}.method public static int32 'call_two'(int32) cil managed{{$}}
; AD:       {{^}}  call int32 'two_bitcast'(int32){{$}}
; AD-LABEL: {{^}}.method public static int32 'call_h'(native int) cil managed{{$}}
; AD:       {{^}}  call int32 'h_bitcast'(native int){{$}}
; AD-LABEL: {{^}}.method public static native int 'call_w'(int32) cil managed{{$}}
; AD:       {{^}}  call native int 'w_bitcast'(int32){{$}}
; AD-LABEL: {{^}}.method public static int32 'call_v'() cil managed{{$}}
; AD:       {{^}}  call int32 'v_bitcast'(){{$}}
; AD-LABEL: {{^}}.method public static void 'call_k'() cil managed{{$}}
; AD:       {{^}}  call void 'k_bitcast'(){{$}}
; AD-LABEL: {{^}}.method public static int32 'call_n'() cil managed{{$}}
; AD:       {{^}}  call int32 'n_bitcast'(){{$}}
; AD-LABEL: {{^}}.method public static native int 'call_wz'() cil managed{{$}}
; AD:       {{^}}  call native int 'wz_bitcast'(int32){{$}}
; AD-LABEL: {{^}}.method public static native int 'call_ws'() cil managed{{$}}
; AD:       {{^}}  call native int 'ws_bitcast'(int32){{$}}
; AD-LABEL: {{^}}.method public static native int 'call_wn'() cil managed{{$}}
; AD:       {{^}}  call native int 'wn_bitcast'(int32){{$}}
; AD-LABEL: {{^}}.method public static int32 'call_hz'() cil managed{{$}}
; AD:       {{^}}  call int32 'hz_bitcast'(int32){{$}}
; AD-LABEL: {{^}}.method public static int32 'call_hs'() cil managed{{$}}
; AD:       {{^}}  call int32 'hs_bitcast'(int32){{$}}
; AD-LABEL: {{^}}.method public static int32 'call_hn'() cil managed{{$}}
; AD:       {{^}}  call int32 'hn_bitcast'(int32){{$}}
; Drop the second argument.
; ADW-LABEL: {{^}}.method {{.*}}static int32 'f_bitcast'(int32, int32) cil managed{{$}}
; ADW-NEXT:  {{^\{$}}
; ADW-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; ADW-NEXT:  {{^}}  ldarg.0{{$}}
; ADW-NEXT:  {{^}}  call int32 'f'(int32){{$}}
; ADW-NEXT:  {{^}}  ret{{$}}
; Zero for the missing argument (Phase 1 passes poison).
; ADW-LABEL: {{^}}.method {{.*}}static int32 'two_bitcast'(int32) cil managed{{$}}
; ADW-NEXT:  {{^\{$}}
; ADW-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; ADW-NEXT:  {{^}}  ldarg.0{{$}}
; ADW-NEXT:  {{^}}  ldc.i4.0{{$}}
; ADW-NEXT:  {{^}}  call int32 'two'(int32, int32){{$}}
; ADW-NEXT:  {{^}}  ret{{$}}
; conv.i4, conv.i (Phase 1: '_bitcast_invalid').
; ADW-LABEL: {{^}}.method {{.*}}static int32 'h_bitcast'(native int) cil managed{{$}}
; ADW-NEXT:  {{^\{$}}
; ADW-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; ADW-NEXT:  {{^}}  ldarg.0{{$}}
; ADW-NEXT:  {{^}}  conv.i4{{$}}
; ADW-NEXT:  {{^}}  call int32 'h'(int32){{$}}
; ADW-NEXT:  {{^}}  ret{{$}}
; ADW-LABEL: {{^}}.method {{.*}}static native int 'w_bitcast'(int32) cil managed{{$}}
; ADW-NEXT:  {{^\{$}}
; ADW-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; ADW-NEXT:  {{^}}  ldarg.0{{$}}
; ADW-NEXT:  {{^}}  conv.i{{$}}
; ADW-NEXT:  {{^}}  call native int 'w'(native int){{$}}
; ADW-NEXT:  {{^}}  ret{{$}}
; Zero for the result of a void definition, a popped result, a conv.i4
; result.
; ADW-LABEL: {{^}}.method {{.*}}static int32 'v_bitcast'() cil managed{{$}}
; ADW-NEXT:  {{^\{$}}
; ADW-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; ADW-NEXT:  {{^}}  call void 'v'(){{$}}
; ADW-NEXT:  {{^}}  ldc.i4.0{{$}}
; ADW-NEXT:  {{^}}  ret{{$}}
; ADW-LABEL: {{^}}.method {{.*}}static void 'k_bitcast'() cil managed{{$}}
; ADW-NEXT:  {{^\{$}}
; ADW-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; ADW-NEXT:  {{^}}  call int32 'k'(){{$}}
; ADW-NEXT:  {{^}}  pop{{$}}
; ADW-NEXT:  {{^}}  ret{{$}}
; ADW-LABEL: {{^}}.method {{.*}}static int32 'n_bitcast'() cil managed{{$}}
; ADW-NEXT:  {{^\{$}}
; ADW-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; ADW-NEXT:  {{^}}  call native int 'n'(){{$}}
; ADW-NEXT:  {{^}}  conv.i4{{$}}
; ADW-NEXT:  {{^}}  ret{{$}}

; IRF-LABEL:  define {{.*}}i32 @f_bitcast(i32 %0, i32 %1)
; IRF:        call i32 @f(i32 %0)
; IRTWO-LABEL: define {{.*}}i32 @two_bitcast(i32 %0)
; IRTWO:      call i32 @two(i32 %0, i32 0)
; IRH-LABEL:  define {{.*}}i32 @h_bitcast(i64 %0)
; IRH:        [[T:%[0-9a-z.]+]] = trunc i64 %0 to i32
; IRH:        call i32 @h(i32 [[T]])
; IRW-LABEL:  define {{.*}}i64 @w_bitcast(i32 %0)
; IRW:        [[S:%[0-9a-z.]+]] = sext i32 %0 to i64
; IRW:        call i64 @w(i64 [[S]])
; IRV-LABEL:  define {{.*}}i32 @v_bitcast()
; IRV:        call void @v()
; IRV-NEXT:   ret i32 0
; IRK-LABEL:  define {{.*}}void @k_bitcast()
; IRK:        call i32 @k()
; IRK-NEXT:   ret void
; IRN-LABEL:  define {{.*}}i32 @n_bitcast()
; IRN:        [[R:%[0-9a-z.]+]] = call i64 @n()
; IRN:        trunc i64 [[R]] to i32
; Narrow arguments: first the int32 of the call site's attribute (zext for
; zeroext and for none, sext for signext), then conv.i for a native int
; parameter; never an i8 extended straight to i64 (the 'not grep' lines).
; IRWZ-LABEL: define {{.*}}i64 @wz_bitcast(i8 {{.*}}%0)
; IRWZ-NOT:   {{^ *}}define
; IRWZ:       [[A:%[0-9a-z.]+]] = zext i8 %0 to i32
; IRWZ-NEXT:  [[B:%[0-9a-z.]+]] = sext i32 [[A]] to i64
; IRWZ-NEXT:  call i64 @wz(i64 [[B]])
; IRWS-LABEL: define {{.*}}i64 @ws_bitcast(i8 {{.*}}%0)
; IRWS-NOT:   {{^ *}}define
; IRWS:       [[A:%[0-9a-z.]+]] = sext i8 %0 to i32
; IRWS-NEXT:  [[B:%[0-9a-z.]+]] = sext i32 [[A]] to i64
; IRWS-NEXT:  call i64 @ws(i64 [[B]])
; IRWN-LABEL: define {{.*}}i64 @wn_bitcast(i8 {{.*}}%0)
; IRWN-NOT:   {{^ *}}define
; IRWN:       [[A:%[0-9a-z.]+]] = zext i8 %0 to i32
; IRWN-NEXT:  [[B:%[0-9a-z.]+]] = sext i32 [[A]] to i64
; IRWN-NEXT:  call i64 @wn(i64 [[B]])
; IRHZ-LABEL: define {{.*}}i32 @hz_bitcast(i8 {{.*}}%0)
; IRHZ-NOT:   {{^ *}}define
; IRHZ:       [[A:%[0-9a-z.]+]] = zext i8 %0 to i32
; IRHZ-NEXT:  call i32 @hz(i32 [[A]])
; IRHS-LABEL: define {{.*}}i32 @hs_bitcast(i8 {{.*}}%0)
; IRHS-NOT:   {{^ *}}define
; IRHS:       [[A:%[0-9a-z.]+]] = sext i8 %0 to i32
; IRHS-NEXT:  call i32 @hs(i32 [[A]])
; IRHN-LABEL: define {{.*}}i32 @hn_bitcast(i8 {{.*}}%0)
; IRHN-NOT:   {{^ *}}define
; IRHN:       [[A:%[0-9a-z.]+]] = zext i8 %0 to i32
; IRHN-NEXT:  call i32 @hn(i32 [[A]])

; Phase 1, without the flag.
; P1-LABEL: {{^}}.method public static int32 'main'() cil managed{{$}}
; P1:       {{^}}  call int32 'f_bitcast'(int32, int32){{$}}
; P1-LABEL: {{^}}.method public static int32 'call_two'(int32) cil managed{{$}}
; P1:       {{^}}  call int32 'two_bitcast'(int32){{$}}
; P1-LABEL: {{^}}.method public static int32 'call_h'(native int) cil managed{{$}}
; P1:       {{^}}  call int32 'h_bitcast_invalid'(native int){{$}}
; P1-LABEL: {{^}}.method public static native int 'call_w'(int32) cil managed{{$}}
; P1:       {{^}}  call native int 'w_bitcast_invalid'(int32){{$}}
; P1-LABEL: {{^}}.method public static int32 'call_v'() cil managed{{$}}
; P1:       {{^}}  call int32 'v_bitcast'(){{$}}
; P1-LABEL: {{^}}.method public static void 'call_k'() cil managed{{$}}
; P1:       {{^}}  call void 'k_bitcast'(){{$}}
; P1-LABEL: {{^}}.method public static int32 'call_n'() cil managed{{$}}
; P1:       {{^}}  call int32 'n_bitcast_invalid'(){{$}}
; P1-LABEL: {{^}}.method public static native int 'call_wz'() cil managed{{$}}
; P1:       {{^}}  call native int 'wz_bitcast_invalid'(int32){{$}}
; P1-LABEL: {{^}}.method public static native int 'call_ws'() cil managed{{$}}
; P1:       {{^}}  call native int 'ws_bitcast_invalid'(int32){{$}}
; P1-LABEL: {{^}}.method public static native int 'call_wn'() cil managed{{$}}
; P1:       {{^}}  call native int 'wn_bitcast_invalid'(int32){{$}}
; P1-LABEL: {{^}}.method public static int32 'call_hz'() cil managed{{$}}
; P1:       {{^}}  call int32 'hz_bitcast_invalid'(int32){{$}}
; P1-LABEL: {{^}}.method public static int32 'call_hs'() cil managed{{$}}
; P1:       {{^}}  call int32 'hs_bitcast_invalid'(int32){{$}}
; P1-LABEL: {{^}}.method public static int32 'call_hn'() cil managed{{$}}
; P1:       {{^}}  call int32 'hn_bitcast_invalid'(int32){{$}}

;--- errors.ll
; int versus double, float versus double, and int versus float, which
; Phase 1 bit-casts.
source_filename = "ld-temp.o"

define double @g(double %x) {
  ret double %x
}

define float @s(float %x) {
  ret float %x
}

define float @fl(float %x) {
  ret float %x
}

define i32 @use_g(i32 %x) {
  %r = call i32 @g(i32 %x)
  ret i32 %r
}

define double @use_s(double %x) {
  %r = call double @s(double %x)
  ret double %r
}

define i32 @use_fl(i32 %x) {
  %r = call i32 @fl(i32 %x)
  ret i32 %r
}

; EG:        {{^}}error: {{.*}}function signature mismatch: g{{$}}
; EG-NEXT:   {{^}}>>> defined as float64(float64){{$}}
; EG-NEXT:   {{^}}>>> called as int32(int32){{$}}
; EG-NEXT:   {{^}}>>> {{.+}}{{$}}
; ES:        {{^}}error: {{.*}}function signature mismatch: s{{$}}
; ES-NEXT:   {{^}}>>> defined as float32(float32){{$}}
; ES-NEXT:   {{^}}>>> called as float64(float64){{$}}
; ES-NEXT:   {{^}}>>> {{.+}}{{$}}
; EFL:       {{^}}error: {{.*}}function signature mismatch: fl{{$}}
; EFL-NEXT:  {{^}}>>> defined as float32(float32){{$}}
; EFL-NEXT:  {{^}}>>> called as int32(int32){{$}}
; EFL-NEXT:  {{^}}>>> {{.+}}{{$}}

; P1E-LABEL: {{^}}.method public static int32 'use_g'(int32) cil managed{{$}}
; P1E:       {{^}}  call int32 'g_bitcast_invalid'(int32){{$}}
; P1E-LABEL: {{^}}.method public static float64 'use_s'(float64) cil managed{{$}}
; P1E:       {{^}}  call float64 's_bitcast_invalid'(float64){{$}}
; P1E-LABEL: {{^}}.method public static int32 'use_fl'(int32) cil managed{{$}}
; P1E:       {{^}}  call int32 'fl_bitcast'(int32){{$}}

;--- narrow.ll
; unsigned char uc(void) { return 200; }, unsigned short us(void) { return
; 40000; } and signed char sc(void) { return -56; }, each called through an
; 'int f();' declaration of another TU.
source_filename = "ld-temp.o"

define zeroext i8 @uc() {
  ret i8 200
}

define zeroext i16 @us() {
  ret i16 40000
}

define signext i8 @sc() {
  ret i8 -56
}

define i32 @call_uc() {
  %r = call i32 @uc()
  ret i32 %r
}

define i32 @call_us() {
  %r = call i32 @us()
  ret i32 %r
}

define i32 @call_sc() {
  %r = call i32 @sc()
  ret i32 %r
}

; The definitions' CIL methods return int32; each wrapper returns the int32
; its definition returns, with no conv.i1/conv.i2 that would turn 200 into
; -56 or 40000 into -25536.
; NR-LABEL: {{^}}.method public static int32 'uc'() cil managed{{$}}
; NR-LABEL: {{^}}.method public static int32 'us'() cil managed{{$}}
; NR-LABEL: {{^}}.method public static int32 'sc'() cil managed{{$}}
; NR-LABEL: {{^}}.method public static int32 'call_uc'() cil managed{{$}}
; NR:       {{^}}  call int32 'uc_bitcast'(){{$}}
; NR-LABEL: {{^}}.method public static int32 'call_us'() cil managed{{$}}
; NR:       {{^}}  call int32 'us_bitcast'(){{$}}
; NR-LABEL: {{^}}.method public static int32 'call_sc'() cil managed{{$}}
; NR:       {{^}}  call int32 'sc_bitcast'(){{$}}
; NR-LABEL: {{^}}.method {{.*}}static int32 'uc_bitcast'() cil managed{{$}}
; NR-NEXT:  {{^\{$}}
; NR-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; NR-NEXT:  {{^}}  call int32 'uc'(){{$}}
; NR-NEXT:  {{^}}  ret{{$}}
; NR-LABEL: {{^}}.method {{.*}}static int32 'us_bitcast'() cil managed{{$}}
; NR-NEXT:  {{^\{$}}
; NR-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; NR-NEXT:  {{^}}  call int32 'us'(){{$}}
; NR-NEXT:  {{^}}  ret{{$}}
; NR-LABEL: {{^}}.method {{.*}}static int32 'sc_bitcast'() cil managed{{$}}
; NR-NEXT:  {{^\{$}}
; NR-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; NR-NEXT:  {{^}}  call int32 'sc'(){{$}}
; NR-NEXT:  {{^}}  ret{{$}}

; The wrappers: the call, then the extension the attribute names.
; IRNR-LABEL: define {{.*}}i32 @uc_bitcast()
; IRNR:       [[R:%[0-9a-z.]+]] = call {{.*}}i8 @uc()
; IRNR-NEXT:  [[E:%[0-9a-z.]+]] = zext i8 [[R]] to i32
; IRNR-NEXT:  ret i32 [[E]]
; IRNR-LABEL: define {{.*}}i32 @us_bitcast()
; IRNR:       [[R:%[0-9a-z.]+]] = call {{.*}}i16 @us()
; IRNR-NEXT:  [[E:%[0-9a-z.]+]] = zext i16 [[R]] to i32
; IRNR-NEXT:  ret i32 [[E]]
; IRNR-LABEL: define {{.*}}i32 @sc_bitcast()
; IRNR:       [[R:%[0-9a-z.]+]] = call {{.*}}i8 @sc()
; IRNR-NEXT:  [[E:%[0-9a-z.]+]] = sext i8 [[R]] to i32
; IRNR-NEXT:  ret i32 [[E]]

;--- narrow64.ll
; The same definitions called through 'long f();': int32 versus native int
; results, so the adapter converts the int32 with conv.i and warns.
source_filename = "ld-temp.o"

define zeroext i8 @ul() {
  ret i8 200
}

define signext i8 @sl() {
  ret i8 -56
}

define i64 @call_ul() {
  %r = call i64 @ul()
  ret i64 %r
}

define i64 @call_sl() {
  %r = call i64 @sl()
  ret i64 %r
}

; WUL:      {{^}}warning: {{.*}}function signature mismatch: ul{{$}}
; WUL-NEXT: {{^}}>>> defined as int32(){{$}}
; WUL-NEXT: {{^}}>>> called as native int(){{$}}
; WUL-NEXT: {{^}}>>> adapter: {{.+}}{{$}}
; WSL:      {{^}}warning: {{.*}}function signature mismatch: sl{{$}}
; WSL-NEXT: {{^}}>>> defined as int32(){{$}}
; WSL-NEXT: {{^}}>>> called as native int(){{$}}
; WSL-NEXT: {{^}}>>> adapter: {{.+}}{{$}}

; 200 stays 200: the zero-extended int32, then conv.i, which instruction
; selection prints as conv.u because the value is known non-negative; -56
; stays -56 through conv.i. No conv.i1 in between.
; NR64-LABEL: {{^}}.method public static native int 'call_ul'() cil managed{{$}}
; NR64:       {{^}}  call native int 'ul_bitcast'(){{$}}
; NR64-LABEL: {{^}}.method public static native int 'call_sl'() cil managed{{$}}
; NR64:       {{^}}  call native int 'sl_bitcast'(){{$}}
; NR64-LABEL: {{^}}.method {{.*}}static native int 'ul_bitcast'() cil managed{{$}}
; NR64-NEXT:  {{^\{$}}
; NR64-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; NR64-NEXT:  {{^}}  call int32 'ul'(){{$}}
; NR64-NEXT:  {{^}}  conv.{{i|u}}{{$}}
; NR64-NEXT:  {{^}}  ret{{$}}
; NR64-LABEL: {{^}}.method {{.*}}static native int 'sl_bitcast'() cil managed{{$}}
; NR64-NEXT:  {{^\{$}}
; NR64-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; NR64-NEXT:  {{^}}  call int32 'sl'(){{$}}
; NR64-NEXT:  {{^}}  conv.i{{$}}
; NR64-NEXT:  {{^}}  ret{{$}}

; IRUL-LABEL: define {{.*}}i64 @ul_bitcast()
; IRUL:       [[R:%[0-9a-z.]+]] = call {{.*}}i8 @ul()
; IRUL-NOT:   sext i8
; IRUL:       zext i8 [[R]] to {{i32|i64}}
; IRUL-NOT:   sext i8
; IRUL:       ret i64
; IRSL-LABEL: define {{.*}}i64 @sl_bitcast()
; IRSL:       [[R:%[0-9a-z.]+]] = call {{.*}}i8 @sl()
; IRSL-NOT:   zext
; IRSL:       sext i8 [[R]] to {{i32|i64}}
; IRSL-NOT:   zext
; IRSL:       ret i64

;--- narrowkey.ll
; 'int hzs(int c) { return c; }' and 'int hsz(int c) { return c; }', each
; called through 'int h(unsigned char)' (zeroext) in one TU and through
; 'int h(signed char)' (signext) in another; the merged module has the
; zeroext call of hzs first and the signext call of hsz first.
source_filename = "ld-temp.o"

define i32 @hzs(i32 %c) {
  ret i32 %c
}

define i32 @hsz(i32 %c) {
  ret i32 %c
}

define i32 @hzs_z() {
  %r = call i32 @hzs(i8 zeroext 200)
  ret i32 %r
}

define i32 @hzs_s() {
  %r = call i32 @hzs(i8 signext -56)
  ret i32 %r
}

define i32 @hsz_s() {
  %r = call i32 @hsz(i8 signext -56)
  ret i32 %r
}

define i32 @hsz_z() {
  %r = call i32 @hsz(i8 zeroext 200)
  ret i32 %r
}

; Each caller's wrapper (the IR prints i8 200 as -56): zext for the zeroext
; call, so 200 arrives as 200, and sext for the signext call, so -56 arrives
; as -56, then the call of the definition.
; NKZ:      {{^ *}}define i32 @[[CALLER]]()
; NKZ-NEXT: = call i32 @[[W:[^(]+]](i8 {{[a-z ]*}}-56)
; NKZ:      {{^ *}}define {{.*}}i32 @[[W]](i8 {{.*}}%0)
; NKZ-NOT:  {{^ *}}define
; NKZ:      [[A:%[0-9a-z.]+]] = zext i8 %0 to i32
; NKZ-NEXT: call i32 @[[DEF]](i32 [[A]])
; NKS:      {{^ *}}define i32 @[[CALLER]]()
; NKS-NEXT: = call i32 @[[W:[^(]+]](i8 {{[a-z ]*}}-56)
; NKS:      {{^ *}}define {{.*}}i32 @[[W]](i8 {{.*}}%0)
; NKS-NOT:  {{^ *}}define
; NKS:      [[A:%[0-9a-z.]+]] = sext i8 %0 to i32
; NKS-NEXT: call i32 @[[DEF]](i32 [[A]])

;--- byvalmix.ll
; b.c: 'long f(long *p, int x) { return p[2] + x; }' (twice, as fbp and fpb);
; a.c: 'struct S { long a, b, c, d; }; long f(struct S s);' calls f(s);
; c.c: 'long f(long *p);' calls f(arr). fbp's byval call comes first, fpb's
; plain call.
source_filename = "ld-temp.o"

%struct.S = type { i64, i64, i64, i64 }

@arr = global [4 x i64] [i64 5, i64 6, i64 7, i64 8], align 8

define i64 @fbp(ptr %p, i32 %x) {
  %q = getelementptr inbounds i64, ptr %p, i64 2
  %v = load i64, ptr %q, align 8
  %w = sext i32 %x to i64
  %r = add i64 %v, %w
  ret i64 %r
}

define i64 @fpb(ptr %p, i32 %x) {
  %q = getelementptr inbounds i64, ptr %p, i64 2
  %v = load i64, ptr %q, align 8
  %w = sext i32 %x to i64
  %r = add i64 %v, %w
  ret i64 %r
}

define i64 @fbp_b() {
  %s = alloca %struct.S, align 8
  store i64 3, ptr %s, align 8
  %r = call i64 @fbp(ptr byval(%struct.S) align 8 %s)
  ret i64 %r
}

define i64 @fbp_p() {
  %r = call i64 @fbp(ptr @arr)
  ret i64 %r
}

define i64 @fpb_p() {
  %r = call i64 @fpb(ptr @arr)
  ret i64 %r
}

define i64 @fpb_b() {
  %s = alloca %struct.S, align 8
  store i64 3, ptr %s, align 8
  %r = call i64 @fpb(ptr byval(%struct.S) align 8 %s)
  ret i64 %r
}

; The byval call: an error, with the object path's reason line.
; BME:      {{^}}error: {{.*}}function signature mismatch: [[NAME]]{{$}}
; BME-NEXT: {{^}}>>> defined as native int(native int, int32){{$}}
; BME-NEXT: {{^}}>>> called as native int(native int){{$}}
; BME-NEXT: {{^}}>>> the aggregates passed by value (byval) differ in position, size or alignment{{$}}
; The plain call: zero for the missing argument.
; BMW:      {{^}}warning: {{.*}}function signature mismatch: [[NAME]]{{$}}
; BMW-NEXT: {{^}}>>> defined as native int(native int, int32){{$}}
; BMW-NEXT: {{^}}>>> called as native int(native int){{$}}
; BMW-NEXT: {{^}}>>> adapter: {{.+}}{{$}}
; Exactly four diagnostics: one error and one warning per definition.
; BMN-COUNT-4: {{^}}{{error|warning}}: {{.*}}function signature mismatch: {{fbp|fpb}}{{$}}

;--- sret.ll
; def.c: 'struct big { long long a, b, c; }; struct big sr(int x)';
; call.c: 'void sr(void *out, int x);' calls sr(buf, 4). One IR type,
; 'void (ptr, i32)', with sret only at the definition.
source_filename = "ld-temp.o"

%struct.big = type { i64, i64, i64 }

define void @sr(ptr sret(%struct.big) align 8 %out, i32 %x) {
  %v = sext i32 %x to i64
  store i64 %v, ptr %out, align 8
  ret void
}

define i32 @use_sr() {
  %buf = alloca [3 x i64], align 8
  call void @sr(ptr %buf, i32 4)
  %v = load i64, ptr %buf, align 8
  %r = trunc i64 %v to i32
  ret i32 %r
}

; SRE:      {{^}}error: {{.*}}function signature mismatch: sr{{$}}
; SRE-NEXT: {{^}}>>> defined as void(native int, int32){{$}}
; SRE-NEXT: {{^}}>>> called as void(native int, int32){{$}}
; SRE-NEXT: {{^}}>>> only the definition passes an aggregate result through a hidden pointer (sret){{$}}

; P1SR-LABEL: {{^}}.method public static int32 'use_sr'() cil managed{{$}}
; P1SR:       {{^}}  call void 'sr'(native int, int32){{$}}

;--- byvalsize.ll
; def.c: 'struct b32 { long long a, b, c, d; }; int bv(struct b32 p)';
; call.c: 'struct b24 { long long a, b, c; }; int bv(struct b24 p);' calls
; bv(s). One IR type, 'i32 (ptr)', with byval of 32 bytes at the definition
; and of 24 bytes at the call.
source_filename = "ld-temp.o"

%struct.b24 = type { i64, i64, i64 }
%struct.b32 = type { i64, i64, i64, i64 }

define i32 @bv(ptr byval(%struct.b32) align 8 %p) {
  %d = getelementptr inbounds i64, ptr %p, i64 3
  %a = load i64, ptr %p, align 8
  %b = load i64, ptr %d, align 8
  %s = add i64 %a, %b
  %r = trunc i64 %s to i32
  ret i32 %r
}

define i32 @use_bv() {
  %s = alloca %struct.b24, align 8
  store i64 7, ptr %s, align 8
  %r = call i32 @bv(ptr byval(%struct.b24) align 8 %s)
  ret i32 %r
}

; BVE:      {{^}}error: {{.*}}function signature mismatch: bv{{$}}
; BVE-NEXT: {{^}}>>> defined as int32(native int){{$}}
; BVE-NEXT: {{^}}>>> called as int32(native int){{$}}
; BVE-NEXT: {{^}}>>> the aggregates passed by value (byval) differ in position, size or alignment{{$}}

; P1BV-LABEL: {{^}}.method public static int32 'use_bv'() cil managed{{$}}
; P1BV:       {{^}}  call int32 'bv'(native int){{$}}

;--- same.ll
; The negatives: each call has the definition's IR type and attributes.
source_filename = "ld-temp.o"

%struct.big = type { i64, i64, i64 }
%struct.b32 = type { i64, i64, i64, i64 }

define void @sr_ok(ptr sret(%struct.big) align 8 %out, i32 %x) {
  %v = sext i32 %x to i64
  store i64 %v, ptr %out, align 8
  ret void
}

define i32 @bv_ok(ptr byval(%struct.b32) align 8 %p) {
  %a = load i64, ptr %p, align 8
  %r = trunc i64 %a to i32
  ret i32 %r
}

define i32 @n_ok(i8 zeroext %c) {
  %r = zext i8 %c to i32
  ret i32 %r
}

define i32 @use_ok() {
  %buf = alloca %struct.big, align 8
  %s = alloca %struct.b32, align 8
  store i64 1, ptr %s, align 8
  call void @sr_ok(ptr sret(%struct.big) align 8 %buf, i32 4)
  %v = load i64, ptr %buf, align 8
  %t = trunc i64 %v to i32
  %b = call i32 @bv_ok(ptr byval(%struct.b32) align 8 %s)
  %n = call i32 @n_ok(i8 zeroext 200)
  %r1 = add i32 %t, %b
  %r = add i32 %r1, %n
  ret i32 %r
}

; SAMEOK-LABEL: {{^}}.method public static int32 'use_ok'() cil managed{{$}}
; SAMEOK:       {{^}}  call void 'sr_ok'(native int, int32){{$}}
; SAMEOK:       {{^}}  call int32 'bv_ok'(native int){{$}}
; SAMEOK:       {{^}}  call int32 'n_ok'(int32){{$}}
