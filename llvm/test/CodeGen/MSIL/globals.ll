; Global variables (ImplementationPlan.md M8, decision D12, plan 5.1; recon
; asmprinter.md 7 and 9): every defined global lives in one unmanaged data
; segment that <Module>::.cctor allocates and fills, and code reaches a global
; only through its own 'initonly native int' address field ('ldsfld native int
; '<name>''), never through the RVA blob. The trailer comes after every method,
; in this order: the image value type ('.pack 1', '.size <initialized
; bytes>'), the 'bytearray' blob (16 upper-case hex bytes per line, the last
; line closed by ')'), the RVA field over it, the private '<msil>data' base
; field, one address field per defined global (public for non-local linkage,
; hidden visibility included; assembly for internal and private), then the
; .cctor. Initialized globals are laid out in IR order, each at its alignment
; (zero padding), and their bytes are exactly what the AsmPrinter emits for
; the initializer: little-endian integers (i1 as one byte, i128 in 16 bytes),
; IEEE half, float and double, struct padding and tail padding, vector
; elements. The zero-initialized globals follow the image (bss.ll), and here
; come after the initialized ones in IR order as well, so the fields and the
; address stores are in IR order. The segment is aligned to 16. llvm.used,
; llvm.compiler.used and globals in section "llvm.metadata" are not data. The
; .cctor allocates TotalSize + MaxAlign - 1 bytes with Marshal::AllocHGlobal,
; aligns the base up, copies the image with cpblk from ldsflda of the RVA field
; (the only use of that field: RVA memory is never written, and '&(label)' data
; is never emitted), clears the rest with initblk and stores each address
; field (no 'ldc.i4; add' for offset 0); constants may use any ldc.i4 encoding,
; and a line may end in a comment. The zero tail starts at Base + 83, an odd
; address, so its initblk has the prefix 'unaligned. 1' (plan section 12, 'M8.c
; amended (alignment prefixes in <Module>::.cctor)'; bss.ll and
; global-relocs.ll have the other cases). Function bodies are exact at -O2; at
; -O0 (no reordering in MSILRegStackify) only their key instructions are
; checked. No GNU directive leaks, no other field or P/Invoke exists, and
; every module assembles.
;
; Only llvm.used, llvm.compiler.used and globals in section "llvm.metadata"
; are skipped (and llvm.global_ctors, global-ctors.ll): a user global whose
; name merely starts with 'llvm.' (a C asm label such as
; 'int foo asm("llvm.foo")') is program data like any other, with its field,
; its bytes and its address store, and a thread_local one is reported like
; any other thread_local global (llvmname.ll, llvmname-tls.ll).
;
; The error parts: an external global variable that emitted code or data
; refers to is an error ("external global variable '<name>' is not supported
; (phase 2 linker)", M8.c), once per global, also for a use in an
; initializer; an extern_weak one is not, since M12 (decision D26 amended,
; extern-weak.ll): whole-program mode makes it null in every use and erases
; it, so its reader loads through a null pointer; one that is never used is
; not an error either, and neither is one that only the body of an
; available_externally function uses, since that body is never emitted
; (availext.ll: llc exits with 0, and there is no field for it). A
; thread_local global is reported by MSILPrepareIR (M2) and becomes an
; ordinary global, so its uses compile to its address field and the module
; still assembles. Code that reads a global in section "llvm.metadata", which
; has no field, is an error naming the function and the global by their IL
; names (plan section 12, 'M8.a and M8.c amended (IL names of `<Module>`
; members)' part (3)): "function '<f>' refers to '<x>', which is not program
; data", once per function and global (metaread.ll: main reads x twice, peek
; once, so two errors).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=".field " --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=int64 --implicit-check-not="&(" --implicit-check-not="'<msil>image'" \
; DEFINE:   --implicit-check-not="{{^[[:space:]]*\.(text|section|globl|weak|p2align|align|type|file|ident|addrsig|bss|comm|lcomm|zero|byte|short|long|quad|ascii|asciz|data$)}}"
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o2.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O2,TRAILER --input-file=%t/main.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o2.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O2,TRAILER --input-file=%t/main.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O0,TRAILER --input-file=%t/main.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/main.ll \
; RUN:   -o %t/main.win.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,O0,TRAILER --input-file=%t/main.win.o0.il
; With comments (the default -asm-verbose) the same fields and image are
; emitted, and the module still assembles.
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/main.ll -o %t/main.verbose.il
; RUN: FileCheck %s --check-prefix=VERBOSE --input-file=%t/main.verbose.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.lnx.o2.dll %t/main.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.win.o2.dll %t/main.win.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.lnx.o0.dll %t/main.lnx.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.win.o0.dll %t/main.win.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/main.verbose.dll %t/main.verbose.il %}

