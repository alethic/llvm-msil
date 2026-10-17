; The aarch64 MSIL triples (Design.md section 3 and 3.1; ImplementationPlan.md
; decisions D3 and D6): aarch64-unknown-linux-gnu-msil,
; aarch64-unknown-linux-musl-msil, aarch64-pc-windows-msvc-msil and
; aarch64-w64-windows-gnu-msil compile with their native data layouts and
; assemble.
;
; 1. The data layout llc gives IR that has none is the native triple's, byte
;    for byte (D3): -stop-after prints the module with it, and it equals the
;    layout LLVM computes for the native triple (opt with the native triple
;    and no target; this only needs the TargetParser, not an AArch64 backend)
;    as well as the literal strings that clang/test/CodeGen/MSIL/datalayout.c
;    pins for clang.
; 2. Code generation uses it. The aarch64 Linux layout prefers 32-bit
;    alignment for i8 and i16 ('i8:8:32-i16:16:32'), so globals without an
;    explicit alignment, i8 arrays included, are placed at multiples of 4 in
;    the data segment (D12), and the relocation that the module initializer
;    stores follows them. The aarch64 Windows layout has no such preference,
;    so its data segment is the x86_64 one.
; 3. Nothing else depends on the architecture: x64 and ARM64 Windows share
;    the LLP64 data model and every layout (Design.md 3.1), so the
;    aarch64-pc-windows-msvc-msil and aarch64-w64-windows-gnu-msil modules
;    are identical to the x86_64 ones except for the triple comment (diff),
;    and the aarch64 Linux methods and P/Invoke declarations (everything
;    before the data segment) are identical to the x86_64 Linux ones.
;    Pointers and i64 are native int on both architectures (D2), and the
;    C library is the triple's (D14).
; 4. Every aarch64 module above (the two -O0 ones included) assembles as an
;    ARM64 image: '-arm64' after the '-x64' of %ilasm-verify selects the
;    machine type, which Design.md 3 derives from the triple's CPU (the IL
;    text itself has no machine directive).

; DEFINE: %{llc} = llc -verify-machineinstrs
; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64
; DEFINE: %{notriple} = sed -e '/^\/\/ target triple   = /d'
; DEFINE: %{code} = sed -e '/^\/\/ target triple   = /d' -e '/^\/\/ ---- data segment/,$d'
; DEFINE: %{msildl} = sed -n -e 's/^  \(target datalayout = .*\)$/\1/p'

; Part 1: the data layout.
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -stop-after=finalize-isel < %s -o %t.a64-gnu.mir
; RUN: %{llc} -mtriple=aarch64-unknown-linux-musl-msil -stop-after=finalize-isel < %s -o %t.a64-musl.mir
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil -stop-after=finalize-isel < %s -o %t.a64-msvc.mir
; RUN: %{llc} -mtriple=aarch64-w64-windows-gnu-msil -stop-after=finalize-isel < %s -o %t.a64-mingw.mir
; RUN: FileCheck %s --check-prefix=DL-LNX -DTRIPLE=aarch64-unknown-linux-gnu-msil --input-file=%t.a64-gnu.mir
; RUN: FileCheck %s --check-prefix=DL-LNX -DTRIPLE=aarch64-unknown-linux-musl-msil --input-file=%t.a64-musl.mir
; RUN: FileCheck %s --check-prefix=DL-WIN -DTRIPLE=aarch64-pc-windows-msvc-msil --input-file=%t.a64-msvc.mir
; RUN: FileCheck %s --check-prefix=DL-WIN -DTRIPLE=aarch64-w64-windows-gnu-msil --input-file=%t.a64-mingw.mir
; RUN: opt -mtriple=aarch64-unknown-linux-gnu -S < %s | grep "^target datalayout = " > %t.a64-gnu.native
; RUN: opt -mtriple=aarch64-unknown-linux-musl -S < %s | grep "^target datalayout = " > %t.a64-musl.native
; RUN: opt -mtriple=aarch64-pc-windows-msvc -S < %s | grep "^target datalayout = " > %t.a64-msvc.native
; RUN: opt -mtriple=aarch64-w64-windows-gnu -S < %s | grep "^target datalayout = " > %t.a64-mingw.native
; RUN: %{msildl} %t.a64-gnu.mir > %t.a64-gnu.msil
; RUN: %{msildl} %t.a64-musl.mir > %t.a64-musl.msil
; RUN: %{msildl} %t.a64-msvc.mir > %t.a64-msvc.msil
; RUN: %{msildl} %t.a64-mingw.mir > %t.a64-mingw.msil
; RUN: diff %t.a64-gnu.native %t.a64-gnu.msil
; RUN: diff %t.a64-musl.native %t.a64-musl.msil
; RUN: diff %t.a64-msvc.native %t.a64-msvc.msil
; RUN: diff %t.a64-mingw.native %t.a64-mingw.msil

