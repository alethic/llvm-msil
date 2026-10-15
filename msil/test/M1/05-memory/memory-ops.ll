; Memory operations from hand-written IR through llc (Milestone 7, D15, D24),
; in shapes that C does not produce directly: sign- and zero-extending loads
; of i8, i16 and i32 to i32 and i64; truncating stores of i64 and i32 to i8,
; i16 and i32, and i1 stores and loads; loads and stores of i24, i40, i48 and
; i128 (split by the legalizer) at odd offsets; loads and stores with align 1
; and 2 of every type, volatile ones included; llvm.memcpy, llvm.memmove and
; llvm.memset with i32 and i64 lengths, constant and variable, volatile and
; not, overlapping moves included; llvm.memcpy.inline and llvm.memset.inline;
; offsets beyond 32 KiB and 64 KiB in a 70000-byte frame; and float memory
; with an extending load, a truncating store and a NaN payload moved through
; float loads and stores. Every buffer escapes into the noinline @escape, so
; llc keeps the stores before the call and the loads after it, and the inputs
; come from main's argc (1 when run without arguments), so nothing folds even
; at -O2. Each check function compares the results, whole buffers with their
; 0xEE guard bytes included, with the values C gives (the expected bytes are
; in the comments) and returns 1 when any differs; main ORs one bit per
; function into the exit status (8 bits, D37): [exit 0] means every check
; passed and bit N names function N.
; RUN: %{msil_ll}

declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1)
declare void @llvm.memmove.p0.p0.i32(ptr, ptr, i32, i1)
declare void @llvm.memset.p0.i32(ptr, i8, i32, i1)
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
declare void @llvm.memcpy.inline.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.inline.p0.i64(ptr, i8, i64, i1)

; llc cannot see into this call, so memory that escapes into it may be read
; and written by it.
define internal void @escape(ptr %p) noinline {
entry:
  ret void
}

; Bit 0: extending loads from %bits = 0x800000008000FF80
; (bytes 80 FF 00 80 00 00 00 80).
define internal i32 @ext_loads(i64 %bits) noinline {
entry:
  %buf = alloca i64, align 8
  store i64 %bits, ptr %buf, align 8
  call void @escape(ptr %buf)
  ; i8 at offset 0 is 0x80
  %v.1 = load i8, ptr %buf, align 1
  %x.2 = sext i8 %v.1 to i32
  %c.3 = icmp ne i32 %x.2, -128
  %x.4 = zext i8 %v.1 to i32
  %c.5 = icmp ne i32 %x.4, 128
  %x.6 = sext i8 %v.1 to i64
  %c.7 = icmp ne i64 %x.6, -128
  %x.8 = zext i8 %v.1 to i64
  %c.9 = icmp ne i64 %x.8, 128
  ; i8 at offset 1 is 0xFF
  %p.10 = getelementptr inbounds i8, ptr %buf, i64 1
  %v.11 = load i8, ptr %p.10, align 1
  %x.12 = sext i8 %v.11 to i32
  %c.13 = icmp ne i32 %x.12, -1
  %x.14 = zext i8 %v.11 to i32
  %c.15 = icmp ne i32 %x.14, 255
  %x.16 = sext i8 %v.11 to i64
  %c.17 = icmp ne i64 %x.16, -1
  %x.18 = zext i8 %v.11 to i64
  %c.19 = icmp ne i64 %x.18, 255
  ; i16 at offset 0 is 0xFF80
  %v.20 = load i16, ptr %buf, align 2
  %x.21 = sext i16 %v.20 to i32
  %c.22 = icmp ne i32 %x.21, -128
  %x.23 = zext i16 %v.20 to i32
  %c.24 = icmp ne i32 %x.23, 65408
  %x.25 = sext i16 %v.20 to i64
  %c.26 = icmp ne i64 %x.25, -128
  %x.27 = zext i16 %v.20 to i64
  %c.28 = icmp ne i64 %x.27, 65408
  ; i16 at offset 2 is 0x8000
  %p.29 = getelementptr inbounds i8, ptr %buf, i64 2
  %v.30 = load i16, ptr %p.29, align 2
  %x.31 = sext i16 %v.30 to i32
  %c.32 = icmp ne i32 %x.31, -32768
  %x.33 = zext i16 %v.30 to i32
  %c.34 = icmp ne i32 %x.33, 32768
  %x.35 = sext i16 %v.30 to i64
  %c.36 = icmp ne i64 %x.35, -32768
  %x.37 = zext i16 %v.30 to i64
  %c.38 = icmp ne i64 %x.37, 32768
  ; i32 at offset 0 is 0x8000FF80
  %v.39 = load i32, ptr %buf, align 4
  %x.40 = sext i32 %v.39 to i64
  %c.41 = icmp ne i64 %x.40, -2147418240
  %x.42 = zext i32 %v.39 to i64
  %c.43 = icmp ne i64 %x.42, 2147549056
  ; i32 at offset 4 is 0x80000000
  %p.44 = getelementptr inbounds i8, ptr %buf, i64 4
  %v.45 = load i32, ptr %p.44, align 4
  %x.46 = sext i32 %v.45 to i64
  %c.47 = icmp ne i64 %x.46, -2147483648
  %x.48 = zext i32 %v.45 to i64
  %c.49 = icmp ne i64 %x.48, 2147483648
  %any.50 = or i1 %c.3, %c.5
  %any.51 = or i1 %any.50, %c.7
  %any.52 = or i1 %any.51, %c.9
  %any.53 = or i1 %any.52, %c.13
  %any.54 = or i1 %any.53, %c.15
  %any.55 = or i1 %any.54, %c.17
  %any.56 = or i1 %any.55, %c.19
  %any.57 = or i1 %any.56, %c.22
  %any.58 = or i1 %any.57, %c.24
  %any.59 = or i1 %any.58, %c.26
  %any.60 = or i1 %any.59, %c.28
  %any.61 = or i1 %any.60, %c.32
  %any.62 = or i1 %any.61, %c.34
  %any.63 = or i1 %any.62, %c.36
  %any.64 = or i1 %any.63, %c.38
  %any.65 = or i1 %any.64, %c.41
  %any.66 = or i1 %any.65, %c.43
  %any.67 = or i1 %any.66, %c.47
  %any.68 = or i1 %any.67, %c.49
  %r = zext i1 %any.68 to i32
  ret i32 %r
}

