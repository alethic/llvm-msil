// The C ABI of the portable triples (ImplementationPlan.md 5.18.2 PC1, PC2 and
// PC4; milestone P-1a): clang's MSILPortableABIInfo, derived from
// DefaultABIInfo, on msil64-unknown-dotnet and msil32-unknown-dotnet.
// - Aggregates of any size are passed indirectly 'byval' at their natural
//   alignment and returned through 'sret'; there is no register coercion of
//   small structs (no {i64,i64} pairs, no iN).
// - Empty records are ignored, as arguments (an override: DefaultABIInfo
//   passes them) and as results.
// - Promotable integers are signext/zeroext (char is signed), enums are
//   their integer type, long double is IEEE binary64 ('double'), long and
//   pointers follow the width (LP64 / ILP32).
// - __int128 is direct (i128) on msil64, which has it; _BitInt is direct up
//   to 128 bits on msil64 and 64 bits on msil32, the widest passed directly
//   (PC2), and indirect above that (PC4): passed 'byval' and returned
//   through 'sret' (_BitInt(128) on msil32, _BitInt(129) and _BitInt(200) on
//   both).
// - va_list is a char* ('ptr'), and va_arg is clang's own char* lowering
//   (emitVoidPtrVAArg) with 8-byte slots on both widths (5.18.2 PC4): a
//   scalar passed directly takes one slot, aligned up first when its type is
//   aligned to more than 8 (__int128 and _BitInt(128) on msil64, 16); an
//   argument passed indirectly (every aggregate, whatever its size or
//   alignment, a _BitInt wider than 128 bits on msil64 and than 64 bits on
//   msil32) takes one slot holding a pointer to the caller's copy, read
//   without realignment and copied (or loaded) at the type's C alignment; an
//   empty record takes no slot. ExpandVariadics' MSILPortable class lays
//   calls out slot for slot the same way, and aligns the caller's copy to the
//   call site's byval alignment (5.18.2 PC5;
//   llvm/test/Transforms/ExpandVariadics/msil-abi.ll, the PORT, PORT64 and
//   PORT32 rows).
// - Inline assembly (PC1): an empty asm statement is the compiler barrier
//   every MSIL triple emits (a single-thread seq_cst fence and
//   llvm.sideeffect); an asm statement with an input or output operand is a
//   Sema error ("invalid input/output size for constraint"), since the
//   portable TargetInfo accepts no operand of any size; any other asm text is
//   "inline assembly is not supported for the MSIL target '<triple>'".
// - -mlong-double-80 and -mlong-double-128 are errors (long double is
//   binary64 on the portable triples).

// REQUIRES: msil-registered-target

// RUN: %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,P64 -DIDX=i64 -DPALIGN=8 --implicit-check-not=i24 \
// RUN:     --implicit-check-not="{ i64, i64 }"
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,P32 -DIDX=i32 -DPALIGN=4 \
// RUN:     --implicit-check-not="{ i64, i64 }"

// Inline assembly.
// RUN: not %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o /dev/null -DASM_OPERAND %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ASMOP
// RUN: not %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o /dev/null -DASM_OUTPUT %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ASMOUT
// RUN: not %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o /dev/null -DASM_TEXT %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ASMTEXT
// ASMOP:   error: invalid input size for constraint 'r'
// ASMOUT:  error: invalid output size for constraint '=r'
// ASMTEXT: error: inline assembly is not supported for the MSIL target 'msil64-unknown-dotnet'

// long double options.
// RUN: not %clang_cc1 -triple msil64-unknown-dotnet -mlong-double-80 -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LD80
// RUN: not %clang_cc1 -triple msil64-unknown-dotnet -mlong-double-128 -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LD128
// RUN: not %clang_cc1 -triple msil32-unknown-dotnet -mlong-double-80 -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LD80
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -mlong-double-64 -emit-llvm-only %s
// LD80:  error: {{.*}}'-mlong-double-80'
// LD128: error: {{.*}}'-mlong-double-128'

