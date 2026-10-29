| Variant | A s | B s | C s | D s | differs from compiler-rt (lines) |
|---|---|---|---|---|---|
| native Windows, VS compiler-rt | 0.5 | 45.86 | 0.4 | 49.58 | none |
| native Windows, this tree compiler-rt | 0.07 | 48.12 | 0.35 | 46.41 | none |
| native Linux, libgcc_s | 21.41 | 593.68 | 3.21 | 155.08 | A:A h->f32 3db7a122e9f79cb7; D:D f32->bf16 NaN; S:S h 7c01; S:S h 7d00; S:S h fc01; S:S f 7fa00000 |
| native Linux, this tree compiler-rt | 2.99 | 72.49 | 3.87 | 62.56 | none |
| MSIL win, compiler-rt helpers, .NET 8 | 0.3 | 53.01 | 0.91 | 85.74 | none |
| MSIL win, compiler-rt helpers, .NET 10 | 1.1 | 60.36 | 0.99 | 67.42 | none |
| MSIL win, compiler-rt helpers, .NET Framework 4.8 | 0.84 | 78.13 | 2.32 | 48.75 | none |
| MSIL lin, compiler-rt helpers, WSL .NET 8 | 2.09 | 75.17 | 5.86 | 93.99 | none |
| MSIL lin, compiler-rt helpers, WSL .NET 10 | 6.92 | 79.65 | 6.46 | 78.17 | none |
| MSIL win, System.Half, .NET 8 | 0.75 | 40.47 | 1.27 | 54.24 | A:A h->f32 3db7a122e9f79cb7; S:S h 7c01; S:S h 7d00; S:S h fc01 |
| MSIL win, System.Half, .NET 10 | 0.95 | 47.06 | 0.72 | 45.36 | A:A h->f32 3db7a122e9f79cb7; S:S h 7c01; S:S h 7d00; S:S h fc01 |
| MSIL lin, System.Half, WSL .NET 10 | 31.72 | 76.1 | 3.97 | 93.44 | A:A h->f32 3db7a122e9f79cb7; S:S h 7c01; S:S h 7d00; S:S h fc01 |

Reference output (native Windows, VS compiler-rt):
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