; Bit 1: %v = 0x1122334455667788 truncated to i8, i16 and i32 at offsets 1, 2
; and 4 and %w = 0xAABBCCDD to i8 and i16 at 8 and 10, over 0xFF bytes; %b
; stored as an i1 and loaded back, then its inverse.
define internal i32 @trunc_stores(i64 %v, i32 %w, i1 %b) noinline {
entry:
  %buf = alloca [16 x i8], align 8
  %flag = alloca i8, align 1
  store i64 -1, ptr %buf, align 8
  %p.1 = getelementptr inbounds i8, ptr %buf, i64 8
  store i64 -1, ptr %p.1, align 8
  %t.2 = trunc i64 %v to i8
  %p.3 = getelementptr inbounds i8, ptr %buf, i64 1
  store i8 %t.2, ptr %p.3, align 1
  %t.4 = trunc i64 %v to i16
  %p.5 = getelementptr inbounds i8, ptr %buf, i64 2
  store i16 %t.4, ptr %p.5, align 1
  %t.6 = trunc i64 %v to i32
  %p.7 = getelementptr inbounds i8, ptr %buf, i64 4
  store i32 %t.6, ptr %p.7, align 1
  %t.8 = trunc i32 %w to i8
  %p.9 = getelementptr inbounds i8, ptr %buf, i64 8
  store i8 %t.8, ptr %p.9, align 1
  %t.10 = trunc i32 %w to i16
  %p.11 = getelementptr inbounds i8, ptr %buf, i64 10
  store i16 %t.10, ptr %p.11, align 1
  store i8 85, ptr %flag, align 1
  store i1 %b, ptr %flag, align 1
  call void @escape(ptr %buf)
  call void @escape(ptr %flag)
  ; bytes FF 88 88 77 88 77 66 55 DD FF DD CC FF FF FF FF
  %q.12 = load i64, ptr %buf, align 8
  %c.13 = icmp ne i64 %q.12, 6153737368853055743
  %p.14 = getelementptr inbounds i8, ptr %buf, i64 8
  %q.15 = load i64, ptr %p.14, align 8
  %c.16 = icmp ne i64 %q.15, -857866275
  %b.17 = load i1, ptr %flag, align 1
  %c.18 = icmp ne i1 %b.17, %b
  %nb.19 = xor i1 %b, true
  store i1 %nb.19, ptr %flag, align 1
  call void @escape(ptr %flag)
  %b.20 = load i1, ptr %flag, align 1
  %c.21 = icmp ne i1 %b.20, %nb.19
  %any.22 = or i1 %c.13, %c.16
  %any.23 = or i1 %any.22, %c.18
  %any.24 = or i1 %any.23, %c.21
  %r = zext i1 %any.24 to i32
  ret i32 %r
}