// The data model the rules below rely on (PC2).
_Static_assert(sizeof(long double) == 8 && _Alignof(long double) == 8, "");
_Static_assert(sizeof(long) == sizeof(void *), "");
_Static_assert((char)-1 < 0, "char is signed");
_Static_assert(sizeof(__builtin_va_list) == sizeof(void *), "");
_Static_assert(_Generic((__WCHAR_TYPE__)0, int: 1, default: 0), "");
_Static_assert(_Generic((__WINT_TYPE__)0, int: 1, default: 0), "");

struct S1 { char a; };
struct S12 { int a, b, c; };
struct E {};
struct Big { long a, b, c; };
struct Al16 { _Alignas(16) int x; };
struct Al32 { _Alignas(32) int x; };
enum Color { RED, GREEN };

// CHECK-LABEL: define {{[^@]*}}i32 @take_s12(ptr noundef byval(%struct.S12) align 4 %s)
int take_s12(struct S12 s) { return s.a + s.c; }

// CHECK-LABEL: define {{[^@]*}}signext i8 @take_s1(ptr noundef byval(%struct.S1) align 1 %s)
char take_s1(struct S1 s) { return s.a; }

// CHECK-LABEL: define {{[^@]*}}void @ret_s12(ptr {{[^,]*}}sret(%struct.S12) align 4 %agg.result, i32 noundef %x)
struct S12 ret_s12(int x) {
  struct S12 s = {x, x + 1, x + 2};
  return s;
}

// CHECK-LABEL: define {{[^@]*}}void @ret_s1(ptr {{[^,]*}}sret(%struct.S1) align 1 %agg.result)
struct S1 ret_s1(void) {
  struct S1 s = {7};
  return s;
}

// CHECK-LABEL: define {{[^@]*}}i32 @take_empty(i32 noundef %a, i32 noundef %b)
int take_empty(int a, struct E e, int b) { return a + b; }

// CHECK-LABEL: define {{[^@]*}}void @ret_empty()
struct E ret_empty(void) {
  struct E e;
  return e;
}

// CHECK-LABEL: define {{[^@]*}}i32 @call_empty()
// CHECK:       call i32 @take_empty(i32 noundef 1, i32 noundef 2)
int call_empty(void) {
  struct E e;
  return take_empty(1, e, 2);
}

// CHECK-LABEL: define {{[^@]*}}i32 @take_enum(i32 noundef %c)
int take_enum(enum Color c) { return c == GREEN; }

// CHECK-LABEL: define {{[^@]*}}signext i8 @pass_char(i8 noundef signext %c)
char pass_char(char c) { return c; }

// CHECK-LABEL: define {{[^@]*}}zeroext i16 @pass_ushort(i16 noundef zeroext %x)
unsigned short pass_ushort(unsigned short x) { return x; }

// CHECK-LABEL: define {{[^@]*}}double @pass_ld(double noundef %x)
long double pass_ld(long double x) { return x; }

// P64-LABEL: define {{[^@]*}}i64 @pass_long(i64 noundef %x)
// P32-LABEL: define {{[^@]*}}i32 @pass_long(i32 noundef %x)
long pass_long(long x) { return x; }

// CHECK-LABEL: define {{[^@]*}}ptr @pass_ptr(ptr noundef %p)
void *pass_ptr(void *p) { return p; }

// CHECK-LABEL: define {{[^@]*}}i64 @pass_bi64(i64 noundef %x)
_BitInt(64) pass_bi64(_BitInt(64) x) { return x; }

// P64-LABEL: define {{[^@]*}}i128 @pass_bi128(i128 noundef %x)
// P32-LABEL: define {{[^@]*}}void @pass_bi128(ptr {{[^,]*}}sret({{[^)]*}}) align {{[0-9]+}} %agg.result, ptr noundef byval({{[^)]*}}) align {{[0-9]+}} {{%[^)]*}})
_BitInt(128) pass_bi128(_BitInt(128) x) { return x; }

#ifdef __SIZEOF_INT128__
// P64-LABEL: define {{[^@]*}}i128 @pass_i128(i128 noundef %x)
__int128 pass_i128(__int128 x) { return x; }
#endif

