# M11 recon: items 1, 2, 8, 9 and 10 ("legs")

Recon agent for the section 12 row 'Deferred after the upstream merge' (2026-10-19), items 1, 2,
8, 9 and 10. The merge review's evidence (wf_226b9df8-523) is lost; everything below was
re-established from scratch on 2026-10-20 at HEAD 0c5044bac349 (branch `msil`, clean tree),
with the existing build `D:\llvm-msil\build` (X86;WebAssembly;MSIL, Release+asserts; not
rebuilt, no cmake/ninja run). No repository file was edited. Scratch work is under
`<scratch>\m11\legs`
(`$S` below). Prototypes the drafter can copy are in
`D:\llvm-msil\msil\orchestration\M11\recon\legs-proto\` (git-ignored):

| File | What |
|---|---|
| `aarch64-aggregate-abi.c` | item 1: proposed `clang/test/CodeGen/MSIL/aarch64-aggregate-abi.c` (passes) |
| `aarch64-agg-probe.c` | item 1: the probe source the tables below come from |
| `lit.cfg.py.pe-machine.diff` | item 1, optional: the one-line `msil_pe_machine` lit parameter used for the cross-execution prototype |
| `check-action-pins.py` | item 2: proposed `msil/utils/ci/check-action-pins.py` (fails now, passes after the fix) |
| `lit-legs.ps1.utc.diff`, `lit-legs.sh.utc.diff` | item 8: the 'utc' leg for both CI scripts (prototypes ran) |
| `mllvm.c` | item 10: proposed `clang/test/CodeGen/MSIL/mllvm.c` (passes) |

**Host load caveat.** During this recon the host CPU sat at 100% (other agents were running a
`clang-cl` compile and up to 8 `llc` processes). Every wall-clock time below that was measured
under that load says so, and an idle-host estimate is derived from the M10 gate's own logs in
`D:\llvm-msil\build\lit-results\*.log` (written 2026-10-19 23:21-23:48 on an idle host).

**Private lit runs.** No shared `Output\` directory was touched:
- msil/test: a private site config `$S\item9\obj\test\lit.site.cfg.py` (absolute paths into
  `D:\llvm-msil\build`, `config.msil_obj_root` = `$S\item9\obj`, loads
  `D:\llvm-msil\msil\test\lit.cfg.py`); `msil-lit.ps1` accepts that rooted path and takes the
  shared build lock, so the gate command shape is unchanged:
  `pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 "-LitArgs: -sv ..." $S\item9\obj\test`.
- clang tests: `$S\csuite\src\` holds a copy of `clang/test/lit.cfg.py`, a site config with
  absolute paths and `clang_obj_root` = `$S\csuite\obj`, and a copy of
  `clang/test/CodeGen/MSIL/lit.local.cfg` (so `%ilasm-verify` and the `ilasm` feature work);
  prototype tests in `$S\csuite\src\CodeGen\MSIL\` run with
  `python D:/llvm-msil/build/bin/llvm-lit.py -v <file>`.

---

## Item 1: upstream de7794fcf781 changed the AArch64 aggregate classification

### The upstream change

`git show de7794fcf781` ("[clang][AArch64] Don't round parameters to multiples of 8-byte
(#215264)", 2026-10-12, inside the merge range d1e15b695381..64f50ffd4f1c) adds, in
`AArch64ABIInfo::classifyArgumentType`, before `Size = llvm::alignTo(Size, Alignment)`
(now `clang/lib/CodeGen/Targets/AArch64.cpp:509-511`):

```cpp
    if (Size < 64 && getDataLayout().isLittleEndian() && !isDarwinPCS())
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), Size));
```

So a non-HFA, non-PST aggregate argument of fewer than 8 bytes is now passed as `iN` of its
exact size (i8, i16, i24, i32, i40, i48, i56) instead of `i64`. Before the change the code fell
through to `Size = alignTo(Size, 64)` = 64 (`AArch64.cpp:513`) and `BaseTy = i64`
(`AArch64.cpp:542-548`), i.e.
`i64`. Upstream's own test updates in the same commit show exactly that, for example
`clang/test/CodeGen/AArch64/args.cpp`: `@empty_arg(i64 %e.coerce, ...)` became
`@empty_arg(i8 %e.coerce, ...)`, and `clang/test/CodeGen/arm64-microsoft-arguments.cpp`:
`call i8 ...func6...(i64 ...)` became `(i8 ...)`, `func5 ... i64` became `i32`. Results were
already `iN` for aggregates of up to 8 bytes (`classifyReturnType`, `AArch64.cpp:616-625`,
unchanged by the commit).

The condition is `!isDarwinPCS()` and little-endian, so **all four Phase-1 aarch64 MSIL triples
are affected**, Windows included (the Win64 ABI kind goes through the same `Size <= 128` block).

### The aarch64 MSIL triples

Plan D6 (ImplementationPlan.md line 145) and `clang/test/CodeGen/MSIL/triples.c`:
`aarch64-unknown-linux-gnu-msil`, `aarch64-unknown-linux-musl-msil`,
`aarch64-pc-windows-msvc-msil`, `aarch64-w64-windows-gnu-msil`. clang allocates
`MSILTargetInfo<LinuxTargetInfo<AArch64leTargetInfo>>`, `MSILTargetInfo<MicrosoftARM64TargetInfo>`
and `MSILTargetInfo<MinGWARM64TargetInfo>` (`clang/lib/Basic/Targets.cpp:154-162`) and picks the
TargetCodeGenInfo by architecture (`clang/lib/CodeGen/CodeGenModule.cpp:150-161`:
`createWindowsAArch64TargetCodeGenInfo(CGM, AArch64ABIKind::Win64)` on Windows, else
`createAArch64TargetCodeGenInfo(CGM, AAPCS)`), so MSIL inherits the AArch64 C ABI unchanged
(Design.md 3). Execution in Phase 1 is x86_64 only (D6); `llvm/test/CodeGen/MSIL/aarch64-triples.ll`
pins only data layouts and assembly of a scalar module.

### What clang and llc emit now

Probe: `legs-proto/aarch64-agg-probe.c` (structs of 1, 2, 3, 4, 5, 6, 7, 8, 12, 16 and 24
bytes, int+float, char+float, HFAs of 2/3 floats, 4 doubles, 1 double, float[3], HVAs of
`vector_size(16)` x2 and `vector_size(8)` x1, a pair of pointers; each `struct T f(struct T x)
{ return x; }`).

```text
cd $S\item1
D:/llvm-msil/build/bin/clang.exe --target=<triple> -O2 -S -emit-llvm agg.c -o agg.<triple>.ll
D:/llvm-msil/build/bin/llc.exe -O2 agg.<triple>.ll -o agg.<triple>.il       (and -O0, -verify-machineinstrs)
D:/llvm-msil/build/bin/clang.exe -cc1 -triple <triple> [-O2] {-emit-llvm | -S} agg.c -o -
```

Results (identical IR on gnu and musl, identical IR on msvc and mingw; the only Linux/Windows IR
difference is `alignstack(8)` / `alignstack(16)` on HFA/HVA arguments on Linux; the IL method
headers are byte-identical on all four triples, at clang -O0 and -O2 and at llc -O0 and -O2):

| C type (size) | IR argument / result (now) | IR argument before de7794fcf781 | IL header (now) |
|---|---|---|---|
| `{char}` (1) | `i8` / `i8` | `i64` | `int32 'f1'(int32)` |
| `{short}` (2) | `i16` / `i16` | `i64` | `int32 'f2'(int32)` |
| `{char a,b,c}` (3) | `i24` / `i24` | `i64` | `int32 'f3'(int32)` |
| `{int}` (4) | `i32` / `i32` | `i64` | `int32 'f4'(int32)` |
| `{char[5]}` (5) | `i40` / `i40` | `i64` | `native int 'f5'(native int)` |
| `{short[3]}` (6) | `i48` / `i48` | `i64` | `native int 'f6'(native int)` |
| `{char[7]}` (7) | `i56` / `i56` | `i64` | `native int 'f7'(native int)` |
| `{int,int}` (8) | `i64` / `i64` | `i64` | `native int 'f8'(native int)` |
| `{int,int,int}` (12) | `[2 x i64]` / `[2 x i64]` | same | `void 'f12'(native int, native int, native int)` (result demoted to a hidden first pointer) |
| `{long long,long long}` (16) | `[2 x i64]` / `[2 x i64]` | same | `void 'f16'(native int, native int, native int)` |
| `{int; float}` (8) | `i64` / `i64` | same | `native int 'fif'(native int)` |
| `{char; float}` (8) | `i64` / `i64` | same | `native int 'fcf'(native int)` |
| HFA `{float,float}` | `[2 x float]` (Linux: `alignstack(8)`) / `%struct.HFA2` | same | `void 'fh2'(native int, float32, float32)` |
| HFA `{float x3}` | `[3 x float]` / `%struct.HFA3` | same | `void 'fh3'(native int, float32, float32, float32)` |
| HFA `{double x4}` | `[4 x double]` / `%struct.HFA4` | same | `void 'fh4'(native int, float64, float64, float64, float64)` |
| HFA `{double}` | `[1 x double]` / `%struct.HD1` | same | `float64 'fhd1'(float64)` |
| HFA `{float[3]}` | `[3 x float]` / `%struct.HFAArr` | same | `void 'fharr'(native int, float32, float32, float32)` |
| HVA `{v4f, v4f}` | `[2 x <4 x float>]` (Linux: `alignstack(16)`) | same | `void 'fhva2'(native int, float32 x8)` |
| HVA `{v2f}` | `[1 x <2 x float>]` | same | `void 'fhvas'(native int, float32, float32)` |
| `{void*,void*}` (16) | `[2 x ptr]` / `[2 x i64]` | same | `void 'fsp2'(native int, native int, native int)` |
| `{int[6]}` (24) | `ptr dead_on_return dereferenceable(24)` + `sret` | same | `void 'fs24'(native int, native int)` |

So the IL change from the merge is: **1-4-byte aggregates are now `int32` parameters (were
`native int`)**; 5-7-byte aggregates stay `native int` in IL (i40/i48/i56 are promoted to i64)
but the IR the backend sees changed from i64 to i40/i48/i56. Results did not change. The
x86_64 SysV triples already used the same i8..i56 shapes (`define i24 @f3(i24 ...)` on
`x86_64-unknown-linux-gnu-msil`), so the backend's odd-width lowering was already covered for
x86_64 (`llvm/test/CodeGen/MSIL/struct-args-sysv.ll:107`, `take_c3(i24)`), but nothing pins the
aarch64 shapes: `grep -rln aarch64 clang/test/CodeGen/MSIL llvm/test/CodeGen/MSIL msil/test` finds
no aggregate test.

Call sites pack correctly. For `call_small(struct S3 *p, struct S5 *q) { g3(*p); g5(*q); }` at
-O2 the IL is `ldarg.0; unaligned. 1; ldind.u2; ldarg.0; ldc.i4 2; conv.i; add; ldind.u1;
ldc.i4.s 16; shl; or; call void 'g3'(int32)` and `... ldind.i4; conv.u; ... ldind.u1; conv.u;
ldc.i4.s 32; shl; or; call void 'g5'(native int)` (little-endian packing of the i24 / i40).
Every module above assembles as an ARM64 image (`ilasm -nologo -quiet -dll -pe64 -arm64`, exit
0, both triples, llc -O0 and -O2), and in the harness run below ILVerify and PEVerify accept it.

### Can aarch64 MSIL run on this host? Not through the harness as it is

The IL of an aarch64 triple is machine-neutral (the machine type exists only in the PE header),
and the runtime does not care about the ABI clang chose, but:

```text
cd $S\item1
clang --target=aarch64-pc-windows-msvc-msil -O2 -S run.c -o run.a64.O2.il      (run.c: small structs, expected exit (14+30+60+8+7+15)&0xff = 134)
ilasm -nologo -quiet -dll -pe64 -arm64 -output=run-arm64\run.dll run.a64.O2.il   -> exit 0
ilasm -nologo -quiet -dll -pe64 -x64   -output=run-x64\run.dll   run.a64.O2.il   -> exit 0
dotnet exec --runtimeconfig run.runtimeconfig.json run.dll   (net8.0)
  run-arm64: Unhandled exception. System.IO.FileLoadException: Could not load file or assembly
             'run, Version=0.0.0.0, ...'   (exit -532462766)
  run-x64:   exit 134 (correct)