; External global variables: exactly one error per used declaration.
; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/extern.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=EXTERN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/extern.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=EXTERN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/extern.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=EXTERN
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/extern.ll \
; RUN:   > %t/extern.lnx.il
; RUN: FileCheck %s --check-prefix=EXTERN-IL --input-file=%t/extern.lnx.il --implicit-check-not=ext_weak
; An external global referenced only from an initializer is an error as well.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/extern-data.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=EXTDATA --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/extern-data.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=EXTDATA --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"

; thread_local: MSILPrepareIR's error, then an ordinary global.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/tls.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=TLS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/tls.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=TLS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/tls.ll > %t/tls.il
; RUN: FileCheck %s --check-prefix=TLS-IL --input-file=%t/tls.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/tls.dll %t/tls.il %}

; A user global named llvm.*: program data.
; DEFINE: %{llvmname} = FileCheck %s --check-prefix=LLVMNAME --implicit-check-not=".field " \
; DEFINE:   --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/llvmname.ll \
; RUN:   -o %t/llvmname.lnx.il 2>&1 | count 0
; RUN: %{llvmname} --input-file=%t/llvmname.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/llvmname.ll \
; RUN:   -o %t/llvmname.win.il 2>&1 | count 0
; RUN: %{llvmname} --input-file=%t/llvmname.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/llvmname.ll \
; RUN:   -o %t/llvmname.win.o0.il 2>&1 | count 0
; RUN: %{llvmname} --input-file=%t/llvmname.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/llvmname.lnx.dll %t/llvmname.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/llvmname.win.dll %t/llvmname.win.il %}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/llvmname-tls.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LLVMTLS
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/llvmname-tls.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=LLVMTLS

; An external global that only an unemitted available_externally body uses.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/availext.ll \
; RUN:   -o %t/availext.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=AVAILEXT --implicit-check-not=".field " --implicit-check-not="'g'" \
; RUN:   --implicit-check-not="'f'" --input-file=%t/availext.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/availext.ll \
; RUN:   -o %t/availext.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=AVAILEXT --implicit-check-not=".field " --implicit-check-not="'g'" \
; RUN:   --implicit-check-not="'f'" --input-file=%t/availext.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/availext.ll \
; RUN:   -o %t/availext.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=AVAILEXT --implicit-check-not=".field " --implicit-check-not="'g'" \
; RUN:   --implicit-check-not="'f'" --input-file=%t/availext.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/availext.lnx.dll %t/availext.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/availext.win.dll %t/availext.win.il %}

; Code that reads a global in section "llvm.metadata": the two errors are
; matched as a group, and a second FileCheck run counts them.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/metaread.ll -o /dev/null \
; RUN:   2> %t/metaread.lnx.err
; RUN: %{diag} --check-prefix=METAREAD --input-file=%t/metaread.lnx.err
; RUN: FileCheck %s --check-prefix=NMETAREAD --input-file=%t/metaread.lnx.err
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/metaread.ll -o /dev/null \
; RUN:   2> %t/metaread.win.err
; RUN: %{diag} --check-prefix=METAREAD --input-file=%t/metaread.win.err
; RUN: FileCheck %s --check-prefix=NMETAREAD --input-file=%t/metaread.win.err
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/metaread.ll -o /dev/null \
; RUN:   2> %t/metaread.win.o0.err
; RUN: %{diag} --check-prefix=METAREAD --input-file=%t/metaread.win.o0.err
; RUN: FileCheck %s --check-prefix=NMETAREAD --input-file=%t/metaread.win.o0.err

;--- main.ll
source_filename = "globals.c"