; DL-LNX:      {{^}}  target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"{{$}}
; DL-LNX-NEXT: {{^}}  target triple = "[[TRIPLE]]"{{$}}
; DL-WIN:      {{^}}  target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-p:64:64-i32:32-i64:64-i128:128-n32:64-S128-Fn32"{{$}}
; DL-WIN-NEXT: {{^}}  target triple = "[[TRIPLE]]"{{$}}

; Parts 2 and 3: the modules.
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil < %s -o %t.a64-gnu.il 2>&1 | count 0
; RUN: %{llc} -mtriple=aarch64-unknown-linux-musl-msil < %s -o %t.a64-musl.il 2>&1 | count 0
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil < %s -o %t.a64-msvc.il 2>&1 | count 0
; RUN: %{llc} -mtriple=aarch64-w64-windows-gnu-msil < %s -o %t.a64-mingw.il 2>&1 | count 0
; RUN: %{llc} -O0 -mtriple=aarch64-unknown-linux-gnu-msil < %s -o %t.a64-gnu.o0.il 2>&1 | count 0
; RUN: %{llc} -O0 -mtriple=aarch64-pc-windows-msvc-msil < %s -o %t.a64-msvc.o0.il 2>&1 | count 0
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.x64-gnu.il 2>&1 | count 0
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.x64-msvc.il 2>&1 | count 0
; RUN: %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %s -o %t.x64-mingw.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,A64LNX -DTRIPLE=aarch64-unknown-linux-gnu-msil -DCLIB=libc \
; RUN:   --input-file=%t.a64-gnu.il
; RUN: %{check} --check-prefixes=CHECK,A64LNX -DTRIPLE=aarch64-unknown-linux-musl-msil -DCLIB=libc \
; RUN:   --input-file=%t.a64-musl.il
; RUN: %{check} --check-prefixes=CHECK,NATURAL -DTRIPLE=aarch64-pc-windows-msvc-msil -DCLIB=ucrtbase \
; RUN:   --input-file=%t.a64-msvc.il
; RUN: %{check} --check-prefixes=CHECK,NATURAL -DTRIPLE=aarch64-w64-windows-gnu-msil -DCLIB=msvcrt \
; RUN:   --input-file=%t.a64-mingw.il
; RUN: %{check} --check-prefixes=CHECK,A64LNX -DTRIPLE=aarch64-unknown-linux-gnu-msil -DCLIB=libc \
; RUN:   --input-file=%t.a64-gnu.o0.il
; RUN: %{check} --check-prefixes=CHECK,NATURAL -DTRIPLE=aarch64-pc-windows-msvc-msil -DCLIB=ucrtbase \
; RUN:   --input-file=%t.a64-msvc.o0.il
; RUN: %{check} --check-prefixes=CHECK,NATURAL -DTRIPLE=x86_64-unknown-linux-gnu-msil -DCLIB=libc \
; RUN:   --input-file=%t.x64-gnu.il
; RUN: %{notriple} %t.a64-msvc.il > %t.a64-msvc.notriple
; RUN: %{notriple} %t.x64-msvc.il > %t.x64-msvc.notriple
; RUN: diff %t.x64-msvc.notriple %t.a64-msvc.notriple
; RUN: %{notriple} %t.a64-mingw.il > %t.a64-mingw.notriple
; RUN: %{notriple} %t.x64-mingw.il > %t.x64-mingw.notriple
; RUN: diff %t.x64-mingw.notriple %t.a64-mingw.notriple
; RUN: %{code} %t.a64-gnu.il > %t.a64-gnu.code
; RUN: %{code} %t.x64-gnu.il > %t.x64-gnu.code
; RUN: FileCheck %s --check-prefix=CODE --input-file=%t.a64-gnu.code
; RUN: diff %t.x64-gnu.code %t.a64-gnu.code