; Bit 2: %a = 0xF1E2D3C4B5A69788 truncated to i24, i48 and i40 at offsets 1,
; 5 and 12 (each narrow value is negative), and the i128 (%b << 64) | %a with
; %b = 0x0123456789ABCDEF at 17 (align 1) and 48 (align 16).
define internal i32 @odd_widths(i64 %a, i64 %b) noinline {
entry:
  %buf = alloca [64 x i8], align 16
  store i64 -1229782938247303442, ptr %buf, align 8
  %p.1 = getelementptr inbounds i8, ptr %buf, i64 8
  store i64 -1229782938247303442, ptr %p.1, align 8
  %p.2 = getelementptr inbounds i8, ptr %buf, i64 16
  store i64 -1229782938247303442, ptr %p.2, align 8
  %p.3 = getelementptr inbounds i8, ptr %buf, i64 24
  store i64 -1229782938247303442, ptr %p.3, align 8
  %p.4 = getelementptr inbounds i8, ptr %buf, i64 32
  store i64 -1229782938247303442, ptr %p.4, align 8
  %p.5 = getelementptr inbounds i8, ptr %buf, i64 40
  store i64 -1229782938247303442, ptr %p.5, align 8
  %p.6 = getelementptr inbounds i8, ptr %buf, i64 48
  store i64 -1229782938247303442, ptr %p.6, align 8
  %p.7 = getelementptr inbounds i8, ptr %buf, i64 56
  store i64 -1229782938247303442, ptr %p.7, align 8
  %alo = zext i64 %a to i128
  %bhi = zext i64 %b to i128
  %bsh = shl i128 %bhi, 64
  %wide = or i128 %bsh, %alo
  %t.8 = trunc i64 %a to i24
  %p.9 = getelementptr inbounds i8, ptr %buf, i64 1
  store i24 %t.8, ptr %p.9, align 1
  %t.10 = trunc i64 %a to i48
  %p.11 = getelementptr inbounds i8, ptr %buf, i64 5
  store i48 %t.10, ptr %p.11, align 1
  %t.12 = trunc i64 %a to i40
  %p.13 = getelementptr inbounds i8, ptr %buf, i64 12
  store i40 %t.12, ptr %p.13, align 1
  %p.14 = getelementptr inbounds i8, ptr %buf, i64 17
  store i128 %wide, ptr %p.14, align 1
  %p.15 = getelementptr inbounds i8, ptr %buf, i64 48
  store i128 %wide, ptr %p.15, align 16
  call void @escape(ptr %buf)
  %p.16 = getelementptr inbounds i8, ptr %buf, i64 1
  %v.17 = load i24, ptr %p.16, align 1
  %x.18 = zext i24 %v.17 to i64
  %c.19 = icmp ne i64 %x.18, 10917768
  %x.20 = sext i24 %v.17 to i64
  %c.21 = icmp ne i64 %x.20, -5859448
  %p.22 = getelementptr inbounds i8, ptr %buf, i64 5
  %v.23 = load i48, ptr %p.22, align 1
  %x.24 = zext i48 %v.23 to i64
  %c.25 = icmp ne i64 %x.24, 232841814644616
  %x.26 = sext i48 %v.23 to i64
  %c.27 = icmp ne i64 %x.26, -48633162066040
  %p.28 = getelementptr inbounds i8, ptr %buf, i64 12
  %v.29 = load i40, ptr %p.28, align 1
  %x.30 = zext i40 %v.29 to i64
  %c.31 = icmp ne i64 %x.30, 844861183880
  %x.32 = sext i40 %v.29 to i64
  %c.33 = icmp ne i64 %x.32, -254650443896
  %p.34 = getelementptr inbounds i8, ptr %buf, i64 17
  %v.35 = load i128, ptr %p.34, align 1
  %lo.36 = trunc i128 %v.35 to i64
  %c.37 = icmp ne i64 %lo.36, %a
  %sh.38 = lshr i128 %v.35, 64
  %hi.39 = trunc i128 %sh.38 to i64
  %c.40 = icmp ne i64 %hi.39, %b
  %p.41 = getelementptr inbounds i8, ptr %buf, i64 48
  %v.42 = load i128, ptr %p.41, align 16
  %lo.43 = trunc i128 %v.42 to i64
  %c.44 = icmp ne i64 %lo.43, %a
  %sh.45 = lshr i128 %v.42, 64
  %hi.46 = trunc i128 %sh.45 to i64
  %c.47 = icmp ne i64 %hi.46, %b
  ; the whole buffer, 0xEE guard bytes included:
  ;   EE 88 97 A6 EE 88 97 A6 B5 C4 D3 EE 88 97 A6 B5
  ;   C4 88 97 A6 B5 C4 D3 E2 F1 EF CD AB 89 67 45 23
  ;   01 EE EE EE EE EE EE EE EE EE EE EE EE EE EE EE
  ;   88 97 A6 B5 C4 D3 E2 F1 EF CD AB 89 67 45 23 01
  %q.48 = load i64, ptr %buf, align 8
  %c.49 = icmp ne i64 %q.48, -6442530183351662354
  %p.50 = getelementptr inbounds i8, ptr %buf, i64 8
  %q.51 = load i64, ptr %p.50, align 8
  %c.52 = icmp ne i64 %q.51, -5357428092332424011
  %p.53 = getelementptr inbounds i8, ptr %buf, i64 16
  %q.54 = load i64, ptr %p.53, align 8
  %c.55 = icmp ne i64 %q.54, -2102120316588816188
  %p.56 = getelementptr inbounds i8, ptr %buf, i64 24
  %q.57 = load i64, ptr %p.56, align 8
  %c.58 = icmp ne i64 %q.57, 2541551405711093745
  %p.59 = getelementptr inbounds i8, ptr %buf, i64 32
  %q.60 = load i64, ptr %p.59, align 8
  %c.61 = icmp ne i64 %q.60, -1229782938247303679
  %p.62 = getelementptr inbounds i8, ptr %buf, i64 40
  %q.63 = load i64, ptr %p.62, align 8
  %c.64 = icmp ne i64 %q.63, -1229782938247303442
  %p.65 = getelementptr inbounds i8, ptr %buf, i64 48
  %q.66 = load i64, ptr %p.65, align 8
  %c.67 = icmp ne i64 %q.66, -1017017724017666168
  %p.68 = getelementptr inbounds i8, ptr %buf, i64 56
  %q.69 = load i64, ptr %p.68, align 8
  %c.70 = icmp ne i64 %q.69, 81985529216486895
  %any.71 = or i1 %c.19, %c.21
  %any.72 = or i1 %any.71, %c.25
  %any.73 = or i1 %any.72, %c.27
  %any.74 = or i1 %any.73, %c.31
  %any.75 = or i1 %any.74, %c.33
  %any.76 = or i1 %any.75, %c.37
  %any.77 = or i1 %any.76, %c.40
  %any.78 = or i1 %any.77, %c.44
  %any.79 = or i1 %any.78, %c.47
  %any.80 = or i1 %any.79, %c.49
  %any.81 = or i1 %any.80, %c.52
  %any.82 = or i1 %any.81, %c.55
  %any.83 = or i1 %any.82, %c.58
  %any.84 = or i1 %any.83, %c.61
  %any.85 = or i1 %any.84, %c.64
  %any.86 = or i1 %any.85, %c.67
  %any.87 = or i1 %any.86, %c.70
  %r = zext i1 %any.87 to i32
  ret i32 %r
}