@counter = global i32 7, align 4
@limit = internal global i64 -2, align 8
@pi = constant double 2.500000e+00, align 8
@bytes = private unnamed_addr constant [3 x i8] c"\01\02\03", align 1
@ratio = global float 1.500000e+00, align 4
@hidden16 = hidden global i16 513, align 2
@local8 = internal global i8 -1, align 1
; Padding after the i8 and after the i16 (tail padding: the struct takes 12
; bytes).
@padded = constant { i8, i32, i16 } { i8 1, i32 2, i16 3 }, align 4
@vec = global <4 x i16> <i16 1, i16 2, i16 3, i16 4>, align 8
; 0x0F0E0D0C0B0A09080706050403020100: bytes 00 to 0F in memory order.
@wide = global i128 20011376718272490338853433276725592320, align 16
@half1 = global half 0xH3C00, align 2
@flag = global i1 true, align 1
@zeroed = global i32 0, align 4
@izero = internal global [2 x i64] zeroinitializer, align 8

; Not data: no field, no bytes.
@llvm.used = appending global [1 x ptr] [ptr @counter], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @limit], section "llvm.metadata"
@.annotation = private unnamed_addr constant [5 x i8] c"note\00", section "llvm.metadata"

; CHECK-LABEL: {{^}}.method public static int32 'get_counter'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'counter'{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'counter'{{$}}
; O0:          {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}} // end of method 'get_counter'{{$}}
define i32 @get_counter() {
  %v = load i32, ptr @counter, align 4
  ret i32 %v
}

