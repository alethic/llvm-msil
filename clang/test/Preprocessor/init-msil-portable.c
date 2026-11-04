// Predefined macros of the portable triples msil64-unknown-dotnet and
// msil32-unknown-dotnet (ImplementationPlan.md 5.18.2 PC1-PC2, 5.18.3 PD1-PD2,
// milestone P-1a). Unlike the '<native triple>-msil' targets of init-msil.c,
// whose predefines are the native triple's, the portable triples have their
// own TargetInfo, so the lists below are complete: every macro clang defines
// for the triple is listed in order, except the few that this test does not
// pin (the clang version macros, __STDC_VERSION__, __SIG_ATOMIC_* and
// __GCC_CONSTRUCTIVE_SIZE/__GCC_DESTRUCTIVE_SIZE). __BITINT_MAXWIDTH__ is
// 8388608 on both widths (llvm::IntegerType::MAX_INT_BITS, as on x86), and
// _Float16 exists on both widths, so the __FLT16_* family (IEEE binary16) is
// listed too; msil64's list is frozen at P-1b's commit (5.18.1 PT10).
//
// The data model is LP64 on msil64 and ILP32 on msil32; size_t, ptrdiff_t and
// intptr_t are 'unsigned long' and 'long' and int64_t and intmax_t are
// 'long long' on both widths; char is signed; wchar_t is a signed 32-bit int;
// wint_t is clang's default 'int' (__WINT_TYPE__ int, __WINT_WIDTH__ 32 and no
// __WINT_UNSIGNED__); long double is IEEE binary64 (8 bytes, the __LDBL_*
// values of double); __BIGGEST_ALIGNMENT__ is 16 on msil64 (8 on msil32);
// atomics are lock-free up to 8 bytes (__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1, _2,
// _4 and _8, never _16); __int128 exists on msil64 only. The target macros are
// __MSIL__, __msil64__ or __msil32__ and __dotnet__ (never __msil__).
//
// The portable target is no host: there is no __ELF__ (the object format is
// MSIL), and no macro of an operating system, a native architecture, an
// instruction set extension, a segment register or a code model is defined,
// with or without its leading or trailing underscores (PD2). Math errno is the
// driver's default, as on Linux, so the driver defines no __NO_MATH_ERRNO__;
// cc1 alone, without -fmath-errno, does (the CC1 runs).

// RUN: %clang --target=msil64-unknown-dotnet -E -dM -x c %s -o - \
// RUN:   | FileCheck %s --check-prefix=MSIL64 \
// RUN:     --implicit-check-not=__ELF__ --implicit-check-not=__unix__ \
// RUN:     --implicit-check-not=__unix --implicit-check-not=unix \
// RUN:     --implicit-check-not=__linux__ --implicit-check-not=__linux \
// RUN:     --implicit-check-not=linux --implicit-check-not=__gnu_linux__ \
// RUN:     --implicit-check-not=_WIN32 --implicit-check-not=_WIN64 \
// RUN:     --implicit-check-not=_MSC_VER --implicit-check-not=__MINGW \
// RUN:     --implicit-check-not=__APPLE__ --implicit-check-not=__x86_64__ \
// RUN:     --implicit-check-not=__x86_64 --implicit-check-not=__amd64__ \
// RUN:     --implicit-check-not=__amd64 --implicit-check-not=__k8__ \
// RUN:     --implicit-check-not=__k8 --implicit-check-not=_M_X64 \
// RUN:     --implicit-check-not=_M_AMD64 --implicit-check-not=__aarch64__ \
// RUN:     --implicit-check-not=__SSE --implicit-check-not=__MMX__ \
// RUN:     --implicit-check-not=__SEG_FS --implicit-check-not=__SEG_GS \
// RUN:     --implicit-check-not=__FLOAT128__ --implicit-check-not=__code_model_ \
// RUN:     --implicit-check-not=__WINT_UNSIGNED__ --implicit-check-not=__NO_MATH_ERRNO__ \
// RUN:     --implicit-check-not=__msil__ --implicit-check-not=__CHAR_UNSIGNED__ \
// RUN:     --implicit-check-not=__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 \
// RUN:     --implicit-check-not=__wasm --implicit-check-not=_ILP32 \
// RUN:     --implicit-check-not=__msil32__
// RUN: %clang --target=msil32-unknown-dotnet -E -dM -x c %s -o - \
// RUN:   | FileCheck %s --check-prefix=MSIL32 \
// RUN:     --implicit-check-not=__ELF__ --implicit-check-not=__unix__ \
// RUN:     --implicit-check-not=__unix --implicit-check-not=unix \
// RUN:     --implicit-check-not=__linux__ --implicit-check-not=__linux \
// RUN:     --implicit-check-not=linux --implicit-check-not=__gnu_linux__ \
// RUN:     --implicit-check-not=_WIN32 --implicit-check-not=_WIN64 \
// RUN:     --implicit-check-not=_MSC_VER --implicit-check-not=__MINGW \
// RUN:     --implicit-check-not=__APPLE__ --implicit-check-not=__x86_64__ \
// RUN:     --implicit-check-not=__x86_64 --implicit-check-not=__amd64__ \
// RUN:     --implicit-check-not=__amd64 --implicit-check-not=__k8__ \
// RUN:     --implicit-check-not=__k8 --implicit-check-not=_M_X64 \
// RUN:     --implicit-check-not=_M_AMD64 --implicit-check-not=__aarch64__ \
// RUN:     --implicit-check-not=__SSE --implicit-check-not=__MMX__ \
// RUN:     --implicit-check-not=__SEG_FS --implicit-check-not=__SEG_GS \
// RUN:     --implicit-check-not=__FLOAT128__ --implicit-check-not=__code_model_ \
// RUN:     --implicit-check-not=__WINT_UNSIGNED__ --implicit-check-not=__NO_MATH_ERRNO__ \
// RUN:     --implicit-check-not=__msil__ --implicit-check-not=__CHAR_UNSIGNED__ \
// RUN:     --implicit-check-not=__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 \
// RUN:     --implicit-check-not=__wasm --implicit-check-not=_LP64 \
// RUN:     --implicit-check-not=__SIZEOF_INT128__ --implicit-check-not=__msil64__

// The normalized spelling with the object format names the same target.
// RUN: %clang --target=msil64-unknown-dotnet-msil -E -dM -x c %s -o - \
// RUN:   | FileCheck %s --check-prefix=SPELL --implicit-check-not=__ELF__
// SPELL: #define __MSIL__ 1
// SPELL: #define __dotnet__ 1
// SPELL: #define __msil64__ 1

// cc1 without the driver's -fmath-errno.
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -E -dM < /dev/null \
// RUN:   | FileCheck %s --check-prefix=CC1
// CC1: #define __NO_MATH_ERRNO__ 1
// CC1: #define __WINT_TYPE__ int