; Bit 3: %v = 0x8877665544332211 (and its low 16 and 32 bits), %d = -1234.0625,
; %f = 0.375 and the pointer %q at odd and 2-aligned offsets of a 0xEE
; buffer, some volatile.
define internal i32 @unaligned(i64 %v, double %d, float %f, ptr %q) noinline {
entry:
  %buf = alloca [72 x i8], align 8
  store i64 -1229782938247303442, ptr %buf, align 8
  %p.1 = getelementptr inbounds i8, ptr %buf, i64 8
  store i64 -1229782938247303442, ptr %p.1, align 8
  %p.2 = getelementptr inbounds i8, ptr %buf, i64 16
  store i64 -1229782938247303442, ptr %p.2, align 8
  %p.3 = getelementptr inbounds i8, ptr %buf, i64 24
  store i64 -1229782938247303442, ptr %p.3, align 8
  %p.4 = getelementptr inbounds i8, ptr %buf, i64 32
  store i64 -1229782938247303442, ptr %p.4, align 8
  %p.5 = getelementptr inbounds i8, ptr %buf, i64 40
  store i64 -1229782938247303442, ptr %p.5, align 8
  %p.6 = getelementptr inbounds i8, ptr %buf, i64 48
  store i64 -1229782938247303442, ptr %p.6, align 8
  %p.7 = getelementptr inbounds i8, ptr %buf, i64 56
  store i64 -1229782938247303442, ptr %p.7, align 8
  %p.8 = getelementptr inbounds i8, ptr %buf, i64 64
  store i64 -1229782938247303442, ptr %p.8, align 8
  %v16 = trunc i64 %v to i16
  %v32 = trunc i64 %v to i32
  %p.9 = getelementptr inbounds i8, ptr %buf, i64 1
  store i16 %v16, ptr %p.9, align 1
  %p.10 = getelementptr inbounds i8, ptr %buf, i64 3
  store i32 %v32, ptr %p.10, align 1
  %p.11 = getelementptr inbounds i8, ptr %buf, i64 9
  store i64 %v, ptr %p.11, align 1
  %p.12 = getelementptr inbounds i8, ptr %buf, i64 18
  store i64 %v, ptr %p.12, align 2
  %p.13 = getelementptr inbounds i8, ptr %buf, i64 27
  store double %d, ptr %p.13, align 1
  %p.14 = getelementptr inbounds i8, ptr %buf, i64 35
  store float %f, ptr %p.14, align 1
  %p.15 = getelementptr inbounds i8, ptr %buf, i64 39
  store ptr %q, ptr %p.15, align 1
  %p.16 = getelementptr inbounds i8, ptr %buf, i64 49
  store volatile i32 %v32, ptr %p.16, align 1
  %p.17 = getelementptr inbounds i8, ptr %buf, i64 53
  store volatile i64 %v, ptr %p.17, align 1
  %p.18 = getelementptr inbounds i8, ptr %buf, i64 62
  store i32 %v32, ptr %p.18, align 2
  call void @escape(ptr %buf)
  %p.19 = getelementptr inbounds i8, ptr %buf, i64 1
  %v.20 = load i16, ptr %p.19, align 1
  %c.21 = icmp ne i16 %v.20, %v16
  %p.22 = getelementptr inbounds i8, ptr %buf, i64 3
  %v.23 = load i32, ptr %p.22, align 1
  %c.24 = icmp ne i32 %v.23, %v32
  %p.25 = getelementptr inbounds i8, ptr %buf, i64 9
  %v.26 = load i64, ptr %p.25, align 1
  %c.27 = icmp ne i64 %v.26, %v
  %p.28 = getelementptr inbounds i8, ptr %buf, i64 18
  %v.29 = load i64, ptr %p.28, align 2
  %c.30 = icmp ne i64 %v.29, %v
  %p.31 = getelementptr inbounds i8, ptr %buf, i64 27
  %v.32 = load double, ptr %p.31, align 1
  %c.33 = fcmp une double %v.32, %d
  %p.34 = getelementptr inbounds i8, ptr %buf, i64 35
  %v.35 = load float, ptr %p.34, align 1
  %c.36 = fcmp une float %v.35, %f
  %p.37 = getelementptr inbounds i8, ptr %buf, i64 39
  %v.38 = load ptr, ptr %p.37, align 1
  %c.39 = icmp ne ptr %v.38, %q
  %p.40 = getelementptr inbounds i8, ptr %buf, i64 49
  %v.41 = load volatile i32, ptr %p.40, align 1
  %c.42 = icmp ne i32 %v.41, %v32
  %p.43 = getelementptr inbounds i8, ptr %buf, i64 53
  %v.44 = load volatile i64, ptr %p.43, align 1
  %c.45 = icmp ne i64 %v.44, %v
  %p.46 = getelementptr inbounds i8, ptr %buf, i64 62
  %v.47 = load i32, ptr %p.46, align 2
  %c.48 = icmp ne i32 %v.47, %v32
  ; loads that straddle the stored values: bytes 0..26 are
  ;   EE 11 22 11 22 33 44 EE EE 11 22 33 44 55 66 77
  ;   88 EE 11 22 33 44 55 66 77 88 EE
  %p.49 = getelementptr inbounds i8, ptr %buf, i64 1
  %v.50 = load i64, ptr %p.49, align 1
  %c.51 = icmp ne i64 %v.50, -1229970661819997679
  %p.52 = getelementptr inbounds i8, ptr %buf, i64 18
  %v.53 = load i32, ptr %p.52, align 2
  %c.54 = icmp ne i32 %v.53, 1144201745
  %p.55 = getelementptr inbounds i8, ptr %buf, i64 22
  %v.56 = load i16, ptr %p.55, align 1
  %c.57 = icmp ne i16 %v.56, 26197
  %p.58 = getelementptr inbounds i8, ptr %buf, i64 12
  %v.59 = load i64, ptr %p.58, align 4
  %c.60 = icmp ne i64 %v.59, 2455005541779789124
  %any.61 = or i1 %c.21, %c.24
  %any.62 = or i1 %any.61, %c.27
  %any.63 = or i1 %any.62, %c.30
  %any.64 = or i1 %any.63, %c.33
  %any.65 = or i1 %any.64, %c.36
  %any.66 = or i1 %any.65, %c.39
  %any.67 = or i1 %any.66, %c.42
  %any.68 = or i1 %any.67, %c.45
  %any.69 = or i1 %any.68, %c.48
  %any.70 = or i1 %any.69, %c.51
  %any.71 = or i1 %any.70, %c.54
  %any.72 = or i1 %any.71, %c.57
  %any.73 = or i1 %any.72, %c.60
  %r = zext i1 %any.73 to i32
  ret i32 %r
}