; CHECK-LABEL: {{^}}.method public static void 'set_limit'(native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'limit'{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  stind.i{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'limit'{{$}}
; O0:          {{^}}  stind.i{{$}}
; CHECK:       {{^}}} // end of method 'set_limit'{{$}}
define void @set_limit(i64 %v) {
  store i64 %v, ptr @limit, align 8
  ret void
}

; CHECK-LABEL: {{^}}.method public static float64 'get_pi'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'pi'{{$}}
; O2-NEXT:     {{^}}  ldind.r8{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'pi'{{$}}
; O0:          {{^}}  ldind.r8{{$}}
; CHECK:       {{^}}} // end of method 'get_pi'{{$}}
define double @get_pi() {
  %v = load double, ptr @pi, align 8
  ret double %v
}

; The address of a global is its field's value.
; CHECK-LABEL: {{^}}.method public static native int 'bytes_addr'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'bytes'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'bytes_addr'{{$}}
define ptr @bytes_addr() {
  ret ptr @bytes
}

; CHECK-LABEL: {{^}}.method public static float32 'get_ratio'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'ratio'{{$}}
; O2-NEXT:     {{^}}  ldind.r4{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'ratio'{{$}}
; O0:          {{^}}  ldind.r4{{$}}
; CHECK:       {{^}}} // end of method 'get_ratio'{{$}}
define float @get_ratio() {
  %v = load float, ptr @ratio, align 4
  ret float %v
}

; CHECK-LABEL: {{^}}.method public static int32 'get_hidden16'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'hidden16'{{$}}
; O2-NEXT:     {{^}}  ldind.i2{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'hidden16'{{$}}
; O0:          {{^}}  ldind.i2{{$}}
; CHECK:       {{^}}} // end of method 'get_hidden16'{{$}}
define i32 @get_hidden16() {
  %v = load i16, ptr @hidden16, align 2
  %w = sext i16 %v to i32
  ret i32 %w
}

; CHECK-LABEL: {{^}}.method public static void 'set_local8'(int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'local8'{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  stind.i1{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'local8'{{$}}
; O0:          {{^}}  stind.i1{{$}}
; CHECK:       {{^}}} // end of method 'set_local8'{{$}}
define void @set_local8(i32 %v) {
  %t = trunc i32 %v to i8
  store i8 %t, ptr @local8, align 1
  ret void
}

; CHECK-LABEL: {{^}}.method public static native int 'wide_addr'() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int 'wide'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'wide_addr'{{$}}
define ptr @wide_addr() {
  ret ptr @wide
}

; CHECK-LABEL: {{^}}.method public static int32 'read_flag'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'flag'{{$}}
; O2-NEXT:     {{^}}  ldind.u1{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'flag'{{$}}
; O0:          {{^}}  ldind.u1{{$}}
; CHECK:       {{^}}} // end of method 'read_flag'{{$}}
define i32 @read_flag() {
  %v = load i8, ptr @flag, align 1
  %w = zext i8 %v to i32
  ret i32 %w
}

; A volatile read keeps its prefix (M7) and reads through the field.
; CHECK-LABEL: {{^}}.method public static int32 'get_zeroed_volatile'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'zeroed'{{$}}
; O2-NEXT:     {{^}}  volatile.{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'zeroed'{{$}}
; O0:          {{^}}  volatile.{{$}}
; O0-NEXT:     {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}} // end of method 'get_zeroed_volatile'{{$}}
define i32 @get_zeroed_volatile() {
  %v = load volatile i32, ptr @zeroed, align 4
  ret i32 %v
}

; The second element of a zero-initialized internal array: the constant offset
; is folded into the address (global-offsets.ll has the other offsets).
; CHECK-LABEL: {{^}}.method public static native int 'izero_elem1'() cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack {{[0-9]+}}{{$}}
; O2-NEXT:     {{^}}  ldsfld native int 'izero'{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4(\.| |\.s )8$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldind.i{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldsfld native int 'izero'{{$}}
; O0:          {{^}}  ldind.i{{$}}
; CHECK:       {{^}}} // end of method 'izero_elem1'{{$}}
define i64 @izero_elem1() {
  %p = getelementptr inbounds [2 x i64], ptr @izero, i64 0, i64 1
  %v = load i64, ptr %p, align 8
  ret i64 %v
}

; The trailer. 83 initialized bytes: counter +0, limit +8, pi +16, bytes
; +24, ratio +28, hidden16 +32, local8 +34, padded +36, vec +48, wide +64,
; half1 +80, flag +82; then zeroed +84 and izero +88 in the zero tail, so
; TotalSize is 104 and AllocHGlobal gets 104 + 16 - 1 = 119.
; TRAILER-NOT:   {{^}}.method
; TRAILER:       {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; TRAILER:       {{^ *}}.pack 1{{( |$)}}
; TRAILER:       {{^ *}}.size 83{{( |$)}}
; TRAILER:       {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; TRAILER-NEXT:  {{^}}  07 00 00 00 00 00 00 00 FE FF FF FF FF FF FF FF{{( |$)}}
; TRAILER-NEXT:  {{^}}  00 00 00 00 00 00 04 40 01 02 03 00 00 00 C0 3F{{( |$)}}
; TRAILER-NEXT:  {{^}}  01 02 FF 00 01 00 00 00 02 00 00 00 03 00 00 00{{( |$)}}
; TRAILER-NEXT:  {{^}}  01 00 02 00 03 00 04 00 00 00 00 00 00 00 00 00{{( |$)}}
; TRAILER-NEXT:  {{^}}  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F{{( |$)}}
; TRAILER-NEXT:  {{^}}  00 3C 01 ){{( |$)}}
; TRAILER:       {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; TRAILER:       {{^}}.field private static initonly native int '<msil>data'{{$}}
; TRAILER:       {{^}}.field public static initonly native int 'counter'{{( |$)}}
; TRAILER:       {{^}}.field assembly static initonly native int 'limit'{{( |$)}}
; TRAILER:       {{^}}.field public static initonly native int 'pi'{{( |$)}}
; TRAILER:       {{^}}.field assembly static initonly native int 'bytes'{{( |$)}}
; TRAILER:       {{^}}.field public static initonly native int 'ratio'{{( |$)}}
; TRAILER:       {{^}}.field public static initonly native int 'hidden16'{{( |$)}}
; TRAILER:       {{^}}.field assembly static initonly native int 'local8'{{( |$)}}
; TRAILER:       {{^}}.field public static initonly native int 'padded'{{( |$)}}
; TRAILER:       {{^}}.field public static initonly native int 'vec'{{( |$)}}
; TRAILER:       {{^}}.field public static initonly native int 'wide'{{( |$)}}
; TRAILER:       {{^}}.field public static initonly native int 'half1'{{( |$)}}
; TRAILER:       {{^}}.field public static initonly native int 'flag'{{( |$)}}
; TRAILER:       {{^}}.field public static initonly native int 'zeroed'{{( |$)}}
; TRAILER:       {{^}}.field assembly static initonly native int 'izero'{{( |$)}}
; TRAILER:       {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; TRAILER-NEXT:  {{^\{$}}
; TRAILER-NEXT:  {{^}}  .maxstack {{[3-9]}}{{$}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 119( |$)}}
; TRAILER-NEXT:  {{^}}  conv.i{{( |$)}}
; TRAILER-NEXT:  {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 15( |$)}}
; TRAILER-NEXT:  {{^}}  conv.i{{( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? -16( |$)}}
; TRAILER-NEXT:  {{^}}  conv.i{{( |$)}}
; TRAILER-NEXT:  {{^}}  and{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsflda valuetype '<msil>image_t' '<msil>image'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 83( |$)}}
; TRAILER-NEXT:  {{^}}  cpblk{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 83( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )0( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 21( |$)}}
; TRAILER-NEXT:  {{^}}  unaligned. 1{{( |$)}}
; TRAILER-NEXT:  {{^}}  initblk{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'counter'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.| |\.s )8( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'limit'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 16( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'pi'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 24( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'bytes'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 28( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'ratio'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 32( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'hidden16'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 34( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'local8'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 36( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'padded'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 48( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'vec'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 64( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'wide'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 80( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'half1'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 82( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'flag'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 84( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'zeroed'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; TRAILER-NEXT:  {{^}}  {{ldc\.i4(\.s)? 88( |$)}}
; TRAILER-NEXT:  {{^}}  add{{( |$)}}
; TRAILER-NEXT:  {{^}}  stsfld native int 'izero'{{( |$)}}
; TRAILER-NEXT:  {{^}}  ret{{( |$)}}
; TRAILER-NEXT:  {{^}}} // end of method '.cctor'{{$}}
; TRAILER-NOT:   {{^}}.method

; VERBOSE:       {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; VERBOSE:       {{^ *}}.size 83{{( |$)}}
; VERBOSE:       {{^}}.data cil '<msil>image_d' = bytearray (
; VERBOSE:       {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'
; VERBOSE:       {{^}}.field private static initonly native int '<msil>data'
; VERBOSE:       {{^}}.field public static initonly native int 'counter'{{( |$)}}
; VERBOSE:       {{^}}.field assembly static initonly native int 'izero'{{( |$)}}
; VERBOSE:       {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed
; VERBOSE:       {{^}}  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
; VERBOSE:       {{^}}  cpblk
; VERBOSE:       {{^}}  initblk
; VERBOSE:       {{^}}  stsfld native int 'izero'

;--- extern.ll
; Two used declarations (one extern_weak) and one that is never used. Since
; M12 the extern_weak one is null, so ext_counter's is the only error (any
; other error fails the check); the functions that use them still compile.
source_filename = "extern.c"

; EXTERN: {{^}}error: {{.*}}external global variable 'ext_counter' is not supported (phase 2 linker){{$}}

; EXTERN-IL-LABEL: {{^}}.method public static native int 'read_weak'() cil managed{{$}}
; EXTERN-IL-NEXT:  {{^\{$}}
; EXTERN-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; EXTERN-IL-NEXT:  {{^}}  ldc.i4.0{{$}}
; EXTERN-IL-NEXT:  {{^}}  conv.i{{$}}
; EXTERN-IL-NEXT:  {{^}}  ldind.i{{$}}
; EXTERN-IL-NEXT:  {{^}}  ret{{$}}

@ext_counter = external global i32, align 4
@ext_unused = external global i32, align 4
@ext_weak = extern_weak global i64, align 8

define i32 @read_ext() {
  %v = load i32, ptr @ext_counter, align 4
  ret i32 %v
}

define i64 @read_weak() {
  %v = load i64, ptr @ext_weak, align 8
  ret i64 %v
}

;--- extern-data.ll
; int *p = &ext_counter;
source_filename = "extern-data.c"

; EXTDATA: error: {{.*}}external global variable 'ext_counter' is not supported (phase 2 linker){{$}}

@ext_counter = external global i32, align 4
@p = global ptr @ext_counter, align 8

;--- tls.ll
; A thread_local variable read through llvm.threadlocal.address (as clang
; emits it) and referenced from an initializer.
source_filename = "tls.c"

; TLS:        {{^}}error: thread-local variable @tv is not supported for MSIL yet{{$}}

; TLS-IL-LABEL: .method {{.*}}'read_tv'(
; TLS-IL:       {{^}}  ldsfld native int 'tv'{{$}}
; TLS-IL-NEXT:  {{^}}  ldind.i4{{$}}
; TLS-IL:       {{^}}.field public static initonly native int 'tv'{{( |$)}}
; TLS-IL:       {{^}}.field public static initonly native int 'ptv'{{( |$)}}
; TLS-IL:       {{^}}  ldsfld native int 'tv'{{( |$)}}
; TLS-IL-NEXT:  {{^}}  stind.i{{( |$)}}

@tv = thread_local global i32 5, align 4
@ptv = global ptr @tv, align 8

define i32 @read_tv() {
  %p = call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @tv)
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

declare ptr @llvm.threadlocal.address.p0(ptr)

;--- llvmname.ll
; int foo asm("llvm.foo") = 5; and the globals that really are not data.
source_filename = "llvmname.c"

; LLVMNAME-LABEL: {{^}}.method public static int32 'read_foo'() cil managed{{$}}
; LLVMNAME:       {{^}}  ldsfld native int 'llvm.foo'{{$}}
; LLVMNAME-NEXT:  {{^}}  ldind.i4{{$}}
; LLVMNAME:       {{^}}} // end of method 'read_foo'{{$}}
; LLVMNAME-NOT:   {{^}}.method
; LLVMNAME:       {{^}}.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
; LLVMNAME:       {{^ *}}.size 4{{( |$)}}
; LLVMNAME:       {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; LLVMNAME-NEXT:  {{^}}  05 00 00 00 ){{( |$)}}
; LLVMNAME:       {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; LLVMNAME:       {{^}}.field private static initonly native int '<msil>data'{{$}}
; LLVMNAME:       {{^}}.field public static initonly native int 'llvm.foo'{{( |$)}}
; LLVMNAME:       {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; LLVMNAME:       {{^}}  cpblk{{( |$)}}
; LLVMNAME-NEXT:  {{^}}  ldsfld native int '<msil>data'{{( |$)}}
; LLVMNAME-NEXT:  {{^}}  stsfld native int 'llvm.foo'{{( |$)}}
; LLVMNAME-NEXT:  {{^}}  ret{{( |$)}}
; LLVMNAME-NEXT:  {{^}}} // end of method '.cctor'{{$}}

@llvm.foo = global i32 5, align 4
@llvm.used = appending global [1 x ptr] [ptr @llvm.foo], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @read_foo], section "llvm.metadata"
@meta = private unnamed_addr constant [5 x i8] c"note\00", section "llvm.metadata"

define i32 @read_foo() {
  %v = load i32, ptr @llvm.foo, align 4
  ret i32 %v
}

;--- llvmname-tls.ll
; _Thread_local int bar asm("llvm.bar") = 1;
source_filename = "llvmname-tls.c"

; LLVMTLS: {{^}}error: thread-local variable @llvm.bar is not supported for MSIL yet{{$}}

@llvm.bar = thread_local global i32 1, align 4

define i32 @read_bar() {
  %p = call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @llvm.bar)
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

declare ptr @llvm.threadlocal.address.p0(ptr)

;--- availext.ll
; g is declared, and only the available_externally definition of f, which is
; never emitted, loads it; main does not use it.
source_filename = "availext.c"

; AVAILEXT-LABEL: {{^}}.method public static int32 'main'() cil managed{{$}}
; AVAILEXT:       {{^}}} // end of method 'main'{{$}}

@g = external global i32, align 4

define available_externally i32 @f() {
  %v = load i32, ptr @g, align 4
  ret i32 %v
}

define i32 @main() {
  ret i32 0
}

;--- metaread.ll
; int x __attribute__((section("llvm.metadata"))) = 1; read in code: main
; reads it twice, peek once.
source_filename = "metaread.c"

; METAREAD-DAG: {{^}}error: {{.*}}function 'main' refers to 'x', which is not program data{{$}}
; METAREAD-DAG: {{^}}error: {{.*}}function 'peek' refers to 'x', which is not program data{{$}}
; NMETAREAD-COUNT-2: {{^}}error:
; NMETAREAD-NOT:     {{^}}error:

@x = global i32 1, section "llvm.metadata", align 4

define i32 @main() {
  %a = load volatile i32, ptr @x, align 4
  %b = load volatile i32, ptr @x, align 4
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @peek() {
  %v = load i32, ptr @x, align 4
  ret i32 %v
}