// MSIL64: #define _LP64 1{{$}}
// MSIL64: #define __ATOMIC_ACQUIRE 2{{$}}
// MSIL64: #define __ATOMIC_ACQ_REL 4{{$}}
// MSIL64: #define __ATOMIC_CONSUME 1{{$}}
// MSIL64: #define __ATOMIC_RELAXED 0{{$}}
// MSIL64: #define __ATOMIC_RELEASE 3{{$}}
// MSIL64: #define __ATOMIC_SEQ_CST 5{{$}}
// MSIL64: #define __BIGGEST_ALIGNMENT__ 16{{$}}
// MSIL64: #define __BITINT_MAXWIDTH__ 8388608{{$}}
// MSIL64: #define __BOOL_WIDTH__ 1{{$}}
// MSIL64: #define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__{{$}}
// MSIL64: #define __CHAR16_TYPE__ unsigned short{{$}}
// MSIL64: #define __CHAR32_TYPE__ unsigned int{{$}}
// MSIL64: #define __CHAR_BIT__ 8{{$}}
// MSIL64: #define __CLANG_ATOMIC_BOOL_LOCK_FREE 2{{$}}
// MSIL64: #define __CLANG_ATOMIC_CHAR16_T_LOCK_FREE 2{{$}}
// MSIL64: #define __CLANG_ATOMIC_CHAR32_T_LOCK_FREE 2{{$}}
// MSIL64: #define __CLANG_ATOMIC_CHAR_LOCK_FREE 2{{$}}
// MSIL64: #define __CLANG_ATOMIC_INT_LOCK_FREE 2{{$}}
// MSIL64: #define __CLANG_ATOMIC_LLONG_LOCK_FREE 2{{$}}
// MSIL64: #define __CLANG_ATOMIC_LONG_LOCK_FREE 2{{$}}
// MSIL64: #define __CLANG_ATOMIC_POINTER_LOCK_FREE 2{{$}}
// MSIL64: #define __CLANG_ATOMIC_SHORT_LOCK_FREE 2{{$}}
// MSIL64: #define __CLANG_ATOMIC_WCHAR_T_LOCK_FREE 2{{$}}
// MSIL64: #define __CONSTANT_CFSTRINGS__ 1{{$}}
// MSIL64: #define __DBL_DECIMAL_DIG__ 17{{$}}
// MSIL64: #define __DBL_DENORM_MIN__ 4.9406564584124654e-324{{$}}
// MSIL64: #define __DBL_DIG__ 15{{$}}
// MSIL64: #define __DBL_EPSILON__ 2.2204460492503131e-16{{$}}
// MSIL64: #define __DBL_HAS_DENORM__ 1{{$}}
// MSIL64: #define __DBL_HAS_INFINITY__ 1{{$}}
// MSIL64: #define __DBL_HAS_QUIET_NAN__ 1{{$}}
// MSIL64: #define __DBL_MANT_DIG__ 53{{$}}
// MSIL64: #define __DBL_MAX_10_EXP__ 308{{$}}
// MSIL64: #define __DBL_MAX_EXP__ 1024{{$}}
// MSIL64: #define __DBL_MAX__ 1.7976931348623157e+308{{$}}
// MSIL64: #define __DBL_MIN_10_EXP__ (-307){{$}}
// MSIL64: #define __DBL_MIN_EXP__ (-1021){{$}}
// MSIL64: #define __DBL_MIN__ 2.2250738585072014e-308{{$}}
// MSIL64: #define __DBL_NORM_MAX__ 1.7976931348623157e+308{{$}}
// MSIL64: #define __DECIMAL_DIG__ __LDBL_DECIMAL_DIG__{{$}}
// MSIL64: #define __FINITE_MATH_ONLY__ 0{{$}}
// MSIL64: #define __FLT16_DECIMAL_DIG__ 5{{$}}
// MSIL64: #define __FLT16_DENORM_MIN__ 5.9604644775390625e-8F16{{$}}
// MSIL64: #define __FLT16_DIG__ 3{{$}}
// MSIL64: #define __FLT16_EPSILON__ 9.765625e-4F16{{$}}
// MSIL64: #define __FLT16_HAS_DENORM__ 1{{$}}
// MSIL64: #define __FLT16_HAS_INFINITY__ 1{{$}}
// MSIL64: #define __FLT16_HAS_QUIET_NAN__ 1{{$}}
// MSIL64: #define __FLT16_MANT_DIG__ 11{{$}}
// MSIL64: #define __FLT16_MAX_10_EXP__ 4{{$}}
// MSIL64: #define __FLT16_MAX_EXP__ 16{{$}}
// MSIL64: #define __FLT16_MAX__ 6.5504e+4F16{{$}}
// MSIL64: #define __FLT16_MIN_10_EXP__ (-4){{$}}
// MSIL64: #define __FLT16_MIN_EXP__ (-13){{$}}
// MSIL64: #define __FLT16_MIN__ 6.103515625e-5F16{{$}}
// MSIL64: #define __FLT16_NORM_MAX__ 6.5504e+4F16{{$}}
// MSIL64: #define __FLT_DECIMAL_DIG__ 9{{$}}
// MSIL64: #define __FLT_DENORM_MIN__ 1.40129846e-45F{{$}}
// MSIL64: #define __FLT_DIG__ 6{{$}}
// MSIL64: #define __FLT_EPSILON__ 1.19209290e-7F{{$}}
// MSIL64: #define __FLT_HAS_DENORM__ 1{{$}}
// MSIL64: #define __FLT_HAS_INFINITY__ 1{{$}}
// MSIL64: #define __FLT_HAS_QUIET_NAN__ 1{{$}}
// MSIL64: #define __FLT_MANT_DIG__ 24{{$}}
// MSIL64: #define __FLT_MAX_10_EXP__ 38{{$}}
// MSIL64: #define __FLT_MAX_EXP__ 128{{$}}
// MSIL64: #define __FLT_MAX__ 3.40282347e+38F{{$}}
// MSIL64: #define __FLT_MIN_10_EXP__ (-37){{$}}
// MSIL64: #define __FLT_MIN_EXP__ (-125){{$}}
// MSIL64: #define __FLT_MIN__ 1.17549435e-38F{{$}}
// MSIL64: #define __FLT_NORM_MAX__ 3.40282347e+38F{{$}}
// MSIL64: #define __FLT_RADIX__ 2{{$}}
// MSIL64: #define __FPCLASS_NEGINF 0x0004{{$}}
// MSIL64: #define __FPCLASS_NEGNORMAL 0x0008{{$}}
// MSIL64: #define __FPCLASS_NEGSUBNORMAL 0x0010{{$}}
// MSIL64: #define __FPCLASS_NEGZERO 0x0020{{$}}
// MSIL64: #define __FPCLASS_POSINF 0x0200{{$}}
// MSIL64: #define __FPCLASS_POSNORMAL 0x0100{{$}}
// MSIL64: #define __FPCLASS_POSSUBNORMAL 0x0080{{$}}
// MSIL64: #define __FPCLASS_POSZERO 0x0040{{$}}
// MSIL64: #define __FPCLASS_QNAN 0x0002{{$}}
// MSIL64: #define __FPCLASS_SNAN 0x0001{{$}}
// MSIL64: #define __GCC_ATOMIC_BOOL_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_ATOMIC_CHAR16_T_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_ATOMIC_CHAR32_T_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_ATOMIC_CHAR_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_ATOMIC_INT_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_ATOMIC_LLONG_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_ATOMIC_LONG_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_ATOMIC_POINTER_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_ATOMIC_SHORT_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_ATOMIC_TEST_AND_SET_TRUEVAL 1{{$}}
// MSIL64: #define __GCC_ATOMIC_WCHAR_T_LOCK_FREE 2{{$}}
// MSIL64: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_1 1{{$}}
// MSIL64: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_2 1{{$}}
// MSIL64: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4 1{{$}}
// MSIL64: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 1{{$}}
// MSIL64: #define __GNUC_MINOR__ 2{{$}}
// MSIL64: #define __GNUC_PATCHLEVEL__ 1{{$}}
// MSIL64: #define __GNUC_STDC_INLINE__ 1{{$}}
// MSIL64: #define __GNUC__ 4{{$}}
// MSIL64: #define __GXX_ABI_VERSION 1002{{$}}
// MSIL64: #define __INT16_C(c) c{{$}}
// MSIL64: #define __INT16_C_SUFFIX__ {{$}}
// MSIL64: #define __INT16_FMTd__ "hd"{{$}}
// MSIL64: #define __INT16_FMTi__ "hi"{{$}}
// MSIL64: #define __INT16_MAX__ 32767{{$}}
// MSIL64: #define __INT16_TYPE__ short{{$}}
// MSIL64: #define __INT32_C(c) c{{$}}
// MSIL64: #define __INT32_C_SUFFIX__ {{$}}
// MSIL64: #define __INT32_FMTd__ "d"{{$}}
// MSIL64: #define __INT32_FMTi__ "i"{{$}}
// MSIL64: #define __INT32_MAX__ 2147483647{{$}}
// MSIL64: #define __INT32_TYPE__ int{{$}}
// MSIL64: #define __INT64_C(c) c##LL{{$}}
// MSIL64: #define __INT64_C_SUFFIX__ LL{{$}}
// MSIL64: #define __INT64_FMTd__ "lld"{{$}}
// MSIL64: #define __INT64_FMTi__ "lli"{{$}}
// MSIL64: #define __INT64_MAX__ 9223372036854775807LL{{$}}
// MSIL64: #define __INT64_TYPE__ long long int{{$}}
// MSIL64: #define __INT8_C(c) c{{$}}
// MSIL64: #define __INT8_C_SUFFIX__ {{$}}
// MSIL64: #define __INT8_FMTd__ "hhd"{{$}}
// MSIL64: #define __INT8_FMTi__ "hhi"{{$}}
// MSIL64: #define __INT8_MAX__ 127{{$}}
// MSIL64: #define __INT8_TYPE__ signed char{{$}}
// MSIL64: #define __INTMAX_C(c) c##LL{{$}}
// MSIL64: #define __INTMAX_C_SUFFIX__ LL{{$}}
// MSIL64: #define __INTMAX_FMTd__ "lld"{{$}}
// MSIL64: #define __INTMAX_FMTi__ "lli"{{$}}
// MSIL64: #define __INTMAX_MAX__ 9223372036854775807LL{{$}}
// MSIL64: #define __INTMAX_TYPE__ long long int{{$}}
// MSIL64: #define __INTMAX_WIDTH__ 64{{$}}
// MSIL64: #define __INTPTR_FMTd__ "ld"{{$}}
// MSIL64: #define __INTPTR_FMTi__ "li"{{$}}
// MSIL64: #define __INTPTR_MAX__ 9223372036854775807L{{$}}
// MSIL64: #define __INTPTR_TYPE__ long int{{$}}
// MSIL64: #define __INTPTR_WIDTH__ 64{{$}}
// MSIL64: #define __INT_FAST16_FMTd__ "hd"{{$}}
// MSIL64: #define __INT_FAST16_FMTi__ "hi"{{$}}
// MSIL64: #define __INT_FAST16_MAX__ 32767{{$}}
// MSIL64: #define __INT_FAST16_TYPE__ short{{$}}
// MSIL64: #define __INT_FAST16_WIDTH__ 16{{$}}
// MSIL64: #define __INT_FAST32_FMTd__ "d"{{$}}
// MSIL64: #define __INT_FAST32_FMTi__ "i"{{$}}
// MSIL64: #define __INT_FAST32_MAX__ 2147483647{{$}}
// MSIL64: #define __INT_FAST32_TYPE__ int{{$}}
// MSIL64: #define __INT_FAST32_WIDTH__ 32{{$}}
// MSIL64: #define __INT_FAST64_FMTd__ "lld"{{$}}
// MSIL64: #define __INT_FAST64_FMTi__ "lli"{{$}}
// MSIL64: #define __INT_FAST64_MAX__ 9223372036854775807LL{{$}}
// MSIL64: #define __INT_FAST64_TYPE__ long long int{{$}}
// MSIL64: #define __INT_FAST64_WIDTH__ 64{{$}}
// MSIL64: #define __INT_FAST8_FMTd__ "hhd"{{$}}
// MSIL64: #define __INT_FAST8_FMTi__ "hhi"{{$}}
// MSIL64: #define __INT_FAST8_MAX__ 127{{$}}
// MSIL64: #define __INT_FAST8_TYPE__ signed char{{$}}
// MSIL64: #define __INT_FAST8_WIDTH__ 8{{$}}
// MSIL64: #define __INT_LEAST16_FMTd__ "hd"{{$}}
// MSIL64: #define __INT_LEAST16_FMTi__ "hi"{{$}}
// MSIL64: #define __INT_LEAST16_MAX__ 32767{{$}}
// MSIL64: #define __INT_LEAST16_TYPE__ short{{$}}
// MSIL64: #define __INT_LEAST16_WIDTH__ 16{{$}}
// MSIL64: #define __INT_LEAST32_FMTd__ "d"{{$}}
// MSIL64: #define __INT_LEAST32_FMTi__ "i"{{$}}
// MSIL64: #define __INT_LEAST32_MAX__ 2147483647{{$}}
// MSIL64: #define __INT_LEAST32_TYPE__ int{{$}}
// MSIL64: #define __INT_LEAST32_WIDTH__ 32{{$}}
// MSIL64: #define __INT_LEAST64_FMTd__ "lld"{{$}}
// MSIL64: #define __INT_LEAST64_FMTi__ "lli"{{$}}
// MSIL64: #define __INT_LEAST64_MAX__ 9223372036854775807LL{{$}}
// MSIL64: #define __INT_LEAST64_TYPE__ long long int{{$}}
// MSIL64: #define __INT_LEAST64_WIDTH__ 64{{$}}
// MSIL64: #define __INT_LEAST8_FMTd__ "hhd"{{$}}
// MSIL64: #define __INT_LEAST8_FMTi__ "hhi"{{$}}
// MSIL64: #define __INT_LEAST8_MAX__ 127{{$}}
// MSIL64: #define __INT_LEAST8_TYPE__ signed char{{$}}
// MSIL64: #define __INT_LEAST8_WIDTH__ 8{{$}}
// MSIL64: #define __INT_MAX__ 2147483647{{$}}
// MSIL64: #define __INT_WIDTH__ 32{{$}}
// MSIL64: #define __LDBL_DECIMAL_DIG__ 17{{$}}
// MSIL64: #define __LDBL_DENORM_MIN__ 4.9406564584124654e-324L{{$}}
// MSIL64: #define __LDBL_DIG__ 15{{$}}
// MSIL64: #define __LDBL_EPSILON__ 2.2204460492503131e-16L{{$}}
// MSIL64: #define __LDBL_HAS_DENORM__ 1{{$}}
// MSIL64: #define __LDBL_HAS_INFINITY__ 1{{$}}
// MSIL64: #define __LDBL_HAS_QUIET_NAN__ 1{{$}}
// MSIL64: #define __LDBL_MANT_DIG__ 53{{$}}
// MSIL64: #define __LDBL_MAX_10_EXP__ 308{{$}}
// MSIL64: #define __LDBL_MAX_EXP__ 1024{{$}}
// MSIL64: #define __LDBL_MAX__ 1.7976931348623157e+308L{{$}}
// MSIL64: #define __LDBL_MIN_10_EXP__ (-307){{$}}
// MSIL64: #define __LDBL_MIN_EXP__ (-1021){{$}}
// MSIL64: #define __LDBL_MIN__ 2.2250738585072014e-308L{{$}}
// MSIL64: #define __LDBL_NORM_MAX__ 1.7976931348623157e+308L{{$}}
// MSIL64: #define __LITTLE_ENDIAN__ 1{{$}}
// MSIL64: #define __LLONG_WIDTH__ 64{{$}}
// MSIL64: #define __LONG_LONG_MAX__ 9223372036854775807LL{{$}}
// MSIL64: #define __LONG_MAX__ 9223372036854775807L{{$}}
// MSIL64: #define __LONG_WIDTH__ 64{{$}}
// MSIL64: #define __LP64__ 1{{$}}
// MSIL64: #define __MEMORY_SCOPE_CLUSTR 5{{$}}
// MSIL64: #define __MEMORY_SCOPE_DEVICE 1{{$}}
// MSIL64: #define __MEMORY_SCOPE_SINGLE 4{{$}}
// MSIL64: #define __MEMORY_SCOPE_SYSTEM 0{{$}}
// MSIL64: #define __MEMORY_SCOPE_WRKGRP 2{{$}}
// MSIL64: #define __MEMORY_SCOPE_WVFRNT 3{{$}}
// MSIL64: #define __MSIL__ 1{{$}}
// MSIL64: #define __NO_INLINE__ 1{{$}}
// MSIL64: #define __OBJC_BOOL_IS_BOOL 0{{$}}
// MSIL64: #define __OPENCL_MEMORY_SCOPE_ALL_SVM_DEVICES 3{{$}}
// MSIL64: #define __OPENCL_MEMORY_SCOPE_DEVICE 2{{$}}
// MSIL64: #define __OPENCL_MEMORY_SCOPE_SUB_GROUP 4{{$}}
// MSIL64: #define __OPENCL_MEMORY_SCOPE_WORK_GROUP 1{{$}}
// MSIL64: #define __OPENCL_MEMORY_SCOPE_WORK_ITEM 0{{$}}
// MSIL64: #define __ORDER_BIG_ENDIAN__ 4321{{$}}
// MSIL64: #define __ORDER_LITTLE_ENDIAN__ 1234{{$}}
// MSIL64: #define __ORDER_PDP_ENDIAN__ 3412{{$}}
// MSIL64: #define __POINTER_WIDTH__ 64{{$}}
// MSIL64: #define __PRAGMA_REDEFINE_EXTNAME 1{{$}}
// MSIL64: #define __PTRDIFF_FMTd__ "ld"{{$}}
// MSIL64: #define __PTRDIFF_FMTi__ "li"{{$}}
// MSIL64: #define __PTRDIFF_MAX__ 9223372036854775807L{{$}}
// MSIL64: #define __PTRDIFF_TYPE__ long int{{$}}
// MSIL64: #define __PTRDIFF_WIDTH__ 64{{$}}
// MSIL64: #define __SCHAR_MAX__ 127{{$}}
// MSIL64: #define __SHRT_MAX__ 32767{{$}}
// MSIL64: #define __SHRT_WIDTH__ 16{{$}}
// MSIL64: #define __SIZEOF_DOUBLE__ 8{{$}}
// MSIL64: #define __SIZEOF_FLOAT__ 4{{$}}
// MSIL64: #define __SIZEOF_INT128__ 16{{$}}
// MSIL64: #define __SIZEOF_INT__ 4{{$}}
// MSIL64: #define __SIZEOF_LONG_DOUBLE__ 8{{$}}
// MSIL64: #define __SIZEOF_LONG_LONG__ 8{{$}}
// MSIL64: #define __SIZEOF_LONG__ 8{{$}}
// MSIL64: #define __SIZEOF_POINTER__ 8{{$}}
// MSIL64: #define __SIZEOF_PTRDIFF_T__ 8{{$}}
// MSIL64: #define __SIZEOF_SHORT__ 2{{$}}
// MSIL64: #define __SIZEOF_SIZE_T__ 8{{$}}
// MSIL64: #define __SIZEOF_WCHAR_T__ 4{{$}}
// MSIL64: #define __SIZEOF_WINT_T__ 4{{$}}
// MSIL64: #define __SIZE_FMTX__ "lX"{{$}}
// MSIL64: #define __SIZE_FMTo__ "lo"{{$}}
// MSIL64: #define __SIZE_FMTu__ "lu"{{$}}
// MSIL64: #define __SIZE_FMTx__ "lx"{{$}}
// MSIL64: #define __SIZE_MAX__ 18446744073709551615UL{{$}}
// MSIL64: #define __SIZE_TYPE__ long unsigned int{{$}}
// MSIL64: #define __SIZE_WIDTH__ 64{{$}}
// MSIL64: #define __STDC_EMBED_EMPTY__ 2{{$}}
// MSIL64: #define __STDC_EMBED_FOUND__ 1{{$}}
// MSIL64: #define __STDC_EMBED_NOT_FOUND__ 0{{$}}
// MSIL64: #define __STDC_HOSTED__ 1{{$}}
// MSIL64: #define __STDC_UTF_16__ 1{{$}}
// MSIL64: #define __STDC_UTF_32__ 1{{$}}
// MSIL64: #define __STDC__ 1{{$}}
// MSIL64: #define __UINT16_C(c) c{{$}}
// MSIL64: #define __UINT16_C_SUFFIX__ {{$}}
// MSIL64: #define __UINT16_FMTX__ "hX"{{$}}
// MSIL64: #define __UINT16_FMTo__ "ho"{{$}}
// MSIL64: #define __UINT16_FMTu__ "hu"{{$}}
// MSIL64: #define __UINT16_FMTx__ "hx"{{$}}
// MSIL64: #define __UINT16_MAX__ 65535{{$}}
// MSIL64: #define __UINT16_TYPE__ unsigned short{{$}}
// MSIL64: #define __UINT32_C(c) c##U{{$}}
// MSIL64: #define __UINT32_C_SUFFIX__ U{{$}}
// MSIL64: #define __UINT32_FMTX__ "X"{{$}}
// MSIL64: #define __UINT32_FMTo__ "o"{{$}}
// MSIL64: #define __UINT32_FMTu__ "u"{{$}}
// MSIL64: #define __UINT32_FMTx__ "x"{{$}}
// MSIL64: #define __UINT32_MAX__ 4294967295U{{$}}
// MSIL64: #define __UINT32_TYPE__ unsigned int{{$}}
// MSIL64: #define __UINT64_C(c) c##ULL{{$}}
// MSIL64: #define __UINT64_C_SUFFIX__ ULL{{$}}
// MSIL64: #define __UINT64_FMTX__ "llX"{{$}}
// MSIL64: #define __UINT64_FMTo__ "llo"{{$}}
// MSIL64: #define __UINT64_FMTu__ "llu"{{$}}
// MSIL64: #define __UINT64_FMTx__ "llx"{{$}}
// MSIL64: #define __UINT64_MAX__ 18446744073709551615ULL{{$}}
// MSIL64: #define __UINT64_TYPE__ long long unsigned int{{$}}
// MSIL64: #define __UINT8_C(c) c{{$}}
// MSIL64: #define __UINT8_C_SUFFIX__ {{$}}
// MSIL64: #define __UINT8_FMTX__ "hhX"{{$}}
// MSIL64: #define __UINT8_FMTo__ "hho"{{$}}
// MSIL64: #define __UINT8_FMTu__ "hhu"{{$}}
// MSIL64: #define __UINT8_FMTx__ "hhx"{{$}}
// MSIL64: #define __UINT8_MAX__ 255{{$}}
// MSIL64: #define __UINT8_TYPE__ unsigned char{{$}}
// MSIL64: #define __UINTMAX_C(c) c##ULL{{$}}
// MSIL64: #define __UINTMAX_C_SUFFIX__ ULL{{$}}
// MSIL64: #define __UINTMAX_FMTX__ "llX"{{$}}
// MSIL64: #define __UINTMAX_FMTo__ "llo"{{$}}
// MSIL64: #define __UINTMAX_FMTu__ "llu"{{$}}
// MSIL64: #define __UINTMAX_FMTx__ "llx"{{$}}
// MSIL64: #define __UINTMAX_MAX__ 18446744073709551615ULL{{$}}
// MSIL64: #define __UINTMAX_TYPE__ long long unsigned int{{$}}
// MSIL64: #define __UINTMAX_WIDTH__ 64{{$}}
// MSIL64: #define __UINTPTR_FMTX__ "lX"{{$}}
// MSIL64: #define __UINTPTR_FMTo__ "lo"{{$}}
// MSIL64: #define __UINTPTR_FMTu__ "lu"{{$}}
// MSIL64: #define __UINTPTR_FMTx__ "lx"{{$}}
// MSIL64: #define __UINTPTR_MAX__ 18446744073709551615UL{{$}}
// MSIL64: #define __UINTPTR_TYPE__ long unsigned int{{$}}
// MSIL64: #define __UINTPTR_WIDTH__ 64{{$}}
// MSIL64: #define __UINT_FAST16_FMTX__ "hX"{{$}}
// MSIL64: #define __UINT_FAST16_FMTo__ "ho"{{$}}
// MSIL64: #define __UINT_FAST16_FMTu__ "hu"{{$}}
// MSIL64: #define __UINT_FAST16_FMTx__ "hx"{{$}}
// MSIL64: #define __UINT_FAST16_MAX__ 65535{{$}}
// MSIL64: #define __UINT_FAST16_TYPE__ unsigned short{{$}}
// MSIL64: #define __UINT_FAST32_FMTX__ "X"{{$}}
// MSIL64: #define __UINT_FAST32_FMTo__ "o"{{$}}
// MSIL64: #define __UINT_FAST32_FMTu__ "u"{{$}}
// MSIL64: #define __UINT_FAST32_FMTx__ "x"{{$}}
// MSIL64: #define __UINT_FAST32_MAX__ 4294967295U{{$}}
// MSIL64: #define __UINT_FAST32_TYPE__ unsigned int{{$}}
// MSIL64: #define __UINT_FAST64_FMTX__ "llX"{{$}}
// MSIL64: #define __UINT_FAST64_FMTo__ "llo"{{$}}
// MSIL64: #define __UINT_FAST64_FMTu__ "llu"{{$}}
// MSIL64: #define __UINT_FAST64_FMTx__ "llx"{{$}}
// MSIL64: #define __UINT_FAST64_MAX__ 18446744073709551615ULL{{$}}
// MSIL64: #define __UINT_FAST64_TYPE__ long long unsigned int{{$}}
// MSIL64: #define __UINT_FAST8_FMTX__ "hhX"{{$}}
// MSIL64: #define __UINT_FAST8_FMTo__ "hho"{{$}}
// MSIL64: #define __UINT_FAST8_FMTu__ "hhu"{{$}}
// MSIL64: #define __UINT_FAST8_FMTx__ "hhx"{{$}}
// MSIL64: #define __UINT_FAST8_MAX__ 255{{$}}
// MSIL64: #define __UINT_FAST8_TYPE__ unsigned char{{$}}
// MSIL64: #define __UINT_LEAST16_FMTX__ "hX"{{$}}
// MSIL64: #define __UINT_LEAST16_FMTo__ "ho"{{$}}
// MSIL64: #define __UINT_LEAST16_FMTu__ "hu"{{$}}
// MSIL64: #define __UINT_LEAST16_FMTx__ "hx"{{$}}
// MSIL64: #define __UINT_LEAST16_MAX__ 65535{{$}}
// MSIL64: #define __UINT_LEAST16_TYPE__ unsigned short{{$}}
// MSIL64: #define __UINT_LEAST32_FMTX__ "X"{{$}}
// MSIL64: #define __UINT_LEAST32_FMTo__ "o"{{$}}
// MSIL64: #define __UINT_LEAST32_FMTu__ "u"{{$}}
// MSIL64: #define __UINT_LEAST32_FMTx__ "x"{{$}}
// MSIL64: #define __UINT_LEAST32_MAX__ 4294967295U{{$}}
// MSIL64: #define __UINT_LEAST32_TYPE__ unsigned int{{$}}
// MSIL64: #define __UINT_LEAST64_FMTX__ "llX"{{$}}
// MSIL64: #define __UINT_LEAST64_FMTo__ "llo"{{$}}
// MSIL64: #define __UINT_LEAST64_FMTu__ "llu"{{$}}
// MSIL64: #define __UINT_LEAST64_FMTx__ "llx"{{$}}
// MSIL64: #define __UINT_LEAST64_MAX__ 18446744073709551615ULL{{$}}
// MSIL64: #define __UINT_LEAST64_TYPE__ long long unsigned int{{$}}
// MSIL64: #define __UINT_LEAST8_FMTX__ "hhX"{{$}}
// MSIL64: #define __UINT_LEAST8_FMTo__ "hho"{{$}}
// MSIL64: #define __UINT_LEAST8_FMTu__ "hhu"{{$}}
// MSIL64: #define __UINT_LEAST8_FMTx__ "hhx"{{$}}
// MSIL64: #define __UINT_LEAST8_MAX__ 255{{$}}
// MSIL64: #define __UINT_LEAST8_TYPE__ unsigned char{{$}}
// MSIL64: #define __USER_LABEL_PREFIX__ {{$}}
// MSIL64: #define __WCHAR_MAX__ 2147483647{{$}}
// MSIL64: #define __WCHAR_MIN__ (-__WCHAR_MAX__ - 1){{$}}
// MSIL64: #define __WCHAR_TYPE__ int{{$}}
// MSIL64: #define __WCHAR_WIDTH__ 32{{$}}
// MSIL64: #define __WINT_MAX__ 2147483647{{$}}
// MSIL64: #define __WINT_MIN__ (-__WINT_MAX__ - 1){{$}}
// MSIL64: #define __WINT_TYPE__ int{{$}}
// MSIL64: #define __WINT_WIDTH__ 32{{$}}
// MSIL64: #define __clang__ 1{{$}}
// MSIL64: #define __clang_literal_encoding__ "UTF-8"{{$}}
// MSIL64: #define __clang_wide_literal_encoding__ "UTF-32"{{$}}
// MSIL64: #define __dotnet__ 1{{$}}
// MSIL64: #define __llvm__ 1{{$}}
// MSIL64: #define __msil64__ 1{{$}}