; Bit 4: memory intrinsics applied in order, %n = 20 (i32), %n2 = 2 (i64),
; %c = 0x5A.
define internal i32 @intrinsics(i32 %n, i64 %n2, i8 %c) noinline {
entry:
  %src = alloca [64 x i8], align 8
  %dst = alloca [64 x i8], align 8
  ; src[k] = k * 7 + 3, dst = 0xEE
  store i64 3759703178913843715, ptr %src, align 8
  %p.1 = getelementptr inbounds i8, ptr %src, i64 8
  store i64 7810752857846137403, ptr %p.1, align 8
  %p.2 = getelementptr inbounds i8, ptr %src, i64 16
  store i64 -6584941536931120525, ptr %p.2, align 8
  %p.3 = getelementptr inbounds i8, ptr %src, i64 24
  store i64 -2533891857998826837, ptr %p.3, align 8
  %p.4 = getelementptr inbounds i8, ptr %src, i64 32
  store i64 1444818751918828259, ptr %p.4, align 8
  %p.5 = getelementptr inbounds i8, ptr %src, i64 40
  store i64 5495867327027683867, ptr %p.5, align 8
  %p.6 = getelementptr inbounds i8, ptr %src, i64 48
  store i64 -8899827067749574061, ptr %p.6, align 8
  %p.7 = getelementptr inbounds i8, ptr %src, i64 56
  store i64 -4848777388817280373, ptr %p.7, align 8
  store i64 -1229782938247303442, ptr %dst, align 8
  %p.8 = getelementptr inbounds i8, ptr %dst, i64 8
  store i64 -1229782938247303442, ptr %p.8, align 8
  %p.9 = getelementptr inbounds i8, ptr %dst, i64 16
  store i64 -1229782938247303442, ptr %p.9, align 8
  %p.10 = getelementptr inbounds i8, ptr %dst, i64 24
  store i64 -1229782938247303442, ptr %p.10, align 8
  %p.11 = getelementptr inbounds i8, ptr %dst, i64 32
  store i64 -1229782938247303442, ptr %p.11, align 8
  %p.12 = getelementptr inbounds i8, ptr %dst, i64 40
  store i64 -1229782938247303442, ptr %p.12, align 8
  %p.13 = getelementptr inbounds i8, ptr %dst, i64 48
  store i64 -1229782938247303442, ptr %p.13, align 8
  %p.14 = getelementptr inbounds i8, ptr %dst, i64 56
  store i64 -1229782938247303442, ptr %p.14, align 8
  call void @escape(ptr %src)
  call void @escape(ptr %dst)
  ; dst[3..23) = src[1..21), i32 length, volatile
  %p.15 = getelementptr inbounds i8, ptr %dst, i64 3
  %p.16 = getelementptr inbounds i8, ptr %src, i64 1
  call void @llvm.memcpy.p0.p0.i32(ptr align 1 %p.15, ptr align 1 %p.16, i32 %n, i1 true)
  ; dst[30..50) = c, i32 length
  %p.17 = getelementptr inbounds i8, ptr %dst, i64 30
  call void @llvm.memset.p0.i32(ptr align 2 %p.17, i8 %c, i32 %n, i1 false)
  ; src[2..22) = src[0..20), overlapping, i32 length, volatile
  %p.18 = getelementptr inbounds i8, ptr %src, i64 2
  call void @llvm.memmove.p0.p0.i32(ptr align 2 %p.18, ptr align 8 %src, i32 %n, i1 true)
  ; dst[55..62) = src[40..47), constant length, volatile
  %p.19 = getelementptr inbounds i8, ptr %dst, i64 55
  %p.20 = getelementptr inbounds i8, ptr %src, i64 40
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p.19, ptr align 8 %p.20, i64 7, i1 true)
  ; dst[62..64) = c, i64 length, volatile
  %p.21 = getelementptr inbounds i8, ptr %dst, i64 62
  call void @llvm.memset.p0.i64(ptr align 2 %p.21, i8 %c, i64 %n2, i1 true)
  ; dst[24..40) = dst[20..36), overlapping, constant length
  %p.22 = getelementptr inbounds i8, ptr %dst, i64 24
  %p.23 = getelementptr inbounds i8, ptr %dst, i64 20
  call void @llvm.memmove.p0.p0.i64(ptr align 8 %p.22, ptr align 4 %p.23, i64 16, i1 false)
  call void @escape(ptr %src)
  call void @escape(ptr %dst)
  ; dst:
  ;   EE EE EE 0A 11 18 1F 26 2D 34 3B 42 49 50 57 5E
  ;   65 6C 73 7A 81 88 8F EE 81 88 8F EE EE EE EE EE
  ;   EE EE 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A
  ;   5A 5A EE EE EE EE EE 1B 22 29 30 37 3E 45 5A 5A
  %q.24 = load i64, ptr %dst, align 8
  %c.25 = icmp ne i64 %q.24, 2746940759196233454
  %p.26 = getelementptr inbounds i8, ptr %dst, i64 8
  %q.27 = load i64, ptr %p.26, align 8
  %c.28 = icmp ne i64 %q.27, 6797990438113063981
  %p.29 = getelementptr inbounds i8, ptr %dst, i64 16
  %q.30 = load i64, ptr %p.29, align 8
  %c.31 = icmp ne i64 %q.30, -1256635681326535579
  %p.32 = getelementptr inbounds i8, ptr %dst, i64 24
  %q.33 = load i64, ptr %p.32, align 8
  %c.34 = icmp ne i64 %q.33, -1229782938253555583
  %p.35 = getelementptr inbounds i8, ptr %dst, i64 32
  %q.36 = load i64, ptr %p.35, align 8
  %c.37 = icmp ne i64 %q.36, 6510615555426938606
  %p.38 = getelementptr inbounds i8, ptr %dst, i64 40
  %q.39 = load i64, ptr %p.38, align 8
  %c.40 = icmp ne i64 %q.39, 6510615555426900570
  %p.41 = getelementptr inbounds i8, ptr %dst, i64 48
  %q.42 = load i64, ptr %p.41, align 8
  %c.43 = icmp ne i64 %q.42, 2012808793459415642
  %p.44 = getelementptr inbounds i8, ptr %dst, i64 56
  %q.45 = load i64, ptr %p.44, align 8
  %c.46 = icmp ne i64 %q.45, 6510592344833665314
  ; src:
  ;   03 0A 03 0A 11 18 1F 26 2D 34 3B 42 49 50 57 5E
  ;   65 6C 73 7A 81 88 9D A4 AB B2 B9 C0 C7 CE D5 DC
  ;   E3 EA F1 F8 FF 06 0D 14 1B 22 29 30 37 3E 45 4C
  ;   53 5A 61 68 6F 76 7D 84 8B 92 99 A0 A7 AE B5 BC
  %q.47 = load i64, ptr %src, align 8
  %c.48 = icmp ne i64 %q.47, 2746940759180773891
  %p.49 = getelementptr inbounds i8, ptr %src, i64 8
  %q.50 = load i64, ptr %p.49, align 8
  %c.51 = icmp ne i64 %q.50, 6797990438113063981
  %p.52 = getelementptr inbounds i8, ptr %src, i64 16
  %q.53 = load i64, ptr %p.52, align 8
  %c.54 = icmp ne i64 %q.53, -6584956990459253659
  %p.55 = getelementptr inbounds i8, ptr %src, i64 24
  %q.56 = load i64, ptr %p.55, align 8
  %c.57 = icmp ne i64 %q.56, -2533891857998826837
  %p.58 = getelementptr inbounds i8, ptr %src, i64 32
  %q.59 = load i64, ptr %p.58, align 8
  %c.60 = icmp ne i64 %q.59, 1444818751918828259
  %p.61 = getelementptr inbounds i8, ptr %src, i64 40
  %q.62 = load i64, ptr %p.61, align 8
  %c.63 = icmp ne i64 %q.62, 5495867327027683867
  %p.64 = getelementptr inbounds i8, ptr %src, i64 48
  %q.65 = load i64, ptr %p.64, align 8
  %c.66 = icmp ne i64 %q.65, -8899827067749574061
  %p.67 = getelementptr inbounds i8, ptr %src, i64 56
  %q.68 = load i64, ptr %p.67, align 8
  %c.69 = icmp ne i64 %q.68, -4848777388817280373
  %any.70 = or i1 %c.25, %c.28
  %any.71 = or i1 %any.70, %c.31
  %any.72 = or i1 %any.71, %c.34
  %any.73 = or i1 %any.72, %c.37
  %any.74 = or i1 %any.73, %c.40
  %any.75 = or i1 %any.74, %c.43
  %any.76 = or i1 %any.75, %c.46
  %any.77 = or i1 %any.76, %c.48
  %any.78 = or i1 %any.77, %c.51
  %any.79 = or i1 %any.78, %c.54
  %any.80 = or i1 %any.79, %c.57
  %any.81 = or i1 %any.80, %c.60
  %any.82 = or i1 %any.81, %c.63
  %any.83 = or i1 %any.82, %c.66
  %any.84 = or i1 %any.83, %c.69
  %r = zext i1 %any.84 to i32
  ret i32 %r
}

