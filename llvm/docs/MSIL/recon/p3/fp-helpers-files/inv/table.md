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
