// The 64-bit lock-free cap of the MSIL triples (ImplementationPlan.md M25.b,
// decision brief R12, 5.15.6). Every MSIL triple caps MaxAtomicInlineWidth at
// 64 bits, since MSIL's 16-byte atomics take the lock path, and keeps the
// native MaxAtomicPromoteWidth, so every layout stays native:
// - on the MSIL triples (x86_64-unknown-linux-gnu-msil without and with cx16,
//   the feature that makes native x86-64 claim 16-byte atomics,
//   aarch64-unknown-linux-gnu-msil, whose native TargetInfo always claims
//   them, and the windows-msvc MSIL triples), __atomic_always_lock_free(8, 0)
//   holds and __atomic_always_lock_free(16, 0) does not (static assertions
//   and the constants returned); __c11_atomic_is_lock_free(16) and
//   __atomic_is_lock_free(16, p) are never folded to true but ask the runtime
//   (__atomic_is_lock_free); a load or store of an _Atomic __int128 or of a
//   16-byte _Atomic struct is the generic __atomic_load or __atomic_store
//   call of size 16, while 8-byte atomics stay inline instructions;
// - _Atomic __int128 and a 16-byte _Atomic struct keep the native size and
//   alignment, 16 and 16, also as members (offsets 16 and 32 of a 48-byte
//   struct), and their globals are aligned to 16;
// - the controls, native x86_64-unknown-linux-gnu with cx16 and native
//   aarch64-unknown-linux-gnu (-DNATIVE16), fold the 16-byte queries to true
//   and keep 16-byte atomics inline (load atomic i128);
// - the driver's -dM -E: __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16, which the
//   native AArch64 TargetInfo always defines and the x86-64 one with cx16
//   (-mcx16, -march=x86-64-v2), is absent on every MSIL triple, while
//   __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 stays; native aarch64-unknown-linux-gnu
//   and native x86_64-unknown-linux-gnu -mcx16 define it (the controls).
// Frontend only.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,MSIL --implicit-check-not="atomic i128"
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -target-feature +cx16 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,MSIL --implicit-check-not="atomic i128"
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,MSIL --implicit-check-not="atomic i128"
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,MSIL --implicit-check-not="atomic i128"
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -target-feature +cx16 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,MSIL --implicit-check-not="atomic i128"
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,MSIL --implicit-check-not="atomic i128"
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +cx16 -DNATIVE16 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,NATIVE --implicit-check-not=@__atomic_
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -DNATIVE16 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,NATIVE --implicit-check-not=@__atomic_

// The predefined macros of the driver.
// DEFINE: %{nocas16} = FileCheck %s --check-prefix=CAS8 --implicit-check-not=__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -E -dM -o - %s | %{nocas16}
// RUN: %clang --target=aarch64-pc-windows-msvc-msil -E -dM -o - %s | %{nocas16}
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -mcx16 -E -dM -o - %s | %{nocas16}
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -march=x86-64-v2 -E -dM -o - %s | %{nocas16}
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -mcx16 -E -dM -o - %s | %{nocas16}
// RUN: %clang --target=aarch64-unknown-linux-gnu -E -dM -o - %s | FileCheck %s --check-prefix=CAS16
// RUN: %clang --target=x86_64-unknown-linux-gnu -mcx16 -E -dM -o - %s | FileCheck %s --check-prefix=CAS16

// CAS8:      #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 1
// CAS16-DAG: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 1
// CAS16-DAG: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 1

typedef struct pair {
  long long a, b;
} pair;

struct holder {
  char c;
  _Atomic __int128 v;
  _Atomic pair p;
};

// CHECK-DAG: @g16 = {{.*}}global i128 0, align 16
// CHECK-DAG: @gp = {{.*}}global %struct.pair zeroinitializer, align 16
// CHECK-DAG: @g8 = {{.*}}global i64 0, align 8
_Atomic __int128 g16;
_Atomic pair gp;
_Atomic long long g8;

_Static_assert(__atomic_always_lock_free(8, 0), "8-byte atomics are lock-free");
_Static_assert(__atomic_always_lock_free(sizeof(g8), &g8), "8-byte atomics are lock-free");
#ifdef NATIVE16
_Static_assert(__atomic_always_lock_free(16, 0), "16-byte atomics are lock-free natively");
#else
_Static_assert(!__atomic_always_lock_free(16, 0), "16-byte atomics are not lock-free for MSIL");
#endif
_Static_assert(sizeof(_Atomic __int128) == 16 && _Alignof(_Atomic __int128) == 16,
               "_Atomic __int128 keeps its native layout");
_Static_assert(sizeof(_Atomic pair) == 16 && _Alignof(_Atomic pair) == 16,
               "a 16-byte _Atomic struct keeps its native layout");
_Static_assert(__builtin_offsetof(struct holder, v) == 16, "native member offset");
_Static_assert(__builtin_offsetof(struct holder, p) == 32, "native member offset");
_Static_assert(sizeof(struct holder) == 48 && _Alignof(struct holder) == 16,
               "native struct layout");

// CHECK-LABEL: define {{.*}}i32 @always8()
// CHECK:         ret i32 1
int always8(void) { return __atomic_always_lock_free(8, 0); }

// CHECK-LABEL: define {{.*}}i32 @always16()
// MSIL:          ret i32 0
// NATIVE:        ret i32 1
int always16(void) { return __atomic_always_lock_free(16, 0); }

// CHECK-LABEL: define {{.*}}i32 @c11_8()
// CHECK:         ret i32 1
int c11_8(void) { return __c11_atomic_is_lock_free(8); }

// CHECK-LABEL: define {{.*}}i32 @c11_16()
// MSIL:          call {{.*}}i1 @__atomic_is_lock_free(i64 noundef 16, ptr noundef null)
// NATIVE-NOT:    call
// NATIVE:        ret i32 1
int c11_16(void) { return __c11_atomic_is_lock_free(16); }

// CHECK-LABEL: define {{.*}}i32 @is16()
// MSIL:          call {{.*}}i1 @__atomic_is_lock_free(i64 noundef 16, ptr noundef @g16)
// NATIVE-NOT:    call
// NATIVE:        ret i32 1
int is16(void) { return __atomic_is_lock_free(16, &g16); }

// CHECK-LABEL: define {{.*}}i64 @ld8()
// CHECK:         load atomic i64, ptr @g8 seq_cst, align 8
long long ld8(void) { return g8; }

// CHECK-LABEL: define {{.*}}void @ld16(ptr
// MSIL:          call void @__atomic_load(i64 noundef 16, ptr noundef @g16, ptr noundef %{{.*}}, i32 noundef 5)
// NATIVE:        load atomic i128, ptr @g16 seq_cst, align 16
void ld16(__int128 *out) { *out = g16; }

// CHECK-LABEL: define {{.*}}void @st16(ptr
// MSIL:          call void @__atomic_store(i64 noundef 16, ptr noundef @g16, ptr noundef %{{.*}}, i32 noundef 5)
// NATIVE:        store atomic i128 %{{.*}}, ptr @g16 seq_cst, align 16
void st16(const __int128 *in) { g16 = *in; }

// CHECK-LABEL: define {{.*}}void @ldp(ptr
// MSIL:          call void @__atomic_load(i64 noundef 16, ptr noundef @gp, ptr noundef %{{.*}}, i32 noundef 5)
// NATIVE:        load atomic i128, ptr @gp seq_cst, align 16
void ldp(pair *out) { *out = gp; }