```

and through lit (private obj dir `$S\item1\obj`):

```text
python D:/llvm-msil/build/bin/llvm-lit.py -v --param msil_triple=aarch64-pc-windows-msvc-msil \
  --param msil_opt_levels=O2 $S\item1\obj\test\M1\09-structs\struct-byval.c
FAIL: ... ilasm -pe64 -arm64 ok, msil-verify.py ok, then
  jitall: error: cannot load ...struct-byval.c.tmp.O2.dll: FileLoadException: Could not load file
  or assembly 'struct-byval, ...'
```

`msil/test/lit.cfg.py:450` derives the ilasm machine from the triple
(`ilasm_flags = {"x86_64": " -pe64 -x64", "aarch64": " -pe64 -arm64"}.get(triple_arch, "")`), so
an x64 CoreCLR refuses every aarch64-triple image. (Cross-execution with an override is in the
'Optional' part below; results of a prototype run are at the end of this item.)

### Recommended fix (test only)

No compiler change: the new shapes are upstream's AAPCS64 decision, both sides of every MSIL
call are compiled by the same clang, and the Phase-1 imports (D14: puts, putchar, libm) take no
aggregates, so nothing can disagree. Pin the shapes:

1. **New `clang/test/CodeGen/MSIL/aarch64-aggregate-abi.c`** (prototype
   `legs-proto/aarch64-aggregate-abi.c`, PASSES in the private clang suite in about 25 s idle,
   59 s under load). `// REQUIRES: msil-registered-target`. RUN lines:
   - IR: `%clang_cc1 -triple <t> -emit-llvm -o - %s | FileCheck %s --check-prefixes=IR,IR-LNX`
     for `aarch64-unknown-linux-gnu-msil` and `-musl-msil`, and `--check-prefixes=IR,IR-WIN`
     for `aarch64-pc-windows-msvc-msil` and `aarch64-w64-windows-gnu-msil`.
   - IL: `%clang_cc1 -triple <t> -S -O2 -o %t.<x>.il %s` then
     `FileCheck %s --check-prefix=IL --implicit-check-not=int64 --input-file=%t.<x>.il` for all
     four triples, and the same at -O0 for the gnu and msvc triples.
   - `%if ilasm %{ %ilasm-verify -arm64 -output=%t.gnu.dll %t.gnu.il %}` and the same for the
     msvc -O0 output (the `-arm64` after `-x64` convention of aarch64-triples.ll:101-104).
   - One function per row of the table above (S24 is the indirect case), each with an
     `IR-LABEL: define {{.*}}i24 @f3(i24 %x.coerce)` style line and an exact
     `IL: {{^}}.method public static int32 'f3'(int32) cil managed{{$}}` line; HFA rows use
     `IR-LNX-LABEL` with `alignstack(8)` and `IR-WIN-LABEL` without it.
   - Call sites: `volatile int sink; __attribute__((noinline)) void g3(struct S3 x) { sink =
     x.c; }`, the same `g5(struct S5)`, and `void call_small(struct S3 *p, struct S5 *q) {
     g3(*p); g5(*q); }` with `IR: call void @g3(i24 %{{.*}})`, `IR: call void @g5(i40
     %{{.*}})`, `IL: call void 'g3'(int32)`, `IL: call void 'g5'(native int)`. (A `return x`
     callee does not work for the call checks: at -O2 clang infers `returned`/`memory(none)` and
     deletes the call; the volatile store keeps it.)
   - Leave HVAs out of the committed test or keep them with a comment: vector_size types are a
     GNU extension; the IL is the scalarized float32 list (D8 'no vector types').
   - The header comment must say why it exists: de7794fcf781 changed i64 to iN for aggregates
     below 8 bytes on all four triples; 1-4-byte ones became `int32` in IL.