// va_arg: one 8-byte slot per scalar.
// CHECK-LABEL: define {{[^@]*}}i32 @va_int(i32 noundef %n, ...)
// CHECK:       [[AP:%.*]] = alloca ptr, align [[PALIGN]]
// CHECK:       call void @llvm.va_start.p0(ptr [[AP]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  {{%.*}} = load i32, ptr [[CUR]], align {{[48]}}
int va_int(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  int v = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return v;
}

// CHECK-LABEL: define {{[^@]*}}double @va_double(i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  {{%.*}} = load double, ptr [[CUR]], align 8
double va_double(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  double v = __builtin_va_arg(ap, double);
  __builtin_va_end(ap);
  return v;
}

// long long is aligned to 8, the slot's alignment: no realignment.
// CHECK-LABEL: define {{[^@]*}}i64 @va_llong(i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  {{%.*}} = load i64, ptr [[CUR]], align 8
long long va_llong(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  long long v = __builtin_va_arg(ap, long long);
  __builtin_va_end(ap);
  return v;
}

// Pointers take a whole slot on msil32 too.
// CHECK-LABEL: define {{[^@]*}}ptr @va_ptr(i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  {{%.*}} = load ptr, ptr [[CUR]], align {{[48]}}
void *va_ptr(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  void *v = __builtin_va_arg(ap, void *);
  __builtin_va_end(ap);
  return v;
}

// Aggregates: the slot holds a pointer to the caller's copy, whatever the
// size.
// CHECK-LABEL: define {{[^@]*}}i32 @va_s12(i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[P:%.*]] = load ptr, ptr [[CUR]], align {{[48]}}
// CHECK-NEXT:  call void @llvm.memcpy.p0.p0.[[IDX]](ptr align 4 {{%.*}}, ptr align 4 [[P]], [[IDX]] 12, i1 false)
int va_s12(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  struct S12 s = __builtin_va_arg(ap, struct S12);
  __builtin_va_end(ap);
  return s.b;
}

// CHECK-LABEL: define {{[^@]*}}signext i8 @va_s1(i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[P:%.*]] = load ptr, ptr [[CUR]], align {{[48]}}
// CHECK-NEXT:  call void @llvm.memcpy.p0.p0.[[IDX]](ptr align 1 {{%.*}}, ptr align 1 [[P]], [[IDX]] 1, i1 false)
char va_s1(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  struct S1 s = __builtin_va_arg(ap, struct S1);
  __builtin_va_end(ap);
  return s.a;
}

#ifdef __SIZEOF_INT128__
// __int128 is aligned to 16: the pointer is aligned up, then 16 bytes taken.
// P64-LABEL: define {{[^@]*}}i128 @va_i128(i32 noundef %n, ...)
// P64:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// P64-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align 8
// P64-NEXT:  [[T:%.*]] = getelementptr inbounds i8, ptr [[CUR]], i{{32|64}} 15
// P64-NEXT:  [[AL:%.*]] = call ptr @llvm.ptrmask.p0.i64(ptr [[T]], i64 -16)
// P64-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[AL]], i64 16
// P64-NEXT:  store ptr [[NEXT]], ptr [[AP]], align 8
// P64-NEXT:  {{%.*}} = load i128, ptr [[AL]], align 16
__int128 va_i128(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  __int128 v = __builtin_va_arg(ap, __int128);
  __builtin_va_end(ap);
  return v;
}
#endif