; Part 4: assembled as ARM64 images.
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-gnu.dll %t.a64-gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-musl.dll %t.a64-musl.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-msvc.dll %t.a64-msvc.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-mingw.dll %t.a64-mingw.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-gnu.o0.dll %t.a64-gnu.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.a64-msvc.o0.dll %t.a64-msvc.o0.il %}

; The code part that is compared with x86_64 ends with the P/Invoke
; declarations: the data segment and everything after it were cut off.
; CODE:     {{^}}// ---- undefined external functions: P/Invoke ----{{$}}
; CODE-NEXT: {{^}}.method public hidebysig static pinvokeimpl("libc" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CODE-NOT: {{.}}

; CHECK:      {{^}}// MSIL module generated by LLVM{{$}}
; CHECK-NEXT: {{^}}// source_filename = "aarch64-triples.c"{{$}}
; CHECK-NEXT: {{^}}// target triple   = "[[TRIPLE]]"{{$}}
; CHECK-NEXT: {{^}}.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }{{$}}
; CHECK-NEXT: {{^}}.assembly 'aarch64-triples' { .ver 0:0:0:0 }{{$}}

source_filename = "aarch64-triples.c"

@c1 = global i8 1
@c2 = global i8 2
@h = global i16 3
@w = global i32 4
@q = global i64 5
@msg = global [3 x i8] c"hi\00"
@p = global ptr @msg

declare i32 @puts(ptr)

; The code reads each global through its address field, whatever its offset.
; CHECK-LABEL: {{^}}.method public static int32 'sum_globals'() cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int 'c1'{{$}}
; CHECK-NEXT:  {{^}}  ldind.u1{{$}}
; CHECK:       {{^}}  ldsfld native int 'c2'{{$}}
; CHECK-NEXT:  {{^}}  ldind.u1{{$}}
; CHECK:       {{^}}  ldsfld native int 'h'{{$}}
; CHECK-NEXT:  {{^}}  ldind.i2{{$}}
; CHECK:       {{^}}  ldsfld native int 'w'{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}} // end of method 'sum_globals'{{$}}
define i32 @sum_globals() {
  %a = load i8, ptr @c1
  %b = load i8, ptr @c2
  %c = load i16, ptr @h
  %d = load i32, ptr @w
  %a32 = zext i8 %a to i32
  %b32 = zext i8 %b to i32
  %c32 = sext i16 %c to i32
  %s1 = add i32 %a32, %b32
  %s2 = add i32 %s1, %c32
  %s3 = add i32 %s2, %d
  ret i32 %s3
}

