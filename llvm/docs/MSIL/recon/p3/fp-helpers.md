# Phase 3 recon: floating-point and integer helpers (f16, bf16, C complex, compiler-rt libcalls, i128, long double)

Key: `fp-helpers` (P3-B). Recon at HEAD `cc4c79787d6a` (M23), branch `msil`, 2026-10-28. Supporting files:
[fp-helpers-files/](fp-helpers-files/) (sources, scripts, trimmed results; `results.txt` holds every number quoted below
under its experiment id F1-F14). Scratch: `C:\llvm-msil-scratch\p3\fp-helpers`. Companion reports: the
[helpers-switches](helpers-switches.md) recon (the helper archive, the switch carrier, BCL intrinsics; its E1-E18 are
cited as HS-E*n*) and the [variadics](variadics.md) recon (`%Lf`, item 3.6 (9)).

Runtimes: .NET Framework 4.8 (4.0.30319.42000), .NET 8.0.33 and 10.0.14 on Windows 11, .NET 8.0.31 and 10.0.13 under
WSL Ubuntu (`msil/utils/msil-wsl-dotnet.sh`). Compilers: the shared build's clang 24 (`build\bin`, never rebuilt).
Native oracles: this clang for `x86_64-pc-windows-msvc` linked by lld-link with the Visual Studio LLVM 22
`clang_rt.builtins-x86_64.lib` ("VS compiler-rt") or with this tree's compiler-rt built natively ("tree compiler-rt"),
and for `x86_64-unknown-linux-gnu` linked by ld.lld with `msil/utils/linux-oracle/start.c` and the E7 sysroot's
`libm.so.6`, `libc.so.6` and `libgcc_s.so.1` ("libgcc", the oracle the lit suite uses) or with the native tree
compiler-rt archive instead of libgcc_s, run under WSL. MSIL helpers come from a 30-source compiler-rt subset compiled
by this clang for each MSIL triple (F3). The host was loaded by other projects throughout (load average 2.5-3.4 in
WSL): wall times are best-of-3 whole-process times and vary by 30 percent or more; ratios within one batch are more
reliable than absolute numbers. Installs: none (the C# probes used the installed SDKs; ildasm/ilasm 10.0.12 from the
existing NuGet cache).

---

## 0. Summary

1. **Inventory (F1, section 1).** 107 C and IR cases, two MSIL triples, `-O0` and `-O2`, `-S` and `-c`, plus the native
   twins. In `-S` mode every compiler-rt name is a compile error; in object mode (`-c`, the default pipeline since
   M23) it is an undefined symbol that msil-ld would resolve from a helper archive, **except** the constrained
   FP -> i128 conversions, which `MSILTargetLowering::ReplaceNodeResults` (`MSILISelLowering.cpp:710-795`) reports at
   compile time in both modes. The helper names met are exactly native clang's for the same triple, with five
   MSIL-specific deviations: (a) bf16 -> f32 calls `__extendbfsf2` where native code shifts inline; (b) f16 -> i128
   and i128 -> f16 go through `__extendhfsf2`/`__truncsfhf2` and an inline i128 expansion where native Linux calls
   `__fixhfti`/`__floattihf`; (c) `long double _Complex` uses `__muldc3`/`__divdc3` on Linux (native: `__mulxc3`),
   because `long double` is `double`; (d) `llvm.clear_cache` calls `__clear_cache` (native x86 emits nothing);
   (e) every i128 division and FP <-> i128 conversion is inline (M12) where native code calls `__divti3`,
   `__floattidf`, ... (641-1,072 IL instructions per division site).
2. **Two new hazards for any helper-based design (F2).** On the Windows MSIL triples a legalizer libcall with an
   `__int128` operand passes it as two `native int` halves, while compiler-rt compiled for the same triple takes it
   **by pointer** (Win64 ABI: `__floattidf` is `float64(native int)`, `__udivti3` `void(native int, native int, native
   int)`). msil-ld then accepted the mismatch with only a *warning* (`adapter: drops 1 extra argument`) and the program
   died with `NullReferenceException` in `__floattidf` on .NET 8 and Framework; the same program on the Linux triple
   is correct. A helper-based i128 path on Windows triples needs X86's `LowerWin64_i128OP` treatment
   (`X86ISelLowering.cpp:30961`), and msil-ld must make a signature mismatch on a compiler-runtime name an error.
3. **f16 (F4, F5, section 3).** compiler-rt's `__extendhfsf2`, `__truncsfhf2` and `__truncdfhf2`, compiled to CIL by
   this toolchain, give **bit-identical results to native compiler-rt for every input**: all 65,536 halves to float and
   double, all 2^32 floats to half, 317,440 double -> half rounding-boundary cases (midpoints, +-1 and +-2 ulps), 63,488
   double-rounding traps and 2^24 random doubles, on .NET 8, .NET 10 and .NET Framework 4.8 (Windows triple) and
   WSL .NET 8 and 10 (Linux triple). **System.Half** (.NET 6+, not Framework) equals them everywhere **except the
   1,022 signaling-NaN halves**, which `(float)Half` quiets (`0x7c01` -> `0x7fc02000`; compiler-rt keeps
   `0x7f802000`); the native Linux oracle's libgcc quiets them too, and also quiets sNaN in f32 -> bf16. Speed:
   in a conversion-only loop (2^28 conversions, whole process) the CIL helpers take 2.4-3.6 s (f32 -> f16) and
   2.4-2.7 s (f16 -> f32) on .NET 8, 10 and Framework, System.Half 2.1-2.3 s, native compiler-rt 2.4 and 1.8 s (F16C
   instructions: 1.7 and 0.5 s), so System.Half is at most 10-35 percent faster. Recommendation: compiler-rt helpers
   by default; no System.Half switch in Phase 3.
4. **bf16 (F4, section 3.5).** No BCL type exists (no `BFloat16` in the .NET 6-10 reference packs). bf16 -> f32 is an
   exact 16-bit shift (native code inlines it); f32/f64 -> bf16 needs rounding: compiler-rt's `__truncsfbf2` and
   `__truncdfbf2` compiled to CIL are bit-identical to native compiler-rt on all 2^32 floats and 2^24 random doubles
   with boundary cases, **after** the bf16 calling-convention fix HS-E3 also found (a defined function returning
   `__bf16` gets `float32` in its IL signature, the libcall expects `int32`; measured here with a one-line
   `fp_trunc.h` guard instead).
5. **C complex (F6, F7, section 4).** clang itself emits `__mulsc3`/`__muldc3`/`__divsc3`/`__divdc3`
   (`clang/lib/CodeGen/CGExprComplex.cpp:801-818`, `:1060-1080`; multiplication only on its NaN slow path).
   **System.Numerics.Complex violates C Annex G**: on a 14^4 matrix of special operands it differs from C in 3,896 of
   38,416 products and 7,343 of 38,416 quotients (.NET 8 and 10; .NET Framework adds 1,534 NaN-sign differences),
   for example `(1+1i)/(0+0i)` is `(inf, inf)` in C and `(NaN, NaN)` in .NET, `(inf+NaN i)*(1+1i)` is `(inf, inf)`
   in C and `(NaN, NaN)` in .NET, `(3+4i)/(1e308+1e308i)` underflows to `(0, 0)` in .NET. compiler-rt's helpers
   compiled to CIL match native compiler-rt on **all** 153,664 matrix results on all five runtimes. The native Linux
   oracle (libgcc) itself differs from compiler-rt in 5,907 of 38,416 double quotients and 139 float quotients
   (last-bit and overflow/underflow handling), so transcript tests with extreme operands must link compiler-rt into
   the Linux oracle as well.