// _BitInt(128): direct on msil64, read like __int128 (the slot aligned to
// 16, the value inline); indirect on msil32, so one 8-byte slot holds a
// pointer to the caller's copy, then the value is loaded through it.
// P64-LABEL: define {{[^@]*}}i128 @va_bi128(i32 noundef %n, ...)
// P64:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// P64-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align 8
// P64-NEXT:  [[T:%.*]] = getelementptr inbounds i8, ptr [[CUR]], i{{32|64}} 15
// P64-NEXT:  [[AL:%.*]] = call ptr @llvm.ptrmask.p0.i64(ptr [[T]], i64 -16)
// P64-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[AL]], i64 16
// P64-NEXT:  store ptr [[NEXT]], ptr [[AP]], align 8
// P64-NEXT:  {{%.*}} = load i128, ptr [[AL]], align 16
// P32-LABEL: define {{[^@]*}}void @va_bi128(ptr {{.*}}sret({{[^)]*}}) {{.*}}%agg.result, i32 noundef %n, ...)
// P32:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// P32-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align 4
// P32-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], i32 8
// P32-NEXT:  store ptr [[NEXT]], ptr [[AP]], align 4
// P32-NEXT:  [[P:%.*]] = load ptr, ptr [[CUR]], align 8
// P32-NEXT:  {{%.*}} = load i128, ptr [[P]], align {{[0-9]+}}
_BitInt(128) va_bi128(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  _BitInt(128) v = __builtin_va_arg(ap, _BitInt(128));
  __builtin_va_end(ap);
  return v;
}

// _BitInt wider than the widest passed directly (128 bits on msil64, 64 on
// msil32): passed byval and returned through sret on both triples, at the
// type's alignment (16 on msil64, 8 on msil32).
// P64-LABEL: define {{[^@]*}}void @pass_bi129(ptr {{[^,]*}}sret(i256) align 16 %agg.result, ptr noundef byval(i256) align 16 {{%[^)]*}})
// P32-LABEL: define {{[^@]*}}void @pass_bi129(ptr {{[^,]*}}sret([24 x i8]) align 8 %agg.result, ptr noundef byval([24 x i8]) align 8 {{%[^)]*}})
unsigned _BitInt(129) pass_bi129(unsigned _BitInt(129) x) { return x; }

// P64-LABEL: define {{[^@]*}}void @pass_bi200(ptr {{[^,]*}}sret(i256) align 16 %agg.result, ptr noundef byval(i256) align 16 {{%[^)]*}})
// P32-LABEL: define {{[^@]*}}void @pass_bi200(ptr {{[^,]*}}sret(i256) align 8 %agg.result, ptr noundef byval(i256) align 8 {{%[^)]*}})
_BitInt(200) pass_bi200(_BitInt(200) x) { return x; }

// As variadic arguments: the caller passes a byval copy (ExpandVariadics
// puts its address in the slot) ...
void va_sink(int n, ...);
// P64-LABEL: define {{[^@]*}}void @call_va_bi(ptr noundef byval(i256) align 16 {{%[^,]*}}, ptr noundef byval(i256) align 16 {{%[^)]*}})
// P64:       call void (i32, ...) @va_sink(i32 noundef 2, ptr noundef byval(i256) align 16 {{%[^,]*}}, ptr noundef byval(i256) align 16 {{%[^)]*}})
// P32-LABEL: define {{[^@]*}}void @call_va_bi(ptr noundef byval([24 x i8]) align 8 {{%[^,]*}}, ptr noundef byval(i256) align 8 {{%[^)]*}})
// P32:       call void (i32, ...) @va_sink(i32 noundef 2, ptr noundef byval([24 x i8]) align 8 {{%[^,]*}}, ptr noundef byval(i256) align 8 {{%[^)]*}})
void call_va_bi(unsigned _BitInt(129) a, _BitInt(200) b) { va_sink(2, a, b); }

// ... and va_arg reads one 8-byte slot holding a pointer to it, neither
// masked nor realigned, then loads the value through it.
// CHECK-LABEL: define {{[^@]*}}void @va_bi129(ptr {{[^,]*}}sret({{[^)]*}}) align {{[0-9]+}} %agg.result, i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[P:%.*]] = load ptr, ptr [[CUR]], align 8
// P64-NEXT:    {{%.*}} = load i256, ptr [[P]], align 16
// P32-NEXT:    {{%.*}} = load i192, ptr [[P]], align 8
unsigned _BitInt(129) va_bi129(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  unsigned _BitInt(129) v = __builtin_va_arg(ap, unsigned _BitInt(129));
  __builtin_va_end(ap);
  return v;
}

