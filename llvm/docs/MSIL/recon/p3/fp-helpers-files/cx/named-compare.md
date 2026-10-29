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