2. **New `llvm/test/CodeGen/MSIL/struct-args-aapcs64.ll`**, the AAPCS64 twin of
   `struct-args-sysv.ll` / `struct-args-win64.ll`: the IR clang -O2 emits for
   `aarch64-unknown-linux-gnu-msil` (callees taking and returning `i8`, `i16`, `i24`, `i32`,
   `i40`, `i48`, `i56`, `i64`, `[2 x i64]`, `[2 x ptr]`, `[2 x float] alignstack(8)`,
   `[4 x double] alignstack(8)`, `%struct.HFA*` results, an indirect `ptr dead_on_return` +
   `sret` case, and callers that build each coerced value from memory), RUN with
   `llc -mtriple=aarch64-unknown-linux-gnu-msil` and `-mtriple=aarch64-pc-windows-msvc-msil`,
   `-O0` and `-O2`, `-asm-verbose=false -verify-machineinstrs`, `SIG:` lines for every method
   header, CHECK lines for the call-site packing (`ldind.u2 ... ldc.i4.s 16; shl; or; call ...
   'g3'(int32)`), `--implicit-check-not=int64`, and `%if ilasm %{ ... %ilasm-verify -arm64 ... %}`.
   Measured: llc -O0/-O2 on both triples and ilasm -arm64 all exit 0 on the probe IR, and the
   21 method headers are the same at -O0 and -O2. This keeps the backend side pinned even if a
   later upstream merge changes clang again (the clang test then shows the IR change, the llc
   test shows the backend still handles the old shapes).