; Bit 5: always-inline copies and fills, %c = 0xA5.
define internal i32 @inline_ops(i8 %c) noinline {
entry:
  %src = alloca [48 x i8], align 8
  %dst = alloca [48 x i8], align 8
  ; src[k] = k * 7 + 3, dst = 0xEE
  store i64 3759703178913843715, ptr %src, align 8
  %p.1 = getelementptr inbounds i8, ptr %src, i64 8
  store i64 7810752857846137403, ptr %p.1, align 8
  %p.2 = getelementptr inbounds i8, ptr %src, i64 16
  store i64 -6584941536931120525, ptr %p.2, align 8
  %p.3 = getelementptr inbounds i8, ptr %src, i64 24
  store i64 -2533891857998826837, ptr %p.3, align 8
  %p.4 = getelementptr inbounds i8, ptr %src, i64 32
  store i64 1444818751918828259, ptr %p.4, align 8
  %p.5 = getelementptr inbounds i8, ptr %src, i64 40
  store i64 5495867327027683867, ptr %p.5, align 8
  store i64 -1229782938247303442, ptr %dst, align 8
  %p.6 = getelementptr inbounds i8, ptr %dst, i64 8
  store i64 -1229782938247303442, ptr %p.6, align 8
  %p.7 = getelementptr inbounds i8, ptr %dst, i64 16
  store i64 -1229782938247303442, ptr %p.7, align 8
  %p.8 = getelementptr inbounds i8, ptr %dst, i64 24
  store i64 -1229782938247303442, ptr %p.8, align 8
  %p.9 = getelementptr inbounds i8, ptr %dst, i64 32
  store i64 -1229782938247303442, ptr %p.9, align 8
  %p.10 = getelementptr inbounds i8, ptr %dst, i64 40
  store i64 -1229782938247303442, ptr %p.10, align 8
  call void @escape(ptr %src)
  call void @escape(ptr %dst)
  ; dst[1..28) = src[5..32)
  %p.11 = getelementptr inbounds i8, ptr %dst, i64 1
  %p.12 = getelementptr inbounds i8, ptr %src, i64 5
  call void @llvm.memcpy.inline.p0.p0.i64(ptr align 1 %p.11, ptr align 1 %p.12, i64 27, i1 false)
  ; dst[30..43) = c
  %p.13 = getelementptr inbounds i8, ptr %dst, i64 30
  call void @llvm.memset.inline.p0.i64(ptr align 2 %p.13, i8 %c, i64 13, i1 false)
  ; dst[43..48) = src[0..5), volatile
  %p.14 = getelementptr inbounds i8, ptr %dst, i64 43
  call void @llvm.memcpy.inline.p0.p0.i64(ptr align 1 %p.14, ptr align 8 %src, i64 5, i1 true)
  call void @escape(ptr %dst)
  ; dst:
  ;   EE 26 2D 34 3B 42 49 50 57 5E 65 6C 73 7A 81 88
  ;   8F 96 9D A4 AB B2 B9 C0 C7 CE D5 DC EE EE A5 A5
  ;   A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 A5 03 0A 11 18 1F
  %q.15 = load i64, ptr %dst, align 8
  %c.16 = icmp ne i64 %q.15, 5785228018379990766
  %p.17 = getelementptr inbounds i8, ptr %dst, i64 8
  %q.18 = load i64, ptr %p.17, align 8
  %c.19 = icmp ne i64 %q.18, -8610466376397267369
  %p.20 = getelementptr inbounds i8, ptr %dst, i64 16
  %q.21 = load i64, ptr %p.20, align 8
  %c.22 = icmp ne i64 %q.21, -4559416697464973681
  %p.23 = getelementptr inbounds i8, ptr %dst, i64 24
  %q.24 = load i64, ptr %p.23, align 8
  %c.25 = icmp ne i64 %q.24, -6510534976619557177
  %p.26 = getelementptr inbounds i8, ptr %dst, i64 32
  %q.27 = load i64, ptr %p.26, align 8
  %c.28 = icmp ne i64 %q.27, -6510615555426900571
  %p.29 = getelementptr inbounds i8, ptr %dst, i64 40
  %q.30 = load i64, ptr %p.29, align 8
  %c.31 = icmp ne i64 %q.30, 2240559549325354405
  %any.32 = or i1 %c.16, %c.19
  %any.33 = or i1 %any.32, %c.22
  %any.34 = or i1 %any.33, %c.25
  %any.35 = or i1 %any.34, %c.28
  %any.36 = or i1 %any.35, %c.31
  %r = zext i1 %any.36 to i32
  ret i32 %r
}