// CHECK-LABEL: define {{[^@]*}}void @va_bi200(ptr {{[^,]*}}sret({{[^)]*}}) align {{[0-9]+}} %agg.result, i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[P:%.*]] = load ptr, ptr [[CUR]], align 8
// P64-NEXT:    {{%.*}} = load i256, ptr [[P]], align 16
// P32-NEXT:    {{%.*}} = load i256, ptr [[P]], align 8
_BitInt(200) va_bi200(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  _BitInt(200) v = __builtin_va_arg(ap, _BitInt(200));
  __builtin_va_end(ap);
  return v;
}

// Aggregates larger than a slot, and over-aligned ones: still one 8-byte
// slot holding a pointer, which is neither masked nor realigned; the copy
// out of the caller's copy uses the C alignment (8 or 4, 16, 32).
// CHECK-LABEL: define {{[^@]*}} @va_big(i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[P:%.*]] = load ptr, ptr [[CUR]], align 8
// P64-NEXT:    call void @llvm.memcpy.p0.p0.i64(ptr align 8 {{%.*}}, ptr align 8 [[P]], i64 24, i1 false)
// P32-NEXT:    call void @llvm.memcpy.p0.p0.i32(ptr align 4 {{%.*}}, ptr align 4 [[P]], i32 12, i1 false)
long va_big(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  struct Big s = __builtin_va_arg(ap, struct Big);
  __builtin_va_end(ap);
  return s.c;
}

// CHECK-LABEL: define {{[^@]*}}i32 @va_al16(i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[P:%.*]] = load ptr, ptr [[CUR]], align 8
// CHECK-NEXT:  call void @llvm.memcpy.p0.p0.[[IDX]](ptr align 16 {{%.*}}, ptr align 16 [[P]], [[IDX]] 16, i1 false)
int va_al16(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  struct Al16 s = __builtin_va_arg(ap, struct Al16);
  __builtin_va_end(ap);
  return s.x;
}

// CHECK-LABEL: define {{[^@]*}}i32 @va_al32(i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[P:%.*]] = load ptr, ptr [[CUR]], align 8
// CHECK-NEXT:  call void @llvm.memcpy.p0.p0.[[IDX]](ptr align 32 {{%.*}}, ptr align 32 [[P]], [[IDX]] 32, i1 false)
int va_al32(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  struct Al32 s = __builtin_va_arg(ap, struct Al32);
  __builtin_va_end(ap);
  return s.x;
}

// An empty record was not passed and takes no slot: the int after it is
// read from the first slot, and only one slot is consumed.
// CHECK-LABEL: define {{[^@]*}}i32 @va_empty(i32 noundef %n, ...)
// CHECK:       call void @llvm.va_start.p0(ptr [[AP:%.*]])
// CHECK-NEXT:  [[CUR:%.*]] = load ptr, ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  [[NEXT:%.*]] = getelementptr inbounds i8, ptr [[CUR]], [[IDX]] 8
// CHECK-NEXT:  store ptr [[NEXT]], ptr [[AP]], align [[PALIGN]]
// CHECK-NEXT:  {{%.*}} = load i32, ptr [[CUR]], align 8
// CHECK-NOT:   getelementptr
// CHECK:       call void @llvm.va_end.p0(ptr [[AP]])
int va_empty(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  struct E e = __builtin_va_arg(ap, struct E);
  int v = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return v;
}

// The compiler barrier.
// CHECK-LABEL: define {{[^@]*}}void @barrier(ptr noundef %p)
// CHECK:       fence syncscope("singlethread") seq_cst
// CHECK-NEXT:  call void @llvm.sideeffect()
void barrier(int *p) {
  *p = 1;
  __asm__ volatile("" ::: "memory");
  *p = 2;
}

#ifdef ASM_OPERAND
void asm_operand(int v) { __asm__ volatile("" : : "r"(v)); }
#endif

#ifdef ASM_OUTPUT
int asm_output(void) {
  int v;
  __asm__ volatile("" : "=r"(v));
  return v;
}
#endif

#ifdef ASM_TEXT
void asm_text(void) { __asm__ volatile("nop"); }
#endif