3. **Optional (execution):** a lit parameter `msil_pe_machine` (`x86_64`|`aarch64`, default the
   triple's architecture) in `msil/test/lit.cfg.py:450`
   (`ilasm_flags = {...}.get(params.get("msil_pe_machine") or triple_arch, "")`, plus a README
   row), so that `--param msil_triple=aarch64-pc-windows-msvc-msil --param
   msil_pe_machine=x86_64` runs the aarch64 IL on the x64 runtimes. A leg over
   `msil/test/M1/09-structs msil/test/M1/10-calls msil/test/M1/11-programs` would then execute
   the new shapes (prototype below: 34/34 pass on aarch64-pc-windows-msvc-msil, 214 s under
   load, about 1 min idle). This changes the D6 statement 'Execution: x86_64 triples only' and
   needs a plan decision; it is not needed to pin the signatures. Use the Windows aarch64 triple
   for such a leg: on aarch64 Linux plain `char` is unsigned and `struct-basic.c` fails by design
   (below).

**Gate step** (fails without the new test because the file does not exist; with the test and a
pre-merge clang it fails on the `int32 'f1'(int32)` lines):

```text
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" clang/test/CodeGen/MSIL/aarch64-aggregate-abi.c llvm/test/CodeGen/MSIL/struct-args-aapcs64.ll
```

Both files are also inside the existing net8 leg paths (`llvm/test/CodeGen/MSIL`,
`clang/test/CodeGen/MSIL`), so `run-m1-gate.ps1` and CI pick them up with no script change.
Runtime about 30-40 s on an idle host (the clang test alone took 25-59 s).

### Prototype cross-execution of the aarch64 shapes (evidence for option 3)

A scratch copy of `msil/test` (`$S\xexec\src`, with its own site config and obj dir) with one
changed line, `msil/test/lit.cfg.py:450` ->
`ilasm_flags = {"x86_64": " -pe64 -x64", "aarch64": " -pe64 -arm64"}.get(params.get("msil_pe_machine") or triple_arch, "")`:

```text
msil-lit.ps1 "-LitArgs: -sv --param msil_require_verify=1 --param msil_require_exec=1 --param msil_triple=aarch64-pc-windows-msvc-msil --param msil_pe_machine=x86_64" \
  $S\xexec\src\M1\09-structs $S\xexec\src\M1\10-calls $S\xexec\src\M1\11-programs
  -> 34 tests, 34 passed (O0 and O2, .NET 8, ILVerify + PEVerify + jitall), 214 s under load
msil-lit.ps1 "-LitArgs: -sv ... --param msil_runner=wsl --param msil_triple=aarch64-unknown-linux-gnu-msil --param msil_pe_machine=x86_64 --param msil_dotnet_runtime=10" <same paths>
  -> 34 tests, 33 passed, 1 failed (234 s under load):
     FAIL M1/09-structs/struct-basic.c: clang warns "struct-basic.c:121:19: result of comparison of
     constant -1 with expression of type 'char' is always false"; the transcript stops after
     'area=9' and the status is [exit 122] instead of [exit 0]
```

The one failure is **not a compiler bug**: plain `char` is unsigned on aarch64 Linux (AAPCS64),
so `n.c == -1` in `MSIL_EXPECT(n.c == -1 && ...)` (`msil/test/M1/09-structs/struct-basic.c:121`,
after `m.c = (char)(zero - 1)` at line 110) is false there, exactly as in a native aarch64-linux
build. The x86_64-derived `.expected` files assume signed plain `char`. If an aarch64 Linux
execution leg is ever added, such tests need `signed char` (or the leg has to exclude them);
the Windows aarch64 triple keeps `char` signed and passed everything. All the struct-by-value
and call tests (struct-byval.c, struct-return.c, nested-aggregates.c, many-args.c, ...) passed on
both aarch64 triples with the new iN shapes.

---

## Item 2: actions/upload-artifact pinned at v7.0.1

```text
grep -rn "upload-artifact@" .github
```

- `.github/workflows/msil-ci.yml:179` (linux job, 'Upload test outputs') and
  `.github/workflows/msil-ci.yml:289` (windows job):
  `uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7.0.1`
- Every upstream use (42 lines in 27 files, e.g. `.github/workflows/docs.yml:228`,
  `.github/workflows/premerge.yaml:130`, `.github/actions/build-container/action.yml:81`):
  `uses: actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9 # v7.0.2`
- Upstream bumped it in d770860630c7 ("Update [Github] Update GHA Dependencies (#231054)",
  2026-10-12, Renovate; also download-artifact v8.0.1 -> v8.0.2, setup-node, codeql), which is in
  the merge range; msil-ci.yml was not touched by the merge.
- `.github/msil/` (`disable-upstream-workflows.sh`, `.gitattributes`) pins nothing. The only other
  copies of the v7.0.1 SHA are the historical recon artifacts
  `llvm/docs/MSIL/recon/tests-ci.md:521,1750,1830` and
  `llvm/docs/MSIL/recon/artifacts/tests-ci/proto/.github/workflows/msil-ci.yml:133,213`; leave them
  (they record what upstream pinned at the time).
- The other pins in msil-ci.yml still equal upstream's: `actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1`
  (86 upstream uses), `actions/cache@55cc8345863c7cc4c66a329aec7e433d2d1c52a9 # v6.1.0` (upstream
  uses the same SHA as `actions/cache/restore` and `actions/cache/save`),
  `hendrikmuhs/ccache-action@f09c25b45002a07be2955cbe52e8cee55643f89d # v1.2.24`,
  `ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756 # v1.13.0`.
  `actions/setup-dotnet@v5` (`msil-ci.yml:106,213`) is unpinned by design (plan M0 CI WP:
  'TODO(user): pin SHA'; no upstream workflow uses it).

The contract (plan line 804, the M0 CI WP): 'third-party actions pinned by SHA exactly as
upstream pins them'.

### Recommended fix

In `.github/workflows/msil-ci.yml` lines 179 and 289 replace
`actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7.0.1` with
`actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9 # v7.0.2`. Verified on a
scratch copy (`$S\item2\fixed\.github`): `diff` shows exactly those two lines, PyYAML
`yaml.safe_load` accepts the file.

To keep the contract from drifting again at the next merge, add
**`msil/utils/ci/check-action-pins.py`** (prototype `legs-proto/check-action-pins.py`, 45 lines,
stdlib only): for every `uses: <owner>/<repo>[/<path>]@<ref> # <version>` line of the
`.github/**/msil-*.y*ml` files it collects the `(ref, version)` pairs that the other `.github`
YAML files use for the same `<owner>/<repo>`, prints `<file>:<line>: <action>@<ref> # <ver>;
upstream: <ref> # <ver> (<n> uses)` for each mismatch and `... not pinned upstream` (informational)
for actions upstream never uses, and exits 1 on any mismatch. Measured:

```text
python legs-proto/check-action-pins.py D:/llvm-msil
.github/workflows/msil-ci.yml:106: actions/setup-dotnet@v5: not pinned upstream
.github/workflows/msil-ci.yml:179: actions/upload-artifact@043fb46d... # v7.0.1; upstream: cf430e03... # v7.0.2 (42 uses)
.github/workflows/msil-ci.yml:213: actions/setup-dotnet@v5: not pinned upstream
.github/workflows/msil-ci.yml:289: actions/upload-artifact@043fb46d... # v7.0.1; upstream: cf430e03... # v7.0.2 (42 uses)
2 mismatched pin(s)            -> exit 1
python legs-proto/check-action-pins.py $S/item2/fixed      (the fixed copy)
0 mismatched pin(s)            -> exit 0
```

(The version comment is compared after stripping '#', so upstream's `#v6.1.0` and `# v6.1.0`
both match; a trailing remark after the version, as on msil-ci.yml's actions/cache lines, is
ignored.) The upstream-merge workflow could run it too; that is optional.

**Gate step** (exit 1 today, about 1 s):

```text
python D:/llvm-msil/msil/utils/ci/check-action-pins.py D:/llvm-msil
```

Cheaper alternative if no new script is wanted (exit 1 today):
`pwsh -NoProfile -Command "if (Select-String -LiteralPath D:/llvm-msil/.github/workflows/msil-ci.yml -Pattern 'upload-artifact@(?!cf430e030ddbb5b0abf93d22962f4752f3646cd9 # v7\.0\.2$)') { exit 1 }"`.

---

## Item 8: wire run-utc-tests.py back into run-m1-gate.ps1 and CI

### What exists

- `msil/utils/run-utc-tests.py` (430 lines): runs `llvm/test/tools/UpdateTestChecks/update_llc_test_checks/<name>.test`
  without lit, because `llvm/test/tools/UpdateTestChecks/lit.local.cfg` sets `llc-binary` only
  when a file named exactly `llc` exists, so lit reports the whole directory UNSUPPORTED on
  Windows. Accepted RUN commands: `cp [-f]`, `%update_llc_test_checks %t<ext>`, `diff -u`,
  `llc <args> < %t<ext> | FileCheck %t<ext>` chained with `&&`; `REQUIRES: <t>-registered-target`
  is checked against `llvm-config --targets-built` and a missing target FAILS (it never skips).
  Exit 0 only if every test passed. It keeps the source tree clean (`PYTHONDONTWRITEBYTECODE=1`;
  `git status --porcelain` was empty after every run).
- Section 12 rows: 2026-10-19 'M5.b run-utc-tests.py contract amended' (the `&&` chains, so it
  runs upstream's x86-mir-only.test; 'The M10 gate no longer runs run-utc-tests.py (M5 gate step
  10 was not carried forward): deferred item below') and the deferred row, item (8). The M5 gate
  step 10 was `python D:/llvm-msil/msil/utils/run-utc-tests.py --llc D:/llvm-msil/build/bin/llc.exe
  msil-basic x86-basic` (plan line 1276); the merge gate ran `msil-basic x86-basic x86-mir-only`.
- `msil/utils/run-m1-gate.ps1` has no utc row: its steps are configure, build, unit-tests,
  `lit-legs.ps1 -Wsl` and ci-sh; it learns the leg names from `lit-legs.ps1 -List`
  (`Get-ExpectedLegs`, lines 123-147) and adds a FAIL row for every expected leg without a row.
- `msil/utils/ci/lit-legs.ps1` legs (lines 67-85): net8, net10, net8-tc0, wsl-net10,
  wsl-net10-tc0, wsl-ilasm, modern-ilasm, roundtrip, expected (non-lit, `Invoke-ExpectedLeg`,
  lines 135-147), all-levels (-Full). `msil/utils/ci/lit-legs.sh` (lines 71-72): net8, net10,
  net8-tc0, net10-tc0, roundtrip, expected, all-levels (--full), upstream (--full, `ninja
  check-llvm check-clang`, which would run the UpdateTestChecks tests natively on Linux, but only
  in full runs).
- `.github/workflows/msil-ci.yml` runs `lit-legs.ps1` / `lit-legs.sh` (lines 171-175, 280-285), so a
  new leg reaches CI with no workflow change.

### Measurements

```text
python msil/utils/run-utc-tests.py --llc D:/llvm-msil/build/bin/llc.exe msil-basic x86-basic x86-mir-only
  PASS x3, "3 of 3 tests passed", exit 0: 17.3 s first (cold), 5.8 s second
python msil/utils/run-utc-tests.py --llc ... <all 52 tests of the directory>
  7 of 52 pass, 12.5 s; the other 45 are 'unsupported RUN command' (14 x
  '--include-generated-funcs', cat, rm, mkdir, echo, --filter, 2>&1, ...) or 'requires
  <target>-registered-target' (systemz, mips, ve, sparc, riscv, powerpc, nvptx, msp430,
  loongarch, lanai, ...): all of them are errors by design, not regressions
python msil/utils/run-utc-tests.py --llc ... msil-basic x86-basic x86-mir-only x86-asm-mir-mixed \
  x86-constant-pool wasm-function-name asm-show-inst
  7 of 7 passed, exit 0, 8.5 s
```

The 7 runnable tests: `msil-basic` (msil+x86), `x86-basic`, `x86-mir-only`, `x86-asm-mir-mixed`,
`x86-constant-pool`, `asm-show-inst` (x86) and `wasm-function-name` (webassembly). **The CI build
has no WebAssembly** (`msil/cmake/caches/MSIL.cmake`: `LLVM_TARGETS_TO_BUILD "X86"`; the host has
`X86;WebAssembly` from msil-configure.ps1's default), and the script fails a test whose target is
missing, so the leg must not list `wasm-function-name`.

### Recommended fix

A non-lit leg `utc` in both leg scripts, after `expected` (prototype diffs
`legs-proto/lit-legs.ps1.utc.diff` and `legs-proto/lit-legs.sh.utc.diff`):

- `msil/utils/ci/lit-legs.ps1`:
  - header (after line 26): `#   utc            run-utc-tests.py: the update_llc_test_checks tests that lit reports`
    / `#                  UNSUPPORTED on Windows (no 'llc' file; plan M5.b and section 12)` (put
    the new M11 gate step number in it).
  - legs array (after line 82): `@{ Name = 'utc'; Utc = $true }`.
  - `$utcTests = @('msil-basic', 'x86-basic', 'x86-mir-only', 'x86-asm-mir-mixed',
    'x86-constant-pool', 'asm-show-inst')` and `function Invoke-UtcLeg($l)`: shared build lock
    (`Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What 'run-utc-tests'`, as
    `Invoke-ExpectedLeg`), `& $MsilPython msil/utils/run-utc-tests.py --llc <build>/bin/llc.exe
    @utcTests 2>&1 | Tee-Object <results>/utc.log`, detail `run-utc-tests.py: <'N of M tests
    passed' line or 'no summary'> (exit <rc>)`, `Ok = ($rc -eq 0)`.
  - dispatch (line 152): `$r = if ($l.Expected) { Invoke-ExpectedLeg $l } elseif ($l.Utc) {
    Invoke-UtcLeg $l } else { Invoke-LitLeg $l }`.
- `msil/utils/ci/lit-legs.sh`: header line `#   utc         run-utc-tests.py: ...` (+ an indented
  continuation) between `expected` and `all-levels`, `legs=(... expected:expected utc:utc
  all-levels:lit:full upstream:upstream:full)`, the same `utc_tests=(...)` list, `run_utc_leg()`
  (`python3 "$MSIL_REPO/msil/utils/run-utc-tests.py" --llc "$build/bin/llc" "${utc_tests[@]}" 2>&1
  | tee "$results/utc.log"`, same detail), and `utc) run_utc_leg || status=FAIL ;;` in the case.
  Running the same script on Linux (where lit could run the directory) keeps both jobs on the same
  fixed list and makes a missing target fail instead of being UNSUPPORTED.
- `msil/utils/run-m1-gate.ps1`: no code change (the row comes from `lit-legs.ps1 -List`); update
  the step-4 header comment (lines 16-21) to name `utc`.
- `.github/workflows/msil-ci.yml`: no step change; the header comment (lines 22-28) may mention the
  update_llc_test_checks tests.
- Docs: `llvm/docs/MSIL/GettingStarted.md` leg table (lines 265-275) gets a row
  `| utc | llvm/test/tools/UpdateTestChecks/update_llc_test_checks (6 tests) | not lit:
  msil/utils/run-utc-tests.py |`; `msil/test/README.md` line ~113 if it enumerates legs.

Prototype verification (scratch copies, results dir redirected to `$S\item8\results`):

```text
pwsh -NoProfile -File $S\item8\ci\lit-legs.ps1 -List      -> ... expected, utc, all-levels (-Full)
pwsh -NoProfile -File $S\item8\ci\lit-legs.ps1 -Leg utc   -> 6 x PASS, "[msil-ci] PASS lit-legs/utc (1:28)
   run-utc-tests.py: 6 of 6 tests passed (exit 0)", exit 0 (88 s under 100% CPU load; 6-9 s idle)
pwsh -NoProfile -File $S\item8\ci\lit-legs.ps1 -BuildDir <empty dir> -Leg utc
   -> "lit-legs utc FAIL 0:13 run-utc-tests.py: no summary (exit 2)", exit 1
WSL: bash -n lit-legs.sh ok; bash lit-legs.sh --list -> net8 net10 net8-tc0 net10-tc0 roundtrip
   expected utc all-levels (--full) upstream (--full); the run-m1-gate ci-sh header parser
   (regex of run-m1-gate.ps1:174) yields the same list; --leg utc on an empty build dir -> FAIL row,
   exit 1
```

**Gate steps:**

```text
python D:/llvm-msil/msil/utils/run-utc-tests.py --llc D:/llvm-msil/build/bin/llc.exe msil-basic x86-basic x86-mir-only x86-asm-mir-mixed x86-constant-pool asm-show-inst   (6-9 s; lit reports this directory UNSUPPORTED on Windows)
pwsh -NoProfile -File D:/llvm-msil/msil/utils/ci/lit-legs.ps1 -Leg utc   (the leg itself; fails today with "unknown leg 'utc'"; about 10 s)
```

and the existing `pwsh -NoProfile -File D:/llvm-msil/msil/utils/run-m1-gate.ps1` step then shows a
`lit-legs utc` row. Note: lit-legs.ps1 writes `build/lit-results/utc.{log}`; that is what every
other leg does.

---

## Item 9: no gate runs the -O1/-O3/-Os/-Oz execution leg

### What exists

- `msil/test/lit.cfg.py:245-252`: `opt_levels = params.get("msil_opt_levels", "O0,O2")`, each of
  `O0,O1,O2,O3,Os,Oz` accepted; `%{msil_c}` runs the pipeline once per level;
  `lit.cfg.py:1052-1053`: `%{msil_ll}` (llc) takes O0..O3 only, so `.ll` tests skip Os/Oz
  (`ll_levels = [o for o in opt_levels if o in ("O0","O1","O2","O3")] or ["O2"]`).
- The leg already exists but only under -Full / --full: `lit-legs.ps1:83-84`
  (`@{ Name = 'all-levels'; Paths = @('msil/test'); Full = $true; Params = {
  'msil_opt_levels=O0,O1,O2,O3,Os,Oz' } }`), `lit-legs.sh:20,72,79,90`; msil-ci.yml runs it only
  when `FULL_CHECK` (schedule, commented out, or a manual `full` dispatch, lines 60-72);
  `run-m1-gate.ps1` only with `-Full`; the M10 gate step 13 runs `run-m1-gate.ps1` without
  `-Full`. So no gate has run O1/O3/Os/Oz.

### Run

```text
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 "-LitArgs: -sv --time-tests --param msil_require_verify=1 --param msil_require_exec=1 --param msil_opt_levels=O1,O3,Os,Oz --xunit-xml-output=$S/item9/levels4.xml" $S\item9\obj\test
```

(the whole msil/test suite: M1 and harness; Windows, x86_64-pc-windows-msvc-msil, .NET 8.0.32,
Framework ilasm, ILVerify + PEVerify + jitall required; the `fib.c.tmp.{O1,O3,Os,Oz}.{il,dll,out}`
files in `$S\item9\obj\test\M1\11-programs\Output` confirm the four levels ran)

Result: **195 tests, 189 passed, 6 unsupported, 0 failed; lit exit 0.** The 6 UNSUPPORTED are
the WSL-only harness tests (`harness/cctor-puts-wsl.test`, `harness/legs/tool-runner-wsl.test`,
`harness/legs/wsl-ilasm-errors.test`, `harness/legs/wsl-ilasm-runs.test`,
`harness/puts-cr-wsl.test`, `harness/puts-wsl.test`), the same 6 the net8 leg reports. No
failure, so no repro to report: the O1/O3/Os/Oz code paths are clean on this suite.

Time: 3128 s wall (52 min) **under 100% CPU load from other agents** (sum of per-test times
24714 s: M1 20152 s, harness 4562 s; the slowest tests took 430-490 s, e.g.
`harness/legs/roundtrip-composites.test` 489 s, `M1/11-programs/bigint-add.c` 444 s). Idle-host
estimate from the M10 logs: the net10 leg ran `msil/test/M1` at O0,O2 in 294 s wall
(per-test sum 2270 s); O1,O3,Os,Oz is 4 levels for the 93 C tests and 2 (O1,O3) for the 42 .ll
tests, i.e. about 1.8x the O0,O2 work, so at most about 9-10 min for msil/test/M1 and 11-12 min
for msil/test on an idle host (the harness adds about 1-2 min; its tests mostly do not depend
on the levels); the calibration below brings the M1 estimate down to 6-10 min.

Calibration under the same load, back to back (private obj dir, `msil/test/M1/11-programs`,
20 C programs):

```text
msil-lit.ps1 "-LitArgs: -sv --param msil_require_verify=1 --param msil_require_exec=1 --param msil_opt_levels=O0,O2"        $S\item9\obj\test\M1\11-programs  -> exit 0, 272 s (CPU 100%)
msil-lit.ps1 "-LitArgs: -sv --param msil_require_verify=1 --param msil_require_exec=1 --param msil_opt_levels=O1,O3,Os,Oz"  $S\item9\obj\test\M1\11-programs  -> exit 0, 354 s (CPU 100%)
```

The same 20 programs at O0,O2 took 40-48 s on the idle host (M10 logs: modern-ilasm 39.8 s,
roundtrip 46.5 s, wsl-ilasm 48.3 s), so the load factor was about 6x, and four levels cost
1.3x (wall) to 1.8x (work) of the two default levels. **Idle-host estimate: msil/test/M1 at
O1,O3,Os,Oz takes about 6-10 min** (net10 leg: 294 s for O0,O2), msil/test about 8-12 min.

### Recommended fix

Run the four extra levels in every gate and in CI, on `msil/test/M1` only (the harness does not
depend on the levels and already runs in net8 and wsl-net10), and keep `all-levels` as the
nightly superset:

- `msil/utils/ci/lit-legs.ps1`: a new default leg after `net8-tc0` (before the -Wsl legs):
  `@{ Name = 'more-levels'; Paths = $m1; Params = { 'msil_opt_levels=O1,O3,Os,Oz' } }`, with a header line
  `#   more-levels    msil/test/M1 at O1,O3,Os,Oz (O0 and O2 run in the legs above)   (step N)`.
  (Alternative: drop `Full = $true` from `all-levels` and narrow it to `$m1` with
  `O1,O3,Os,Oz`; then nothing runs all six levels in one leg. Recommended: keep `all-levels`
  under -Full unchanged and add `more-levels`.)
- `msil/utils/ci/lit-legs.sh`: `more-levels:lit` in `legs=(...)` after `net10-tc0`,
  `more-levels) printf '%s\n' "${m1[@]}" ;;` in `leg_paths`, `more-levels) echo
  msil_opt_levels=O1,O3,Os,Oz ;;` in `leg_params`, and the header line (the ci-sh check compares
  the header with `--list`).
- `msil/utils/run-m1-gate.ps1`: header comment only (the row comes from `-List`).
- `llvm/docs/MSIL/GettingStarted.md` leg table: a `more-levels` row; `msil/test/README.md` 'What
  runs' paragraph if it lists the gate legs.
- Cost: about +6-10 min per gate run and per CI job on an idle host (the WSL legs are not repeated;
  a WSL variant `wsl-more-levels` would add another ~10 min and is not needed: the levels change
  the IL, not the runtime).

**Gate step** (as an explicit M11 gate command, in addition to the leg):

```text
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_require_exec=1 --param msil_opt_levels=O1,O3,Os,Oz" msil/test/M1   (6-10 min idle; all 136 M1 tests pass today)
```

(The value has no ':' so no leading blank is needed; a leading blank is harmless.) This step
does not fail today, because nothing is broken at these levels; it is a coverage step. The
leg `pwsh -NoProfile -File D:/llvm-msil/msil/utils/ci/lit-legs.ps1 -Leg more-levels` fails today
with "unknown leg 'more-levels'".

---

## Item 10: -mllvm through clang on an MSIL triple

### The upstream rewrite

In the merge range (git log d1e15b695381..64f50ffd4f1c):

- **d7316136bc3e** "[Support][IR] Set library options per LLVMContext (#231407)", 2026-10-14:
  `OptionsStruct<..., perContext = true>`; `clang -cc1 parses -mllvm into its CompilerInstance and
  assigns the arguments to the code generation LLVMContext at each input`.
  `CompilerInstance::parseLLVMArgs` now calls `llvm::cl::parseCommandLineOptions(LibraryOptions,
  ...)` (`clang/lib/Frontend/CompilerInstance.cpp:1146`, was `llvm::cl::ParseCommandLineOptions`),
  `CompilerInstance` gains `llvm::cl::LibraryOptionsContext LibraryOptions`, and
  `CodeGenAction::BeginSourceFileAction` does `VMContext->getLibraryOptions().assign(CI.getLibraryOptions())`
  (`clang/lib/CodeGen/CodeGenAction.cpp:357`). Upstream test: `clang/test/CodeGen/mllvm-library-options.ll`.
- **bc8218b282ea** "[Option] Make OptionsStruct per-context by default (#232170)", 2026-10-14:
  adds `MCTargetOptions::LibraryOptions`, which clang sets in
  `EmitAssemblyHelper::CreateTargetMachine` (`clang/lib/CodeGen/BackendUtil.cpp:613-614`:
  `Options.MCOptions.LibraryOptions = &TheModule->getContext().getLibraryOptions();`) for every
  target, MSIL included. `TargetPassConfig` reads its options through it
  (`llvm/lib/CodeGen/TargetPassConfig.cpp:61-64`, `getOpts(TM)`), and at HEAD
  `llvm/lib/CodeGen/CodeGenOptions.td:11` is `def CodeGenOptions : OptionsStruct;`
  (per-context): `-stop-after`, `-start-before`, `-verify-machineinstrs`, `-print-after-isel`,
  `-disable-cgp`, `-function-splitting`, `-disable-branch-fold`, ... are per-context now.
- **372a4c359910** (Passes, Scalar, Instrumentation, CGData per-context; `PipelineTuningOptions`
  carries the library options, `BackendUtil.cpp:916`), **068c1afed04f** (cc1as).
- Plain `cl::opt`s are still global: the MSIL backend has exactly one,
  `-msil-assembly-name` (`llvm/lib/Target/MSIL/MSILAsmPrinter.cpp:93-97`, decision D28; the fork
  adds no other cl::opt: `git diff d1e15b695381 2c51a5701ac6 -- llvm/lib llvm/include clang/lib
  clang/include ':!llvm/lib/Target/MSIL' | grep '^+.*cl::opt'` is empty), and `-print-after`,
  `-print-after-all` (`llvm/lib/IR/PrintPasses.cpp:40-46`), `-debug-pass`.
- No MSIL test passes `-mllvm` (`grep -rn mllvm clang/test/CodeGen/MSIL clang/test/Driver/msil-toolchain.c
  clang/test/Preprocessor/init-msil.c llvm/test/CodeGen/MSIL msil/test` is empty);
  `llvm/test/CodeGen/MSIL/assembly-name.ll` covers `-msil-assembly-name` through llc only.

### Probes (all with D:/llvm-msil/build/bin/clang.exe, `$S\item10`)

```text
clang -cc1 -triple x86_64-unknown-linux-gnu-msil -S -O2 -mllvm -msil-assembly-name=foo m.c -o -   -> .assembly 'foo' { .ver 0:0:0:0 }
clang -cc1 -triple x86_64-unknown-linux-gnu-msil -S -O2 m.c -o -                                  -> .assembly 'm' { .ver 0:0:0:0 }
clang --target=x86_64-pc-windows-msvc-msil -S -O2 -mllvm -msil-assembly-name=foo m.c -o -         -> .assembly 'foo'
clang --target=aarch64-unknown-linux-gnu-msil -S -O2 -mllvm -msil-assembly-name=bar m.c -o -      -> .assembly 'bar'
clang --target=x86_64-pc-windows-msvc-msil -S -mllvm -msil-assembly-name=foo m.c -o - -###
   -> "-cc1" ... "-triple" "x86_64-pc-windows-msvc19.33.0-msil" ... "-mllvm" "-msil-assembly-name=foo"
clang ... -mllvm -msil-assembly-name=a -mllvm -msil-assembly-name=b    -> exit 0, .assembly 'b' (last wins; llc behaves the same)
clang ... -S -mllvm -msil-assembly-name=z m.c n.c   (two inputs, in-process cc1)  -> m.s and n.s both .assembly 'z'
clang ... -fno-integrated-cc1 -S -mllvm -msil-assembly-name=q m.c   -> .assembly 'q'
clang ... -S -mllvm -msil-no-such-option m.c   -> "clang (LLVM option parsing): Unknown command line argument
   '-msil-no-such-option'.", exit 1
clang -cc1 ... -mllvm -print-after-all   -> "*** IR Dump After MSIL Prepare IR (msil-prepare-ir) ***", "... MSIL Fix
   Function Bitcasts", "... MSIL Optimize Returned", and "# *** IR Dump After MSIL <pass> (msil-...) ***:" for
   isel, argument-move, set-mem-flags, clean-code-after-trap, nullify-dbg-value-lists, replace-phys-regs,
   optimize-live-intervals, reg-stackify, reg-coloring, explicit-locals, reg-numbering, stack-check, asm-printer
clang -cc1 ... -mllvm -print-after=msil-reg-stackify   -> "# *** IR Dump After MSIL Register Stackify (msil-reg-stackify) ***:" x2 (f, main)
clang -cc1 ... -mllvm -debug-pass=Structure   -> the legacy codegen pipeline with every MSIL pass
   (MSIL Prepare IR ... MSIL Instruction Selection, MSIL Argument Move, ... MSIL Stack Check, MSIL Assembly Printer)
clang -cc1 ... -mllvm -print-after-isel  (per-context CodeGenOptions)  -> "# After Instruction Selection:",
   "# Machine code for function f: IsSSA, TracksLiveness", "%0:i32 = ARGUMENT_I32 1, implicit $arguments", ...
clang -cc1 ... -mllvm -verify-machineinstrs -mllvm -debug-pass=Structure   -> 42 "Verify generated machine code" lines (0 without the option)
clang --target=... -mllvm -disable-cgp -mllvm -debug-pass=Structure          -> 0 "CodeGen Prepare" lines (1 without)
clang -cc1 -triple x86_64-pc-windows-msvc-msil -O1 -S (InstCombine, per-context) on
   'void g(int c,int a,int b){int d=a/b; if(c) use(d);}':
   default:                           brfalse @BB2_2; div; call void 'use'(int32)      (sdiv sunk)
   -mllvm -instcombine-code-sinking=0: div; brfalse @BB2_2; call void 'use'(int32)      (sdiv stays in entry)
```

Conclusion: both kinds of option reach the MSIL backend from `-cc1` and the driver, on x86_64 and
aarch64 MSIL triples; nothing is broken. (Side observation for item 5, not this recon's: the
`-debug-pass=Structure` list shows `-lowerinvoke -unreachableblockelim` twice, before and after
`-codegenprepare`.)

### Recommended fix (test only)

**New `clang/test/CodeGen/MSIL/mllvm.c`** (prototype `legs-proto/mllvm.c`, PASSES in the private
clang suite, 28-36 s under load). `// REQUIRES: msil-registered-target`. It covers:

1. the global MSIL option through `-cc1` and the driver: `DEFAULT: {{^}}.assembly 'mllvm' {
   .ver 0:0:0:0 }{{$}}` without it; `FOO-NOT: .assembly 'mllvm'` / `FOO: {{^}}.assembly 'foo' {
   .ver 0:0:0:0 }{{$}}` with `-mllvm -msil-assembly-name=foo` for `%clang_cc1 -triple
   x86_64-unknown-linux-gnu-msil`, `%clang --target=x86_64-pc-windows-msvc-msil`, `%clang
   --target=aarch64-unknown-linux-gnu-msil`, and with two occurrences (`=a` then `=foo`, last
   wins);
2. the driver forwards it: `-###` and `DRIVER: "-cc1" {{.*}}"-triple"
   "x86_64-pc-windows-msvc{{[0-9.]*}}-msil"` / `DRIVER-SAME: "-mllvm" "-msil-assembly-name=foo"`
   (the driver normalizes the triple to `x86_64-pc-windows-msvc19.33.0-msil`; a literal
   `"x86_64-pc-windows-msvc-msil"` does not match, the first prototype failed on exactly that);
3. an unknown option fails: `not %clang_cc1 ... -mllvm -msil-no-such-option` and `UNKNOWN: Unknown
   command line argument '-msil-no-such-option'`;
4. global pass-manager options see the MSIL passes: `-mllvm -print-after=msil-reg-stackify`
   (`PRINT: # *** IR Dump After MSIL Register Stackify (msil-reg-stackify) ***:` then `PRINT: #
   Machine code for function f:`) and `-mllvm -debug-pass=Structure` (`STRUCT:` CodeGen Prepare,
   MSIL Instruction Selection, MSIL Register Stackify, MSIL Assembly Printer, in order);
5. per-context CodeGen options reach the MSIL TargetPassConfig: `-mllvm -print-after-isel`
   (`ISEL: # After Instruction Selection:` / `ISEL-NEXT: # Machine code for function f: IsSSA,
   TracksLiveness` / `ISEL: ARGUMENT_I32`), `-mllvm -disable-cgp -mllvm -debug-pass=Structure`
   through the driver (`NOCGP-NOT: CodeGen Prepare` around `NOCGP: MSIL Instruction Selection`)
   and `-mllvm -verify-machineinstrs -mllvm -debug-pass=Structure` (`VERIFY: MSIL Register
   Stackify` / `VERIFY-NEXT: Verify generated machine code`);
6. a per-context IR-library option changes the IL: `-O1` with and without `-mllvm
   -instcombine-code-sinking=0` on `void g(int c, int a, int b) { int d = a / b; if (c) use(d); }`
   with `use` defined (`volatile int sink; __attribute__((noinline)) void use(int x) { sink = x;
   }`; an undefined `use` is an MSIL error 'not a Phase-1 import'): `SINK: brfalse` before `SINK:
   div`, `NOSINK: div` before `NOSINK: brfalse`.

Every check was confirmed to discriminate (counts above without the option). `-stop-after` /
`-stop-before` are deliberately left out (item 4 is about them; `-stop-after=msil-isel` aborts).
`-stackmap-version` was not used: MSIL emits no stack maps, so it would show nothing.

No `llvm/test` change is needed (`assembly-name.ll` covers llc). Optional: the driver part could
live in `clang/test/Driver/msil-toolchain.c` instead; keeping it in one CodeGen test is simpler.

**Gate step** (fails today because the file does not exist; about 15-20 s idle):

```text
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv" clang/test/CodeGen/MSIL/mllvm.c
```

The file is inside the net8 leg's `clang/test/CodeGen/MSIL`, so run-m1-gate.ps1 and CI run it
with no script change.

---

## Summary for the drafter

| Item | Fix | Files | New gate step | Runtime |
|---|---|---|---|---|
| 1 | test only | new `clang/test/CodeGen/MSIL/aarch64-aggregate-abi.c`, new `llvm/test/CodeGen/MSIL/struct-args-aapcs64.ll`; optional `msil/test/lit.cfg.py` `msil_pe_machine` + README row | msil-lit.ps1 on the two files | ~30-40 s |
| 2 | 2-line pin bump + pin checker | `.github/workflows/msil-ci.yml` (lines 179, 289), new `msil/utils/ci/check-action-pins.py` | `python msil/utils/ci/check-action-pins.py D:/llvm-msil` | ~1 s |
| 8 | new `utc` leg | `msil/utils/ci/lit-legs.ps1`, `msil/utils/ci/lit-legs.sh`, comments in `msil/utils/run-m1-gate.ps1` and msil-ci.yml, `llvm/docs/MSIL/GettingStarted.md` | run-utc-tests.py (6 tests) and `lit-legs.ps1 -Leg utc` | 6-10 s |
| 9 | new default `more-levels` leg | `msil/utils/ci/lit-legs.ps1`, `msil/utils/ci/lit-legs.sh`, comments, GettingStarted.md | msil-lit.ps1 msil/test/M1 at O1,O3,Os,Oz | 6-10 min idle |
| 10 | test only | new `clang/test/CodeGen/MSIL/mllvm.c` | msil-lit.ps1 on the file | ~15-20 s |

Ownership: `lit-legs.ps1` and `lit-legs.sh` are touched by items 8 and 9 and must be owned by one
WP (an infra WP 'CI legs: utc and more-levels'); `msil-ci.yml` (item 2 and comments of 8/9) by
one WP; the two clang tests and the llc test can be one test WP (independent author: it needs no
implementation file, only clang/llc output). The disk use of all of this is negligible (no build;
lit outputs of the more-levels leg in `build/tools/msil/test/M1/**/Output` grow by about 2x the
current O0/O2 outputs).