; An i64 is a native int (D2) on both architectures.
; CHECK-LABEL: {{^}}.method public static native int 'get_q'() cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int 'q'{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'get_q'{{$}}
define i64 @get_q() {
  %e = load i64, ptr @q
  ret i64 %e
}

; CHECK-LABEL: {{^}}.method public static int32 'main'() cil managed{{$}}
; CHECK:       {{^}}  ldsfld native int 'p'{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; CHECK:       {{^}}  call int32 'sum_globals'(){{$}}
; CHECK:       {{^}}  call native int 'get_q'(){{$}}
; CHECK:       {{^}}} // end of method 'main'{{$}}
define i32 @main() {
  %m = load ptr, ptr @p
  %r = call i32 @puts(ptr %m)
  %s = call i32 @sum_globals()
  %q = call i64 @get_q()
  %q32 = trunc i64 %q to i32
  %t = add i32 %s, %q32
  ret i32 %t
}

; CHECK-LABEL: {{^}}// ---- undefined external functions: P/Invoke ----{{$}}
; CHECK-NEXT:  {{^}}.method public hidebysig static pinvokeimpl("[[CLIB]]" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}

; aarch64 Linux: c2, h and the i8 array msg start at multiples of 4; q and p
; keep their 8-byte alignment.
; A64LNX:      {{^}}// ---- data segment: 40 bytes (40 initialized + 0 zero), align {{[0-9]+}} ----{{$}}
; A64LNX:      {{^}}  .size 40{{$}}
; A64LNX:      {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; A64LNX-NEXT: {{^}}  01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00   // 0x0000{{$}}
; A64LNX-NEXT: {{^}}  05 00 00 00 00 00 00 00 68 69 00 00 00 00 00 00   // 0x0010{{$}}
; A64LNX-NEXT: {{^}}  00 00 00 00 00 00 00 00 )                         // 0x0020{{$}}
; A64LNX-NEXT: {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; A64LNX-NEXT: {{^}}.field private static initonly native int '<msil>data'{{$}}
; A64LNX-NEXT: {{^}}.field public static initonly native int 'c1' // +0{{$}}
; A64LNX-NEXT: {{^}}.field public static initonly native int 'c2' // +4{{$}}
; A64LNX-NEXT: {{^}}.field public static initonly native int 'h' // +8{{$}}
; A64LNX-NEXT: {{^}}.field public static initonly native int 'w' // +12{{$}}
; A64LNX-NEXT: {{^}}.field public static initonly native int 'q' // +16{{$}}
; A64LNX-NEXT: {{^}}.field public static initonly native int 'msg' // +24{{$}}
; A64LNX-NEXT: {{^}}.field public static initonly native int 'p' // +32{{$}}

; x86_64 and aarch64 Windows: natural alignment, as on x86_64.
; NATURAL:      {{^}}// ---- data segment: 32 bytes (32 initialized + 0 zero), align {{[0-9]+}} ----{{$}}
; NATURAL:      {{^}}  .size 32{{$}}
; NATURAL:      {{^}}.data cil '<msil>image_d' = bytearray ({{$}}
; NATURAL-NEXT: {{^}}  01 02 03 00 04 00 00 00 05 00 00 00 00 00 00 00   // 0x0000{{$}}
; NATURAL-NEXT: {{^}}  68 69 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ) // 0x0010{{$}}
; NATURAL-NEXT: {{^}}.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'{{$}}
; NATURAL-NEXT: {{^}}.field private static initonly native int '<msil>data'{{$}}
; NATURAL-NEXT: {{^}}.field public static initonly native int 'c1' // +0{{$}}
; NATURAL-NEXT: {{^}}.field public static initonly native int 'c2' // +1{{$}}
; NATURAL-NEXT: {{^}}.field public static initonly native int 'h' // +2{{$}}
; NATURAL-NEXT: {{^}}.field public static initonly native int 'w' // +4{{$}}
; NATURAL-NEXT: {{^}}.field public static initonly native int 'q' // +8{{$}}
; NATURAL-NEXT: {{^}}.field public static initonly native int 'msg' // +16{{$}}
; NATURAL-NEXT: {{^}}.field public static initonly native int 'p' // +24{{$}}

; After setting the address fields, the module initializer stores the address
; of msg into p, at p's offset (the relocation).
; CHECK-LABEL:  {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK:        {{^}}  stsfld native int 'p'{{$}}
; CHECK-NEXT:   {{^}}  ldsfld native int '<msil>data'{{$}}
; A64LNX-NEXT:  {{^}}  ldc.i4 32{{$}}
; NATURAL-NEXT: {{^}}  ldc.i4 24{{$}}
; CHECK-NEXT:   {{^}}  add{{$}}
; CHECK-NEXT:   {{^}}  ldsfld native int 'msg'{{$}}
; CHECK-NEXT:   {{^}}  stind.i{{$}}
; CHECK-NEXT:   {{^}}  ret{{$}}
; CHECK-NEXT:   {{^}}} // end of method '.cctor'{{$}}

; CHECK-LABEL:  {{^}}.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed{{$}}
; CHECK:        {{^}}  call int32 'main'(){{$}}
; CHECK-NEXT:   {{^}}  ret{{$}}
; CHECK-NEXT:   {{^}}} // end of method '<msil>Main'{{$}}
; CHECK-NOT:    {{.}}