; Bit 6: %v = 0x0123456789ABCDEF (and its low 8, 16 and 32 bits) and %d = 0.5
; at offsets 32767, 40000, 65520, 65539 and 69999 of a 70000-byte frame.
define internal i32 @big_offsets(i64 %v, double %d, i32 %k) noinline {
entry:
  %big = alloca [70000 x i8], align 8
  %x32 = trunc i64 %v to i32
  %x16 = trunc i64 %v to i16
  %x8 = trunc i64 %v to i8
  %p.1 = getelementptr inbounds i8, ptr %big, i64 65539
  store i32 %x32, ptr %p.1, align 1
  %p.2 = getelementptr inbounds i8, ptr %big, i64 69999
  store i8 %x8, ptr %p.2, align 1
  %p.3 = getelementptr inbounds i8, ptr %big, i64 40000
  store i64 %v, ptr %p.3, align 8
  %p.4 = getelementptr inbounds i8, ptr %big, i64 32767
  store i16 %x16, ptr %p.4, align 1
  %p.5 = getelementptr inbounds i8, ptr %big, i64 65520
  store double %d, ptr %p.5, align 8
  call void @escape(ptr %big)
  %p.6 = getelementptr inbounds i8, ptr %big, i64 65539
  %v.7 = load i32, ptr %p.6, align 1
  %c.8 = icmp ne i32 %v.7, %x32
  %p.9 = getelementptr inbounds i8, ptr %big, i64 69999
  %v.10 = load i8, ptr %p.9, align 1
  %c.11 = icmp ne i8 %v.10, %x8
  %p.12 = getelementptr inbounds i8, ptr %big, i64 40000
  %v.13 = load i64, ptr %p.12, align 8
  %c.14 = icmp ne i64 %v.13, %v
  %p.15 = getelementptr inbounds i8, ptr %big, i64 32767
  %v.16 = load i16, ptr %p.15, align 1
  %c.17 = icmp ne i16 %v.16, %x16
  %p.18 = getelementptr inbounds i8, ptr %big, i64 65520
  %v.19 = load double, ptr %p.18, align 8
  %c.20 = fcmp une double %v.19, %d
  ; the same bytes through a variable index (%k = 65539) and a negative offset
  %idx = sext i32 %k to i64
  %pk = getelementptr inbounds i8, ptr %big, i64 %idx
  %vk = load i32, ptr %pk, align 1
  %c.21 = icmp ne i32 %vk, %x32
  %pneg = getelementptr inbounds i8, ptr %pk, i64 -32772
  %vneg = load i16, ptr %pneg, align 1
  %c.22 = icmp ne i16 %vneg, %x16
  %any.23 = or i1 %c.8, %c.11
  %any.24 = or i1 %any.23, %c.14
  %any.25 = or i1 %any.24, %c.17
  %any.26 = or i1 %any.25, %c.20
  %any.27 = or i1 %any.26, %c.21
  %any.28 = or i1 %any.27, %c.22
  %r = zext i1 %any.28 to i32
  ret i32 %r
}