// MSIL32: #define _ILP32 1{{$}}
// MSIL32: #define __ATOMIC_ACQUIRE 2{{$}}
// MSIL32: #define __ATOMIC_ACQ_REL 4{{$}}
// MSIL32: #define __ATOMIC_CONSUME 1{{$}}
// MSIL32: #define __ATOMIC_RELAXED 0{{$}}
// MSIL32: #define __ATOMIC_RELEASE 3{{$}}
// MSIL32: #define __ATOMIC_SEQ_CST 5{{$}}
// MSIL32: #define __BIGGEST_ALIGNMENT__ 8{{$}}
// MSIL32: #define __BITINT_MAXWIDTH__ 8388608{{$}}
// MSIL32: #define __BOOL_WIDTH__ 1{{$}}
// MSIL32: #define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__{{$}}
// MSIL32: #define __CHAR16_TYPE__ unsigned short{{$}}
// MSIL32: #define __CHAR32_TYPE__ unsigned int{{$}}
// MSIL32: #define __CHAR_BIT__ 8{{$}}
// MSIL32: #define __CLANG_ATOMIC_BOOL_LOCK_FREE 2{{$}}
// MSIL32: #define __CLANG_ATOMIC_CHAR16_T_LOCK_FREE 2{{$}}
// MSIL32: #define __CLANG_ATOMIC_CHAR32_T_LOCK_FREE 2{{$}}
// MSIL32: #define __CLANG_ATOMIC_CHAR_LOCK_FREE 2{{$}}
// MSIL32: #define __CLANG_ATOMIC_INT_LOCK_FREE 2{{$}}
// MSIL32: #define __CLANG_ATOMIC_LLONG_LOCK_FREE 2{{$}}
// MSIL32: #define __CLANG_ATOMIC_LONG_LOCK_FREE 2{{$}}
// MSIL32: #define __CLANG_ATOMIC_POINTER_LOCK_FREE 2{{$}}
// MSIL32: #define __CLANG_ATOMIC_SHORT_LOCK_FREE 2{{$}}
// MSIL32: #define __CLANG_ATOMIC_WCHAR_T_LOCK_FREE 2{{$}}
// MSIL32: #define __CONSTANT_CFSTRINGS__ 1{{$}}
// MSIL32: #define __DBL_DECIMAL_DIG__ 17{{$}}
// MSIL32: #define __DBL_DENORM_MIN__ 4.9406564584124654e-324{{$}}
// MSIL32: #define __DBL_DIG__ 15{{$}}
// MSIL32: #define __DBL_EPSILON__ 2.2204460492503131e-16{{$}}
// MSIL32: #define __DBL_HAS_DENORM__ 1{{$}}
// MSIL32: #define __DBL_HAS_INFINITY__ 1{{$}}
// MSIL32: #define __DBL_HAS_QUIET_NAN__ 1{{$}}
// MSIL32: #define __DBL_MANT_DIG__ 53{{$}}
// MSIL32: #define __DBL_MAX_10_EXP__ 308{{$}}
// MSIL32: #define __DBL_MAX_EXP__ 1024{{$}}
// MSIL32: #define __DBL_MAX__ 1.7976931348623157e+308{{$}}
// MSIL32: #define __DBL_MIN_10_EXP__ (-307){{$}}
// MSIL32: #define __DBL_MIN_EXP__ (-1021){{$}}
// MSIL32: #define __DBL_MIN__ 2.2250738585072014e-308{{$}}
// MSIL32: #define __DBL_NORM_MAX__ 1.7976931348623157e+308{{$}}
// MSIL32: #define __DECIMAL_DIG__ __LDBL_DECIMAL_DIG__{{$}}
// MSIL32: #define __FINITE_MATH_ONLY__ 0{{$}}
// MSIL32: #define __FLT16_DECIMAL_DIG__ 5{{$}}
// MSIL32: #define __FLT16_DENORM_MIN__ 5.9604644775390625e-8F16{{$}}
// MSIL32: #define __FLT16_DIG__ 3{{$}}
// MSIL32: #define __FLT16_EPSILON__ 9.765625e-4F16{{$}}
// MSIL32: #define __FLT16_HAS_DENORM__ 1{{$}}
// MSIL32: #define __FLT16_HAS_INFINITY__ 1{{$}}
// MSIL32: #define __FLT16_HAS_QUIET_NAN__ 1{{$}}
// MSIL32: #define __FLT16_MANT_DIG__ 11{{$}}
// MSIL32: #define __FLT16_MAX_10_EXP__ 4{{$}}
// MSIL32: #define __FLT16_MAX_EXP__ 16{{$}}
// MSIL32: #define __FLT16_MAX__ 6.5504e+4F16{{$}}
// MSIL32: #define __FLT16_MIN_10_EXP__ (-4){{$}}
// MSIL32: #define __FLT16_MIN_EXP__ (-13){{$}}
// MSIL32: #define __FLT16_MIN__ 6.103515625e-5F16{{$}}
// MSIL32: #define __FLT16_NORM_MAX__ 6.5504e+4F16{{$}}
// MSIL32: #define __FLT_DECIMAL_DIG__ 9{{$}}
// MSIL32: #define __FLT_DENORM_MIN__ 1.40129846e-45F{{$}}
// MSIL32: #define __FLT_DIG__ 6{{$}}
// MSIL32: #define __FLT_EPSILON__ 1.19209290e-7F{{$}}
// MSIL32: #define __FLT_HAS_DENORM__ 1{{$}}
// MSIL32: #define __FLT_HAS_INFINITY__ 1{{$}}
// MSIL32: #define __FLT_HAS_QUIET_NAN__ 1{{$}}
// MSIL32: #define __FLT_MANT_DIG__ 24{{$}}
// MSIL32: #define __FLT_MAX_10_EXP__ 38{{$}}
// MSIL32: #define __FLT_MAX_EXP__ 128{{$}}
// MSIL32: #define __FLT_MAX__ 3.40282347e+38F{{$}}
// MSIL32: #define __FLT_MIN_10_EXP__ (-37){{$}}
// MSIL32: #define __FLT_MIN_EXP__ (-125){{$}}
// MSIL32: #define __FLT_MIN__ 1.17549435e-38F{{$}}
// MSIL32: #define __FLT_NORM_MAX__ 3.40282347e+38F{{$}}
// MSIL32: #define __FLT_RADIX__ 2{{$}}
// MSIL32: #define __FPCLASS_NEGINF 0x0004{{$}}
// MSIL32: #define __FPCLASS_NEGNORMAL 0x0008{{$}}
// MSIL32: #define __FPCLASS_NEGSUBNORMAL 0x0010{{$}}
// MSIL32: #define __FPCLASS_NEGZERO 0x0020{{$}}
// MSIL32: #define __FPCLASS_POSINF 0x0200{{$}}
// MSIL32: #define __FPCLASS_POSNORMAL 0x0100{{$}}
// MSIL32: #define __FPCLASS_POSSUBNORMAL 0x0080{{$}}
// MSIL32: #define __FPCLASS_POSZERO 0x0040{{$}}
// MSIL32: #define __FPCLASS_QNAN 0x0002{{$}}
// MSIL32: #define __FPCLASS_SNAN 0x0001{{$}}
// MSIL32: #define __GCC_ATOMIC_BOOL_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_ATOMIC_CHAR16_T_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_ATOMIC_CHAR32_T_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_ATOMIC_CHAR_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_ATOMIC_INT_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_ATOMIC_LLONG_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_ATOMIC_LONG_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_ATOMIC_POINTER_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_ATOMIC_SHORT_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_ATOMIC_TEST_AND_SET_TRUEVAL 1{{$}}
// MSIL32: #define __GCC_ATOMIC_WCHAR_T_LOCK_FREE 2{{$}}
// MSIL32: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_1 1{{$}}
// MSIL32: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_2 1{{$}}
// MSIL32: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4 1{{$}}
// MSIL32: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 1{{$}}
// MSIL32: #define __GNUC_MINOR__ 2{{$}}
// MSIL32: #define __GNUC_PATCHLEVEL__ 1{{$}}
// MSIL32: #define __GNUC_STDC_INLINE__ 1{{$}}
// MSIL32: #define __GNUC__ 4{{$}}
// MSIL32: #define __GXX_ABI_VERSION 1002{{$}}
// MSIL32: #define __ILP32__ 1{{$}}
// MSIL32: #define __INT16_C(c) c{{$}}
// MSIL32: #define __INT16_C_SUFFIX__ {{$}}
// MSIL32: #define __INT16_FMTd__ "hd"{{$}}
// MSIL32: #define __INT16_FMTi__ "hi"{{$}}
// MSIL32: #define __INT16_MAX__ 32767{{$}}
// MSIL32: #define __INT16_TYPE__ short{{$}}
// MSIL32: #define __INT32_C(c) c{{$}}
// MSIL32: #define __INT32_C_SUFFIX__ {{$}}
// MSIL32: #define __INT32_FMTd__ "d"{{$}}
// MSIL32: #define __INT32_FMTi__ "i"{{$}}
// MSIL32: #define __INT32_MAX__ 2147483647{{$}}
// MSIL32: #define __INT32_TYPE__ int{{$}}
// MSIL32: #define __INT64_C(c) c##LL{{$}}
// MSIL32: #define __INT64_C_SUFFIX__ LL{{$}}
// MSIL32: #define __INT64_FMTd__ "lld"{{$}}
// MSIL32: #define __INT64_FMTi__ "lli"{{$}}
// MSIL32: #define __INT64_MAX__ 9223372036854775807LL{{$}}
// MSIL32: #define __INT64_TYPE__ long long int{{$}}
// MSIL32: #define __INT8_C(c) c{{$}}
// MSIL32: #define __INT8_C_SUFFIX__ {{$}}
// MSIL32: #define __INT8_FMTd__ "hhd"{{$}}
// MSIL32: #define __INT8_FMTi__ "hhi"{{$}}
// MSIL32: #define __INT8_MAX__ 127{{$}}
// MSIL32: #define __INT8_TYPE__ signed char{{$}}
// MSIL32: #define __INTMAX_C(c) c##LL{{$}}
// MSIL32: #define __INTMAX_C_SUFFIX__ LL{{$}}
// MSIL32: #define __INTMAX_FMTd__ "lld"{{$}}
// MSIL32: #define __INTMAX_FMTi__ "lli"{{$}}
// MSIL32: #define __INTMAX_MAX__ 9223372036854775807LL{{$}}
// MSIL32: #define __INTMAX_TYPE__ long long int{{$}}
// MSIL32: #define __INTMAX_WIDTH__ 64{{$}}
// MSIL32: #define __INTPTR_FMTd__ "ld"{{$}}
// MSIL32: #define __INTPTR_FMTi__ "li"{{$}}
// MSIL32: #define __INTPTR_MAX__ 2147483647L{{$}}
// MSIL32: #define __INTPTR_TYPE__ long int{{$}}
// MSIL32: #define __INTPTR_WIDTH__ 32{{$}}
// MSIL32: #define __INT_FAST16_FMTd__ "hd"{{$}}
// MSIL32: #define __INT_FAST16_FMTi__ "hi"{{$}}
// MSIL32: #define __INT_FAST16_MAX__ 32767{{$}}
// MSIL32: #define __INT_FAST16_TYPE__ short{{$}}
// MSIL32: #define __INT_FAST16_WIDTH__ 16{{$}}
// MSIL32: #define __INT_FAST32_FMTd__ "d"{{$}}
// MSIL32: #define __INT_FAST32_FMTi__ "i"{{$}}
// MSIL32: #define __INT_FAST32_MAX__ 2147483647{{$}}
// MSIL32: #define __INT_FAST32_TYPE__ int{{$}}
// MSIL32: #define __INT_FAST32_WIDTH__ 32{{$}}
// MSIL32: #define __INT_FAST64_FMTd__ "lld"{{$}}
// MSIL32: #define __INT_FAST64_FMTi__ "lli"{{$}}
// MSIL32: #define __INT_FAST64_MAX__ 9223372036854775807LL{{$}}
// MSIL32: #define __INT_FAST64_TYPE__ long long int{{$}}
// MSIL32: #define __INT_FAST64_WIDTH__ 64{{$}}
// MSIL32: #define __INT_FAST8_FMTd__ "hhd"{{$}}
// MSIL32: #define __INT_FAST8_FMTi__ "hhi"{{$}}
// MSIL32: #define __INT_FAST8_MAX__ 127{{$}}
// MSIL32: #define __INT_FAST8_TYPE__ signed char{{$}}
// MSIL32: #define __INT_FAST8_WIDTH__ 8{{$}}
// MSIL32: #define __INT_LEAST16_FMTd__ "hd"{{$}}
// MSIL32: #define __INT_LEAST16_FMTi__ "hi"{{$}}
// MSIL32: #define __INT_LEAST16_MAX__ 32767{{$}}
// MSIL32: #define __INT_LEAST16_TYPE__ short{{$}}
// MSIL32: #define __INT_LEAST16_WIDTH__ 16{{$}}
// MSIL32: #define __INT_LEAST32_FMTd__ "d"{{$}}
// MSIL32: #define __INT_LEAST32_FMTi__ "i"{{$}}
// MSIL32: #define __INT_LEAST32_MAX__ 2147483647{{$}}
// MSIL32: #define __INT_LEAST32_TYPE__ int{{$}}
// MSIL32: #define __INT_LEAST32_WIDTH__ 32{{$}}
// MSIL32: #define __INT_LEAST64_FMTd__ "lld"{{$}}
// MSIL32: #define __INT_LEAST64_FMTi__ "lli"{{$}}
// MSIL32: #define __INT_LEAST64_MAX__ 9223372036854775807LL{{$}}
// MSIL32: #define __INT_LEAST64_TYPE__ long long int{{$}}
// MSIL32: #define __INT_LEAST64_WIDTH__ 64{{$}}
// MSIL32: #define __INT_LEAST8_FMTd__ "hhd"{{$}}
// MSIL32: #define __INT_LEAST8_FMTi__ "hhi"{{$}}
// MSIL32: #define __INT_LEAST8_MAX__ 127{{$}}
// MSIL32: #define __INT_LEAST8_TYPE__ signed char{{$}}
// MSIL32: #define __INT_LEAST8_WIDTH__ 8{{$}}
// MSIL32: #define __INT_MAX__ 2147483647{{$}}
// MSIL32: #define __INT_WIDTH__ 32{{$}}
// MSIL32: #define __LDBL_DECIMAL_DIG__ 17{{$}}
// MSIL32: #define __LDBL_DENORM_MIN__ 4.9406564584124654e-324L{{$}}
// MSIL32: #define __LDBL_DIG__ 15{{$}}
// MSIL32: #define __LDBL_EPSILON__ 2.2204460492503131e-16L{{$}}
// MSIL32: #define __LDBL_HAS_DENORM__ 1{{$}}
// MSIL32: #define __LDBL_HAS_INFINITY__ 1{{$}}
// MSIL32: #define __LDBL_HAS_QUIET_NAN__ 1{{$}}
// MSIL32: #define __LDBL_MANT_DIG__ 53{{$}}
// MSIL32: #define __LDBL_MAX_10_EXP__ 308{{$}}
// MSIL32: #define __LDBL_MAX_EXP__ 1024{{$}}
// MSIL32: #define __LDBL_MAX__ 1.7976931348623157e+308L{{$}}
// MSIL32: #define __LDBL_MIN_10_EXP__ (-307){{$}}
// MSIL32: #define __LDBL_MIN_EXP__ (-1021){{$}}
// MSIL32: #define __LDBL_MIN__ 2.2250738585072014e-308L{{$}}
// MSIL32: #define __LDBL_NORM_MAX__ 1.7976931348623157e+308L{{$}}
// MSIL32: #define __LITTLE_ENDIAN__ 1{{$}}
// MSIL32: #define __LLONG_WIDTH__ 64{{$}}
// MSIL32: #define __LONG_LONG_MAX__ 9223372036854775807LL{{$}}
// MSIL32: #define __LONG_MAX__ 2147483647L{{$}}
// MSIL32: #define __LONG_WIDTH__ 32{{$}}
// MSIL32: #define __MEMORY_SCOPE_CLUSTR 5{{$}}
// MSIL32: #define __MEMORY_SCOPE_DEVICE 1{{$}}
// MSIL32: #define __MEMORY_SCOPE_SINGLE 4{{$}}
// MSIL32: #define __MEMORY_SCOPE_SYSTEM 0{{$}}
// MSIL32: #define __MEMORY_SCOPE_WRKGRP 2{{$}}
// MSIL32: #define __MEMORY_SCOPE_WVFRNT 3{{$}}
// MSIL32: #define __MSIL__ 1{{$}}
// MSIL32: #define __NO_INLINE__ 1{{$}}
// MSIL32: #define __OBJC_BOOL_IS_BOOL 0{{$}}
// MSIL32: #define __OPENCL_MEMORY_SCOPE_ALL_SVM_DEVICES 3{{$}}
// MSIL32: #define __OPENCL_MEMORY_SCOPE_DEVICE 2{{$}}
// MSIL32: #define __OPENCL_MEMORY_SCOPE_SUB_GROUP 4{{$}}
// MSIL32: #define __OPENCL_MEMORY_SCOPE_WORK_GROUP 1{{$}}
// MSIL32: #define __OPENCL_MEMORY_SCOPE_WORK_ITEM 0{{$}}
// MSIL32: #define __ORDER_BIG_ENDIAN__ 4321{{$}}
// MSIL32: #define __ORDER_LITTLE_ENDIAN__ 1234{{$}}
// MSIL32: #define __ORDER_PDP_ENDIAN__ 3412{{$}}
// MSIL32: #define __POINTER_WIDTH__ 32{{$}}
// MSIL32: #define __PRAGMA_REDEFINE_EXTNAME 1{{$}}
// MSIL32: #define __PTRDIFF_FMTd__ "ld"{{$}}
// MSIL32: #define __PTRDIFF_FMTi__ "li"{{$}}
// MSIL32: #define __PTRDIFF_MAX__ 2147483647L{{$}}
// MSIL32: #define __PTRDIFF_TYPE__ long int{{$}}
// MSIL32: #define __PTRDIFF_WIDTH__ 32{{$}}
// MSIL32: #define __SCHAR_MAX__ 127{{$}}
// MSIL32: #define __SHRT_MAX__ 32767{{$}}
// MSIL32: #define __SHRT_WIDTH__ 16{{$}}
// MSIL32: #define __SIZEOF_DOUBLE__ 8{{$}}
// MSIL32: #define __SIZEOF_FLOAT__ 4{{$}}
// MSIL32: #define __SIZEOF_INT__ 4{{$}}
// MSIL32: #define __SIZEOF_LONG_DOUBLE__ 8{{$}}
// MSIL32: #define __SIZEOF_LONG_LONG__ 8{{$}}
// MSIL32: #define __SIZEOF_LONG__ 4{{$}}
// MSIL32: #define __SIZEOF_POINTER__ 4{{$}}
// MSIL32: #define __SIZEOF_PTRDIFF_T__ 4{{$}}
// MSIL32: #define __SIZEOF_SHORT__ 2{{$}}
// MSIL32: #define __SIZEOF_SIZE_T__ 4{{$}}
// MSIL32: #define __SIZEOF_WCHAR_T__ 4{{$}}
// MSIL32: #define __SIZEOF_WINT_T__ 4{{$}}
// MSIL32: #define __SIZE_FMTX__ "lX"{{$}}
// MSIL32: #define __SIZE_FMTo__ "lo"{{$}}
// MSIL32: #define __SIZE_FMTu__ "lu"{{$}}
// MSIL32: #define __SIZE_FMTx__ "lx"{{$}}
// MSIL32: #define __SIZE_MAX__ 4294967295UL{{$}}
// MSIL32: #define __SIZE_TYPE__ long unsigned int{{$}}
// MSIL32: #define __SIZE_WIDTH__ 32{{$}}
// MSIL32: #define __STDC_EMBED_EMPTY__ 2{{$}}
// MSIL32: #define __STDC_EMBED_FOUND__ 1{{$}}
// MSIL32: #define __STDC_EMBED_NOT_FOUND__ 0{{$}}
// MSIL32: #define __STDC_HOSTED__ 1{{$}}
// MSIL32: #define __STDC_UTF_16__ 1{{$}}
// MSIL32: #define __STDC_UTF_32__ 1{{$}}
// MSIL32: #define __STDC__ 1{{$}}
// MSIL32: #define __UINT16_C(c) c{{$}}
// MSIL32: #define __UINT16_C_SUFFIX__ {{$}}
// MSIL32: #define __UINT16_FMTX__ "hX"{{$}}
// MSIL32: #define __UINT16_FMTo__ "ho"{{$}}
// MSIL32: #define __UINT16_FMTu__ "hu"{{$}}
// MSIL32: #define __UINT16_FMTx__ "hx"{{$}}
// MSIL32: #define __UINT16_MAX__ 65535{{$}}
// MSIL32: #define __UINT16_TYPE__ unsigned short{{$}}
// MSIL32: #define __UINT32_C(c) c##U{{$}}
// MSIL32: #define __UINT32_C_SUFFIX__ U{{$}}
// MSIL32: #define __UINT32_FMTX__ "X"{{$}}
// MSIL32: #define __UINT32_FMTo__ "o"{{$}}
// MSIL32: #define __UINT32_FMTu__ "u"{{$}}
// MSIL32: #define __UINT32_FMTx__ "x"{{$}}
// MSIL32: #define __UINT32_MAX__ 4294967295U{{$}}
// MSIL32: #define __UINT32_TYPE__ unsigned int{{$}}
// MSIL32: #define __UINT64_C(c) c##ULL{{$}}
// MSIL32: #define __UINT64_C_SUFFIX__ ULL{{$}}
// MSIL32: #define __UINT64_FMTX__ "llX"{{$}}
// MSIL32: #define __UINT64_FMTo__ "llo"{{$}}
// MSIL32: #define __UINT64_FMTu__ "llu"{{$}}
// MSIL32: #define __UINT64_FMTx__ "llx"{{$}}
// MSIL32: #define __UINT64_MAX__ 18446744073709551615ULL{{$}}
// MSIL32: #define __UINT64_TYPE__ long long unsigned int{{$}}
// MSIL32: #define __UINT8_C(c) c{{$}}
// MSIL32: #define __UINT8_C_SUFFIX__ {{$}}
// MSIL32: #define __UINT8_FMTX__ "hhX"{{$}}
// MSIL32: #define __UINT8_FMTo__ "hho"{{$}}
// MSIL32: #define __UINT8_FMTu__ "hhu"{{$}}
// MSIL32: #define __UINT8_FMTx__ "hhx"{{$}}
// MSIL32: #define __UINT8_MAX__ 255{{$}}
// MSIL32: #define __UINT8_TYPE__ unsigned char{{$}}
// MSIL32: #define __UINTMAX_C(c) c##ULL{{$}}
// MSIL32: #define __UINTMAX_C_SUFFIX__ ULL{{$}}
// MSIL32: #define __UINTMAX_FMTX__ "llX"{{$}}
// MSIL32: #define __UINTMAX_FMTo__ "llo"{{$}}
// MSIL32: #define __UINTMAX_FMTu__ "llu"{{$}}
// MSIL32: #define __UINTMAX_FMTx__ "llx"{{$}}
// MSIL32: #define __UINTMAX_MAX__ 18446744073709551615ULL{{$}}
// MSIL32: #define __UINTMAX_TYPE__ long long unsigned int{{$}}
// MSIL32: #define __UINTMAX_WIDTH__ 64{{$}}
// MSIL32: #define __UINTPTR_FMTX__ "lX"{{$}}
// MSIL32: #define __UINTPTR_FMTo__ "lo"{{$}}
// MSIL32: #define __UINTPTR_FMTu__ "lu"{{$}}
// MSIL32: #define __UINTPTR_FMTx__ "lx"{{$}}
// MSIL32: #define __UINTPTR_MAX__ 4294967295UL{{$}}
// MSIL32: #define __UINTPTR_TYPE__ long unsigned int{{$}}
// MSIL32: #define __UINTPTR_WIDTH__ 32{{$}}
// MSIL32: #define __UINT_FAST16_FMTX__ "hX"{{$}}
// MSIL32: #define __UINT_FAST16_FMTo__ "ho"{{$}}
// MSIL32: #define __UINT_FAST16_FMTu__ "hu"{{$}}
// MSIL32: #define __UINT_FAST16_FMTx__ "hx"{{$}}
// MSIL32: #define __UINT_FAST16_MAX__ 65535{{$}}
// MSIL32: #define __UINT_FAST16_TYPE__ unsigned short{{$}}
// MSIL32: #define __UINT_FAST32_FMTX__ "X"{{$}}
// MSIL32: #define __UINT_FAST32_FMTo__ "o"{{$}}
// MSIL32: #define __UINT_FAST32_FMTu__ "u"{{$}}
// MSIL32: #define __UINT_FAST32_FMTx__ "x"{{$}}
// MSIL32: #define __UINT_FAST32_MAX__ 4294967295U{{$}}
// MSIL32: #define __UINT_FAST32_TYPE__ unsigned int{{$}}
// MSIL32: #define __UINT_FAST64_FMTX__ "llX"{{$}}
// MSIL32: #define __UINT_FAST64_FMTo__ "llo"{{$}}
// MSIL32: #define __UINT_FAST64_FMTu__ "llu"{{$}}
// MSIL32: #define __UINT_FAST64_FMTx__ "llx"{{$}}
// MSIL32: #define __UINT_FAST64_MAX__ 18446744073709551615ULL{{$}}
// MSIL32: #define __UINT_FAST64_TYPE__ long long unsigned int{{$}}
// MSIL32: #define __UINT_FAST8_FMTX__ "hhX"{{$}}
// MSIL32: #define __UINT_FAST8_FMTo__ "hho"{{$}}
// MSIL32: #define __UINT_FAST8_FMTu__ "hhu"{{$}}
// MSIL32: #define __UINT_FAST8_FMTx__ "hhx"{{$}}
// MSIL32: #define __UINT_FAST8_MAX__ 255{{$}}
// MSIL32: #define __UINT_FAST8_TYPE__ unsigned char{{$}}
// MSIL32: #define __UINT_LEAST16_FMTX__ "hX"{{$}}
// MSIL32: #define __UINT_LEAST16_FMTo__ "ho"{{$}}
// MSIL32: #define __UINT_LEAST16_FMTu__ "hu"{{$}}
// MSIL32: #define __UINT_LEAST16_FMTx__ "hx"{{$}}
// MSIL32: #define __UINT_LEAST16_MAX__ 65535{{$}}
// MSIL32: #define __UINT_LEAST16_TYPE__ unsigned short{{$}}
// MSIL32: #define __UINT_LEAST32_FMTX__ "X"{{$}}
// MSIL32: #define __UINT_LEAST32_FMTo__ "o"{{$}}
// MSIL32: #define __UINT_LEAST32_FMTu__ "u"{{$}}
// MSIL32: #define __UINT_LEAST32_FMTx__ "x"{{$}}
// MSIL32: #define __UINT_LEAST32_MAX__ 4294967295U{{$}}
// MSIL32: #define __UINT_LEAST32_TYPE__ unsigned int{{$}}
// MSIL32: #define __UINT_LEAST64_FMTX__ "llX"{{$}}
// MSIL32: #define __UINT_LEAST64_FMTo__ "llo"{{$}}
// MSIL32: #define __UINT_LEAST64_FMTu__ "llu"{{$}}
// MSIL32: #define __UINT_LEAST64_FMTx__ "llx"{{$}}
// MSIL32: #define __UINT_LEAST64_MAX__ 18446744073709551615ULL{{$}}
// MSIL32: #define __UINT_LEAST64_TYPE__ long long unsigned int{{$}}
// MSIL32: #define __UINT_LEAST8_FMTX__ "hhX"{{$}}
// MSIL32: #define __UINT_LEAST8_FMTo__ "hho"{{$}}
// MSIL32: #define __UINT_LEAST8_FMTu__ "hhu"{{$}}
// MSIL32: #define __UINT_LEAST8_FMTx__ "hhx"{{$}}
// MSIL32: #define __UINT_LEAST8_MAX__ 255{{$}}
// MSIL32: #define __UINT_LEAST8_TYPE__ unsigned char{{$}}
// MSIL32: #define __USER_LABEL_PREFIX__ {{$}}
// MSIL32: #define __WCHAR_MAX__ 2147483647{{$}}
// MSIL32: #define __WCHAR_MIN__ (-__WCHAR_MAX__ - 1){{$}}
// MSIL32: #define __WCHAR_TYPE__ int{{$}}
// MSIL32: #define __WCHAR_WIDTH__ 32{{$}}
// MSIL32: #define __WINT_MAX__ 2147483647{{$}}
// MSIL32: #define __WINT_MIN__ (-__WINT_MAX__ - 1){{$}}
// MSIL32: #define __WINT_TYPE__ int{{$}}
// MSIL32: #define __WINT_WIDTH__ 32{{$}}
// MSIL32: #define __clang__ 1{{$}}
// MSIL32: #define __clang_literal_encoding__ "UTF-8"{{$}}
// MSIL32: #define __clang_wide_literal_encoding__ "UTF-32"{{$}}
// MSIL32: #define __dotnet__ 1{{$}}
// MSIL32: #define __llvm__ 1{{$}}
// MSIL32: #define __msil32__ 1{{$}}