6. **i128 (F8-F11, section 5).** In a 26-site number-theory workload (`nt128.c`: mulmod, Miller-Rabin, Pollard rho,
   u128 decimal I/O, i128 rationals, timestamps) M12's inline expansion makes `main` 27,355 IL bytes / 17,087
   instructions and 42,059 bytes of JIT code; with calls instead, `main` is 8,562 IL bytes (11,948 bytes of JIT code)
   plus one copy of the helpers per assembly (compiler-rt's: 1,451 IL bytes; outlined `linkonce_odr` functions with
   M12's expansion inside: 3,024). **A function with 36 inline i128 divisions crosses RyuJIT's size limit and is
   compiled `Tier-0 switched MinOpts`** on .NET 8 and 10 (IL 36,470 bytes, JIT code 104,840 bytes). Speed is decided by
   the algorithm more than by inlining: compiler-rt's (Knuth D, fast path for divisors below 2^64) runs the workload
   1.5-2.4x faster than ExpandIRInsts' shift-subtract loop (Windows .NET 8 and Framework: 4.55/4.94 s inline,
   4.37/4.32 s outlined, 2.69/2.52 s with compiler-rt's algorithm as ABI-neutral IR, native 0.95 s) and the 36-site
   function 2.8-3x faster; `UInt128`/`Int128` (.NET 7+) bodies are no faster than compiler-rt's algorithm.
   Recommendation: one outlined `linkonce_odr` function per operation kind and assembly whose body is compiler-rt's
   algorithm in IR (decision I1): no archive, no Win64 lowering, no switch, the same code in `-S` and objects.
7. **long double (F12, F13, section 2).** Every MSIL triple uses IEEE double (8 bytes, 53-bit mantissa,
   `MSILTargetInfo::useDoubleForLongDouble`, `clang/lib/Basic/Targets/MSIL.h:55-59`), recorded as `LongDoubleSize = 8`
   in every link-info header (`MSILLink.h:357-367`) and compared by msil-ld (`lld/MSIL/Driver.cpp:1398-1404`). Natively
   it is x87 80-bit in 16 bytes (x86_64 Linux, x86_64 MinGW) or binary128 (aarch64 Linux); `alignof(max_align_t)` is 8
   instead of 16 there. Observable consequences: native `l` functions are link errors today (M15; `sqrtl`, `expl`,
   `strtold` measured), `printf("%Lf")` through glibc reads 16 bytes of x87 data from the stack (measured natively: a
   double argument prints `nan`, then a stale earlier value), stock-clang bitcode's `x86_fp80` is an error. A helper
   archive member with forwarding wrappers (`long double sqrtl(long double x) { return sqrt(x); }`) links and runs
   (measured), because archive definitions win over native libraries. Recommendation: keep `long double` = `double`
   (Design.md 3), forward the `l` functions in the helper archive on the wide-`long double` triples, and diagnose `%L`
   formats (decision L1-L3).
8. **Other legalizer libcalls (section 6):** `__powidf2`/`__powisf2` (Linux triples; Windows triples call `pow`/`powf`,
   as natively), constrained FP <-> i128 (`__floattidf`, `__fixdfti`, ...), f16/bf16 <-> i128 (via the f16/bf16
   helpers), `__clear_cache` (should be dropped), plus libm names that are ordinary P/Invokes for msil-ld (`fma`,
   `lrint`, `llroundf`, `ldexpf`, `frexpf`, `roundeven` (absent from UCRT), `sqrtf16` (absent from every libc; native
   has the same unresolved call)). `__float128`/`fp128` and `x86_fp80` stay errors; `__float128` cannot be switched
   off in the front end because glibc's `<bits/floatn.h>` then fails (`unsupported machine mode '__TC__'`, F14).

---

## 1. Inventory: what each operation compiles to today (F1)

### 1.1 Method

`fp-helpers-files/scripts/inv_cases.py` holds 107 cases (one function `f` each, C or IR); `inv_run.py` compiles each
with `build\bin\clang` for `x86_64-pc-windows-msvc-msil` and `x86_64-unknown-linux-gnu-msil` at `-O0` and `-O2`: the
frontend IR (`-S -emit-llvm`, to separate frontend calls from legalizer calls), `-S` (ILAsm text: IL instruction count
of `f`, external calls, `localloc`, diagnostics) and `-c` (CIL object: `llvm-nm -u`), plus a native `-c` for
`x86_64-pc-windows-msvc` and `x86_64-unknown-linux-gnu` (`llvm-nm -u`). `inv_table.py` renders the table below
(`inv/table.md`; raw results in the scratch `inv/summary.tsv`).

### 1.2 Results

"p3 error" is `runtime helper '<name>' is not available for MSIL yet (phase 3)`; "not a Phase-1 import" is the
standalone import-table error (D14). IL counts are of `f` alone; L = the method uses `localloc` (D13 frame or D16
bitcast slot). In object mode the third column lists what msil-ld must resolve.

| Case | `-S` IL instrs: win -O0 / -O2, lin -O0 / -O2 (L = localloc) | `-c` undefined symbols (win / lin) | native x86_64 (win / lin) |
|---|---|---|---|
| `h_add` | ERR helper `__truncsfhf2` (p3 error) (all four) | __extendhfsf2, __truncsfhf2 | __extendhfsf2, __truncsfhf2 |
| `h_mul` | ERR helper `__truncsfhf2` (p3 error) (all four) | __extendhfsf2, __truncsfhf2 | __extendhfsf2, __truncsfhf2 |
| `h_div` | ERR helper `__truncsfhf2` (p3 error) (all four) | __extendhfsf2, __truncsfhf2 | __extendhfsf2, __truncsfhf2 |
| `h_cmp` | ERR helper `__extendhfsf2` (p3 error) (all four) | __extendhfsf2 | __extendhfsf2 |
| `h_ident` | 15 L / 2 (both triples) | - | - |
| `h_mem_only` | 24 L / 5 (both triples) | - | - |
| `h_ld_to_f32` | ERR helper `__extendhfsf2` (p3 error) (all four) | __extendhfsf2 | __extendhfsf2 |
| `h_st_from_f32` | ERR helper `__truncsfhf2` (p3 error) (all four) | __truncsfhf2 | __truncsfhf2 |
| `h_st_from_f64` | ERR helper `__truncdfhf2` (p3 error) (all four) | __truncdfhf2 | __truncdfhf2 |
| `h_to_f64` | ERR helper `__extendhfsf2` (p3 error) (all four) | __extendhfsf2 | __extendhfsf2 |
| `h_to_i32` | ERR helper `__extendhfsf2` (p3 error) (all four) | __extendhfsf2 | __extendhfsf2 |
| `i32_to_h` | ERR helper `__truncsfhf2` (p3 error) (all four) | __truncsfhf2 | __truncsfhf2 |
| `h_to_u64` | ERR helper `__extendhfsf2` (p3 error) (all four) | __extendhfsf2 | __extendhfsf2 |
| `u64_to_h` | ERR helper `__truncsfhf2` (p3 error) (all four) | __truncsfhf2 | __truncsfhf2 |
| `h_to_i128` | ERR helper `__extendhfsf2` (p3 error) (all four) | __extendhfsf2 | __extendhfsf2, __fixsfti / __fixhfti |
| `i128_to_h` | ERR helper `__truncsfhf2` (p3 error) (all four) | __truncsfhf2 | __floattisf, __truncsfhf2 / __floattihf |
| `h_sqrt` | ERR `sqrtf16`: not a Phase-1 import (all four) | sqrtf16 | sqrtf16 |
| `h_fabs` | 22 L / 7 (both triples) | - | - |
| `h_fma` | ERR helper `__truncdfhf2` (p3 error) (all four) | __extendhfsf2, __truncdfhf2 | __extendhfsf2, __truncdfhf2, fma |
| `h_strict_add` | ERR helper `__truncsfhf2` (p3 error); helper `__extendhfsf2` (p3 error) (all four) | __extendhfsf2, __truncsfhf2 | __extendhfsf2, __truncsfhf2 |
| `b_ld_to_f32` | ERR helper `__extendbfsf2` (p3 error) (all four) | __extendbfsf2 | - |
| `b_st_from_f32` | ERR helper `__truncsfbf2` (p3 error) (all four) | __truncsfbf2 | __truncsfbf2 |
| `b_st_from_f64` | ERR helper `__truncdfbf2` (p3 error) (all four) | __truncdfbf2 | __truncdfbf2 |
| `b_ident` | ERR helper `__extendbfsf2` (p3 error); helper `__truncsfbf2` (p3 error) / 2 (both triples) | - / - (-O0: __extendbfsf2, __truncsfbf2 / __extendbfsf2, __truncsfbf2) | - |
| `b_to_f32` | ERR helper `__extendbfsf2` (p3 error); helper `__truncsfbf2` (p3 error) / 2 (both triples) | - / - (-O0: __extendbfsf2, __truncsfbf2 / __extendbfsf2, __truncsfbf2) | - |
| `b_add_mem` | ERR helper `__truncsfbf2` (p3 error) (all four) | __extendbfsf2, __truncsfbf2 | __truncsfbf2 |
| `b_to_i128` | 288 L / 197 / 253 L / 197 | - | __fixsfti |
| `i128_to_b` | ERR helper `__extendbfsf2` (p3 error); helper `__truncsfbf2` (p3 error) (all four) | __extendbfsf2, __truncsfbf2 | __floattisf, __truncsfbf2 |
| `cx_muld` | ERR `__muldc3`: not a Phase-1 import (all four) | __muldc3 | __muldc3 |
| `cx_divd` | ERR `__divdc3`: not a Phase-1 import (all four) | __divdc3 | __divdc3 |
| `cx_mulf` | ERR `__mulsc3`: not a Phase-1 import (all four) | __mulsc3 | __mulsc3 |
| `cx_divf` | ERR `__divsc3`: not a Phase-1 import (all four) | __divsc3 | __divsc3 |
| `cx_mulld` | ERR `__muldc3`: not a Phase-1 import (all four) | __muldc3 | __muldc3 / __mulxc3 |
| `cx_divld` | ERR `__divdc3`: not a Phase-1 import (all four) | __divdc3 | __divdc3 / __divxc3 |
| `cx_divd_real` | 57 L / 21 / 83 L / 14 | - | - |
| `cx_muld_limited` | 75 L / 40 / 103 L / 22 | - | - |
| `cx_divd_limited` | 83 L / 52 / 111 L / 34 | - | - |
| `cx_divd_improved` | 180 L / 131 L / 213 L / 113 L | - | - |
| `cx_divf_promoted` | 121 L / 125 L / 138 L / 48 | - | - |
| `cx_divd_promoted` | 180 L / 131 L / 213 L / 113 L | - | - |
| `cx_muld_fast` | 75 L / 40 / 103 L / 22 | - | - |
| `cx_cabs` | ERR `cabs`: not a Phase-1 import (all four) | cabs | cabs |
| `cx_cexp` | ERR `cexp`: not a Phase-1 import (all four) | cexp | cexp |
| `cx_csqrtf` | ERR `csqrtf`: not a Phase-1 import (all four) | csqrtf | csqrtf |
| `i_mul` | 175 L / 88 / 134 L / 70 | - | - |
| `i_wmul` | 128 L / 62 / 93 L / 62 | - | - |
| `i_udiv` | 896 L / 659 / 853 L / 641 | - | __udivti3 |
| `i_sdiv` | 977 L / 738 / 934 L / 718 | - | __divti3 |
| `i_urem` | 989 L / 730 / 946 L / 712 | - | __umodti3 |
| `i_srem` | 1072 L / 801 / 1029 L / 781 | - | __modti3 |
| `i_udiv10` | 704 L / 482 / 667 L / 473 | - | - |
| `i_udiv64` | 789 L / 559 / 761 L / 550 | - | __udivti3 |
| `i_shl` | 130 L / 57 / 112 L / 50 | - | - |
| `i_ashr` | 133 L / 56 / 111 L / 51 | - | - |
| `i_to_f64` | 630 L / 459 L / 620 L / 450 L | - | __floattidf |
| `u_to_f32` | 551 L / 370 L / 541 L / 361 L | - | __floatuntisf |
| `f64_to_i` | 317 L / 236 L / 282 L / 236 L | - | __fixdfti |
| `f32_to_u` | 165 L / 98 L / 130 L / 98 L | - | __fixunssfti |
| `i_mulo` | 406 L / 296 / 399 L / 278 | - | - |
| `u_mulo` | 286 L / 199 / 279 L / 181 | - | - |
| `i_popcount` | 131 L / 80 / 121 L / 71 | - | - |
| `i_clz` | 241 L / 175 / 231 L / 166 | - | - |
| `i_strict_to_f64` | ERR helper `__floattidf` (p3 error) (all four) | __floattidf | __floattidf |
| `i_strict_from_f64` | ERR helper `__fixdfti` (p3 error) (all four) | ERR helper `__fixdfti` (p3 error) | __fixdfti |
| `bi256_mul` | 577 L / 412 / 556 L / 412 | - | - |
| `bi256_div` | 1922 L / 1546 L / 1916 L / 1546 L | - | - |
| `bi256_to_f64` | 1100 L / 844 L (both triples) | - | - |
| `l_mulo` | 116 L / 65 (both triples) | - | - |
| `ld_sizeof` | 2 (all four) | - | - |
| `ld_add` | 21 L / 4 (both triples) | - | - |
| `ld_sqrtl` | ERR `sqrtl`: not a Phase-1 import (all four) | sqrtl | sqrtl |
| `ld_fmodl` | ERR `fmodl`: not a Phase-1 import (all four) | fmodl | fmodl |
| `ld_to_i128` | 317 L / 236 L / 282 L / 236 L | - | __fixdfti / __fixxfti |
| `f128_mul` | ERR `__float128` unsupported (front end) / ERR `__float128` unsupported (front end) / ERR type `fp128` unsupported / ERR type `fp128` unsupported | ERR `__float128` unsupported (front end) / ERR type `fp128` unsupported | <err>, E:__float128 is not supported on this target / __multf3 |
| `fma_d` | 38 L (fma) / 5 (fma) (both triples) | fma | fma |
| `fma_f` | 38 L (fmaf) / 5 (fmaf) (both triples) | fmaf | fmaf |
| `fmuladd_contract` | 43 L / 6 (both triples) | - | - |
| `frexp_d` | 21 L (frexp) / 4 (frexp) (both triples) | frexp | frexp |
| `frexp_f` | 33 L (frexp) / 6 (frexp) / 21 L (frexpf) / 4 (frexpf) | frexp / frexpf | frexp / frexpf |
| `ldexp_d` | 23 L (ldexp) / 4 (ldexp) / 27 L (ldexp) / 4 (ldexp) | ldexp | ldexp |
| `ldexp_f` | ERR `ldexpf`: not a Phase-1 import / ERR `ldexpf`: not a Phase-1 import / 27 L (ldexpf) / 4 (ldexpf) | ldexpf | ldexpf |
| `powi_d` | 22 L (pow) / 5 (pow) / ERR helper `__powidf2` (p3 error) / ERR helper `__powidf2` (p3 error) | pow / __powidf2 | pow / __powidf2 |
| `powi_f` | 28 L (powf) / 5 (powf) / ERR helper `__powisf2` (p3 error) / ERR helper `__powisf2` (p3 error) | powf / __powisf2 | powf / __powisf2 |
| `lrint_d` | ERR `lrint`: not a Phase-1 import (all four) | lrint | lrint |
| `llrint_f` | ERR `llrintf`: not a Phase-1 import (all four) | llrintf | llrintf |
| `lround_d` | ERR `lround`: not a Phase-1 import (all four) | lround | lround |
| `rint_d` | 16 L (rint) / 3 (rint) (both triples) | rint | rint |
| `roundeven_d` | ERR libcall `roundeven`: not a Phase-1 import (all four) | roundeven | roundeven |
| `strict_add_div` | 25 L / 6 (both triples) | - | - |
| `strict_sqrt` | 16 L (sqrt) / 3 (sqrt) (both triples) | sqrt | sqrt |
| `strict_to_i64` | 16 L / 3 (both triples) | - | - |
| `strict_fma` | 38 L (fma) / 5 (fma) (both triples) | fma | fma |
| `model_strict` | 26 L (floor) / 7 (floor) (both triples) | floor | floor |
| `clear_cache` | ERR helper `__clear_cache` (p3 error) (all four) | __clear_cache | - |
| `sat_i32_f64` | 47 / 27 (both triples) | - | - |
| `sat_i64_f32` | 29 / 21 (both triples) | - | - |
| `sat_i128_f64` | 322 L / 259 L (both triples) | - | __fixdfti |
| `sat_i32_f16` | ERR helper `__extendhfsf2` (p3 error) (all four) | __extendhfsf2 | __extendhfsf2 |
| `lrint_ir` | ERR libcall `lrint`: not a Phase-1 import (all four) | lrint | - |
| `llround_ir` | ERR libcall `llroundf`: not a Phase-1 import (all four) | llroundf | llroundf |
| `constrained_fadd` | 4 (all four) | - | - |
| `constrained_sitofp_i128` | ERR helper `__floattidf` (p3 error) (all four) | __floattidf | __floattidf |
| `constrained_fptoui_i128` | ERR helper `__fixunssfti` (p3 error) (all four) | ERR helper `__fixunssfti` (p3 error) | __fixunssfti |
| `constrained_fptrunc_f16` | ERR helper `__truncdfhf2` (p3 error) (all four) | __truncdfhf2 | __truncdfhf2 |
| `constrained_sqrt` | 3 (sqrt) (all four) | sqrt | - |
| `fp128_add` | ERR type `fp128` unsupported (all four) | ERR type `fp128` unsupported | __addtf3 |
| `x86fp80_add` | ERR type `x86_fp80` unsupported (all four) | ERR type `x86_fp80` unsupported | - |

### 1.3 Where the diagnostics come from (verified at HEAD)

| Site | What it does |
|---|---|
| `MSILISelLowering.cpp:139-146` | f16 and bf16 are not legal types: `FP16_TO_FP`/`FP_TO_FP16` Expand (the legalizer calls `__extendhfsf2`, `__truncsfhf2`, `__truncdfhf2`), `BF16_TO_FP`/`FP_TO_BF16` Custom (`LowerBF16Conversion`, `:1176-1209`, calls `__extendbfsf2`, `__truncsfbf2`, `__truncdfbf2` through `makeLibCall`) |
| `MSILISelLowering.cpp:165-174`, `:351-366`, `:398-408` | `setMaxDivRemBitWidthSupported(64)`, `setMaxLargeFPConvertBitWidthSupported(64)` (M12): ExpandIRInsts expands i128 division and plain FP <-> i128 conversions inline; i128 `SDIV/UDIV/SREM/UREM` and FP -> i128 are Custom so that a division or conversion the DAG itself builds reaches `ReplaceNodeResults` |
| `MSILISelLowering.cpp:710-795` (`ReplaceNodeResults`) | reports `runtime helper '__divti3'/'__fixdfti'/...` and returns UNDEF; this runs in object mode too, which is why `i_strict_from_f64` and `constrained_fptoui_i128` fail under `-c` |
| `MSILISelLowering.cpp:190-216` | constrained ops on f16/bf16 become plain ops (`getPlainFP`), then the f16/bf16 helpers |
| `llvm/include/llvm/IR/RuntimeLibcalls.td`, `llvm/lib/IR/RuntimeLibcalls.cpp` | no MSIL entry: the libcall set is the native triple's (x86_64 or aarch64 Linux/Windows), which is why `powi` is `__powidf2` on Linux triples and `pow` on windows-msvc ones, exactly as natively |
| `MSILISelLowering.cpp:225-250` | `ldexpf`/`frexpf` promoted to the double functions where the C runtime lacks the float ones (MSVC, MinGW's msvcrt) |
| `MSILISelLoweringCall.cpp:252-273` (object mode) | an external-symbol libcall becomes an undefined object symbol: no error |
| `MSILISelLoweringCall.cpp:274-285` (linked text) and `:298-308` (standalone `-S`) | `runtime helper ... (phase 3)` for any `__` name without an import attribute |
| `MSILAsmPrinter.cpp:1290-1302`, `:1355-1368`, `:1430-1442` | the same text for frontend calls (`__muldc3`) in standalone text and for `__` declarations in linked text |
| `MSILTargetTransformInfo.cpp:56-65` | `areTypesABICompatible` keeps ArgumentPromotion from creating half/bfloat by-value parameters "until phase 3" |
| `MSILPrepareIR.cpp:232-235`, `:1280-1286` | `floating-point type 'fp128'/'x86_fp80' is not supported for MSIL` (per function; a global of such a type is data only and compiles) |
| `clang/lib/Basic/Targets/MSIL.h:55-59`, `:67`, `:93-100` | `long double` = IEEE double; `-mlong-double-80/128` rejected |
| `lld/MSIL/LTO.cpp:883-905` | LTO's libcall pre-binding skips `__` names ("those are phase 3's helpers") |
| `lld/MSIL/NativeLibraries.cpp:205-280` | the native `long double` function list and `hasWideNativeLongDouble` (not Windows, or x86_64 non-MSVC Windows) |
| `lld/MSIL/Signatures.cpp:146-166` | a call-site/definition mismatch that an adapter can bridge is a *warning* (plan 5.9.8); applies to helper names too (F2) |

### 1.4 Findings of the inventory

- **Object mode already defers almost everything to the link.** Except for the constrained FP -> i128 conversions
  (`ReplaceNodeResults`), every helper case compiles with `-c` and leaves the native helper name undefined. So the
  archive of HS-H1 makes these programs link without any backend change; the backend work is the -S/linked-text
  policy, the Win64 i128 convention, bf16 signatures and the constrained conversions.
- **Frontend vs legalizer.** `__muldc3` & co. are frontend calls (`ir=` column) with C ABI lowering for the triple, so
  they always match a compiler-rt definition compiled for the same triple. Legalizer libcalls are built by
  `makeLibCall`/`LowerCallTo` with generic type splitting, which differs from the C ABI only for i128 on Win64 (F2).
- **f16 in signatures is fine** (`h_ident`: `int32 f(int32)`, no helper); bf16 in signatures is `float32` (the
  generic `TargetLoweringBase` rule at `TargetLoweringBase.cpp:1538-1546` gives bf16 f32's register type), which costs
  an `__extendbfsf2` + `__truncsfbf2` pair at `-O0` (`b_ident`, `b_to_f32`) and breaks the archive's bf16 helpers
  (HS-E3).
- **Complex arithmetic flags remove the helpers** (`-fcx-limited-range`, `-ffast-math`,
  `-fcomplex-arithmetic=improved|promoted`): 22-131 IL inline; `promoted` on `double _Complex` warns
  (`-Whigher-precision-for-complex-division`: no wider `long double`).
- **`__builtin_mul_overflow` on i128 is inline** (181-296 IL), no `__muloti4`; `_BitInt(256)` division is inline
  (1,546 IL) on every target (above 128 bits LLVM always expands).
- **`lrint`/`lround` family**: `llvm.lrint` is a libm call (native x86 inlines `cvtsd2si`); `Math.Round(x)` + `conv.i8`
  would be exact for in-range values (helpers-switches S5's mapping).

## 2. long double (F12, F13)

### 2.1 The data model per triple (F12)

`ld/ma.c` and `clang -dM -E` for each triple (`results.txt` F12):

| Triple | native `long double` (size/align/`LDBL_MANT_DIG`) | MSIL | native `alignof(max_align_t)` / `sizeof` | MSIL | `__SIZEOF_FLOAT128__` on MSIL |
|---|---|---|---|---|---|
| `x86_64-pc-windows-msvc` | 8/8/53 | 8/8/53 | 8 / 8 | 8 / 8 | no |
| `x86_64-unknown-linux-gnu` | x87: 16/16/64 | 8/8/53 | 16 / 32 | 8 / 16 | **16** (and `__FLOAT128__`) |
| `x86_64-w64-windows-gnu` | x87: 16/16/64 | 8/8/53 | 16 / 32 | 8 / 16 | **16** |
| `aarch64-unknown-linux-gnu` | binary128: 16/16/113 | 8/8/53 | 16 / 32 | 8 / 16 | no |

- clang: `MSILTargetInfo` (`clang/lib/Basic/Targets/MSIL.h:55-59`) sets double's width, alignment and format before
  the native `handleTargetFeatures` and again in `adjust()` (`:93-100`, which rejects `-mlong-double-80/128` with
  `err_opt_not_valid_on_target`).
- The link-info header's data model has `LongDoubleSize`, always 8 (`MSILLink.h:357-367`, "stock bitcode's x86_fp80
  is a documented limit"); msil-ld compares it among objects and LTO objects (`lld/MSIL/Driver.cpp:1398-1404`) and
  with libraries' symbol tables (`lld/MSIL/AssemblyFiles.cpp:363-405`, M22). A program and a library always agree.
- `__float128` cannot be turned off for the Linux and MinGW MSIL triples: glibc's `<bits/floatn.h>` decides
  `__HAVE_FLOAT128` from the compiler version alone and then uses `__attribute__((__mode__(__TC__)))`; with the
  type unavailable clang stops in `floatn.h:83` with `unsupported machine mode '__TC__'` (F14: `<math.h>` of the glibc
  sysroot parsed for `x86_64-pc-windows-msvc-msil`, whose `HasFloat128` is false). `_Float128` *objects* compile
  (`_Float128 q;` is a 16-byte global); arithmetic, or a function with an `fp128` parameter, is `floating-point type
  'fp128' is not supported for MSIL` (`MSILPrepareIR.cpp:232-235`, `:1280-1286`). The status quo is right.

### 2.2 What breaks, measured (F13)

1. **Native `l` functions** (`ld/ldl.c` with the glibc sysroot headers): clang declares `double @sqrtl(double)`,
   `double @expl(double)`, `double @strtold(ptr, ptr)`; msil-ld reports `native function 'sqrtl' (libm.so.6) takes or
   returns a value that a P/Invoke cannot pass natively yet (phase 3)` and the same for `expl` and `strtold` (the M15
   list, `NativeLibraries.cpp:216-249`, plus the `f64x`/`f128` suffix rule).
2. **`printf("%Lf")` through glibc** (`ld/ldn2.c`, native Linux: a `double` passed where `%Lf` expects `long double`,
   which is what an MSIL caller passes, since the IR type is `double`): the first line printed `nan`, the third the
   *previous* call's `long double` (`48.635176` instead of `97.270352`). glibc reads 16 bytes from the overflow area
   while the double went to the FP register save area (variadics recon 3.6 (9)). `scanf("%Lf")` would *write* 16
   bytes into an 8-byte object.
3. **Stock-clang bitcode** for x86_64 Linux/MinGW has `x86_fp80`: `floating-point type 'x86_fp80' is not supported`
   (inventory `x86fp80_add`), a documented compatibility limit (Design.md 3).
4. **Layout interop**: a struct with a `long double` field, or `max_align_t`, has another size and alignment than in
   the native library. No conformance target passes such a struct to native code.
5. **Precision**: programs see `LDBL_MANT_DIG == 53` and `sizeof(long double) == 8`, which portable programs handle
   (SQLite falls back to its own double-double arithmetic when `long double` is not wider; Lua's default is
   `LUA_FLOAT_DOUBLE`; zlib uses none).

### 2.3 What Phase 3 should do

- **Forward the `l` functions on the wide triples (measured).** An archive member `ld/ldwrap.c` with
  `long double sqrtl(long double x) { return sqrt(x); }` (and `expl`, `strtold`), compiled for
  `x86_64-unknown-linux-gnu-msil` into an `llvm-ar` archive, made `ldl.c` link (the archive definition wins over
  `libm.so.6`) and print `4038514d6df535d1`, `8`, `53` under WSL .NET 10 (native: the same double result, `16`, `64`).
  A generated `msil/longdouble.c` would hold one wrapper per name of `nativeLongDoubleFunctions` with a `double`
  counterpart (`sqrtl` -> `sqrt`, `strtold` -> `strtod`, `wcstold` -> `wcstod`, `frexpl` -> `frexp`, `nanl` -> `nan`,
  `cabsl` -> `cabs`, glibc's `sincosl`/`exp10l`, the `f64x` names), two real bodies (`nexttoward(double, long double)`
  is `nextafter`; `nexttowardf(float x, long double y)` compares in double: `x == y ? (float)y : nextafterf(x, x < y
  ? INFINITY : -INFINITY)` plus the NaN cases), and nothing for the `f128` names (they stay link errors). Only the
  archives of the wide triples get it (`hasWideNativeLongDouble`); windows-msvc binds the native ones today. The M15
  error remains the backstop for a name without a wrapper.
- **`%L` formats**: a clang warning on MSIL triples whose native `long double` is wider, for printf/scanf-family
  format strings with `L` on a floating conversion (the format checker already knows the expected type). A real fix
  (an x87 or binary128 image of the double in the native `va_list` for arguments whose C type is `long double`)
  needs a clang call-site marker, because the IR type is `double`, and belongs to P3-B variadics if a target program
  needs it (zlib, Lua and SQLite do not).
- **Keep the data model.** x87 80-bit arithmetic cannot be emulated from compiler-rt (its 16 `x86_80_BIT` sources use
  the x87 itself and fail with `xf_float` undefined, HS-E1); binary128 emulation exists (`GENERIC_TF_SOURCES`) but
  needs fp128 softening in the backend and a data-model change (`LongDoubleSize` 16, an ABI version bump). No Phase 3
  target needs either.

## 3. f16 and bf16 (F4, F5)

### 3.1 Today

f16 values are held as integers (`TypeSoftPromoteHalf`, register type i16, `int32` in IL); arithmetic is
`fpext` -> f32 operation -> `fptrunc` in the IR (clang's `_Float16` excess precision on x86), so only the
conversions need helpers: `__extendhfsf2` (`float32(int32)`), `__truncsfhf2` (`int32(float32)`), `__truncdfhf2`
(`int32(float64)`); f16 -> f64 is `__extendhfsf2` + `conv.r8`, as natively. bf16 values are held as integers too,
but bf16's *register* type is f32 (`TargetLoweringBase.cpp:1538-1546`), so a `__bf16` parameter or return value is
`float32` in the IL signature; `LowerBF16Conversion` calls `__extendbfsf2` (`float32(int32)`), `__truncsfbf2`
(`int32(float32)`), `__truncdfbf2` (`int32(float64)`). `_Float16 _Complex` operations are `__mulsc3`/`__divsc3` on
promoted floats (`<2 x half>` parameters become two `int32`s).

### 3.2 Exhaustive equality (F4)

`f16/f16conv.c` hashes the result bits of: (A) all 65,536 halves -> float and -> double; (B) all 2^32 floats -> half,
NaN and non-NaN inputs separately; (C) double -> half at every rounding boundary of every finite non-negative half
(the midpoint and +-1, +-2 double ulps, both signs: 317,440 conversions), 63,488 "double-rounding traps" (a double
one ulp above or below a midpoint that is a float, compared with the float path), and 2^24 random doubles with
exponents 2^-26..2^17; (D) all 65,536 bf16 -> float, all 2^32 floats -> bf16, 2^22 random doubles -> bf16 with two
boundary cases each; (S) 36 named NaN, subnormal, overflow and rounding cases printed in full. Every conversion is in a
`noinline` function, so each is a real helper call. Reference: native Windows with the VS compiler-rt. Seconds are
whole-process wall times of one run per part (loaded host).

| Variant | part A s | B s | C s | D s | lines that differ from compiler-rt |
|---|---|---|---|---|---|
| native Windows, VS compiler-rt | 0.5 | 45.86 | 0.4 | 49.58 | none |
| native Windows, this tree compiler-rt | 0.07 | 48.12 | 0.35 | 46.41 | none |
| native Linux, libgcc_s | 21.41 | 593.68 | 3.21 | 155.08 | `A h->f32 3db7a122e9f79cb7`, `D f32->bf16 NaN`, `S: half 7c01`, `S: half 7d00`, `S: half fc01`, `S: float 7fa00000` |
| native Linux, this tree compiler-rt | 2.99 | 72.49 | 3.87 | 62.56 | none |
| MSIL win, compiler-rt helpers, .NET 8 | 0.3 | 53.01 | 0.91 | 85.74 | none |
| MSIL win, compiler-rt helpers, .NET 10 | 1.1 | 60.36 | 0.99 | 67.42 | none |
| MSIL win, compiler-rt helpers, .NET Framework 4.8 | 0.84 | 78.13 | 2.32 | 48.75 | none |
| MSIL lin, compiler-rt helpers, WSL .NET 8 | 2.09 | 75.17 | 5.86 | 93.99 | none |
| MSIL lin, compiler-rt helpers, WSL .NET 10 | 6.92 | 79.65 | 6.46 | 78.17 | none |
| MSIL win, System.Half, .NET 8 | 0.75 | 40.47 | 1.27 | 54.24 | `A h->f32 3db7a122e9f79cb7`, `S: half 7c01`, `S: half 7d00`, `S: half fc01` |
| MSIL win, System.Half, .NET 10 | 0.95 | 47.06 | 0.72 | 45.36 | `A h->f32 3db7a122e9f79cb7`, `S: half 7c01`, `S: half 7d00`, `S: half fc01` |
| MSIL lin, System.Half, WSL .NET 10 | 31.72 | 76.1 | 3.97 | 93.44 | `A h->f32 3db7a122e9f79cb7`, `S: half 7c01`, `S: half 7d00`, `S: half fc01` |

The System.Half rows replace only the three f16 helpers; their part D still uses compiler-rt's bf16 helpers.
Reference hashes and counts (hash, number of results in hex):

```
A h->f32 3db7a122e9f79cb7 000000010000
A h->f64 5d18c4d2fa1b9bc5 000000010000
B f32->h nonNaN 8878cb5d64537be7 0000ff000002
B f32->h NaN b93571acc4784534 000000fffffe
C f64->h bounds b8e0f2adcc2cc9e3 00000004d800
C f64->h dbl-round c807d8058fc3eed2 00000000f800
C f64->h random 1c0b3b33cb4cdca8 000001000000
D bf16->f32 580f3ec0d9becab4 000000010000
D f32->bf16 nonNaN ba45373e41c40727 0000ff000002
D f32->bf16 NaN fba81c992ffc4751 000000fffffe
D f64->bf16 70c28264f3828e3b 000000c00000
```

Named cases where implementations differ (the other named lines are equal everywhere):

| Input | compiler-rt (native and MSIL) | libgcc (native Linux) | System.Half (.NET 8/10) |
|---|---|---|---|
| half `7c01` -> float | `7f802000` (sNaN kept) | `7fc02000` (quieted) | `7fc02000` |
| half `7d00` -> float | `7fa00000` | `7fe00000` | `7fe00000` |
| half `fc01` -> float | `ff802000` | `ffc02000` | `ffc02000` |
| float `7fa00000` -> bf16 | `7fa0` (sNaN kept) | `7fe0` | n/a (no BCL bf16) |
| float `7fa00000` -> half | `7f00` (quieted by both) | `7f00` | `7f00` |
| half `7c01` -> double | `7ff8040000000000` (quieted by `cvtss2sd`/`conv.r8` everywhere) | same | same |

Native code built with `-mf16c` (the `vcvtph2ps`/`vcvtps2ph` instructions instead of the helpers) gives part A's hash
`d7f8ac501aca7a66`, the same as libgcc and System.Half: the hardware quiets sNaN too. A C# check of all 65,536 halves (`f16/halfcheck-Program.cs`, F5) confirms that `(float)Half` differs from
compiler-rt's rule on exactly the 1,022 signaling-NaN halves and on no other input, on .NET 8.0.33 and 10.0.14.

### 3.3 System.Half (F5)

- **Availability**: `System.Half` (.NET 5); `BitConverter.HalfToInt16Bits`/`Int16BitsToHalf` in every reference
  pack from 6.0.36 to 10.0.14 (checked by name); nothing in .NET Framework 4.8 (the patched program fails there with
  `Could not load file or assembly 'System.Runtime, Version=8.0.0.0'`). No `BFloat16` type in any of these packs.
- **IL shape** used (`scripts/patch_il.py`, recipe `half`: the bodies of the three helpers replaced in the ildasm text
  of the linked program, reassembled by ilasm 10.0.12):

```
__extendhfsf2(int32) : float32   ldarg.0; conv.i2
                                 call valuetype System.Half System.BitConverter::Int16BitsToHalf(int16)
                                 call float32 System.Half::op_Explicit(valuetype System.Half); ret
__truncsfhf2(float32) : int32    ldarg.0; call valuetype System.Half System.Half::op_Explicit(float32)
                                 call int16 System.BitConverter::HalfToInt16Bits(valuetype System.Half); conv.u2; ret
__truncdfhf2(float64) : int32    the same with op_Explicit(float64)
```

  RyuJIT inlines these wrappers into their callers (they no longer appear in `DOTNET_JitDisasmSummary`); the
  compiler-rt versions are JIT-compiled as 230-, 370- and 453-byte methods (`__extendhfsf2`, `__truncsfhf2`,
  `__truncdfhf2`, WSL .NET 10).
- **Results**: equal to compiler-rt on every non-sNaN input of A-D, including all 2^32 floats and the 63,488
  double-rounding traps (so `(Half)double` rounds once, directly); different on the 1,022 sNaN halves (3.2).
- **Speed** (`f16/f16bench.c`: 2^28 conversions in a loop with little else, best of 3 whole-process seconds, F5):

| Loop (2^28 conversions) | native, VS compiler-rt | native, `-mf16c` | MSIL compiler-rt .NET 8 | .NET 10 | Framework 4.8 | MSIL System.Half .NET 8 | .NET 10 |
|---|---|---|---|---|---|---|---|
| f32 -> f16 | 2.38 | 1.74 | 3.57 | 2.41 | 2.51 | 2.33 | 2.11 |
| f16 -> f32 | 1.79 | 0.48 | 2.57 | 2.67 | 2.39 | 2.29 | 2.08 |

All variants print the same checksum (`c984a9b0`, `4289db54`). The loop also pays a `localloc`-slot bitcast per
iteration in MSIL (D16), common to both MSIL columns.

### 3.4 Recommendation for f16

compiler-rt's helpers from the toolchain's archive, on every triple and runtime, no switch: bit-exact with native
compiler-rt (Windows; Linux when linked with compiler-rt), they run on .NET Framework and need nothing from the
references. A System.Half lowering would change only sNaN results (towards libgcc and the F16C instructions) and
would gain at most 10-35 percent in conversion-bound loops (F5), on .NET 6 and later only. If it is ever added, it is msil-ld's choice, not a codegen one: under a link-time switch,
msil-ld can *synthesize* `__extendhfsf2`/`__truncsfhf2`/`__truncdfhf2` with the bodies above when a reference
defines `System.Half`, instead of extracting compiler-rt's members, with no call-site rewriting (helpers-switches
S3's token rewriting is not needed for a symbol msil-ld resolves anyway). helpers-switches S5 (bitcasts through
`BitConverter` or a union value type instead of a `localloc` slot) also speeds these helpers up: 24 of the 30
compiled helpers use `localloc`, only for bitcasts (F3).

### 3.5 bf16

- **Calling convention (HS-H4, confirmed):** a defined function returning `__bf16` is `float32` in IL, the libcall
  `int32`. Fixes: (a) the backend passes bf16 in signatures as its 16-bit pattern in `int32`, like f16 (override
  `getRegisterTypeForCallingConv`/`getNumRegistersForCallingConv` for bf16 in `MSILTargetLowering`; this changes the
  IL signature of user functions with `__bf16` parameters, which cannot link against the helpers today anyway, and
  removes the `-O0` helper pair of `b_ident`); or (b) compile compiler-rt's `truncsfbf2.c`/`truncdfbf2.c` with
  `dst_t` = `uint16_t` under `__MSIL__` (the one-line `fp_trunc.h` guard used here, `crt/fp_trunc.h.diff`).
  Recommended: (a); (b) only as a stopgap.
- **bf16 -> f32 should be inline**: it is `bits << 16` reinterpreted, exact for every input (native code inlines it).
  Today `LowerBF16Conversion` calls `__extendbfsf2` (64-66 IL instructions with a `localloc` slot) only to avoid the
  generic expansion's bitcast through memory; once BITCAST is cheap (helpers-switches S5) the generic Expand is the
  better lowering.
- **f32/f64 -> bf16 stays a helper** (`__truncsfbf2` 176, `__truncdfbf2` 217-218 IL): bit-identical to native
  compiler-rt on all 2^32 floats and the random and boundary doubles (part D) on all five runtimes.
- No BCL type to switch to.

## 4. C complex (F6, F7)

### 4.1 Who emits what

clang's `ComplexExprEmitter` (`clang/lib/CodeGen/CGExprComplex.cpp`) emits, under the default
`-fcomplex-arithmetic=full`: for `*` an inline product with a NaN check whose slow path calls `__mulsc3`/`__muldc3`
(`getComplexMultiplyLibCallName`, `:801-818`), for `/` always `__divsc3`/`__divdc3` (`:1060-1080`). `long double
_Complex` uses the `dc3` names on every MSIL triple (native Linux: `__mulxc3`/`__divxc3`); `_Float16 _Complex` is
promoted to the `sc3` ones. `-fcx-limited-range`/`-ffast-math` (22-52 IL inline), `-fcomplex-arithmetic=improved`
(Smith, 113-131 IL) and `=promoted` (float in double, 48-125 IL; on `double` it warns that no wider type exists) need
no helper.

### 4.2 System.Numerics.Complex is not Annex G (F6)

`cx/cx.c` (C) and `cx/complex-probe-Program.cs` (C#) compute 20 named cases and the 14^4 matrix of operands
`{0, -0, 1, -1, 1.5, -2.25, 1e-310, 1e300, -1e300, inf, -inf, NaN, 3, 1e-20}` for the real and imaginary parts of
both operands. Named cases (`cx/named-compare.md`; bold = differs from C/compiler-rt):

| Case | C: compiler-rt (native, and MSIL on 5 runtimes) | native Linux libgcc | .NET 8/10 Complex | .NET Framework Complex |
|---|---|---|---|---|
| `ordinary` | (0.0, 7.3125) | (0.0, 7.3125) | (0.0, 7.3125) | (0.0, 7.3125) |
| `(inf+nan i)*(1+1i)` | (inf, inf) | (inf, inf) | **(-nan, -nan)** | **(-nan, -nan)** |
| `(nan+inf i)*(2+0i)` | (-nan, inf) | (-nan, inf) | **(-nan, -nan)** | **(-nan, -nan)** |
| `(inf+0i)*(0+0i)` | (-nan, -nan) | (-nan, -nan) | (-nan, -nan) | (-nan, -nan) |
| `(1e300+1e300i)^2` | (-nan, inf) | (-nan, inf) | (-nan, inf) | (-nan, inf) |
| `(0+inf i)*(0+inf i)` | (-inf, -nan) | (-inf, -nan) | (-inf, -nan) | (-inf, -nan) |
| `(inf+inf i)*(nan+nan i)` | (-nan, -nan) | (-nan, -nan) | (-nan, -nan) | (-nan, -nan) |
| `(1+1i)/(0+0i)` | (inf, inf) | (inf, inf) | **(-nan, -nan)** | **(-nan, -nan)** |
| `(1+1i)/(-0+0i)` | (-inf, -inf) | (-inf, -inf) | **(-nan, -nan)** | **(-nan, -nan)** |
| `(1+1i)/(inf+nan i)` | (0.0, 0.0) | (0.0, 0.0) | **(-nan, -nan)** | **(-nan, -nan)** |
| `(1+1i)/(inf+inf i)` | (0.0, 0.0) | (0.0, 0.0) | **(-nan, -nan)** | **(-nan, -nan)** |
| `(inf+inf i)/(1+1i)` | (inf, -nan) | (inf, -nan) | (inf, -nan) | (inf, -nan) |
| `(inf+nan i)/(1+1i)` | (inf, -inf) | (inf, -inf) | **(-nan, -nan)** | **(-nan, -nan)** |
| `(1e300+1e300i)/(1e300+1i)` | (1.0, 1.0) | (1.0, 1.0) | (1.0, 1.0) | (1.0, 1.0) |
| `(1e-300+1e-300i)/(1e-300+1e-300i)` | (1.0, 0.0) | (1.0, 0.0) | (1.0, 0.0) | (1.0, 0.0) |
| `(1+1i)/(1e-310+1e-310i)` | (inf, 0.0) | (inf, 0.0) | (inf, 0.0) | (inf, 0.0) |
| `(3+4i)/(1e308+1e308i)` | (3.4999999999999997e-308, 5e-309) | **(3.5e-308, 5e-309)** | **(0.0, 0.0)** | **(0.0, 0.0)** |
| `(1e308+1e308i)/(1e-308+1e-308i)` | (inf, 0.0) | **(inf, -nan)** | (inf, 0.0) | (inf, 0.0) |
| `(nan+0i)/(0+0i)` | (-nan, -nan) | (-nan, -nan) | (-nan, -nan) | **(-nan, nan)** |
| `(0+0i)/(0+0i)` | (-nan, -nan) | (-nan, -nan) | (-nan, -nan) | (-nan, -nan) |

.NET's `Complex` multiplies naively and divides with Smith's algorithm, without Annex G's recovery of infinities and
zeros and without scaling (`(3+4i)/(1e308+1e308i)` underflows to 0). Matrix ("NaN-only" = differences in NaN sign
or payload only):

| Pair | `double *` | `double /` | `float *` | `float /` |
|---|---|---|---|---|
| MSIL (compiler-rt CIL helpers; .NET 8, 10, Framework, WSL 8, 10) vs native compiler-rt (VS and tree) | 0 of 38,416 | 0 | 0 | 0 |
| native Linux libgcc vs compiler-rt | 0 | **5,907** (0 NaN-only) | 0 | **139** |
| .NET 8 and .NET 10 `Complex` vs C | **3,896** | **7,343** | n/a | n/a |
| .NET Framework `Complex` vs C | **3,896** | **8,877** (1,534 NaN-only) | n/a | n/a |
| .NET Framework vs .NET 8 `Complex` | 0 | 1,708 (all NaN-only) | n/a | n/a |

Of the .NET 8 product differences, 1,432 are C `(inf, inf)` vs .NET `(NaN, NaN)` and 2,464 C `(NaN, inf)` or
`(inf, NaN)` vs `(NaN, NaN)`; of the quotients, 968 are C `(0, 0)` vs `(NaN, NaN)`, 952 C `(inf, inf)` vs `(NaN,
NaN)`, about 4,900 differ in finite values or signs of zero (overflow and underflow handling). The full 153,664-line
matrix printed by the MSIL program on .NET 8 is byte-identical to the native compiler-rt one.

### 4.3 compiler-rt vs libgcc: the oracle question

libgcc's `__divdc3` (used by the native Linux oracle through `libgcc_s.so.1`) differs from compiler-rt's in 5,907 of
38,416 double quotients: 3,653 by at most 2 ulps (compiler-rt's `logb`/`scalbn` scaling gives `1/1e300 =
9.9999999999999986e-301`, libgcc `1e-300`), 1,215 more (NaN against 0 for `(0+1i)/(0+1e-310i)`, the imaginary part of
`(1e308+1e308i)/(1e-308+1e-308i)` `0` against NaN, signs of zero); 139 float quotients differ too, products agree. So
with compiler-rt helpers, MSIL on the Linux triple equals native *compiler-rt*, not native *libgcc*: transcript tests
with extreme complex operands must link the native compiler-rt archive (built by the same step as the MSIL one, as
`crt/native-lin` here) into the E7 oracle, or keep to ordinary operands (as the KISS FFT exit program does).

### 4.4 Speed (F7)

`cx/cxbench.c`: 20 M complex multiply + divide steps, best of 3 whole-process seconds:

| Variant | .NET 8 | .NET 10 | Framework 4.8 | native |
|---|---|---|---|---|
| MSIL, compiler-rt CIL helpers (default) | 1.45 | 1.29 | 1.35 | |
| MSIL, `-fcx-limited-range` (inline naive) | 0.78 | | | |
| MSIL, `-fcomplex-arithmetic=improved` (inline Smith) | 0.61 | | | |
| native, VS compiler-rt | | | | 0.89 |
| C# `System.Numerics.Complex` (in-process loop: 210 / 179 / 358 ms) | 0.80 | 0.66 | 0.73 | |

All print the same checksum (ordinary operands, where every implementation agrees). The helpers cost about 0.5-0.6 s
per 20 M divisions more than inline code; programs that want speed over Annex G already have the clang flags.

### 4.5 The `<complex.h>` library functions

`cabs` binds and runs (SysV passes `double _Complex` as two doubles, Win64 by pointer: plain P/Invoke shapes). `cexp`,
`csqrt`, ... return a `double _Complex`: on SysV `{double, double}` in XMM0:XMM1, on Win64 through an `sret`
pointer; msil-ld rejects both today (`native function 'cexp' (libm.so.6) takes or returns a value that a P/Invoke
cannot pass natively yet (phase 3)`, the same with `api-ms-win-crt-math-l1-1-0.dll`; `cx/complex-libm.c`). That is
Status.md's struct-by-value P/Invoke item (another topic); the Win64 form is already a pointer-only signature
(`void cexp(native int sret, native int z)`), which a P/Invoke could pass as is.

### 4.6 Recommendation for complex

compiler-rt's `__mulsc3`, `__muldc3`, `__divsc3`, `__divdc3` (642-1,669 IL instructions each, once per assembly)
from the helper archive; never `System.Numerics.Complex`; no switch (no BCL type follows Annex G, and `float _Complex`
has none at all).

## 5. i128 (F8-F11)

### 5.1 Today (M12)

ExpandIRInsts expands every i128 division and remainder whose divisor is not a power of two, and every plain
FP <-> i128 conversion, before instruction selection (`MSILISelLowering.cpp:398-408`; ExpandIRInsts keeps only
power-of-two divisors out, `llvm/lib/CodeGen/ExpandIRInsts.cpp:81-89`). Per function (inventory, IL instructions at
`-O0` / `-O2`, both triples): `udiv` 853-896 / 641-659, `sdiv` 934-977 / 718-738, `urem` 946-989 / 712-730, `srem`
1,029-1,072 / 781-801, `udiv` by the constant 10 667-704 / 473-482, i128 -> double 620-630 / 450-459, double -> i128
282-317 / 236 (Q36 and backlog S12 leave the replacement of this expansion to Phase 3). Native code calls `__udivti3` & co., except for constant divisors, which X86 expands with
`TargetLowering::expandDIVREMByConstant` (`LowerWin64_i128OP` tries it first, `X86ISelLowering.cpp:30967-30971`). A
hand-written equivalent of that expansion for `x / 10` (`i128/div10.c`: divide by 2, remainder modulo 5 from the sum
of the 64-bit halves, multiply by the inverse of 5 modulo 2^128; checked against `x / 10` on 4 M values) is 103 IL
instructions, against 473 from ExpandIRInsts.

### 5.2 A real workload (F8)

`i128/nt128.c` uses the idioms of real `__int128` C code: `(u128)a * b % m` (Miller-Rabin and Pollard rho on u64),
u128 <-> decimal text (`/ 10`, `% 10`), exact i128 rationals (gcd by `%`, normalization by `/`), nanosecond timestamps
split by 1e9, isqrt by Newton steps, binomials, a 96.32 fixed-point divide. At `-O2` everything inlines into `main`,
which then holds 26 i128 divisions and remainders (21 with a variable divisor: 11 `urem`, 8 `udiv`, 2 `sdiv`; 5 `udiv`
by 10). Variants (Linux triple unless noted; every variant prints the native result `hash 76bb6cb7ad653295`):

| Variant | How | IL bytes (`main` + helpers) | IL instructions | JIT code bytes (WSL .NET 10, TC=0) |
|---|---|---|---|---|
| inline (M12) | today | 27,355 | 17,087 | 42,059 |
| compiler-rt helpers | the 21 variable-divisor operations rewritten to `__udivti3`/`__umodti3`/`__divti3` calls in the `-O2` IR (`scripts/i128_to_calls.py`), the archive linked | 8,562 + 1,451 | 5,330 + 981 | 11,948 + 1,963 |
| outlined `linkonce_odr` | the same sites call `define linkonce_odr hidden i128 @__msil_udiv128(i128 %a, i128 %b) noinline comdat { udiv }`, one per kind (`scripts/i128_outline.py`), expanded by ExpandIRInsts once each | 8,562 + 3,024 | 5,330 + 2,020 | 11,948 + 3,875 |
| `UInt128`/`Int128` | the compiler-rt variant with the bodies of `__udivti3`, `__umodti3`, `__divti3` replaced by .NET 7 operators (`scripts/patch_il.py`, recipe `int128`) | 8,562 + 3 x 57 | | 11,948 + 31-135 per wrapper (the JIT inlines `op_Division`'s fast path; `DivideSlow` 1,196 bytes in CoreLib) |
| inline, `-O0` | today | 46,140 in 27 functions | 22,448 | |

The `UInt128` helper body (`__divti3` shown; SysV order: result pointer, a.lo, a.hi, b.lo, b.hi):

```
.locals init (valuetype System.Int128 a, valuetype System.Int128 b, valuetype System.Int128 r)
ldloca.s a; ldarg.2; conv.u8; ldarg.1; conv.u8; call instance void System.Int128::.ctor(uint64, uint64)
ldloca.s b; ldarg.s 4; conv.u8; ldarg.3; conv.u8; call instance void System.Int128::.ctor(uint64, uint64)
ldloc.0; ldloc.1; call valuetype System.Int128 System.Int128::op_Division(valuetype System.Int128, valuetype System.Int128); stloc.2
ldarg.0; ldloc.2; call uint64 System.Int128::op_Explicit(valuetype System.Int128); stind.i8
ldarg.0; ldc.i4.8; add; ldloc.2; ldc.i4.s 64; call valuetype System.Int128 System.Int128::op_RightShift(valuetype System.Int128, int32)
call uint64 System.Int128::op_Explicit(valuetype System.Int128); stind.i8; ret
```

Per variable-divisor site the inline expansion costs about 560 IL instructions (895 IL bytes) more than a call
(`(17,087 - 5,330) / 21`); a call site is 6-12 IL instructions (`nt-helper.il`, `IL_020e`-`IL_022e`: the halves, the
result slot address, `call`, the result loads). One copy of the helpers pays for itself at two sites.

Speed (best of 3 whole-process seconds, 20 repetitions of the workload, F8):

| Variant | WSL .NET 8 | WSL .NET 10 | Windows .NET 8 | Windows Framework 4.8 | native |
|---|---|---|---|---|---|
| inline (M12), `-O2` | 9.11 | 12.54 | 4.55 | 4.94 | |
| outlined `linkonce_odr`, ExpandIRInsts bodies (c1) | 9.37 | 10.46 | 4.37 | 4.32 | |
| compiler-rt helpers (archive, Linux triple) | 6.26 | 5.22 | | | |
| compiler-rt algorithm as IR (Windows triple, `nt-irhelp-win`) (c2) | | | 2.69 | 2.52 | |
| `UInt128`/`Int128` bodies | 5.93 | 5.77 | | | |
| inline, `-O0` | | 13.09 | | | |
| native (Linux libgcc, under WSL / Windows VS compiler-rt) | | | | | 2.38 / 0.95 |

The Windows columns come from a second, quieter batch (`timing.txt`, last seven lines); the WSL VM was more loaded
(the same inline program took 9.1-12.5 s there). Every variant prints `hash ad61743b86772d21` at 20 repetitions.
Outlining alone does not change speed; compiler-rt's algorithm (Knuth D with a fast path for divisors below 2^64,
the `(u128)a*b % m` case) is 1.5-2.4x faster than ExpandIRInsts' 128-step shift-subtract loop, and `Int128` is not
faster than it.

### 5.3 The MinOpts cliff (F9)

RyuJIT compiles a method that is too large with MinOpts instead of full optimization. `i128/many36.c` is one
function with 18 `u128 /` and 18 `u128 %` by variables (a realistic size for an unrolled bignum or calendar
routine), called 200,000 times; with M12's inline expansion (`DOTNET_JitDisasmSummary=1`,
`DOTNET_TieredCompilation=0`, WSL .NET 8.0.31 and 10.0.13, `scripts/wsljit.sh`):

| Function | IL bytes | JIT result |
|---|---|---|
| `many20` (10 + 10 operations), inline | 20,226 | FullOpts, 35,118 bytes (.NET 10), 35,084 (.NET 8) |
| `many36` (18 + 18), inline | 36,470 | **`Tier-0 switched MinOpts`**, 104,840 bytes (.NET 10), 105,108 (.NET 8) |
| `many36`, outlined | 2,122 (+ 914 + 1,005 for the two outlined kinds) | FullOpts, 3,359 bytes |

Speed (200,000 calls, best of 3 whole-process seconds; every variant prints the native result):

| Function | WSL .NET 8 | WSL .NET 10 | native (WSL) |
|---|---|---|---|
| `many20` inline (FullOpts) | 5.83 | 2.17 | |
| `many20` compiler-rt helpers | 0.89 | 1.11 | |
| `many36` inline (MinOpts) | 10.73 | 8.20 | |
| `many36` outlined, ExpandIRInsts body (FullOpts) | 8.96 | 5.42 | |
| `many36` compiler-rt helpers | 3.55 | 2.93 | 1.08 |

MinOpts costs 20-50 percent here (`many36` inline against outlined); the larger loss is ExpandIRInsts' algorithm
(outlined against compiler-rt: 1.8-2.5x).

(Which of RyuJIT's `DEFAULT_MIN_OPTS_*` limits (IL instructions, IL bytes, basic blocks, local variable counts) fired
is *unverified*; the switch lies between 20,226 and 36,470 IL bytes here.)

### 5.4 The Win64 libcall convention (F2)

`i128/strict_i128.c` (a constrained `__int128` -> double and `unsigned __int128` -> float conversion, the FP <-> i128
cases that name a helper in object mode today) linked against the helper archive:

- Linux triple: the libcall `__floattidf(native int lo, native int hi)` matches compiler-rt's definition (SysV passes
  `__int128` in two registers); output equals native on WSL .NET 8 and 10.
- Windows triple: compiler-rt's definition, compiled by clang with the Win64 ABI, is `float64 __floattidf(native int
  pointer)`; the legalizer's call is `float64(native int, native int)`. msil-ld printed `warning: function signature
  mismatch: __floattidf ... adapter: drops 1 extra argument` (and the same for `__floatuntisf`) and linked; the program
  died with `System.NullReferenceException at __floattidf(IntPtr) at <msil>adapt___floattidf_0(IntPtr, IntPtr)` on
  .NET 8 and .NET Framework (exit `0xC0000005`). Native Windows code is correct because X86 lowers these libcalls
  itself (`LowerWin64_i128OP`, `LowerWin64_FP_TO_INT128`, `LowerWin64_INT128_TO_FP`, `X86ISelLowering.cpp:30961-31090`):
  operands are spilled to 16-byte stack temporaries and passed by pointer, the result comes back as `v2i64` (for MSIL:
  a hidden result pointer, which is what the compiled helper's `<2 x i64>` return becomes:
  `__udivti3` is `void(native int, native int, native int)` in the Windows archive, `void(native int x 5)` in the
  Linux one).
- Consequences: (1) any design in which the legalizer calls compiler-rt's i128 helpers needs the same lowering in
  `MSILTargetLowering` for Windows triples (the temporaries are `localloc`-frame slots, D13); (2) msil-ld must refuse,
  not adapt, a signature mismatch on a compiler-runtime name: plan 5.9.8's adapter imitates native behaviour for user
  code, while a helper mismatch is always a toolchain bug. The outlined `linkonce_odr` functions (5.2) avoid (1): they
  are IR functions with `i128` parameters on every triple, never C-ABI-lowered.

### 5.5 Options

- (a) Keep M12's per-site expansion: no work, but 600-1,050 IL instructions per site, the MinOpts cliff, and
  ExpandIRInsts' algorithm, a 128-step shift-subtract loop that is the slowest of all variants measured (5.2, 5.3).
- (b) compiler-rt helpers called by the legalizer in object and LTO mode (`setMaxDivRemBitWidthSupported(128)` there,
  ExpandIRInsts for standalone `-S`): needs the Win64 lowering (5.4); a constant-divisor path (`ReplaceNodeResults`
  must leave constants to `expandDIVREMByConstant`: today it reports `runtime helper '__udivti3'` for `x / 10` when
  the expansion is off, measured with `-mllvm -expand-div-rem-bits=128`); the `udivmodti4.c` asm guard
  (`crt/udivmodti4.c.diff`); and the archive. Object-form text (`-mllvm -msil-object-form`, which the M19 `obj-text`
  leg compares with objects) follows object mode, but standalone `-S` keeps the inline shape.
- (c) Outlined per-module `linkonce_odr hidden` COMDAT functions, created in `MSILPrepareIR::doInitialization` (which
  runs before ExpandIRInsts' `runOnFunction` in the legacy pass manager, backlog recon 2.2) for each i128 operation
  kind a module uses (`udiv`, `sdiv`, `urem`, `srem`, and the four FP <-> i128 conversion directions), every
  variable-divisor site rewritten into a call. Identical in standalone `-S`, object-form text, objects and LTO; no
  archive dependency; no ABI question (IR functions with `i128` parameters lower the same on every triple); one copy
  per assembly after COMDAT deduplication (HS-E11). Two possible bodies: (c1) the plain instruction, expanded by
  ExpandIRInsts (M12's verified expansion: same speed as today, measured `nt-outl`); (c2) compiler-rt's algorithm
  (`__udivmodti4`: Knuth D on 64-bit digits with a fast path for divisors below 2^64, the common `(u128)a*b % m`
  case) as IR. (c2) is measured here as `nt-irhelp-win`: compiler-rt's `udivmodti4.c`, `udivti3.c`, `umodti3.c`,
  `divti3.c`, `modti3.c` compiled to IR for `x86_64-unknown-linux-gnu-msil` (SysV keeps `i128` parameters in the
  IR), retargeted to the Windows triple and linked with the rewritten program: correct on .NET 8 and .NET Framework,
  no Win64 problem. The backend can carry that IR as text parsed into the module (regenerated from compiler-rt by a
  script), or build it with `IRBuilder` (about 80 lines of C to port).
- (d) `UInt128`/`Int128` (.NET 7+) as the body of the private helper, under a link-time switch with a reference check
  (msil-ld synthesizes the body, as for System.Half in 3.4).

Recommendation: **(c2)**, starting from (c1) if the IR body is not ready, with constant divisors kept inline (later
an `expandDIVREMByConstant`-style lowering: 103 against 473 IL for `/ 10`) and the constrained FP <-> i128
intrinsics turned into plain instructions first (CIL has one rounding mode and no FP exceptions; `LowerStrictFP`
already does this for f32/f64). (d) is not needed: `Int128` bodies ran the workload no faster than compiler-rt's algorithm (5.8-5.9 s against 5.2-6.3 s)
and exist only from .NET 7. (b) is not needed once (c2) exists.

## 6. Other compiler-rt calls the legalizer emits

| Call | When | Today | Phase 3 |
|---|---|---|---|
| `__powidf2`, `__powisf2` | `__builtin_powi`, `llvm.powi` on Linux (and MinGW) triples | `-S` error; object: undefined | archive (50 IL each); windows-msvc triples call `pow`/`powf`, as natively (results can differ from powi's repeated squaring, as natively) |
| `__floattidf`, `__floattisf`, `__floatuntidf`, `__floatuntisf` | constrained `sitofp`/`uitofp` from i128 | `-S` error; object: undefined (Windows: wrong ABI, F2) | constrained -> plain in MSILPrepareIR, then inline or outlined (5.5) |
| `__fixdfti`, `__fixsfti`, `__fixunsdfti`, `__fixunssfti` | constrained `fptosi`/`fptoui` to i128 | error in both modes (`ReplaceNodeResults`) | the same |
| `__extendhfsf2`, `__truncsfhf2` for f16 <-> i128 | `(__int128)h`, `(_Float16)i128` | f16 helpers + inline i128 (native Linux calls `__fixhfti`, `__floattihf`) | f16 helpers; i128 -> float -> half rounds twice exactly as native Windows' `__floattisf` + `__truncsfhf2` does |
| `__extendbfsf2`, `__truncsfbf2` for bf16 <-> i128 | `(__bf16)i128` | bf16 helpers | the same (bf16 -> i128 is inline since M12) |
| `__mulsc3`, `__muldc3`, `__divsc3`, `__divdc3` | frontend, complex `*` (slow path) and `/` | `-S` error; object: undefined | archive (section 4) |
| `__clear_cache` | `__builtin___clear_cache`, `llvm.clear_cache` | `-S` error; object: undefined | lower to nothing (native x86 emits nothing; CIL has no instruction cache) |
| `__atomic_*` | AtomicExpand with `setMaxAtomicSizeInBitsSupported(0)` | errors | atomics topic (Interlocked); compiler-rt `atomic.c` for odd sizes (HS 2.6) |
| `fma`, `fmaf`, `frexp`, `frexpf`, `ldexp`, `ldexpf`, `lrint`, `llrintf`, `lround`, `llroundf`, `rint`, `roundeven`, `sqrtf16` | libm names from intrinsics | `-S`: D14 table or "not a Phase-1 import"; object: undefined, bound by msil-ld to the host libm | unchanged (host libm, D14); `roundeven` is missing from UCRT and `sqrtf16` from every libc (native code has the same unresolved call); helpers-switches S5 maps the exact ones to `Math` |
| `__multf3`, `__addtf3`, ... | `__float128`/`fp128` arithmetic | `floating-point type 'fp128' is not supported for MSIL` | unchanged (2.1) |

## 7. Recommendations per family

| Family | Default (runs on Framework 4.8 and modern .NET alike) | Switch | Helper source | Tests |
|---|---|---|---|---|
| f16 conversions | compiler-rt `__extendhfsf2`, `__extendhfdf2`, `__truncsfhf2`, `__truncdfhf2` | none in Phase 3 (a link-time System.Half synthesis is possible later, 3.4) | compiler-rt C in the archive | `f16conv.c`-style hashes vs native compiler-rt on every runtime: parts A, C, S in the milestone suite; B and D (2^32 conversions, 40-95 s per runtime here) in the phase gate |
| bf16 | extend inline; compiler-rt `__truncsfbf2`/`__truncdfbf2` | none (no BCL type) | compiler-rt C after the signature fix | parts D and S |
| C complex | compiler-rt `__mulsc3`, `__muldc3`, `__divsc3`, `__divdc3` | none | compiler-rt C | `cx.c` named cases and matrix hash vs native compiler-rt (Windows oracle as is; Linux oracle linked with native compiler-rt) |
| i128 division and FP <-> i128 conversions | outlined `linkonce_odr` function per kind (constants inline), body = compiler-rt's algorithm as IR | none needed (an `Int128`/`UInt128` body under a link-time switch is possible) | IR materialized by MSILPrepareIR from compiler-rt's C (regenerated by a script) or built with `IRBuilder` | `nt128.c` transcript, a 36-site JIT-tier check (`DOTNET_JitDisasmSummary`), the existing `i128-fp-conv.c` |
| constrained FP <-> i128 | the plain conversion (one rounding mode) | none | none | `strict_i128.c` on both triples, Windows included |
| powi | `__powidf2`/`__powisf2` on Linux/MinGW triples, `pow` on MSVC triples | none | compiler-rt C | transcript vs native |
| `long double` | `double`; `l` functions forwarded on the wide triples | none | generated C wrappers in the archive | `ldl.c`-style links on the Linux triple; a lit test that every listed name has a wrapper or stays an error |
| `__clear_cache` | nothing | none | none | FileCheck |

## 8. Decisions

**F1. f16 conversions: compiler-rt helpers or System.Half?** Options: (a) compiler-rt helpers everywhere; (b)
System.Half behind a codegen switch with a reference check, helpers otherwise; (c) System.Half bodies synthesized by
msil-ld under a link-time switch when a reference defines `System.Half`. Recommended **(a)**; (c) later if wanted.
Evidence: F4 (helpers bit-identical to native compiler-rt on every input, five runtimes), F5 (System.Half differs on
the 1,022 sNaN halves; System.Half 10-35 percent faster in a conversion-only loop; absent on Framework), 3.4.

**F2. bf16 in signatures.** Options: (a) bits in `int32` like f16 (backend); (b) a `uint16_t` result in compiler-rt's
`fp_trunc.h` under `__MSIL__`. Recommended **(a)**, with bf16 -> f32 inline. Evidence: HS-E3, F4 (part D equal with
(b)), 3.5.

**F3. C complex.** Options: compiler-rt helpers; System.Numerics.Complex; inline Annex G code generated by the
backend. Recommended **compiler-rt helpers**. Evidence: F6 (3,896 and 7,343 of 38,416 differences for `Complex`, 0
for the CIL helpers).

**F4. The Linux oracle for helper tests.** The E7 oracle links libgcc_s, whose complex division differs from
compiler-rt on 15 percent of the special-operand matrix and whose f16/bf16 conversions quiet sNaN. Options: (a) build
the native compiler-rt archive for `x86_64-unknown-linux-gnu` in the same step as the MSIL one and link it into the
oracle before `libgcc_s.so.1` for the helper tests; (b) keep libgcc and restrict tests to operands where both agree.
Recommended **(a)**. Evidence: F4, F6, 4.3. Needs the user only if (a) counts as changing the oracle's definition
("native clang's output").

**I1. i128 division and FP <-> i128 conversions.** Options: (a) keep M12's per-site expansion; (b) compiler-rt
helpers called by the legalizer in object/LTO mode; (c1) outlined per-module `linkonce_odr` functions (one per
operation kind, COMDAT-deduplicated) with ExpandIRInsts bodies; (c2) the same with compiler-rt's algorithm as IR;
(d) `Int128`/`UInt128` bodies under a switch. Recommended **(c2)** (via (c1) if needed), (d) not needed by default.
Evidence: F8 (IL -58 to -63 %, JIT code -62 to -67 % on a 26-site workload), F9 (MinOpts at 36 sites in one
function), the speed tables of 5.2 and 5.3 (compiler-rt's algorithm 1.5-2.4x faster than ExpandIRInsts' on the workload, 1.8-6.5x on the 20- and 36-site functions), F2 (Win64 hazard of (b)), F10.

**I2. Signature mismatches on compiler-runtime names in msil-ld.** Options: (a) keep plan 5.9.8's adapter and
warning; (b) an error for any name in the codegen triple's RuntimeLibcalls set. Recommended **(b)**, together with
HS-H3 (never bind such a name natively). Evidence: F2.

**L1. `long double` on the wide-native triples.** Options: (a) keep today's link errors; (b) forward the `l`
functions in the helper archive; (c) an msil-ld alias table; (d) emulate x87/binary128. Recommended **(b)**. Evidence:
F13 (wrapper archive links and runs), 2.3. Needs the user: a program that calls `sqrtl` then links and computes in
double precision instead of failing to link.

**L2. `%L` in printf/scanf formats on the wide-native triples.** Options: (a) nothing; (b) a clang warning; (c) native
images of `long double` variadic arguments (clang marker + variadics pass). Recommended **(b)** in Phase 3, (c) on
demand. Evidence: F13 (glibc prints `nan` or a stale value), variadics recon 3.6 (9).

**L3. Data model.** Keep `long double` = `double`, `LongDoubleSize` 8, `__float128` predefined but unusable on the
Linux and MinGW triples. Recommended: keep. Evidence: F14 (turning `__float128` off breaks glibc's headers), 2.3.

## 9. Work packages (disjoint files; sizes rough)

1. **WP-FP1 helper archive families** (with helpers-switches WP-H1, which owns the build): the MSIL source list gains
   the f16, bf16, complex and powi sources of `crt_build.py` (13 of its 30; the i128 sources are WP-FP3's input for
   its IR, and archive members only if I1 (b) is chosen, with the `udivmodti4.c` guard); a generated `msil/longdouble.c` for the wide
   triples (about 180 wrappers generated from `NativeLibraries.cpp`'s table by a script that also checks that table
   and wrappers agree). About 250 lines.
2. **WP-FP2 backend** (`MSILISelLowering.cpp`, `MSILISelLoweringCall.cpp`, `MSILAsmPrinter.cpp`): bf16 registers in
   signatures (F2 (a)); BF16_TO_FP back to Expand after S5; `clear_cache` -> nothing; the `-S` diagnostics reworded
   ("needs msil-ld and the helper archive"); tests `call-errors.ll`, `fp-expand.ll`, `libcall-policy.ll`, the bf16
   tests. About 120 lines plus tests.
3. **WP-FP3 i128 outlining** (`MSILPrepareIR.cpp` `doInitialization`: constrained FP <-> i128 to plain, outlined
   COMDAT functions with compiler-rt's algorithm as their bodies, a script that regenerates that IR from
   `compiler-rt/lib/builtins`; the `ReplaceNodeResults` backstop kept): about 150 lines plus the IR; tests: IR
   FileCheck of the outlined functions in `-S` and objects, the `nt128.c` transcript, a JIT-tier test of a 36-site function, `strict_i128.c` on
   both triples.
4. **WP-FP4 msil-ld** (`lld/MSIL/Signatures.cpp`, `NativeLibraries.cpp`): I2's error for RuntimeLibcalls names, with
   HS-H3's refusal of native binding. About 40 lines; `lld/test/MSIL` tests with yaml2obj objects.
5. **WP-FP5 clang** (format checking in `clang/lib/Sema`): L2's warning for `%L` on MSIL triples with a wide native
   `long double`. About 40 lines; a `clang/test/Sema` test.
6. **WP-FP6 tests and oracle** (`msil/test/<phase-3 dir>`, `lit.cfg.py`): the f16/bf16/complex/i128/long double
   transcripts and the native compiler-rt archive for the E7 oracle (F4 (a)).

## 10. Late goals

- **Portable triple `msil64-unknown-dotnet`:** every helper here is architecture-free C or IR, so the archive and the
  outlined i128 functions carry over; its `long double` should be `double` from the start (no wide native
  `long double` to forward, no `%L` problem); compiler-rt needs the `__MSIL__` alias guard in `int_lib.h` (HS 2.3).
  System.Half and Int128 bodies fit there only if the portable triple drops .NET Framework.
- **Managed calls from C:** not precluded; msil-ld synthesizing a C symbol's body from a BCL member (System.Half,
  Int128) is a small instance of binding a C name to a .NET member.
- **Inline IL:** no interaction.
- **FeatureIdeas (multi-triple assemblies):** helpers in `<Module>` would collide by name between triples (HS 2.6), the
  outlined `__msil_*` functions too; a per-triple holder class later solves both.
- **Final cleanup:** the "(phase 3)" texts listed in 1.3 change with WP-FP2.

## 11. Risks and open questions

- Exhaustive f16 tests (parts B and D, 2^32 conversions each) take 40-95 s per runtime on this host (libgcc's
  soft-fp: 594 s for part B); keep them out of the milestone gates.
- Outlining changes the shape of every program with i128 division (calls instead of inline code); `-S` and object
  forms stay consistent under (c), which the M18/M19 parity legs need.
- Which MinOpts limit `many36` crossed is not identified (*unverified*); a test should pin "FullOpts" for a 36-site
  function after outlining rather than the threshold.
- libgcc and compiler-rt differ, so "matches native" depends on the Linux oracle's runtime library (F4).
- Not measured: aarch64 MSIL triples at run time (compile-only, D6), MinGW (`x86_64-w64-windows-gnu-msil`), whether
  RyuJIT inlines the `long double` forwarding wrappers (a P/Invoke inside a three-instruction method).

## 12. Experiments index

| Id | What | Files |
|---|---|---|
| F1 | inventory: 107 cases x 2 triples x 2 levels, `-S`, `-c`, native | `scripts/inv_cases.py`, `inv_run.py`, `inv_table.py`, `inv/table.md` |
| F2 | Win64 i128 libcall vs compiler-rt ABI; msil-ld adapter | `i128/strict_i128.c`, `results.txt` |
| F3 | compiler-rt subset archive per triple; helper IL sizes | `scripts/crt_build.py`, `crt/*.diff`, `scripts/crt_sizes.py`, `crt/sizes.txt` |
| F4 | exhaustive f16/bf16 conversions, 12 variants | `f16/f16conv.c`, `scripts/f16_matrix.py`, `scripts/f16_compare.py`, `f16/matrix.md` |
| F5 | System.Half: IL patch, sNaN count, speed | `scripts/patch_il.py`, `f16/halfcheck-Program.cs`, `f16/f16bench.c`, `results.txt` |
| F6 | complex: Annex G vs System.Numerics.Complex vs libgcc | `cx/cx.c`, `cx/complex-probe-Program.cs`, `cx/named-compare.md`, `results.txt` |
| F7 | complex speed | `cx/cxbench.c`, `cx/complex-bench-Program.cs`, `results.txt` |
| F8 | i128 real workload: inline, helper, outlined, Int128 | `i128/nt128.c`, `scripts/i128_to_calls.py`, `scripts/i128_outline.py`, `scripts/patch_il.py`, `scripts/codesize.py`, `results.txt` |
| F9 | MinOpts cliff | `i128/many36.c`, `scripts/wsljit.sh`, `results.txt` |
| F10 | Int128 helper bodies | `scripts/patch_il.py` (recipe `int128`), `results.txt` |
| F11 | constant-divisor expansion estimate | `i128/div10.c` |
| F12 | `long double` data model, `max_align_t`, predefines | `ld/ma.c`, `results.txt` |
| F13 | native `l` functions, `%Lf` through glibc, wrapper archive | `ld/ldl.c`, `ld/ldn2.c`, `ld/ldwrap.c`, `results.txt` |
| F14 | `__float128` unavailable breaks glibc's headers | `results.txt` |
| timing | every speed number of F5, F7-F9 | `scripts/timing.py`, `timing.txt` |