; Bit 7: %f = 0.1f, %d = 1/3, %nan = 0x7FC12345.
define internal i32 @fp_memory(float %f, double %d, i32 %nan) noinline {
entry:
  %sf = alloca float, align 4
  %sd = alloca double, align 8
  %sw = alloca [2 x i32], align 4
  store float %f, ptr %sf, align 4
  store double %d, ptr %sd, align 8
  call void @escape(ptr %sf)
  call void @escape(ptr %sd)
  ; an extending load (float to double) and a truncating store (double to float)
  %lf = load float, ptr %sf, align 4
  %lfx = fpext float %lf to double
  %fx = fpext float %f to double
  %c.1 = fcmp une double %lfx, %fx
  %ld = load double, ptr %sd, align 8
  %ldt = fptrunc double %ld to float
  store float %ldt, ptr %sf, align 4
  call void @escape(ptr %sf)
  %lf2 = load float, ptr %sf, align 4
  %dt = fptrunc double %d to float
  %c.2 = fcmp une float %lf2, %dt
  ; a quiet NaN with a payload stored as an integer, moved as a float, read back
  store i32 %nan, ptr %sw, align 4
  call void @escape(ptr %sw)
  %nf = load float, ptr %sw, align 4
  %sw1 = getelementptr inbounds i8, ptr %sw, i64 4
  store float %nf, ptr %sw1, align 4
  call void @escape(ptr %sw)
  %nb = load i32, ptr %sw1, align 4
  %c.3 = icmp ne i32 %nb, %nan
  %any.4 = or i1 %c.1, %c.2
  %any.5 = or i1 %any.4, %c.3
  %r = zext i1 %any.5 to i32
  ret i32 %r
}

define i32 @main(i32 %argc, ptr %argv) {
entry:
  %one = sext i32 %argc to i64
  %onef = sitofp i32 %argc to float
  %oned = sitofp i32 %argc to double
  %bits = mul i64 %one, -9223372034707226752
  %r0 = call i32 @ext_loads(i64 %bits)
  %v1 = mul i64 %one, 1234605616436508552
  %w1 = mul i32 %argc, -1430532899
  %b1 = trunc i32 %argc to i1
  %r1 = call i32 @trunc_stores(i64 %v1, i32 %w1, i1 %b1)
  %a2 = mul i64 %one, -1017017724017666168
  %b2 = mul i64 %one, 81985529216486895
  %r2 = call i32 @odd_widths(i64 %a2, i64 %b2)
  %v3 = mul i64 %one, -8613303245920329199
  %d3 = fmul double %oned, -1234.0625
  %f3 = fmul float %onef, 0.375
  %r3 = call i32 @unaligned(i64 %v3, double %d3, float %f3, ptr %argv)
  %n4 = mul i32 %argc, 20
  %m4 = mul i64 %one, 2
  %c4 = mul i32 %argc, 90
  %c4b = trunc i32 %c4 to i8
  %r4 = call i32 @intrinsics(i32 %n4, i64 %m4, i8 %c4b)
  %c5 = mul i32 %argc, 165
  %c5b = trunc i32 %c5 to i8
  %r5 = call i32 @inline_ops(i8 %c5b)
  %v6 = mul i64 %one, 81985529216486895
  %d6 = fmul double %oned, 0.5
  %k6 = mul i32 %argc, 65539
  %r6 = call i32 @big_offsets(i64 %v6, double %d6, i32 %k6)
  %f7 = fmul float %onef, 0x3FB99999A0000000
  %d7 = fdiv double %oned, 3.0
  %nan7 = mul i32 %argc, 2143363909
  %r7 = call i32 @fp_memory(float %f7, double %d7, i32 %nan7)
  %s1 = shl i32 %r1, 1
  %s2 = shl i32 %r2, 2
  %s3 = shl i32 %r3, 3
  %s4 = shl i32 %r4, 4
  %s5 = shl i32 %r5, 5
  %s6 = shl i32 %r6, 6
  %s7 = shl i32 %r7, 7
  %o1 = or i32 %r0, %s1
  %o2 = or i32 %o1, %s2
  %o3 = or i32 %o2, %s3
  %o4 = or i32 %o3, %s4
  %o5 = or i32 %o4, %s5
  %o6 = or i32 %o5, %s6
  %o7 = or i32 %o6, %s7
  ret i32 %o7
}
