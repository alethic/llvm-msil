# MSIL recon: test infrastructure and CI (`tests-ci`)

- Key: `tests-ci`. Date: 2026-10-12. Checkout: `msil-milestone1` @ `8da03434abcc` (llvm-project, LLVM 23 era; every path and line number below was read in this tree).
- Scope: how MSIL tests are gated and laid out (lit, unit, clang), where end-to-end execution tests live and how they run (C to `.il` to ilasm to dotnet, compared with expected output), the milestone ladder with its test gates, GitHub Actions for the fork, and the fast local loop.
- Method: read the lit, CMake and CI sources; then **built and ran a working prototype of the execution harness** on this host (Windows 11, .NET Framework 4.8 ilasm, .NET 8.0.31 and 10.0.12, ILVerify 10.0.12, PEVerify) and in WSL Ubuntu 26.04 (.NET 10.0.12). Since no MSIL backend exists yet, a stand-in "clang/llc" emitted hand-written IL. Everything else in the pipeline ran for real: lit, ilasm, ilverify, PEVerify, dotnet, .NET Framework, and a native oracle built with the stock VS clang 22.1.3 and link.exe.
- Prototype: `C:\Users\JEROME~1\AppData\Local\Temp\claude\D--llvm-msil\fa776849-ce33-4cee-bcc7-05382d063d3e\scratchpad\recon\tests-ci\proto\` (tree in Appendix A, which reproduces every file in full). Raw experiments are one level up in `...\recon\tests-ci\` (`e1_hello.il` … `e4_buf.il`, `wsl_probe.sh`).
- "Verified" means run or read here. "(unverified)" marks things I could not check offline: NuGet package versions, GitHub-hosted runner contents, and timings.

---

## 0. Summary of decisions

| Topic | Decision |
|---|---|
| llc/MIR tests | `llvm/test/CodeGen/MSIL/`, gated by a `lit.local.cfg` with the exact WebAssembly idiom (`if "MSIL" not in config.root.targets: config.unsupported = True`). The same file adds an optional `ilasm` feature and a `%ilasm-verify` substitution, so llc tests can also check that their output assembles. This copies NVPTX's `%if ptxas %{ ... \| %ptxas-verify %}`. No upstream lit file changes. |
| Unit tests | `llvm/unittests/Target/MSIL/` (picked up automatically for any built target), new `TripleTest` cases, and the `"msil"` exemption in `llvm/unittests/MC/TargetRegistry.cpp:35`. New gtest names contain `MSIL`, so `--filter=MSIL` selects them. |
| clang tests | As in clang.md §6. Frontend tests have **no** `REQUIRES`. Anything that runs the backend has `REQUIRES: msil-registered-target`, which clang's lit config derives from `llvm-config --targets-built`. |
| Execution tests | A new top-level project **`msil/`** (suite in `msil/test/`) wired in as an **LLVM external project**: `-DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil`. That needs **zero upstream CMake edits** and follows the HLSL OffloadTest precedent. Tool discovery happens in its own `lit.cfg.py`. Without the MSIL target, clang, ilasm or a .NET runtime, every execution test is UNSUPPORTED, so `check-all` never breaks. |
| Per-test pipeline | One RUN line, `// RUN: %{msil_c}` (C) or `; RUN: %{msil_ll}` (IR through llc). Each runs compile, `ilasm -dll`, `msil-verify` (ILVerify plus PEVerify on Windows), and `dotnet exec`, then compares a transcript with `<stem>.expected`. It does this for every level in `--param msil_opt_levels` (default `O0,O2`). Optional legs, guarded by `%if`, add a native-oracle build and a .NET Framework run. |
| Expected output | Checked-in transcripts: stdout with LF line endings plus a final `[exit N]` (or `[unhandled exception: T]`). They are **generated from a native build** by `msil/utils/update-expected.py` and re-checked against native in CI (`--param msil_native_oracle=required`). The checked-in file is the contract; the native build is the oracle that keeps it honest. |
| Triples for execution | Windows: `x86_64-pc-windows-msvc-msil` (puts from `ucrtbase`, verified). Linux: `x86_64-unknown-linux-gnu-msil` (`DllImport("libc")` verified on glibc 2.43 / .NET 10). Override with `--param msil_triple=`. |
| CI | `.github/workflows/msil-ci.yml` with a Linux job (setup-dotnet 8+10, ilasm from NuGet `runtime.linux-x64.Microsoft.NETCore.ILAsm`, ILVerify 10.0.12) and a Windows job (MSVC env, .NET Framework ilasm, PEVerify, .NET Framework run, native oracle). Both use sccache, `-C msil/cmake/caches/MSIL.cmake`, and `ninja check-msil`. Nightly adds all opt levels plus `check-llvm check-clang`. The upstream workflows are **disabled through repository settings** (`gh workflow disable`, script provided), not by deleting files, so merges stay conflict-free. |
| Gating | A ladder of 15 gates (M1.0 to M1.14, section 8). Exit-code-only tests come first, then output through P/Invoke. Tests are written **XFAIL-first** by a test agent, and the implementing agent removes the `XFAIL`. A reviewer agent runs a fixed checklist. |
| Fast loop | From a VS x64 prompt: `ninja -C build llc` and `python build\bin\llvm-lit.py -sv llvm\test\CodeGen\MSIL`. Then `ninja -C build clang` and `python build\bin\llvm-lit.py -sv msil\test\M1\<stage>`. Section 10. |

Findings that other components must act on (details in §3.3, §6.6 and §12):
1. **ILAsm keywords collide with C identifiers.** ilasm rejects a method named `add`, `ret`, `value` or `int32`, a parameter named `off`, and an assembly named `throw`. Quoting (`'add'`) fixes all of them, and quoted MSVC-mangled names such as `'?foo@@YAHXZ'` work. **The AsmPrinter must single-quote every identifier it prints.** (Verified, §11 E7.)
2. **ILVerify 10.0.12 cannot verify `calli`.** It prints `Error []: ... ImportCalli not implemented`, cannot ignore that message, and skips the rest of the method. It also **does not flag `native int + int64`**, which PEVerify does flag ("Unexpected type on the stack"). Windows CI therefore runs PEVerify too. (Verified, E6.)
3. **`update_llc_test_checks.py` picks the x86 scrubber for `x86_64-...-msil`** (`asm.py:562-631` matches on triple prefix), so it cannot generate MSIL checks until it gets the patch in §3.4.
4. This working copy has `core.autocrlf=true`, so the working tree is CRLF (`Design.md` is CRLF). See §12, risk 1.

---

## 1. What exists in this checkout (facts the design builds on)

### 1.1 Per-target lit gating
- Every `lit.local.cfg` under `llvm/test/**/WebAssembly/` is the two-liner `if not "WebAssembly" in config.root.targets: config.unsupported = True`. `llvm/test/MC/WebAssembly/lit.local.cfg` spells it `if "WebAssembly" not in config.root.targets:`. `llvm/test/tools/llvm-dwp/WebAssembly/lit.local.cfg` also sets `config.suffixes = [".s"]`.
- `config.root.targets` comes from `llvm/test/lit.cfg.py:496` (`config.targets = frozenset(config.targets_to_build.split())`). Lines 498-499 add `<arch>-registered-target` features. `targets_to_build` is `@TARGETS_TO_BUILD@` (`llvm/test/lit.site.cfg.py.in:30`), which `configure_lit_site_cfg` builds from `LLVM_TARGETS_TO_BUILD` (`llvm/cmake/modules/AddLLVM.cmake:2225-2228`). After `llvm/CMakeLists.txt:736-739` that list already includes `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`, so an experimental `MSIL` target **is** in `config.root.targets`, and the feature `msil-registered-target` exists.
- clang: `clang/test/lit.cfg.py:403-416` builds the same `<arch>-registered-target` features from `llvm-config --targets-built`. `clang/test/CodeGen/WebAssembly/` has **no** `lit.local.cfg`. Frontend tests there (`wasm-main.c`, `wasm-varargs.c`) have no REQUIRES. Tests that invoke the backend use a per-file `REQUIRES: webassembly-registered-target` (`wasm-eh.ll:1`).
- lld (phase 2): `lld/test/lit.cfg.py:75-91` maps `--targets-built` names to short features (`"WebAssembly": "wasm"`), and `lld/test/wasm/lit.local.cfg` checks `"wasm" in config.available_features`. An `lld/test/MSIL` suite would need `"MSIL": "msil"` added there, a one-line hook.

### 1.2 Check targets come for free, but with heavy dependencies
- `llvm/test/CMakeLists.txt:290-295`: `add_lit_testsuites(LLVM ... DEPENDS ${LLVM_TEST_DEPENDS})` creates `check-llvm-codegen-msil` automatically. It depends on the whole `LLVM_TEST_DEPENDS` list (87 entries, lines 75-164), which is fine for CI and slow for an edit-test loop.
- `clang/test/CMakeLists.txt:245` does the same for `check-clang-codegen-msil` (with `CLANG_TEST_DEPS`). Lines 251-283 show the upstream precedent for a **lean** check target (`check-clang-cir`) that lists only the tools its tests use. `check-msil` (§6.2) copies that pattern.
- `add_lit_testsuites` (`AddLLVM.cmake:2498-2560`) makes one `check-<project>-<dir>` per sub-directory and skips anything under `Inputs`.

### 1.3 Unit-test plumbing
- `llvm/unittests/Target/CMakeLists.txt:1-5` runs `add_subdirectory` on `llvm/unittests/Target/<T>` for every `<T>` in `LLVM_TARGETS_TO_BUILD`, including experimental targets, so `llvm/unittests/Target/MSIL/` needs no hook. Template: `llvm/unittests/Target/WebAssembly/CMakeLists.txt` with `add_llvm_target_unittest(...)` (`llvm/unittests/CMakeLists.txt:13-15`, which links the static libraries).
- `llvm/unittests/MC/TargetRegistry.cpp:21-43` (`TargetHasArchType`) asserts that every target's name maps to an arch, skipping only `"cpp"` (line 35). MSIL needs `Name != "cpp" && Name != "msil"` (registry-tm.md §1.3).
- `llvm/unittests/TargetParser/TripleTest.cpp`: object formats are tested in `TEST(TripleTest, FileFormat)` (3181-3290, e.g. `Triple("s390x---goff")`, `setObjectFormat` + `getObjectFormatTypeName`), Windows normalization in `NormalizeWindows` (3475-3530), and exception models in `DefaultExceptionHandling` (3316).
- The unit lit suite is `build/test/Unit` (`llvm/test/Unit/lit.cfg.py`, GoogleTest format, exec root `build/unittests`).

### 1.4 lit facts that shape the design (all verified in `llvm/utils/lit`)
- **The external shell is deprecated.** `lit/formats/shtest.py:19-32` raises if `execute_external=True` unless forced, with the message "deprecated as of LLVM-23". RUN lines therefore go through lit's **internal shell on Linux too**: no bash, no `$?`, no subshells. The internal shell supports `&&`, `||`, `;`, pipes and redirections (`TestRunner.py:552-570`), plus the built-ins `cd export echo mkdir rm ulimit umask : cat diff` (`TestRunner.py:584-603`) and `not` (line 642). On Windows, `lit/llvm/config.py:36-60` always uses the internal shell and finds Git's `usr\bin` (observed: "using lit tools: C:\Program Files\Git\usr\bin").
- Conditional RUN text: `%if feature %{ ... %} %else %{ ... %}` (`TestRunner.py:1671-1750`). With `config.recursiveExpansionLimit` set, substitutions expand to a fixed point (`processLineToFixedPoint`, line 1777). `%if` is re-evaluated on every pass, so **a config substitution can contain `%if`**, and nested `%{s:stem}`-style names inside the branch are fine because the branch scanner only looks for `%if` and `%}`. Verified in the prototype.
- `%{s:stem}`, `%{s:basename}` and `%{t:stem}` exist (`TestRunner.py:1372-1374`). `%{readfile:...}` exists (line 219). `DEFINE:`/`REDEFINE:` exist (line 1529).
- Each `lit.local.cfg` gets a `copy.deepcopy` of the parent config (`lit/discovery.py:116`), so a sub-directory can replace a substitution safely.
- The child environment is filtered (`lit/TestingConfig.py:17-90`). On Windows `INCLUDE`, `LIB`, `USERPROFILE` and `HOME` pass through, but not `DOTNET_ROOT`, `ProgramFiles`, `LOCALAPPDATA` or `NUGET_PACKAGES`, so the MSIL config forwards those itself.
- Environment knobs: `LIT_OPTS` (`cl_arguments.py:539`), `LIT_FILTER`/`LIT_FILTER_OUT` (450/457), `LIT_XFAIL` (470). `FindTool("llc")` appends `-verify-machineinstrs` when `LLVM_ENABLE_MACHINE_VERIFIER=1` (`lit/llvm/subst.py:24`), which costs nothing to turn on for MSIL CI.
- `ToolSubst` wraps a key in `\b...\b`, so `%clang` never matches inside `%clang_msil` (`subst.py:91-103`). Raw `config.substitutions` tuples are plain regexes, so their names must not be prefixes of one another.

### 1.5 Precedents for external tools in tests
- **NVPTX/ptxas:** `llvm/test/lit.cfg.py:462-491` adds the `ptxas` feature and `%ptxas-verify` when `LLVM_PTXAS_EXECUTABLE` or the CMake value is set. Tests write `; RUN: %if ptxas %{ llc < %s | %ptxas-verify %}` (`llvm/test/CodeGen/NVPTX/MachineSink-call.ll:2`). That is the model for `%ilasm-verify`.
- **HLSL OffloadTest:** `clang/cmake/caches/HLSL.cmake:24-29` uses an execution suite that is built as `-DLLVM_EXTERNAL_PROJECTS=OffloadTest -DLLVM_EXTERNAL_OFFLOADTEST_SOURCE_DIR=...`. That is the model for `msil/`.
- **cross-project-tests:** added by `llvm/projects/CMakeLists.txt:49-51` when `LLVM_ENABLE_PROJECTS` contains it. Its `lit.cfg.py` probes for dexter, gdb, lldb and the clang DWARF version at every start (lines 113-400). §6.1 explains why `msil/` is a better home.

### 1.6 External-project mechanics (why `msil/` needs no CMake edits)
`llvm/CMakeLists.txt:310-345` turns on every name in `LLVM_EXTERNAL_PROJECTS`. `llvm/tools/CMakeLists.txt:58-67` calls `add_llvm_external_project(<p>)` for each one. Because `llvm/tools/msil` does not exist, `add_llvm_subdirectory` (`AddLLVM.cmake:1913-1966`) uses `LLVM_EXTERNAL_MSIL_SOURCE_DIR` and builds into `build/tools/msil`. `configure_lit_site_cfg` records the main and site config pair (`AddLLVM.cmake:2320-2325`), and `utils/llvm-lit` is configured afterwards (`llvm/CMakeLists.txt:1515` comes after `tools` at 1457). As a result, `llvm-lit msil/test/...` works with **source** paths, like `llvm-lit llvm/test/...`. Documented in `llvm/docs/CMake.md:750-756`.

### 1.7 History
The LLVM 2.x MSIL backend was deleted in `bd24404718a6` (2010-08-28) with the message "It isn't maintained, is buggy, **has no testcases** and hasn't kept up with ToT". The test suite is the main defense against the same outcome (Design.md risk 5).

---

## 2. Test tiers and where each one lives

| Tier | Location | Gate (REQUIRES / unsupported) | Runs | Needs .NET? | Phase |
|---|---|---|---|---|---|
| Triple / registry unit | `llvm/unittests/TargetParser/TripleTest.cpp`, `llvm/unittests/MC/TargetRegistry.cpp` | none (pure C++) | `TargetParserTests`, `MCTests` | no | M1.1-M1.2 |
| Target unit | `llvm/unittests/Target/MSIL/` (`MSILTests`) | exists only when MSIL is built | gtest | no | M1.4+ |
| llc / MIR FileCheck | `llvm/test/CodeGen/MSIL/*.ll, *.mir` | `lit.local.cfg` (`MSIL` in `config.root.targets`); optional `%if ilasm` | `llc`, `FileCheck` | optional (ilasm round trip) | M1.2+ |
| Pass tests | `llvm/test/Transforms/<Pass>/MSIL/` (e.g. `LoopVectorize/MSIL/novector.ll` from registry-tm §8.5) | same `lit.local.cfg` idiom | `opt` | no | M1.5+ |
| clang frontend | `clang/test/Preprocessor/init-msil.c`, `clang/test/Driver/msil-toolchain.c`, `clang/test/CodeGen/MSIL/*.c` | none for `-emit-llvm`/`-E`/`-###`; `REQUIRES: msil-registered-target` for `-S` | `clang -cc1` | no | M1.3 |
| Execution | `msil/test/**` | directory `lit.local.cfg` requires `msil-exec` | clang/llc, ilasm, ILVerify, dotnet | **yes** | M1.4+ |
| MC / object (later) | `llvm/test/MC/MSIL/` | `lit.local.cfg` idiom | `llvm-mc`, `llvm-objdump`/`obj2yaml` | no | phase 2 |
| Linker (later) | `lld/test/MSIL/` | `"msil" in config.available_features` (needs the lld lit hook) | `msil-ld` | optional | phase 2 |
| Conformance (later) | llvm-test-suite / GCC torture, out of tree | own harness | | yes | phase 4 |

---

## 3. Tier: llc / MIR FileCheck tests (`llvm/test/CodeGen/MSIL`)

### 3.1 `llvm/test/CodeGen/MSIL/lit.local.cfg` (new; verified against a stub config, §11 E14)
```python
if "MSIL" not in config.root.targets:
    config.unsupported = True

# Like NVPTX's %ptxas-verify (llvm/test/lit.cfg.py:462-491): when an ilasm is
# available, tests can also check that the llc output assembles:
#   ; RUN: %if ilasm %{ llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -o %t.il && %ilasm-verify -output=%t.dll %t.il %}
# LLVM_ILASM_EXECUTABLE overrides the search; set it to "" to disable.
import glob
import os
import platform
import re

import lit.util


def _version_key(path):
    m = re.search(r"(\d+)\.(\d+)\.(\d+)", path)
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)


def _find_ilasm():
    path = os.environ.get("LLVM_ILASM_EXECUTABLE")
    if path is not None:
        return path if os.path.isfile(path) else None
    path = lit.util.which("ilasm")
    if path:
        return path
    if platform.system() == "Windows":
        path = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Microsoft.NET",
                            "Framework64", "v4.0.30319", "ilasm.exe")
        return path if os.path.isfile(path) else None
    nuget = os.environ.get("NUGET_PACKAGES") or os.path.join(
        os.path.expanduser("~"), ".nuget", "packages")
    hits = glob.glob(os.path.join(nuget, "runtime.*.microsoft.netcore.ilasm", "*",
                                  "runtimes", "*", "native", "ilasm"))
    hits = [h for h in hits if os.access(h, os.X_OK)]
    return max(hits, key=_version_key) if hits else None


_ilasm = _find_ilasm()
if _ilasm:
    config.available_features.add("ilasm")
    config.substitutions.append(("%ilasm-verify", '"%s" -nologo -quiet -dll' % _ilasm))
```
Use the same two-line gate (without the ilasm part) in `llvm/test/Transforms/*/MSIL/lit.local.cfg` and, in phase 2, `llvm/test/MC/MSIL/lit.local.cfg`.

### 3.2 Conventions
- **Always pass `-mtriple`.** Most tests use both `x86_64-unknown-linux-gnu-msil` and `x86_64-pc-windows-msvc-msil` with a shared `CHECK` prefix. Where they differ (P/Invoke library name, `long`), add `LNX`/`WIN` prefixes. The IR must not depend on a `target datalayout` line: let llc compute it (registry-tm §3).
- Add `-verify-machineinstrs` to RUN lines, as WebAssembly tests do. CI also sets `LLVM_ENABLE_MACHINE_VERIFIER=1`.
- When a test produces a complete module, end it with the ilasm round trip: `; RUN: %if ilasm %{ llc ... -o %t.il && %ilasm-verify -output=%t.dll %t.il %}`.
- Error tests: `; RUN: not llc -mtriple=... < %s 2>&1 | FileCheck %s --check-prefix=ERR`. Use them for everything M1 rejects: `-filetype=obj`, variadic definitions, TLS, module `asm`, `llvm.global_dtors` (passes-frame §11), vectors if not scalarized.
- MIR tests (`*.mir`) live in the same directory (WebAssembly precedent: `explicit-locals.mir`), use `-run-pass=<msil-pass-name>`, and are listed in passes-frame.md §12.
- Names: kebab-case after the feature (`switch.ll`, `frame-index.ll`). Keep WebAssembly's name when porting a test (`reg-stackify.ll`, `function-bitcasts.ll`, `add-prototypes.ll`).

### 3.3 Example (syntax is a placeholder until the AsmPrinter is designed; quoting per finding 1)
```llvm
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs | FileCheck %s
; RUN: %if ilasm %{ llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -o %t.il && %ilasm-verify -output=%t.dll %t.il %}

; 'add' is an ILAsm keyword: the name must be quoted or ilasm rejects the file.
define i32 @add(i32 %a, i32 %b) {
; CHECK-LABEL: .method {{.*}} int32 'add'(int32{{.*}}, int32{{.*}}) cil managed
; CHECK:         .maxstack 2
; CHECK:         ldarg.0
; CHECK-NEXT:    ldarg.1
; CHECK-NEXT:    add
; CHECK-NEXT:    ret
  %r = add i32 %a, %b
  ret i32 %r
}
```
Add an `identifier-quoting.ll` test for functions, globals and parameters named `add`, `ret`, `value`, `int32`, `off`, `throw`, `call`, plus `?foo@@YAHXZ` (MSVC mangling, which clang.md §7.6 says shows up on Windows) and `a.b$c`. Its main check is that the ilasm round trip succeeds.

### 3.4 Make `update_llc_test_checks.py` work for MSIL (edit to `llvm/utils/UpdateTestChecks/asm.py`)
Today `get_run_handler(triple)` (`asm.py:562-631`) picks the longest matching *prefix*, so `x86_64-…-msil` gets the x86 regex and finds no functions. CHECK-LABELs also need care: a call `call int32 'add'(...)` prints the name exactly as the definition does, so a label of `'add'(` could match a call site in an earlier function. The label must anchor on `.method`. Proposed change, which assumes the AsmPrinter prints each method header on one line and closes the method with `} // end of method '<name>'` (ildasm style; the AsmPrinter agent should confirm):
```python
# asm.py, next to the other ASM_FUNCTION_*_RE
ASM_FUNCTION_MSIL_RE = re.compile(
    r"^[ \t]*\.method[^\n]*?'(?P<func>[^'\n]+)'(?P<func_name_separator>)\([^\n]*\n"
    r"(?P<body>.*?)\n"
    r"^[ \t]*\}[ \t]*// end of method '(?P=func)'",
    flags=(re.M | re.S),
)
# Label that cannot match a call site: "; CHECK-LABEL: .method {{.*}}'add'("
MSIL_CHECK_LABEL_FORMAT = "{} %s-LABEL: .method {{{{.*}}}}%s'%s'%s%s("

def scrub_asm_msil(asm, args):
    asm = common.SCRUB_WHITESPACE_RE.sub(r" ", asm)
    asm = string.expandtabs(asm, 2)
    return common.SCRUB_TRAILING_WHITESPACE_RE.sub(r"", asm)

def get_run_handler(triple):
    # MSIL triples are "<native triple>-msil" (Design.md 3); check before the
    # arch-prefix table, which would otherwise choose x86/aarch64.
    if triple.endswith("-msil"):
        return (scrub_asm_msil, ASM_FUNCTION_MSIL_RE)
    ...
```
Also give `asm.add_checks(...)` an optional `check_label_format` argument (default unchanged), and have `update_llc_test_checks.py` pass `asm.MSIL_CHECK_LABEL_FORMAT.format(";")` for prefixes whose RUN triple ends in `-msil` (record that in the RUN-line loop at lines 40-60). The change is small, isolated and inside `llvm/utils`. Until it lands, tests are hand-written.

---

## 4. Tier: unit tests

1. `llvm/unittests/TargetParser/TripleTest.cpp` (gate M1.1), new `TEST`s whose names include `MSIL`:
   ```cpp
   TEST(TripleTest, MSILObjectFormat) {
     for (const char *N : {"x86_64-unknown-linux-gnu-msil", "aarch64-unknown-linux-gnu-msil",
                           "x86_64-unknown-linux-musl-msil", "x86_64-pc-windows-msvc-msil",
                           "aarch64-pc-windows-msvc-msil", "x86_64-w64-windows-gnu-msil",
                           "aarch64-apple-darwin-msil"}) {
       Triple T(N);
       EXPECT_EQ(Triple::MSIL, T.getObjectFormat()) << N;
       EXPECT_TRUE(T.isOSBinFormatMSIL()) << N;
       EXPECT_EQ(N, T.str());
     }
     Triple T("x86_64-unknown-linux-gnu");
     T.setObjectFormat(Triple::MSIL);
     EXPECT_EQ("msil", Triple::getObjectFormatTypeName(T.getObjectFormat()));
     EXPECT_EQ("x86_64-unknown-linux-gnu-msil", T.str());
   }
   TEST(TripleTest, MSILNormalizeKeepsFormat) {
     // Windows normalization dropped an unknown 5th component (registry-tm 9).
     EXPECT_EQ("x86_64-pc-windows-msvc-msil", Triple::normalize("x86_64-pc-windows-msvc-msil"));
     EXPECT_EQ("x86_64-pc-windows-msvc19.40.0-msil", Triple::normalize("x86_64-pc-windows-msvc19.40.0-msil"));
     EXPECT_EQ("x86_64-pc-windows-msvc-msil", Triple::normalize("x86_64-pc-win32-msil"));
   }
   TEST(TripleTest, MSILDataLayoutMatchesNative) {   // registry-tm 8.1
     for (StringRef N : {"x86_64-unknown-linux-gnu", "x86_64-pc-windows-msvc",
                         "aarch64-unknown-linux-gnu", "aarch64-apple-darwin"})
       EXPECT_EQ(Triple(N).computeDataLayout(),
                 Triple((N + "-msil").str()).computeDataLayout()) << N;
     EXPECT_EQ(ExceptionHandling::None,
               Triple("x86_64-unknown-linux-gnu-msil").getDefaultExceptionHandling());
   }
   ```
   (The API names follow registry-tm.md. Line numbers to insert after: `FileFormat` ends at 3290 and `NormalizeWindows` at 3530.)
2. `llvm/unittests/MC/TargetRegistry.cpp:35`: `if (Name != "cpp" && Name != "msil")`, plus `TEST(TargetRegistry, MSILLookupByObjectFormat)` (registry-tm §8.2). Guard it with `#ifdef LLVM_HAS_MSIL_TARGET`, or look the target up and `GTEST_SKIP()` when it is not registered.
3. `llvm/unittests/Target/MSIL/CMakeLists.txt` (gate M1.4):
   ```cmake
   include_directories(${PROJECT_SOURCE_DIR}/lib/Target/MSIL ${PROJECT_BINARY_DIR}/lib/Target/MSIL)
   set(LLVM_LINK_COMPONENTS CodeGen CodeGenTypes Core MC MIRParser TargetParser
       MSILCodeGen MSILDesc MSILInfo)
   add_llvm_target_unittest(MSILTests
     MSILNameQuotingTest.cpp     # every ILAsm keyword round-trips quoted; mangled names
     MSILStackEffectTest.cpp     # per-opcode pop/push table vs StackPeakExtra (passes-frame 8)
     MSILSignatureTest.cpp       # MVT/EVT -> int32/int64/float32/float64/native int
     )
   ```
   Use `WebAssemblyExceptionInfoTest.cpp:24-41` as the template for creating a TargetMachine.

---

## 5. Tier: clang tests

Write the files exactly as in clang.md §6.1-6.5: `Preprocessor/init-msil.c`, `Driver/msil-toolchain.c`, `CodeGen/MSIL/{long-double,inline-asm,ms-inline-asm,system-headers,triples}.c`, plus `CodeGen/MSIL/datalayout.c` from registry-tm §8.4. Rules:
- `-E`, `-###`, `-emit-llvm` and `-fsyntax-only` tests have **no** REQUIRES: they never build a TargetMachine.
- Anything that runs `-S` through the backend has `// REQUIRES: msil-registered-target`.
- `clang/test/CodeGen/MSIL/` needs **no** `lit.local.cfg`. That follows WebAssembly, and it keeps the frontend tests running in builds without the MSIL backend.

---

## 6. Tier: execution tests (`msil/`)

### 6.1 Where they live, and why
| Option | For | Against |
|---|---|---|
| `clang/test/...` with REQUIRES | No new wiring | clang tests are compile-only by convention. The dotnet/ilasm detection would go into clang's `lit.cfg.py` (an upstream file), and each run would pull in `CLANG_TEST_DEPS`. |
| `cross-project-tests/msil/` | Existing project that already finds clang and lld | Needs `LLVM_ENABLE_PROJECTS=cross-project-tests` and an edit to `cross-project-tests/CMakeLists.txt` (upstream). Its `lit.cfg.py` probes dexter, gdb, lldb and the clang DWARF version on every run. |
| **Top-level `msil/` as an LLVM external project** (chosen) | **Zero upstream edits** (§1.6). Own lean `lit.cfg.py` and site config. Upstream precedent (HLSL OffloadTest). Room for the runtime assembly (`MsilC.Runtime`, Design 6) and later tools. Granular `check-msil-*` targets. | Two extra CMake variables; the cache file below sets them. |

Layout:
```
msil/
  .gitattributes                # "* text=auto eol=lf": expected files must stay LF
  CMakeLists.txt
  cmake/caches/MSIL.cmake       # cmake -C: projects, targets, external project, flags
  test/
    CMakeLists.txt  lit.cfg.py  lit.site.cfg.py.in  README.md
    Inputs/msil_test.h          # shared freestanding helpers (puts-based output)
    harness/                    # self-tests of the harness; need ilasm+dotnet, not clang
    M1/                         # milestone-1 programs, one sub-directory per gate (section 7)
    M2/  M3/                    # later phases (section 7.4)
  utils/
    msil-run.py                 # run a .dll/.exe/native exe, emit transcript, compare
    msil-verify.py              # ILVerify (+ PEVerify) with an allow list
    ilverify-allowed.txt
    update-expected.py          # generate or check .expected from a native build
    tools/msil-tools.proj       # NuGet download of ilasm/ildasm for Linux and macOS
```

### 6.2 Wiring and check targets
- Configure with `cmake -G Ninja -S llvm -B build -C msil/cmake/caches/MSIL.cmake` (Appendix A.3), or with the explicit `-DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil`.
- `msil/test/CMakeLists.txt` (Appendix A.2) defines:
  - `check-msil-exec`: this suite. It is part of `check-all` and UNSUPPORTED without .NET.
  - `check-msil-m1`, `check-msil-m1-00-smoke`, … : one target per directory, from `add_lit_testsuites(MSIL ...)`.
  - **`check-msil`**: the umbrella that CI and developers run. A single lit invocation over `llvm/test/CodeGen/MSIL`, `clang/test/CodeGen/MSIL`, `clang/test/Preprocessor/init-msil.c`, `clang/test/Driver/msil-toolchain.c` and the execution suite. Its dependencies are lean: `clang llc FileCheck not count split-file llvm-config llvm-mc`.
- A missing tool in clang's or llvm's lit config only produces a warning (they use `unresolved="warn"`; checked `clang/test/lit.cfg.py:110-136`). The lean list still has to include `llvm-config`, because both configs run `llvm-config` through `feature_config` when they load.

### 6.3 Detection, features, parameters (from `msil/test/lit.cfg.py`, Appendix A.4)

Tool search order: `--param msil_<tool>=PATH` > `$MSIL_<TOOL>` > CMake `find_program` value > `PATH` > well-known places. **An empty override disables the tool** (`--param msil_ilverify=`).

| Tool | Well-known places searched | Feature |
|---|---|---|
| ilasm | Windows: `%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe` (verified present). All hosts: `$NUGET_PACKAGES` or `~/.nuget/packages/runtime.<rid>.microsoft.netcore.ilasm/<ver>/runtimes/<rid>/native/ilasm[.exe]`, newest version first, executable bit required | `msil-ilasm` |
| dotnet | `$DOTNET_ROOT`, `C:\Program Files\dotnet`, `/usr/share/dotnet`, `/usr/lib/dotnet` (Ubuntu packages, verified in WSL), `/usr/local/share/dotnet`, `~/.dotnet`. It then parses `dotnet --list-runtimes` into `dotnet-runtime-<major>` features and writes `msil-net<major>.runtimeconfig.json` into the exec root. | `msil-dotnet`, `dotnet-runtime-8`, `dotnet-runtime-10`, … |
| ilverify | `~/.dotnet/tools/ilverify[.exe]`. It also runs `--version`, because dotnet-ilverify 10.x is a net10.0 app and cannot start without .NET 10. | `msil-ilverify` |
| PEVerify (Windows) | `%ProgramFiles(x86)%\Microsoft SDKs\Windows\v*\bin\NETFX 4.* Tools\x64\PEVerify.exe` (verified present) | `msil-peverify` |
| .NET Framework (Windows) | `Framework64\v4.0.30319\clr.dll` | `msil-netfx`; `msil-run-netfx` with `--param msil_run_netfx=1` |
| native oracle | `--param msil_native_oracle=1` (or `required`, which is fatal if it cannot run): clang is built, the host backend (X86/AArch64) is built, and on Windows `LIB` is set by a VS developer environment | `msil-native-oracle` |
| everything needed to compile and run C | clang + `msil-registered-target` + `msil-ilasm` + `msil-dotnet` | **`msil-exec`** |

Other features: `msil-registered-target` and `<t>-registered-target` (from `targets_to_build`), `msil-triple=<triple>`, `msil-triple-windows`/`msil-triple-linux`, and the standard `system-windows`/`system-linux` (`lit/llvm/config.py:103-121`).

lit parameters:

| `--param` | Default | Meaning |
|---|---|---|
| `msil_triple` | host-derived: Windows `x86_64-pc-windows-msvc-msil`, Linux `x86_64-unknown-linux-gnu-msil` (or `aarch64-…`), macOS `aarch64-apple-darwin-msil` | triple used by `%clang_msil` and `%llc_msil` |
| `msil_opt_levels` | `O0,O2` | levels that `%{msil_c}`/`%{msil_ll}` run (nightly: `O0,O1,O2,O3,Os,Oz`) |
| `msil_dotnet_runtime` | `8` if installed (Design 7.1 default profile), else the newest | which Microsoft.NETCore.App major to run on |
| `msil_dotnet_env` | empty | `K=V;K=V` for the program, e.g. `DOTNET_TieredCompilation=0` (full-opt JIT) or `DOTNET_TieredPGO=0` |
| `msil_native_oracle` | `0` | `1` = also build natively and compare when possible; `required` = config error if not possible (CI) |
| `msil_run_netfx` | `0` | Windows: also `ilasm -exe` and run on .NET Framework 4.8 |
| `msil_ilasm`, `msil_dotnet`, `msil_ilverify`, `msil_peverify` | auto | tool path override; empty = disabled |

Substitutions:

| Name | Expands to |
|---|---|
| `%msil_triple` | the triple |
| `%clang_msil` | `"<build>/bin/clang" --target=<msil triple>` |
| `%llc_msil` | `"<build>/bin/llc" -mtriple=<msil triple>` |
| `%clang_native` | `"<build>/bin/clang" --target=<msil triple minus -msil>` |
| `%ilasm` | `"<ilasm>" -nologo -quiet` (both Framework and CoreCLR ilasm take `-dll`/`-exe`/`-output=`; the CoreCLR spelling is **unverified on Linux**) |
| `%dotnet` | `"<dotnet>"` |
| `%msil_exec` | `python msil-run.py --expect-os <triple os> --dotnet D --runtimeconfig C [--env ...]` |
| `%msil_verify` | `python msil-verify.py --ilverify I --runtime-dir R --allow ilverify-allowed.txt [--peverify P]`, or `--skip` when no ILVerify is configured |
| `%msil_inputs` | `msil/test/Inputs` |
| `%{msil_cflags}` | `-ffreestanding -I"msil/test/Inputs"` (milestone 1; directories may replace it, §7.4) |
| `%{msil_c}` / `%{msil_ll}` | the composite pipelines in §6.4 |

### 6.4 The per-test RUN pipeline
A C program test is one line. The `.expected` file next to it is the oracle:
```c
// Recursion, a loop, and output through puts. Exit code fib(10) = 55.
// RUN: %{msil_c}
#include "msil_test.h"
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main(void) {
  for (int i = 0; i < 10; ++i)
    put_u64((unsigned)fib(i));
  return fib(10) & 0xff;
}
```
`fib.expected`:
```
0
1
1
2
3
5
8
13
21
34
[exit 55]
```
For each level `L` in `msil_opt_levels`, `%{msil_c}` expands to (whitespace added):
```
%clang_msil %{msil_cflags} -L -S %s -o %t.L.il
&& %ilasm -dll -output=%t.L.dll %t.L.il
&& %msil_verify %t.L.dll
&& %msil_exec --expect %S/%{s:stem} --out %t.L.out %t.L.dll
   %if msil-native-oracle %{ && %clang_native %{msil_cflags} -L %s -o %t.L.native.exe
                             && %msil_exec --native --expect %S/%{s:stem} --out %t.L.native.out %t.L.native.exe %}
   %if msil-run-netfx %{ && %ilasm -exe -output=%t.L.exe %t.L.il
                         && %msil_exec --netfx --expect %S/%{s:stem} --out %t.L.netfx.out %t.L.exe %}
```
`%{msil_ll}` is the same with `%llc_msil -L %s -o %t.L.il` as the first step, and native `%clang_native -Wno-override-module -L %s`. **Use `.ll` tests for the early gates:** llc runs no IPO, so a `noinline` callee executes even at `-O2`. A C test at `-O2` folds `add(2, 3)` to a constant (§8, note 2).
```llvm
; Arithmetic on values the optimizer cannot see: llc does no IPO, so the
; noinline callee really executes even at -O2. Exit code 40 + 2 = 42.
; RUN: %{msil_ll}
define internal i32 @add(i32 %a, i32 %b) noinline {
  %r = add i32 %a, %b
  ret i32 %r
}
define i32 @main() {
  %r = call i32 @add(i32 40, i32 2)
  ret i32 %r
}
```
Special cases use the atomic substitutions. Examples: a trap test whose native behavior differs, so it has no oracle leg; or a test that passes arguments or stdin through `%msil_exec --stdin F prog.dll a b`:
```
// RUN: %clang_msil %{msil_cflags} -O2 -S %s -o %t.il
// RUN: %ilasm -dll -output=%t.dll %t.il
// RUN: %msil_verify %t.dll
// RUN: %msil_exec --expect %S/%{s:stem} %t.dll
```
Multi-file tests (phase 2) use `split-file %s %t` with a `.test` suffix. `split-file` is already in the dependency list.

Verified in the prototype (§11 E8): the composite expands and runs all 8 commands per level (16 for `O0,O2`). The `%if` legs work. Failures print a unified diff (E8). With `--param msil_ilasm=`, every test reports UNSUPPORTED on Windows, and also on Linux when no ilasm is present.

### 6.5 Expected outputs: checked-in transcripts, kept honest by a native oracle
- **Transcript format** (`msil-run.py`): stdout with CRLF folded to LF, then a status line. The status is `[exit N]`, `[unhandled exception: System.T]`, `[signal N]`, `[crash 0x…]` or `[timeout]`. If stdout does not end in a newline, `[no newline at end of output]` is added before the status. The same format comes out of all three runners: `dotnet exec`, .NET Framework and native.
- **Why checked-in files and not "always compare with native":** the files keep tests deterministic and fast. They work on machines and CI legs with no native toolchain (macOS later, cross triples). They also make the contract reviewable: an agent changing behavior must change a visible file.
- **Why keep a native oracle:** Design 12 says programs must "match native clang's output". `update-expected.py --clang build/bin/clang <tests>` writes `.expected` from a native `-O0` build of the same source, using the same transcript format. In CI, `--param msil_native_oracle=required` rebuilds every test natively at every opt level and compares it with the same file. That catches stale expected files and UB in tests (native `-O0` and `-O2` disagreeing).
- **Per-OS overrides:** `<stem>.windows.expected` / `<stem>.linux.expected` take precedence. The OS is chosen from the **target triple** (`--expect-os`), because the triple fixes `sizeof(long)`, `wchar_t` and so on. Prefer not to need overrides: M1 tests avoid `long`, `wchar_t`, `long double` and any printing of pointers.
- Verified behaviors the format depends on (E3-E5): UCRT and glibc `puts` output is flushed at process exit through both file and pipe redirection. Windows `puts` writes CRLF, Linux writes LF. Exit codes propagate. An unhandled exception exits with 0xE0434352 on Windows (.NET 8 and .NET Framework 4.8) and with SIGABRT/134 on Linux. The helper reports both as `[unhandled exception: T]` based on stderr ("Unhandled exception." from CoreCLR, "Unhandled Exception:" from .NET Framework).

### 6.6 Verification policy (Design 12: "no stack-shape errors")
- `msil-verify.py` runs `ilverify <dll> -r <runtime dir>/*.dll`. Our IL references only `mscorlib`, and the facade in the runtime directory resolves it (verified). The wrapper parses `[IL]: Error [Code]:` lines and **fails on any code not in `ilverify-allowed.txt`**. The allow list starts as `Unverifiable` (localloc, cpblk, initblk, calli unmanaged), `InitLocals` (no `init` on `.locals`, per passes-frame §6.6), `StackByRef` (ldind/stind through native int) and `UnmanagedPointer`. Codes measured on M1-shaped IL are in §11 E6. The 10.0.12 enum list is in Appendix B.
- ILVerify's `--ignore-error-file` matches codes, but it **cannot** suppress `Error []: ... ImportCalli not implemented`. The wrapper turns that message into a note ("rest of method not verified").
- **On Windows, PEVerify runs as well** (`/nologo /il`). It catches `native int` + `int64` mixing, which ILVerify misses. That is exactly the hazard passes-frame §13.3 flags for `i64` pointers. It only reports the first error per method, and it prints `[MD]` internal-error warnings for global methods, which the wrapper ignores. Allowed PEVerify messages: "Instruction cannot be verified.", "Expected ByRef on the stack.", "initlocals must be set", "Unmanaged pointers are not a verifiable type."
- If no ILVerify is configured, `%msil_verify` becomes a no-op that says so, so the suite still runs. CI always has it.

### 6.7 Windows vs Linux
| Aspect | Windows (dev box, CI `windows-2022`) | Linux (CI `ubuntu-24.04`, WSL) |
|---|---|---|
| Triple for execution | `x86_64-pc-windows-msvc-msil` (LLP64) | `x86_64-unknown-linux-gnu-msil` (LP64) |
| M1 `puts` P/Invoke | `DllImport("ucrtbase")` works (verified). The backend must choose this per triple; `"libc"` does not exist on Windows. | `DllImport("libc")` works through CoreCLR's special case, even with only `libc.so.6` present (verified: glibc 2.43, .NET 10.0.12). `"ucrtbase"` throws `DllNotFoundException` (verified). |
| ilasm | .NET Framework ilasm (always present with .NET Framework 4.x) | NuGet `runtime.linux-x64.Microsoft.NETCore.ILAsm` (version unverified); `chmod +x` after restore |
| Assemblies are portable | An assembly built by Windows ilasm ran unchanged on Linux .NET 10 (verified) | |
| Verifiers | ILVerify + PEVerify | ILVerify only |
| Extra runtime | .NET Framework 4.8 (`msil_run_netfx=1`): M1 IL uses no BCL types, so the same `.il` runs on both runtimes (verified) | none |
| Native oracle | `clang --target=x86_64-pc-windows-msvc` with link.exe, needs the VS environment (`LIB`) (verified with stock clang 22.1.3) | `clang --target=x86_64-unknown-linux-gnu` with the system `ld` and crt (needs `libc6-dev`/`libgcc`; preinstalled on hosted runners, unverified) |
| Output line endings | CRLF (normalized away) | LF |
| Crash / exception status | 0xE0434352 | 134 (SIGABRT) |
| Crash dialogs | `msil-run.py` calls `SetErrorMode(SEM_FAILCRITICALERRORS\|SEM_NOGPFAULTERRORBOX\|SEM_NOOPENFILEERRORBOX)` so WER cannot hang a run | `DOTNET_DbgEnableMiniDump=0` |
| Line endings in the checkout | this working copy has `core.autocrlf=true`, so the tree is CRLF; `msil/.gitattributes` forces LF under `msil/` | LF |

---

## 7. Layout, naming and the milestone-1 program list

### 7.1 Naming rules
- `msil/test/M<phase>/<NN>-<area>/<name>.c|.ll` + `<name>.expected`. `NN` orders directories by dependency, so the lowest-numbered failing directory points at the earliest broken capability.
- The first comment line says what the test covers. Any RUN line other than `%{msil_c}`/`%{msil_ll}` carries a comment explaining why.
- Tests must be valid, UB-free C11 that behaves the same natively at `-O0` and `-O2` (the oracle enforces this). No `long`, no `long double`, no varargs, no libc beyond `puts` in M1, no printing of addresses, no floating-point printing (convert to scaled integers).
- Before P/Invoke exists (gates M1.4-M1.7), tests report only through the exit code. **Self-checking** style: `MSIL_EXPECT(cond)` returns `1 + __LINE__ % 255` on failure. Add this macro to `msil_test.h`:
  `#define MSIL_EXPECT(c) do { if (!(c)) return 1 + (__LINE__ % 255); } while (0)`
- `-O0` and `-O2` always run (the default `msil_opt_levels`). Nightly runs `O0,O1,O2,O3,Os,Oz` plus `DOTNET_TieredCompilation=0`.

### 7.2 Milestone-1 programs (about 60; Design 12's "about 25 programs" is the 10-programs set plus the struct and fnptr tests)
| Dir (gate) | Tests | Output |
|---|---|---|
| `00-smoke` (M1.4) | `return-0.c`, `return-42.c`, `return-call.c` (main calls a static function), `add-args.ll` | exit code |
| `01-arith` (M1.5) | `int32-ops.ll`, `int64-ops.ll`, `narrow-ints.ll` (i8/i16 with signext/zeroext), `fp-ops.ll` (incl. NaN compares), `conversions.ll` (in-range fptosi/uitofp…), `arith.c`, `compare-select.c` | exit code (bitmask of failures) |
| `02-control` (M1.6) | `loops.c`, `break-continue.c`, `switch-dense.c`, `switch-sparse.c`, `switch-i64.ll` (cases above 2^32; passes-frame E2 showed `switch` needs care), `irreducible.c` (goto), `trap.c` (explicit RUN lines, no oracle; expects `[unhandled exception: …]`) | exit code |
| `03-calls` (M1.7) | `many-args.c` (12 mixed int/long long/double args), `recursion.c` (Ackermann), `mutual-recursion.c`, `kr-call.c` (no-prototype call: AddMissingPrototypes / FixFunctionBitcasts), `void-calls.c`, `deep-recursion.c` | exit code |
| `04-output` (M1.8) | `puts-hello.c`, `puts-many.c` (2000 lines: buffering and flush at exit), `print-ints.c` (helpers, including LLONG_MIN and hex) | stdout |
| `05-frames` (M1.9) | `address-taken-local.c`, `local-array.c`, `frame-align.c` (`_Alignas(32)`, passes-frame §6.5), `frames-recursion.c`, `alloca-loop.c` (dynamic alloca; XFAIL or diagnostic until the shadow stack, passes-frame §6.8) | stdout |
| `06-globals` (M1.10) | `global-scalars.c`, `global-array-sum.c`, `bss-zero.c`, `const-tables.c`, `string-table.c` (`const char *names[]`: relocations), `reloc-global-ptrs.c`, `static-locals.c`, `volatile-inputs.c` | stdout |
| `07-memory` (M1.11) | `memcpy-memset.c` (struct copy and big initializers lowered to `llvm.memcpy`/`llvm.memset`, then cpblk/initblk), `packed-unaligned.c`, `pointer-arith.c`, `bit-builtins.c` (bswap, popcount, clz, ctz, rotates) | stdout |
| `08-fnptr` (M1.12) | `fnptr-call.c`, `dispatch-table.c` (global table of function pointers), `fnptr-in-struct.c`, `qsort-callback.c` (own qsort with a comparator) | stdout |
| `09-structs` (M1.13) | `struct-basic.c`, `struct-byval.c`, `struct-return.c` (sret), `struct-heavy.c` (vec3 math, linked list in a static pool), `bitfields.c`, `unions.c`, `nested-aggregates.c` | stdout |
| `10-programs` (M1.14, exit criteria) | `fib.c`, `sieve.c`, `quicksort.c` (global array), `mergesort.c`, `nqueens.c`, `hanoi.c`, `matrix-mult.c` (double, printed scaled), `crc32.c` (table and bit ops), `fnv1a.c`, `strings.c` (own strlen/strcmp/reverse/itoa), `string-table-sort.c`, `bigint-add.c`, `life.c` (global grid), `bytecode-vm.c` (dispatch through a function-pointer table), `mandelbrot-fixed.c`, `collatz.c` | stdout |

### 7.3 `msil/test/M1/lit.local.cfg`
```python
# Milestone 1 (Design.md section 12): freestanding C, the only import is puts.
# Needs clang with the MSIL target, ilasm and a .NET runtime; otherwise every
# test here is UNSUPPORTED rather than failing.
if "msil-exec" not in config.available_features:
    config.unsupported = True
```

### 7.4 Later phases
- `msil/test/M2/` (objects and linker): `.test` files with `split-file`, `%clang_msil -c`, `msil-ld`, archives, `-flto`, `-shared` plus a consumer assembly, and P/Invoke resolution from native export tables. A local config will add `REQUIRES: lld` / an `msil-ld` feature.
- `msil/test/M3/` (libc and runtime): hosted C. `M3/lit.local.cfg` replaces the flags:
  ```python
  config.substitutions = [s for s in config.substitutions if s[0] != "%{msil_cflags}"]
  config.substitutions.append(("%{msil_cflags}", "-I" + config.msil_src_root + "/test/Inputs"))
  ```
  Tests there use `printf`, `malloc`, setjmp, TLS and atomics. Expect `.windows.expected` overrides for UCRT vs glibc formatting differences. The `net48` profile becomes a second `%{msil_c}` leg (`-mdotnet=net48` plus the `msil-run-netfx` runner).
- Phase 4: llvm-test-suite and GCC torture as out-of-tree runs driven by the same `msil-run.py` transcript format.

---

## 8. Milestone ladder (gates) and the agent review protocol

Every gate is a PR into the integration branch. **CI must be green on both OSes** (both jobs run `check-msil` with the native oracle; Windows adds PEVerify and .NET Framework).

| Gate | Capability landed | Tests that must exist and pass | Local gate command (after §10 setup) |
|---|---|---|---|
| **M1.0** Harness | `msil/` project, cache file, CI workflow, harness self-tests; the backend may not exist yet | `msil/test/harness/*.test` (prototype: 6 tests); everything else UNSUPPORTED | `python build\bin\llvm-lit.py -sv msil\test\harness` |
| **M1.1** Triple | `Triple::MSIL`, parse, normalize, print, DataLayout, EH none | §4.1 TripleTest cases | `ninja -C build TargetParserTests && build\unittests\TargetParser\TargetParserTests.exe --gtest_filter=*MSIL*` |
| **M1.2** Registry + empty target | target registered, lookup by object format, `llc` emits an empty but valid module, `-filetype=obj` rejected | `TargetRegistry` change; `CodeGen/MSIL/target-selection.ll`, `empty-module.ll` (+ilasm round trip), `obj-unsupported.ll` | `ninja -C build llc MCTests && python build\bin\llvm-lit.py -sv llvm\test\CodeGen\MSIL` |
| **M1.3** clang frontend | TargetInfo wrapper, inline-asm rejection, driver `-S` | clang.md §6.1-6.5 + `datalayout.c` | `ninja -C build clang && python build\bin\llvm-lit.py -sv clang\test\CodeGen\MSIL clang\test\Preprocessor\init-msil.c clang\test\Driver\msil-toolchain.c` |
| **M1.4** First executable | module header, method definitions, `ret` of constants and args, generated `Main`, identifier quoting | `module-skeleton.ll`, `ret.ll`, `args.ll`, `entrypoint.ll`, `identifier-quoting.ll`; `MSILNameQuotingTest`; exec `M1/00-smoke` | `ninja -C build llc clang && python build\bin\llvm-lit.py -sv llvm\test\CodeGen\MSIL msil\test\M1\00-smoke` |
| **M1.5** Arithmetic | ISel for int/fp/compare/casts; RegStackify, ExplicitLocals, `.maxstack` | `int-arith.ll`, `fp-arith.ll`, `conversions.ll`, `comparisons.ll`, `select.ll`, `reg-stackify.ll`, `explicit-locals.mir`, `maxstack.ll`, `stack-check-errors.mir`; `MSILStackEffectTest`; exec `01-arith` | `… msil\test\M1\01-arith` |
| **M1.6** Control flow | br/brcond/switch/unreachable/trap | `br.ll`, `switch.ll`, `switch-unreachable-default.ll`, `unreachable.ll`, `irreducible.ll`; exec `02-control` | `… 02-control` |
| **M1.7** Calls | direct calls, signatures, no-prototype calls | `call.ll`, `returned.ll`, `function-bitcasts.ll`, `add-prototypes.ll`, `unused-argument.ll`; exec `03-calls` | `… 03-calls` |
| **M1.8** P/Invoke output | `puts` imported per triple | `pinvoke-puts.ll` (LNX `"libc"`, WIN `"ucrtbase"`); exec `04-output` | `… 04-output` |
| **M1.9** Frames | localloc frame, frame index, over-alignment | `frame-index.ll`, `frame-overalign.ll`, `localloc-root.mir`, `args.ll`; exec `05-frames` | `… 05-frames` |
| **M1.10** Globals | data segment, module initializer, relocations, string literals | `globals.ll`, `global-relocs.ll`, `module-initializer.ll`, `string-literals.ll`; exec `06-globals` | `… 06-globals` |
| **M1.11** Memory | memcpy/memset to cpblk/initblk, unaligned access, bit intrinsics | `memintrinsics.ll`, `unaligned.ll`, `bit-intrinsics.ll`; exec `07-memory` | `… 07-memory` |
| **M1.12** Function pointers | `ldftn` + `calli`, tables | `ldftn-calli.ll`, `fnptr-table.ll`; exec `08-fnptr` | `… 08-fnptr` |
| **M1.13** Aggregates | byval/sret ABI shapes from clang | `byval.ll`, `sret.ll`; exec `09-structs` | `… 09-structs` |
| **M1.14** Exit | Design 12 exit criteria | exec `10-programs` (16) + everything above; `-O0`/`-O2`; ILVerify clean; native oracle; both OSes | `ninja -C build check-msil` with `LIT_OPTS="--param msil_native_oracle=required"` |

Notes:
1. Gates M1.1-M1.3 run in parallel; they need no execution. M1.4 is the first gate that needs ilasm and dotnet.
2. Before globals exist (M1.10), C tests at `-O2` are largely constant-folded, and C tests cannot use `volatile` inputs (they need a frame or a global). Runtime coverage of ISel at `-O2` comes from the `.ll` tests through `%{msil_ll}`, where llc does no IPO. From M1.10 on, programs read their inputs from `volatile` globals.

**Workflow for multiple agents** (the user asked for agents reviewing each other's work):
1. **Test-first.** A test agent adds the gate's tests marked `// XFAIL: *` (lit runs them and expects failure). The CI stays green, and the tests are reviewed before any implementation exists.
2. **Implementation.** The implementing agent removes the `XFAIL` lines. lit reports an unexpected pass if the marker is left behind, so a forgotten XFAIL cannot hide.
3. **Reviewer checklist** (a different agent):
   - The gate command passes locally. CI is green on Linux and Windows. No `XFAIL`/`UNSUPPORTED` remains for this gate's tests without a written reason.
   - Every new or changed `.expected` was produced by `update-expected.py`, and `update-expected.py --check` passes on the changed directories. Manual edits are allowed only for `.windows/.linux.expected` overrides, each with a comment in the test.
   - `ilverify-allowed.txt` is unchanged, or each new code has a comment naming the construct that produces it.
   - New llc tests have CHECK-LABEL per function, run both triples where output can differ, and end with the `%if ilasm` round trip when they produce a full module.
   - Test sources follow §7.1 (no UB, no `long`, …). The native oracle passing at `-O0` and `-O2` is the mechanical proof.
   - No upstream file changed outside the registration hooks listed in this report and in registry-tm.md / clang.md.
4. **Required status checks:** `MSIL CI / linux`, `MSIL CI / windows`.

---

## 9. CI for the fork

### 9.1 What is in `.github/workflows` today
58 workflow files (`.yml`/`.yaml`), all inherited from upstream; nothing MSIL-specific. Upstream pins its actions by SHA: `actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1`, `hendrikmuhs/ccache-action@f09c25b45002a07be2955cbe52e8cee55643f89d # v1.2.24`, `ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756 # v1.13.0`, `actions/cache@55cc8345863c7cc4c66a329aec7e433d2d1c52a9 # v6.1.0`, `actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7.0.1`. No upstream workflow uses `actions/setup-dotnet`, so its SHA has to be pinned by hand.

Almost every upstream job is guarded by `github.repository_owner == 'llvm'` or `github.repository == 'llvm/llvm-project'`. In the fork those runs show up as "skipped" and cost nothing. The exceptions, which can actually run in the fork:

| Workflow | Trigger in the fork | Effect |
|---|---|---|
| `gha-codeql.yml` | PRs **to `main`** touching `.github/**`; daily schedule | runs CodeQL on workflow files |
| `release-binaries-all.yml` | weekly schedule; PRs touching release files | `setup-variables` job runs (guarded only on PR-closed) |
| `hlsl-matrix.yaml` | PRs to `main` touching DirectX/HLSL paths | job waits for a self-hosted `hlsl-macos` runner that never comes |
| `test-suite.yml`, `libcxx-pr-benchmark.yml` | `/test-suite` or benchmark comments | comment-driven |
| `libcxx-benchmark-commit.yml`, `libcxx-benchmark-libstdcxx.yml`, `hlsl-test-all.yaml` | dispatch / workflow_call only | none |

Also visible: the remote has a branch `dependabot/npm_and_yarn/mlir/utils/vscode/...`, so Dependabot is active on the fork. `.github/CODEOWNERS` names `@llvm/...` teams that do not exist in `alethic`; that is harmless.

### 9.2 Keeping upstream workflows quiet (recommendation)
1. **Disable them in repository settings, not in files.** Deleting or editing upstream workflow files causes modify/delete conflicts on every upstream merge. `.github/msil/disable-upstream-workflows.sh` (Appendix A.12) runs `gh workflow disable` on every active workflow whose path does not start with `.github/workflows/msil-`. The `gh` 2.101.0 flags `--all`, `--limit`, `--json id,path,state` and `disable <id>` were verified locally. Note: there are 58 files and the default `--limit` is 50. Re-run the script after each upstream sync, because newly added upstream workflows start enabled. This is a settings change; **the user runs it** (it needs `actions: write`).
2. Name every fork workflow `msil-*.yml` and guard its jobs with `github.repository_owner == 'alethic'`.
3. **Branches:** make an integration branch (`msil-main`, or the current `msil-milestone1`) the **default branch**. GitHub runs `schedule` triggers only from the default branch, and PRs target it by default. Keep `main` as a plain upstream mirror. The MSIL workflow does not trigger on `main`.
4. Optional (user decision): turn off Dependabot security updates for the fork, and restrict allowed actions to the five or six used here (Settings → Actions → "Allow select actions"). Then any upstream workflow that slips through fails at startup.

### 9.3 `.github/workflows/msil-ci.yml`
Full text in Appendix A.11 (PyYAML parses it; the triggers and both jobs were checked). Shape:
- Triggers: push to `msil-main` and `msil-milestone*`; PRs into `msil-*` and `msil/**` (ignoring `llvm/docs/**`); nightly `cron: '17 5 * * *'`; `workflow_dispatch` with `full`.
- **Linux job** (`ubuntu-24.04`): free disk; `apt-get install ninja-build clang lld`; `actions/setup-dotnet` with `8.0.x` and `10.0.x`; cache `~/.nuget/packages` and `~/.dotnet/tools`; `dotnet restore msil/utils/tools/msil-tools.proj -p:MsilToolsRid=linux-x64` (downloads `runtime.linux-x64.Microsoft.NETCore.ILAsm`/`ILDAsm` **8.0.0**; version unverified) and then `chmod +x`; `dotnet tool update -g dotnet-ilverify --version 10.0.12` (that package and version exist: installed on this host). sccache through `hendrikmuhs/ccache-action` (`variant: sccache`, 2G). Configure with `-C msil/cmake/caches/MSIL.cmake -DCMAKE_C(XX)_COMPILER=clang(++) -DLLVM_USE_LINKER=lld -DCMAKE_C(XX)_COMPILER_LAUNCHER=sccache -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`. **PCH is on by default in this checkout** (`AddLLVM.cmake:98-143`); sccache cannot cache it, and upstream CI turns it off (`.ci/monolithic-linux.sh`). Build only `clang llc FileCheck not count split-file llvm-config llvm-mc TargetParserTests MCTests MSILTests`. Then run the unit tests with `llvm-lit build/test/Unit "--filter=MSIL|TripleTest|TargetRegistry"`, then `ninja check-msil` with `LIT_OPTS=--param msil_dotnet_runtime=8 --param msil_native_oracle=required`, then the execution suite again on .NET 10 with `DOTNET_TieredCompilation=0`. Nightly adds all opt levels and `ninja check-llvm check-clang` (regressions in upstream suites caused by our hooks). On failure, upload `lit-results.xml` and the `Output/` directories.
- **Windows job** (`windows-2022`): `git config --global core.autocrlf false` *before* checkout; `ilammy/msvc-dev-cmd` (provides `LIB`/`INCLUDE` for the native oracle); setup-dotnet 8+10; `choco install ninja` (the upstream libc-overlay-tests.yml does the same); ILVerify; sccache 3G. Configure as on Linux plus `-DCMAKE_C(XX)_COMPILER=clang-cl -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` (needed for sccache; upstream `libc-overlay-tests.yml:91-102` does the same). Run `check-msil` with `LIT_OPTS=--param msil_dotnet_runtime=8 --param msil_native_oracle=required --param msil_run_netfx=1`. PEVerify is used automatically if present (unverified on the image).
- **lld is not built in M1.** The native oracle links with the system `ld` (Linux) and `link.exe` (Windows); lld joins at phase 2 for `msil-ld`. **Only the X86 target is built** besides MSIL. clang tests with `aarch64-*-msil` triples are frontend-only and need no AArch64 backend. Add `AArch64` when an aarch64 host or aarch64 oracle appears.
- Budget (all unverified): a cold Release+asserts build of clang+llc with X86 and MSIL on a 4-vCPU hosted runner is roughly 1.5-3 h (Windows slower), well under the 6 h job limit. Warm sccache brings it to about 15-40 min. The MSIL tests themselves take seconds (§11 E11). **If the repository is private**, standard hosted runners have 2 vCPUs and minutes are billed. Consider a self-hosted runner on this 8-core box (Windows, with WSL for Linux), or larger runners.

### 9.4 CMake cache file (`msil/cmake/caches/MSIL.cmake`, Appendix A.3)
`Release`, assertions, `LLVM_ENABLE_PROJECTS=clang`, `LLVM_TARGETS_TO_BUILD=X86`, `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL` (registry-tm §2), `LLVM_EXTERNAL_PROJECTS=msil`, and `LLVM_EXTERNAL_MSIL_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../..`. Upstream cache files use `CMAKE_CURRENT_LIST_DIR` too (`clang/cmake/caches/3-stage.cmake`). Also `LLVM_OPTIMIZED_TABLEGEN=ON`, benchmarks, examples and docs off, static analyzer and ARCMT off, and `LLVM_LIT_ARGS="-sv --time-tests"`.

---

## 10. Fast local loop (this host: Windows 11, 8 cores, VS 2026, about 34 GB free on D:)

`vcvars64.bat` puts `cmake` 4.3.1, `ninja` 1.13.2, `clang-cl` and `lld-link` on PATH (verified). No sccache or ccache is installed, so plain ninja incremental builds are used.

One-time configure (from cmd):
```bat
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
cd /d D:\llvm-msil
cmake -G Ninja -S llvm -B build -C msil\cmake\caches\MSIL.cmake ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DLLVM_ENABLE_LLD=ON
```
(`/build*` is already in `.gitignore`. Stay with Release+asserts: a Debug clang on Windows does not fit in 34 GB.)

Inner loops (run from the same VS prompt; `llvm-lit` is `build\bin\llvm-lit.py` on Windows, per `AddLLVM.cmake:2358-2361`):
```bat
:: backend only: rebuilds LLVMMSILCodeGen and relinks llc
ninja -C build llc FileCheck not count split-file llvm-config
python build\bin\llvm-lit.py -sv llvm\test\CodeGen\MSIL
python build\bin\llvm-lit.py -sv llvm\test\CodeGen\MSIL\reg-stackify.ll

:: end to end for one gate directory (needs clang relinked too)
ninja -C build clang llc
python build\bin\llvm-lit.py -sv msil\test\M1\05-frames
python build\bin\llvm-lit.py -sv msil\test\M1\10-programs\quicksort.c --param msil_opt_levels=O2

:: what CI runs, with the native oracle and .NET Framework
set LIT_OPTS=--param msil_native_oracle=required --param msil_run_netfx=1
ninja -C build check-msil

:: refresh / check expected transcripts from a native build
python msil\utils\update-expected.py --clang build\bin\clang.exe msil\test\M1\06-globals\string-table.c
python msil\utils\update-expected.py --clang build\bin\clang.exe --check msil\test\M1
```
Debugging a failure: lit `-v` prints every expanded command, which can be copied and re-run. Artifacts live in `build\tools\msil\test\M1\<dir>\Output\<test>.c.tmp.<L>.{il,dll,out,native.exe}`. `ildasm` and `PEVerify` are in `C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\`. `set DOTNET_JitDisasm=main` with `DOTNET_JitStdOutFile` shows what RyuJIT made of a method (passes-frame E4/E5 used this).

Optional Linux execution without a Linux LLVM build: compile and assemble on Windows with `--param msil_triple=x86_64-unknown-linux-gnu-msil`, then run the `.dll` in WSL. `wsl.exe -d Ubuntu -- dotnet exec --runtimeconfig <net10 cfg> /mnt/<path>.dll` propagates exit codes and LF output at about 0.2 s per run once WSL is warm (verified; only .NET 10.0.12 is in WSL). Adding a `--wsl` mode to `msil-run.py` that translates paths would take about 15 lines. Not done.

---

## 11. Verified experiments (scratch: `...\scratchpad\recon\tests-ci\`)

| # | What | Result |
|---|---|---|
| E1 | `e1_hello.il` (global methods only, `pinvokeimpl("ucrtbase")` puts, generated `Main`). Framework ilasm `-dll` and `-exe` | Runs with `dotnet exec --runtimeconfig` on .NET 8 (dll and exe), and directly as an exe on .NET Framework 4.8. rc 42, "hello, world". |
| E2 | `dotnet e1_hello.dll` without runtimeconfig | rc 131: hostpolicy is missing, so it is treated as self-contained. Hence `dotnet exec --runtimeconfig`. The output file name need not match `.assembly` (prototype names files `fib.c.tmp.O0.dll`). |
| E3 | `e3_libc.il` built on Windows, run in WSL (Ubuntu 26.04.1, glibc 2.43, .NET 10.0.12) | `DllImport("libc")` resolves with only `libc.so.6` present. rc 42. The Windows-built PE runs unchanged. `"ucrtbase"` on Linux gives `DllNotFoundException`, rc 134. |
| E4 | `e4_buf.il` (three puts, exit 7), output to a pipe and a file on Linux; file redirect on Windows | All output is flushed at exit. Windows bytes are CRLF (14 bytes for "hello, world"), Linux bytes are LF. |
| E5 | `throw.il` (`ldnull; throw`) on .NET 8 and .NET Framework (Windows), and in WSL | 0xE0434352 on both Windows runtimes; 134 on Linux. stderr starts "Unhandled exception." (CoreCLR) or "Unhandled Exception:" (Framework). |
| E6 | `e2_verify.il` (localloc frame, cpblk/initblk, ldftn+calli, `native int` + `int64`, a bad join, a fall-off) through ILVerify 10.0.12 and PEVerify | ILVerify: `Unverifiable`, `InitLocals`, `StackByRef`, `[]` "ImportCalli not implemented", `StackOverflow`, `ReturnEmpty`, `PathStackDepth`, `MethodFallthrough`; **no error for `native int` + `int64`**. `-g`/`--ignore-error-file` match codes and cannot suppress the calli message. PEVerify: first error per method, flags `native int`/`int64` as "Unexpected type on the stack", treats calli as unverifiable. rc: ILVerify 2 on errors, 0 when clean. |
| E7 | ilasm on method names `add ret value int32 'add' 'value' 'ret' main_ '?foo@@YAHXZ' 'a.b$c'`; parameter `off`; assembly `throw` | Unquoted keywords fail (rc 1); quoted names and `main_` succeed. |
| E8 | Prototype lit suite (Appendix A) with lit from this checkout | Windows: 7/7 pass with `--param msil_native_oracle=required --param msil_run_netfx=1`, with `msil_opt_levels=O0,O1,O2`, and in the default configuration; `-a` showed all 8 commands per level. `--param msil_ilasm=` makes everything UNSUPPORTED. Cross-triple `--param msil_triple=x86_64-unknown-linux-gnu-msil` passes the harness tests. WSL: config loads, finds `/usr/bin/dotnet` with runtime 10.0.12, everything UNSUPPORTED (no ilasm). `msil-run.py` on Linux: expected-file match, flush, and the exception status all correct. |
| E9 | `%if feature %{ … %}` inside a recursively expanded config substitution, with `%{s:stem}` inside the branch | Works. |
| E10 | Native oracle: stock clang 22.1.3, `--target=x86_64-pc-windows-msvc -O2 -ffreestanding`, link.exe from the vcvars environment | `fib.native.exe` rc 55, correct output. `update-expected.py` `--check` passes, and write mode regenerates a byte-identical `fib.expected`. |
| E11 | Timing, 5 runs each | `dotnet exec` about 125 ms, .NET Framework exe about 90 ms, ilasm about 90 ms, ILVerify about 240 ms. About 60 tests × 2 levels is roughly 10 s on 8 workers. |
| E12 | `dotnet msbuild msil-tools.proj -getItem:PackageDownload` (evaluation only, no restore); `gh workflow list --json` fields; PyYAML on `msil-ci.yml` | The project evaluates; the fields are `id,name,path,state`; the YAML parses. |
| E13 | `wsl.exe -d Ubuntu -- dotnet exec …` from Windows | Exit codes 7 and 42 propagate; LF output; about 0.2 s per run when warm. Git-bash needs `MSYS_NO_PATHCONV=1`. |
| E14 | `llvm/test/CodeGen/MSIL/lit.local.cfg` (§3.1) executed against a stub config | With MSIL: feature `ilasm` and `%ilasm-verify` set to the Framework ilasm. Without MSIL: unsupported. |
| E15 | Host inventory | dotnet SDKs 6.0.428, 8.0.425, 9.0.318, 10.0.401; runtimes 6.0.36, 8.0.31, 9.0.20, 10.0.12; global tool `dotnet-ilverify 10.0.12` (`tools/net10.0/any`); PEVerify and ildasm in `NETFX 4.8 Tools\x64`; no ILAsm/ILDAsm in the NuGet cache; Python 3.14.7 with PyYAML 6.0.3; `gh` 2.101.0. |

---

## 12. Risks, open questions, cross-team notes

Risks:
1. **CRLF working tree.** `git config core.autocrlf` is `true` here and files are CRLF on disk. lit and FileCheck mostly cope, but expected files and any byte-exact test do not. Add `msil/.gitattributes` with `* text=auto eol=lf`. Changing `core.autocrlf` and renormalizing the existing checkout is the **user's call**. Windows CI sets it to `false` before checkout.
2. **ILVerify gaps** (calli, `native int`/`int64`) mean Linux CI has weaker stack-type checking than Windows. Keep PEVerify in the Windows job a required check. Revisit when ILVerify implements calli.
3. **Unverified NuGet details:** that `runtime.linux-x64.Microsoft.NETCore.ILAsm` 8.0.0 exists, that its executable bit survives extraction, and that the CoreCLR ilasm accepts the same `-dll -output= -nologo -quiet` flags. The first Linux CI run settles all three. Fallback: pin a 9.0.x or 10.0.x version in `msil-tools.proj`.
4. **dotnet-ilverify 10.x needs the .NET 10 runtime.** CI installs `10.0.x` for that reason, even though tests run on 8.
5. **Hosted-runner capacity** (§9.3): build time and the 2-vCPU private-repo case.
6. **Expected-file drift** once printf arrives (M3): UCRT and glibc differ in `%e`, `%p`, `%a` and NaN spelling. Plan on `.windows.expected` overrides, or keep tests to integers.
7. **The native oracle needs UB-free tests.** That is a feature, but agents will hit it. The reviewer checklist covers it.
8. **`update_llc_test_checks` is not usable for MSIL** until the §3.4 patch, so early llc tests are written by hand.
9. **Fork drift:** the design touches upstream files only in the registration hooks: `llvm/unittests/MC/TargetRegistry.cpp:35`, new `TripleTest` cases, `llvm/utils/UpdateTestChecks/asm.py` (+ `update_llc_test_checks.py`), and in phase 2 `lld/test/lit.cfg.py:76-91`. Everything else is under `msil/`, `llvm/test/**/MSIL`, `clang/test/**/MSIL` and `.github/{workflows/msil-*,msil/}`.

Open questions (for the lead):
1. Which DLL should the M1 `puts` P/Invoke use for `*-windows-msvc-msil`? `ucrtbase` (recommended, verified), `msvcrt`, or `api-ms-win-crt-stdio-l1-1-0`?
2. Does the M1 generated `Main` pass `argc`/`argv`? That decides whether execution tests can pass arguments (`%msil_exec prog.dll a b`) before phase 3.
3. Should the .NET Framework leg (`msil_run_netfx=1`) be **required** on Windows in M1? Recommended yes while the IL stays BCL-free, which is cheap and catches JIT differences. If a later M1 feature is net8-only, the leg needs a per-test opt-out (feature `msil-net8-only` plus `%if`).
4. Which branch becomes the fork's default (`msil-main` or `msil-milestone1`)? Is the repository public (runner size and cost)?
5. Final AsmPrinter syntax for method headers and footers, which the §3.4 regex depends on.

Requirements on other components (from what the tests need):
- **AsmPrinter:** single-quote every identifier (method, field, assembly, module and parameter names, or omit parameter names); one-line `.method` headers; close with `} // end of method '<name>'`; comment string `//` (clang.md §7.3).
- **Backend:** choose the P/Invoke library per triple (`libc` for Linux, `ucrtbase` for Windows); keep M1 output free of BCL type references so the .NET Framework leg stays possible; never emit `native int` and `int64` in one arithmetic operation (PEVerify will fail it).
- **clang driver:** `-S` with `--target=*-msil` must work from the plain `clang` driver on both hosts (clang.md §5).

---

## Appendix A: prototype files (all verified on this host as described in §11 E8)

Paths are relative to the repository root they are proposed for. The prototype tree also contains `fake/` and `M1/Inputs/*.il.in`, which **are not** proposed files. They stood in for the not-yet-existing backend.

### A.1 `msil/CMakeLists.txt`
```cmake
# The MSIL project: execution tests now, the runtime support assembly later.
# Built as an LLVM external project, so no upstream CMake file changes:
#   cmake -S llvm -B build -DLLVM_EXTERNAL_PROJECTS=msil \
#         -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil ...
# or simply: cmake -S llvm -B build -C msil/cmake/caches/MSIL.cmake
set(LLVM_SUBPROJECT_TITLE "MSIL")
set(MSIL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(MSIL_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

if(NOT "MSIL" IN_LIST LLVM_TARGETS_TO_BUILD)
  message(STATUS "MSIL project: the MSIL target is not built; "
                 "execution tests will report UNSUPPORTED")
endif()

if(LLVM_INCLUDE_TESTS)
  add_subdirectory(test)
endif()
```
`msil/.gitattributes`:
```
* text=auto eol=lf
*.expected text eol=lf
```

### A.2 `msil/test/CMakeLists.txt`
(Not configured here; no LLVM build exists. Every function and variable it uses was checked in §1.)
```cmake
# Execution tests for the MSIL target (msil/test). See msil/test/README.md.

# Configure-time discovery. lit.cfg.py repeats the search at test time, so
# installing .NET later only needs a re-run of lit, not of CMake.
set(_msil_hints)
if(WIN32)
  list(APPEND _msil_hints "$ENV{WINDIR}/Microsoft.NET/Framework64/v4.0.30319")
endif()
find_program(MSIL_ILASM_EXECUTABLE NAMES ilasm HINTS ${_msil_hints}
  DOC "ilasm used by the MSIL execution tests")
find_program(MSIL_DOTNET_EXECUTABLE NAMES dotnet HINTS ENV DOTNET_ROOT
  DOC "dotnet host used by the MSIL execution tests")
find_program(MSIL_ILVERIFY_EXECUTABLE NAMES ilverify
  HINTS "$ENV{HOME}/.dotnet/tools" "$ENV{USERPROFILE}/.dotnet/tools"
  DOC "ILVerify (dotnet tool dotnet-ilverify)")
if(WIN32)
  file(GLOB _msil_peverify
    "$ENV{ProgramFiles\(x86\)}/Microsoft SDKs/Windows/v*/bin/NETFX 4.* Tools/x64/PEVerify.exe")
  list(SORT _msil_peverify ORDER DESCENDING)
  list(GET _msil_peverify 0 _msil_peverify_first)
  set(MSIL_PEVERIFY_EXECUTABLE "${_msil_peverify_first}" CACHE FILEPATH "PEVerify")
endif()

# Only what the RUN lines invoke, so check-msil does not drag in every tool.
set(MSIL_TEST_DEPS clang llc FileCheck not count split-file llvm-config)

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
  PATHS
  "LLVM_SOURCE_DIR"
  "LLVM_BINARY_DIR"
  "LLVM_TOOLS_DIR"
  "LLVM_LIBS_DIR"
  "MSIL_SOURCE_DIR"
  "MSIL_BINARY_DIR"
  )

# check-msil-exec: this suite only. Part of check-all; UNSUPPORTED without .NET.
add_lit_testsuite(check-msil-exec "Running the MSIL execution tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${MSIL_TEST_DEPS}
  )

# check-msil-m1, check-msil-m1-03-memory, ...: one target per directory.
add_lit_testsuites(MSIL ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${MSIL_TEST_DEPS}
  )

# check-msil: every MSIL-specific lit test in the tree, with a lean
# dependency list. Source paths work because llvm-lit maps each lit.cfg.py
# to its generated lit.site.cfg.py.
set(_clang_src "${LLVM_EXTERNAL_CLANG_SOURCE_DIR}")
set(_msil_lit_paths
  ${LLVM_MAIN_SRC_DIR}/test/CodeGen/MSIL
  ${_clang_src}/test/CodeGen/MSIL
  ${_clang_src}/test/Preprocessor/init-msil.c
  ${_clang_src}/test/Driver/msil-toolchain.c
  ${CMAKE_CURRENT_BINARY_DIR}
  )
# Phase 2 adds ${LLVM_MAIN_SRC_DIR}/test/MC/MSIL and lld/test/MSIL here.
add_lit_target(check-msil "Running all MSIL tests"
  ${_msil_lit_paths}
  DEPENDS ${MSIL_TEST_DEPS} llvm-mc
  )
```
Note: `_msil_peverify` is empty when PEVerify is absent, and then `list(GET ... 0 ...)` fails. Guard it with `if(_msil_peverify)` in the real file.

### A.3 `msil/cmake/caches/MSIL.cmake`
```cmake
# Developer and CI configuration for the MSIL fork.
#   cmake -G Ninja -S llvm -B build -C msil/cmake/caches/MSIL.cmake
# Anything here can be overridden with -D on the same command line.
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(LLVM_ENABLE_ASSERTIONS ON CACHE BOOL "")
# lld is not needed until phase 2 (msil-ld); the native oracle links with the
# system linker (ld on Linux, link.exe from the VS environment on Windows).
set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "")
# X86 stays built: clang passes x86 -mllvm options for x86 triples
# (registry-tm.md 2.5), and the native oracle needs a host backend.
set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "")
set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD "MSIL" CACHE STRING "")
set(LLVM_EXTERNAL_PROJECTS "msil" CACHE STRING "")
set(LLVM_EXTERNAL_MSIL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "")
set(LLVM_OPTIMIZED_TABLEGEN ON CACHE BOOL "")
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "")
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "")
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "")
set(CLANG_ENABLE_STATIC_ANALYZER OFF CACHE BOOL "")
set(CLANG_ENABLE_ARCMT OFF CACHE BOOL "")
set(LLVM_LIT_ARGS "-sv --time-tests" CACHE STRING "")
```

### A.4 `msil/test/lit.site.cfg.py.in`
```python
@LIT_SITE_CFG_IN_HEADER@

import sys

config.host_triple = "@LLVM_HOST_TRIPLE@"
config.target_triple = "@LLVM_TARGET_TRIPLE@"
config.llvm_src_root = path(r"@LLVM_SOURCE_DIR@")
config.llvm_obj_root = path(r"@LLVM_BINARY_DIR@")
config.llvm_tools_dir = lit_config.substitute(path(r"@LLVM_TOOLS_DIR@"))
config.llvm_libs_dir = lit_config.substitute(path(r"@LLVM_LIBS_DIR@"))
config.lit_tools_dir = path(r"@LLVM_LIT_TOOLS_DIR@")
config.python_executable = "@Python3_EXECUTABLE@"
config.targets_to_build = "@TARGETS_TO_BUILD@".split()
config.enable_assertions = @ENABLE_ASSERTIONS@
config.msil_src_root = path(r"@MSIL_SOURCE_DIR@")
config.msil_obj_root = path(r"@MSIL_BINARY_DIR@")
# Found by find_program() at configure time; may be empty. lit.cfg.py also
# searches PATH and well-known locations, and --param msil_<tool>=PATH wins.
config.msil_ilasm = r"@MSIL_ILASM_EXECUTABLE@"
config.msil_dotnet = r"@MSIL_DOTNET_EXECUTABLE@"
config.msil_ilverify = r"@MSIL_ILVERIFY_EXECUTABLE@"
config.msil_peverify = r"@MSIL_PEVERIFY_EXECUTABLE@"

import lit.llvm
lit.llvm.initialize(lit_config, config)

# Let the main config do the real work.
lit_config.load_config(config, os.path.join(config.msil_src_root, "test", "lit.cfg.py"))
```

### A.5 `msil/test/lit.cfg.py`
```python
# -*- Python -*-
#
# lit configuration for the MSIL execution tests:
#
#   prog.c --clang --target=<native>-msil -S--> prog.il --ilasm--> prog.dll
#          --ilverify--> (no stack-shape errors) --dotnet exec--> transcript
#
# Each test is compared with a checked-in expected transcript (stdout plus an
# "[exit N]" line, see msil/utils/msil-run.py). When --param
# msil_native_oracle=1 is given, the same source is also built natively with
# the just-built clang for the host and must produce the same transcript.
#
# Nothing here is fatal when .NET is absent: the directories that need it
# mark themselves UNSUPPORTED, so check-all keeps working everywhere.

import glob
import json
import os
import platform
import re
import subprocess

import lit.formats
import lit.util
from lit.llvm import llvm_config
from lit.llvm.subst import ToolSubst

config.name = "MSIL"
# LLVM 23 deprecates the external shell; RUN lines use lit's internal shell
# on every host, so they must not rely on bash.
config.test_format = lit.formats.ShTest()
config.suffixes = [".c", ".il", ".ll", ".test"]
config.excludes = ["Inputs", "CMakeLists.txt", "README.md"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.msil_obj_root, "test")
# %{msil_c} expands to other substitutions, which expand again.
config.recursiveExpansionLimit = 10

llvm_config.use_default_substitutions()  # FileCheck, not, count, %python
llvm_config.use_clang(required=False)  # %clang, %clang_cc1, feature 'clang'

params = lit_config.params
features = config.available_features
utils_dir = os.path.join(config.msil_src_root, "utils")
inputs_dir = os.path.join(config.test_source_root, "Inputs")
system = platform.system()
is_windows = system == "Windows"
exe_ext = ".exe" if is_windows else ""


def note(msg):
    lit_config.note("msil: " + msg)


def quote(path):
    return '"%s"' % path


def version_key(path_or_version):
    """'10.0.1' sorts after '8.0.31'."""
    m = re.search(r"(\d+)\.(\d+)\.(\d+)", path_or_version)
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)


def first_file(candidates):
    for c in candidates:
        # X_OK matters on Linux: NuGet may extract ilasm without the x bit.
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return os.path.normpath(c)
    return None


def find_tool(name, site_value, extra=()):
    """Precedence: --param msil_<name>=PATH, $MSIL_<NAME>, CMake, PATH, extra.

    An explicitly empty override (--param msil_ilverify=) disables the tool.
    """
    override = params.get("msil_" + name)
    if override is None:
        override = os.environ.get("MSIL_" + name.upper())
    if override is not None:
        return first_file([override])
    return first_file([site_value, lit.util.which(name), *extra])


def host_rid():
    arch = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64"
    return {"Windows": "win", "Linux": "linux", "Darwin": "osx"}.get(system, "") + "-" + arch


def default_msil_triple():
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64",
            "aarch64": "aarch64"}.get(platform.machine().lower(), "x86_64")
    if is_windows:
        return arch + "-pc-windows-msvc-msil"
    if system == "Darwin":
        return arch + "-apple-darwin-msil"
    return arch + "-unknown-linux-gnu-msil"


# ---------------------------------------------------------------- the target
if "MSIL" in config.targets_to_build:
    features.add("msil-registered-target")
for t in config.targets_to_build:
    features.add(t.lower() + "-registered-target")

msil_triple = params.get("msil_triple") or default_msil_triple()
native_triple = msil_triple[: -len("-msil")] if msil_triple.endswith("-msil") else msil_triple
features.add("msil-triple=" + msil_triple)
if "-windows-" in msil_triple:
    features.add("msil-triple-windows")
elif "-linux-" in msil_triple:
    features.add("msil-triple-linux")

# ---------------------------------------------------------------- ilasm
windir = os.environ.get("WINDIR", r"C:\Windows")
netfx_dir = os.path.join(windir, "Microsoft.NET", "Framework64", "v4.0.30319")
nuget_root = os.environ.get("NUGET_PACKAGES") or os.path.join(
    os.path.expanduser("~"), ".nuget", "packages")
rid = host_rid()
ilasm_extra = []
if is_windows:
    ilasm_extra.append(os.path.join(netfx_dir, "ilasm.exe"))
# runtime.<rid>.Microsoft.NETCore.ILAsm from NuGet (the only ilasm on Linux/macOS).
ilasm_extra += sorted(
    glob.glob(os.path.join(nuget_root, "runtime.%s.microsoft.netcore.ilasm" % rid,
                           "*", "runtimes", rid, "native", "ilasm" + exe_ext)),
    key=version_key, reverse=True)
ilasm = find_tool("ilasm", getattr(config, "msil_ilasm", ""), ilasm_extra)
if ilasm:
    features.add("msil-ilasm")
    note("ilasm: " + ilasm)
else:
    note("ilasm not found; execution tests are UNSUPPORTED")

# ---------------------------------------------------------------- dotnet
dotnet_extra = [os.path.join(os.environ.get("DOTNET_ROOT", ""), "dotnet" + exe_ext)]
dotnet_extra += ([r"C:\Program Files\dotnet\dotnet.exe"] if is_windows else
                 ["/usr/share/dotnet/dotnet", "/usr/lib/dotnet/dotnet",
                  "/usr/local/share/dotnet/dotnet",
                  os.path.join(os.path.expanduser("~"), ".dotnet", "dotnet")])
dotnet = find_tool("dotnet", getattr(config, "msil_dotnet", ""), dotnet_extra)

runtimes = {}  # major -> (version string, directory of that runtime)
if dotnet:
    try:
        listing = subprocess.run([dotnet, "--list-runtimes"], capture_output=True,
                                 text=True, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        listing = ""
    for m in re.finditer(r"^Microsoft\.NETCore\.App (\S+) \[(.+)\]\s*$", listing, re.M):
        ver, base = m.group(1), m.group(2)
        major = version_key(ver)[0]
        if major not in runtimes or version_key(ver) > version_key(runtimes[major][0]):
            runtimes[major] = (ver, os.path.join(base, ver))
for major in runtimes:
    features.add("dotnet-runtime-%d" % major)

wanted = params.get("msil_dotnet_runtime")  # e.g. "8" or "10"
if wanted:
    runtime_major = int(wanted) if int(wanted) in runtimes else None
    if runtime_major is None:
        lit_config.warning("msil: .NET runtime %s requested but not installed" % wanted)
else:
    # Design.md 7.1: net8.0 is the default profile; otherwise take the newest.
    runtime_major = 8 if 8 in runtimes else (max(runtimes) if runtimes else None)

runtimeconfig = None
runtime_dir = None
if dotnet and runtime_major is not None:
    features.add("msil-dotnet")
    runtime_dir = runtimes[runtime_major][1]
    os.makedirs(config.test_exec_root, exist_ok=True)
    runtimeconfig = os.path.join(config.test_exec_root,
                                 "msil-net%d.runtimeconfig.json" % runtime_major)
    with open(runtimeconfig, "w") as f:
        json.dump({"runtimeOptions": {
            "tfm": "net%d.0" % runtime_major,
            "rollForward": "LatestPatch",
            "framework": {"name": "Microsoft.NETCore.App",
                          "version": "%d.0.0" % runtime_major}}}, f, indent=2)
    note("dotnet: %s (Microsoft.NETCore.App %s)" % (dotnet, runtimes[runtime_major][0]))
else:
    note("no usable .NET runtime; execution tests are UNSUPPORTED")

# ---------------------------------------------------------------- verifiers
ilverify = find_tool("ilverify", getattr(config, "msil_ilverify", ""), [
    os.path.join(os.path.expanduser("~"), ".dotnet", "tools", "ilverify" + exe_ext)])
if ilverify:
    try:  # dotnet-ilverify 10.x is itself a net10.0 app; make sure it starts.
        ok = subprocess.run([ilverify, "--version"], capture_output=True,
                            timeout=120).returncode == 0
    except (OSError, subprocess.SubprocessError):
        ok = False
    if not ok:
        note("ilverify at %s does not run; verification disabled" % ilverify)
        ilverify = None
if ilverify and runtime_dir:
    features.add("msil-ilverify")

peverify = None  # --param msil_peverify= (empty) turns it off
if is_windows:
    sdk = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    hits = glob.glob(os.path.join(sdk, "Microsoft SDKs", "Windows", "v*", "bin",
                                  "NETFX 4.* Tools", "x64", "PEVerify.exe"))
    peverify = find_tool("peverify", getattr(config, "msil_peverify", ""),
                         sorted(hits, reverse=True))
    if peverify:
        features.add("msil-peverify")

if is_windows and os.path.isfile(os.path.join(netfx_dir, "clr.dll")):
    features.add("msil-netfx")
    if lit.util.pythonize_bool(params.get("msil_run_netfx", "0")):
        features.add("msil-run-netfx")

# ---------------------------------------------------------------- native oracle
oracle = params.get("msil_native_oracle", "0")
if oracle not in ("0", "", "false", "False", "off"):
    host_target = {"x86_64": "X86", "aarch64": "AArch64"}.get(native_triple.split("-")[0])
    problems = []
    if not getattr(config, "clang", None):
        problems.append("clang not built")
    if host_target not in config.targets_to_build:
        problems.append("%s target not built" % host_target)
    if is_windows and not os.environ.get("LIB"):
        problems.append("LIB not set (run from a VS developer prompt)")
    if problems:
        msg = "msil: native oracle unavailable: " + ", ".join(problems)
        (lit_config.fatal if oracle == "required" else lit_config.warning)(msg)
    else:
        features.add("msil-native-oracle")

# All prerequisites for compiling and running C programs.
if {"clang", "msil-registered-target", "msil-ilasm", "msil-dotnet"} <= features:
    features.add("msil-exec")

# ---------------------------------------------------------------- environment
for var in ("DOTNET_ROOT", "DOTNET_ROOT_X64", "DOTNET_ROOT_ARM64", "NUGET_PACKAGES",
            "ProgramFiles", "ProgramFiles(x86)", "ProgramData", "LOCALAPPDATA",
            "APPDATA", "WINDIR", "LIBPATH"):
    if var in os.environ:
        config.environment[var] = os.environ[var]
config.environment["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
config.environment["DOTNET_NOLOGO"] = "1"

# ---------------------------------------------------------------- substitutions
python = quote(config.python_executable)
run_script = quote(os.path.join(utils_dir, "msil-run.py"))
verify_script = quote(os.path.join(utils_dir, "msil-verify.py"))

triple_os = ("windows" if "-windows-" in msil_triple else
             "darwin" if "-apple-" in msil_triple else "linux")
exec_cmd = "%s %s --expect-os %s" % (python, run_script, triple_os)
if dotnet and runtimeconfig:
    exec_cmd += " --dotnet %s --runtimeconfig %s" % (quote(dotnet), quote(runtimeconfig))
for kv in filter(None, params.get("msil_dotnet_env", "").split(";")):
    exec_cmd += " --env " + kv  # e.g. DOTNET_TieredCompilation=0

if "msil-ilverify" in features:
    verify_cmd = "%s %s --ilverify %s --runtime-dir %s --allow %s" % (
        python, verify_script, quote(ilverify), quote(runtime_dir),
        quote(os.path.join(utils_dir, "ilverify-allowed.txt")))
    if peverify:
        verify_cmd += " --peverify " + quote(peverify)
else:
    verify_cmd = "%s %s --skip" % (python, verify_script)

clang = getattr(config, "clang", None) or "clang-not-found"
tools = [
    ToolSubst("%msil_triple", command=msil_triple),
    ToolSubst("%clang_msil", command=quote(clang), extra_args=["--target=" + msil_triple]),
    ToolSubst("%clang_native", command=quote(clang), extra_args=["--target=" + native_triple]),
    ToolSubst("%ilasm", command=quote(ilasm or "ilasm-not-found"),
              extra_args=["-nologo", "-quiet"]),
    ToolSubst("%dotnet", command=quote(dotnet or "dotnet-not-found")),
    ToolSubst("%msil_exec", command=exec_cmd),
    ToolSubst("%msil_verify", command=verify_cmd),
]
llvm_config.add_tool_substitutions(tools)
config.substitutions.append(("%msil_inputs", inputs_dir))

# %{msil_c}: the whole compile -> assemble -> verify -> run -> compare pipeline,
# once per optimization level in --param msil_opt_levels (default O0,O2).
# Each level writes %t.<level>.{il,dll,out}. The native-oracle and .NET
# Framework legs are guarded by %if so the line stays valid when disabled.
opt_levels = [o.strip() for o in params.get("msil_opt_levels", "O0,O2").split(",") if o.strip()]


def c_pipeline(level):
    t = "%t." + level
    cmd = " && ".join([
        "%clang_msil %{msil_cflags} -" + level + " -S %s -o " + t + ".il",
        "%ilasm -dll -output=" + t + ".dll " + t + ".il",
        "%msil_verify " + t + ".dll",
        "%msil_exec --expect %S/%{s:stem} --out " + t + ".out " + t + ".dll",
    ])
    cmd += (" %if msil-native-oracle %{ && %clang_native %{msil_cflags} -" + level
            + " %s -o " + t + ".native.exe && %msil_exec --native --expect %S/%{s:stem}"
            + " --out " + t + ".native.out " + t + ".native.exe %}")
    cmd += (" %if msil-run-netfx %{ && %ilasm -exe -output=" + t + ".exe " + t + ".il"
            + " && %msil_exec --netfx --expect %S/%{s:stem} --out " + t + ".netfx.out "
            + t + ".exe %}")
    return cmd


config.substitutions.append(("%{msil_c}", " && ".join(c_pipeline(o) for o in opt_levels)))


# %{msil_ll}: the same for hand-written IR run through llc. llc does no IPO,
# so values passed to noinline functions stay opaque even at -O2; this is how
# the early gates get runtime coverage before globals and frames exist.
# The .ll files carry no target triple or datalayout.
def ll_pipeline(level):
    t = "%t." + level
    cmd = " && ".join([
        "%llc_msil -" + level + " %s -o " + t + ".il",
        "%ilasm -dll -output=" + t + ".dll " + t + ".il",
        "%msil_verify " + t + ".dll",
        "%msil_exec --expect %S/%{s:stem} --out " + t + ".out " + t + ".dll",
    ])
    cmd += (" %if msil-native-oracle %{ && %clang_native -Wno-override-module -" + level
            + " %s -o " + t + ".native.exe && %msil_exec --native --expect %S/%{s:stem}"
            + " --out " + t + ".native.out " + t + ".native.exe %}")
    return cmd


llc = lit.util.which("llc", config.llvm_tools_dir) or "llc-not-found"
llvm_config.add_tool_substitutions(
    [ToolSubst("%llc_msil", command=quote(llc), extra_args=["-mtriple=" + msil_triple])])
config.substitutions.append(("%{msil_ll}", " && ".join(ll_pipeline(o) for o in opt_levels)))
# Directories override this in lit.local.cfg (milestone 1: freestanding).
config.substitutions.append(("%{msil_cflags}", "-ffreestanding -I" + quote(inputs_dir)))
```
Follow-ups when productizing: on Linux the oracle check should also require `lit.util.which("ld")`, and `int(wanted)` should reject non-numeric input with a clear message.

### A.6 Directory configs
`msil/test/M1/lit.local.cfg`: see §7.3. `msil/test/harness/lit.local.cfg`:
```python
# Self-tests of the harness (msil-run.py, msil-verify.py, tool discovery).
# They use hand-written .il, so they need ilasm and dotnet but not clang.
if not {"msil-ilasm", "msil-dotnet"} <= config.available_features:
    config.unsupported = True
```

### A.7 Harness self-tests (`msil/test/harness/`)
`exit-code.test`:
```
# The transcript ends with the exit code.
RUN: %ilasm -dll -output=%t.dll %S/Inputs/exit7.il
RUN: %msil_exec --expect %S/Inputs/exit7 %t.dll
```
`unhandled-exception.test` (expected: `[unhandled exception: System.NullReferenceException]`):
```
# An unhandled CLR exception is reported by type, the same on every OS.
RUN: %ilasm -dll -output=%t.dll %S/Inputs/throw.il
RUN: %msil_exec --expect %S/Inputs/throw %t.dll
```
`verify-rejects-bad-stack.test`:
```
# Stack-shape errors fail msil-verify.
REQUIRES: msil-ilverify
RUN: %ilasm -dll -output=%t.dll %S/Inputs/badstack.il
RUN: not %msil_verify %t.dll 2> %t.err
RUN: %python -c "import sys; t=open(sys.argv[1]).read(); sys.exit(0 if 'PathStackDepth' in t and 'MethodFallthrough' in t else 1)" %t.err
```
`verify-accepts-unverifiable.test`:
```
# Unverifiable-by-design constructs pass, and calli still runs correctly.
REQUIRES: msil-ilverify
RUN: %ilasm -dll -output=%t.dll %S/Inputs/unverifiable-ok.il
RUN: %msil_verify -v %t.dll
RUN: %msil_exec %t.dll > %t.out
RUN: %python -c "import sys; sys.exit(0 if open(sys.argv[1]).read() == '[exit 9]\n' else 1)" %t.out
```
`verify-ptr-int64-mix.test`:
```
# Mixing native int and int64 is caught only by PEVerify (Windows).
REQUIRES: msil-ilverify, msil-peverify
RUN: %ilasm -dll -output=%t.dll %S/Inputs/ptr-int64-mix.il
RUN: not %msil_verify %t.dll 2> %t.err
RUN: %python -c "import sys; t=open(sys.argv[1]).read(); sys.exit(0 if 'peverify:' in t and 'Unexpected type on the stack' in t and 'ilverify:' not in t else 1)" %t.err
# It still runs: the x64 JIT treats int64 as native int.
RUN: %msil_exec %t.dll > %t.out
RUN: %python -c "import sys; sys.exit(0 if open(sys.argv[1]).read() == '[exit 5]\n' else 1)" %t.out
```
Inputs (all start with `.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }`):
- `exit7.il`: `Main` does `ldc.i4.7; ret`. `exit7.expected` is `[exit 7]`.
- `throw.il`: `.assembly 'throw' { }` (quoted: `throw` is a keyword); `Main` does `ldnull; throw`.
- `badstack.il`: `badjoin(int32)` pushes 2 values on one path and 1 on the other, then `ret`; `falloff(int32)` ends with `L1: nop`.
- `unverifiable-ok.il`: `frame(int32)` does `ldc.i4 32; localloc; stloc fp; ldloc fp; ldarg a; stind.i4; ldloc fp; ldind.i4; ldftn int32 sq(int32); calli int32(int32); ret`, and `Main` calls `frame(3)`, giving exit 9.
- `ptr-int64-mix.il`: `mix(native int p, int64 o)` does `ldarg.0; ldarg.1; add; ret`, and `Main` returns `conv.i4` of `mix(0, 5)`.

### A.8 `msil/test/Inputs/msil_test.h`
```c
/* Helpers shared by the MSIL execution tests.
 *
 * Milestone 1 programs are freestanding: the only import is puts (Design.md
 * section 12), so all output goes through these helpers, one value per line.
 * Everything is static so each test stays a single translation unit. The same
 * source is also built natively (the oracle), so nothing here may depend on
 * the MSIL target.
 */
#ifndef MSIL_TEST_H
#define MSIL_TEST_H

int puts(const char *s);

/* Self-checking tests (usable before puts works): nonzero exit = failing line. */
#define MSIL_EXPECT(c) do { if (!(c)) return 1 + (__LINE__ % 255); } while (0)

/* Writes the decimal digits of v ending just before end; returns the start. */
static char *msil_fmt_u64(unsigned long long v, char *end) {
  do {
    *--end = (char)('0' + (int)(v % 10));
    v /= 10;
  } while (v);
  return end;
}

static void put_str(const char *s) { puts(s); }

static void put_u64(unsigned long long v) {
  char buf[24];
  buf[23] = 0;
  puts(msil_fmt_u64(v, buf + 23));
}

static void put_i64(long long v) {
  char buf[24];
  buf[23] = 0;
  /* Negate in unsigned arithmetic so LLONG_MIN works. */
  unsigned long long m = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
  char *p = msil_fmt_u64(m, buf + 23);
  if (v < 0)
    *--p = '-';
  puts(p);
}

static void put_hex(unsigned long long v) {
  static const char digits[] = "0123456789abcdef";
  char buf[20];
  char *p = buf + 19;
  *p = 0;
  do {
    *--p = digits[v & 15];
    v >>= 4;
  } while (v);
  *--p = 'x';
  *--p = '0';
  puts(p);
}

/* "name=value" on one line. */
static void put_kv(const char *name, long long v) {
  char buf[96];
  char num[24];
  int n = 0;
  while (*name && n < 64)
    buf[n++] = *name++;
  buf[n++] = '=';
  num[23] = 0;
  unsigned long long m = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
  char *p = msil_fmt_u64(m, num + 23);
  if (v < 0)
    buf[n++] = '-';
  while (*p)
    buf[n++] = *p++;
  buf[n] = 0;
  puts(buf);
}

#endif /* MSIL_TEST_H */
```
(The prototype ran without `MSIL_EXPECT`; the macro is the §7.1 addition.)

### A.9 `msil/utils/msil-run.py`
```python
#!/usr/bin/env python3
"""Run a program built by the MSIL test suite and normalize what it did.

The normalized transcript is the program's stdout with line endings folded to
LF, followed by one status line:

    [exit N]                          normal termination with exit code N
    [unhandled exception: T]          the CLR reported an unhandled exception T
    [signal N]                        killed by a signal (POSIX)
    [timeout]                         did not finish within --timeout seconds

The same transcript format is produced for the three ways a test runs a
program, so they can all be compared with one expected file:

    msil-run.py --dotnet D --runtimeconfig C -- prog.dll [args]   (modern .NET)
    msil-run.py --netfx -- prog.exe [args]                        (.NET Framework)
    msil-run.py --native -- prog.exe [args]                       (native oracle)

With --expect STEM the transcript is compared with STEM.<os>.expected if that
file exists, else STEM.expected. <os> (windows, linux or darwin) comes from
--expect-os, which lit sets from the target triple. A mismatch
prints a unified diff and exits 1. With --out FILE the transcript is written
there as well.
"""

import argparse
import difflib
import os
import platform
import re
import subprocess
import sys

IS_WINDOWS = platform.system() == "Windows"

# CoreCLR prints "Unhandled exception. System.Foo: ..." and .NET Framework
# prints "Unhandled Exception: System.Foo: ...".
UNHANDLED_RE = re.compile(r"Unhandled [Ee]xception[.:]\s*([\w.`+]+)")


def suppress_crash_dialogs():
    """Keep WER and 'program has stopped working' dialogs from hanging CI."""
    if not IS_WINDOWS:
        return
    import ctypes

    SEM_FAILCRITICALERRORS = 0x0001
    SEM_NOGPFAULTERRORBOX = 0x0002
    SEM_NOOPENFILEERRORBOX = 0x8000
    ctypes.windll.kernel32.SetErrorMode(
        SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX
    )


def build_command(args):
    prog = os.path.abspath(args.program)
    if args.native or args.netfx:
        if args.netfx and not IS_WINDOWS:
            sys.exit("msil-run: --netfx is only available on Windows")
        return [prog] + args.args
    if not args.dotnet or not args.runtimeconfig:
        sys.exit("msil-run: --dotnet and --runtimeconfig are required for .dll programs")
    return [args.dotnet, "exec", "--runtimeconfig", args.runtimeconfig, prog] + args.args


def child_env(args):
    env = dict(os.environ)
    env.setdefault("DOTNET_CLI_TELEMETRY_OPTOUT", "1")
    env.setdefault("DOTNET_NOLOGO", "1")
    env.setdefault("DOTNET_SKIP_FIRST_TIME_EXPERIENCE", "1")
    env.setdefault("DOTNET_DbgEnableMiniDump", "0")
    for kv in args.env:
        key, sep, value = kv.partition("=")
        if not sep:
            sys.exit("msil-run: --env expects KEY=VALUE, got %r" % kv)
        env[key] = value
    return env


def status_line(proc_rc, stderr_text):
    m = UNHANDLED_RE.search(stderr_text)
    if m:
        return "[unhandled exception: %s]" % m.group(1)
    if proc_rc < 0:  # POSIX: killed by a signal
        return "[signal %d]" % -proc_rc
    if IS_WINDOWS and proc_rc >= 0x80000000:
        # NTSTATUS-style crash codes (access violation, stack overflow, ...).
        return "[crash 0x%08X]" % proc_rc
    return "[exit %d]" % proc_rc


def transcript(stdout_bytes, status):
    text = stdout_bytes.decode("utf-8", errors="backslashreplace")
    text = text.replace("\r\n", "\n")
    if text and not text.endswith("\n"):
        text += "\n[no newline at end of output]\n"
    return text + status + "\n"


def host_os():
    return {"Windows": "windows", "Linux": "linux", "Darwin": "darwin"}.get(
        platform.system(), platform.system().lower()
    )


def expected_file(stem, osname):
    for cand in (stem + "." + osname + ".expected", stem + ".expected"):
        if os.path.isfile(cand):
            return cand
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--native", action="store_true", help="run a native executable")
    mode.add_argument("--netfx", action="store_true", help="run a .exe on .NET Framework")
    p.add_argument("--dotnet", help="path to the dotnet host")
    p.add_argument("--runtimeconfig", help="runtimeconfig.json for 'dotnet exec'")
    p.add_argument("--env", action="append", default=[], metavar="KEY=VALUE")
    p.add_argument("--stdin", help="file to feed on stdin (default: empty)")
    p.add_argument("--timeout", type=float, default=120.0)
    p.add_argument("--out", help="also write the transcript to this file")
    p.add_argument("--expect", metavar="STEM", help="compare with STEM[.<os>].expected")
    p.add_argument("--expect-os", default=host_os(),
                   help="<os> for STEM.<os>.expected: the OS of the target triple "
                        "(it decides sizeof(long) etc.), default: the host")
    p.add_argument("--show-stderr", action="store_true", help="echo the program's stderr")
    p.add_argument("program")
    p.add_argument("args", nargs="*")
    args = p.parse_args()

    suppress_crash_dialogs()
    cmd = build_command(args)
    stdin = open(args.stdin, "rb") if args.stdin else subprocess.DEVNULL
    try:
        proc = subprocess.run(
            cmd,
            stdin=stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=child_env(args),
            timeout=args.timeout,
        )
        stderr_text = proc.stderr.decode("utf-8", errors="backslashreplace")
        out = transcript(proc.stdout, status_line(proc.returncode, stderr_text))
    except subprocess.TimeoutExpired as e:
        stderr_text = (e.stderr or b"").decode("utf-8", errors="backslashreplace")
        out = transcript(e.stdout or b"", "[timeout]")
    finally:
        if stdin is not subprocess.DEVNULL:
            stdin.close()

    if args.show_stderr and stderr_text:
        sys.stderr.write(stderr_text)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
    if not args.expect:
        sys.stdout.write(out)
        return 0

    exp_path = expected_file(args.expect, args.expect_os)
    if exp_path is None:
        sys.stderr.write("msil-run: no expected file for %s\n" % args.expect)
        sys.stderr.write("---- actual ----\n" + out)
        return 1
    with open(exp_path, "r", encoding="utf-8", newline="") as f:
        expected = f.read().replace("\r\n", "\n")
    if expected == out:
        return 0
    sys.stderr.write("msil-run: output of %s does not match %s\n" % (args.program, exp_path))
    sys.stderr.writelines(
        difflib.unified_diff(
            expected.splitlines(True), out.splitlines(True), exp_path, "actual"
        )
    )
    if stderr_text:
        sys.stderr.write("---- program stderr ----\n" + stderr_text)
    return 1


if __name__ == "__main__":
    sys.exit(main())
```

### A.10 `msil/utils/msil-verify.py` and `ilverify-allowed.txt`
```python
#!/usr/bin/env python3
"""Check an assembly with ILVerify (and optionally PEVerify) for stack-shape errors.

Design.md section 12: "ilverify reports no stack-shape errors (unverifiable
pointer use is expected)". The output of the backend is unverifiable by design,
so this wrapper fails only on error codes that are NOT in the allow list.

    msil-verify.py --ilverify PATH --runtime-dir DIR --allow FILE prog.dll
    msil-verify.py --skip prog.dll            (no verifier available: no-op)

ILVerify 10.0.12 notes (measured):
  * Error lines look like "[IL]: Error [Code]: [file : Type::Method(...)][offset 0x..] text".
  * A method containing `calli` yields "Error []: [...] ImportCalli not implemented"
    and the rest of that method is not verified. Reported here as a note, not a failure.
  * It does NOT flag `native int` + `int64` arithmetic; PEVerify does
    ("Unexpected type on the stack"). Pass --peverify on Windows to cover that.
PEVerify reports only the first error per method.
"""

import argparse
import glob
import os
import re
import subprocess
import sys

IL_ERROR_RE = re.compile(r"^\[IL\]: Error \[(?P<code>\w*)\]: \[(?P<where>[^\]]*)\](?P<rest>.*)$")
PEVERIFY_ERROR_RE = re.compile(r"^\[IL\]: Error: \[(?P<where>[^\]]*)\](?P<rest>.*)$")

# PEVerify has no codes; these message fragments are the unverifiable-by-design ones.
PEVERIFY_ALLOWED = (
    "Instruction cannot be verified.",
    "Expected ByRef on the stack.",
    "initlocals must be set",
    "Unmanaged pointers are not a verifiable type.",
)


def load_allow(path):
    pats = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                pats.append(re.compile(line))
    return pats


def short(where):
    # "C:\...\x.dll : <Module>::f(int32)" -> "<Module>::f(int32)"
    return where.split(" : ", 1)[-1]


def run_ilverify(args, allow):
    cmd = [args.ilverify, args.assembly]
    for d in args.runtime_dir:
        cmd += ["-r", os.path.join(d, "*.dll")]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    text = proc.stdout.decode("utf-8", errors="backslashreplace")
    bad, notes = [], []
    for line in text.splitlines():
        m = IL_ERROR_RE.match(line.strip())
        if not m:
            continue
        code, where, rest = m.group("code"), short(m.group("where")), m.group("rest").strip()
        if code == "" and "ImportCalli not implemented" in rest:
            notes.append("ilverify: %s contains calli; rest of method not verified" % where)
        elif any(p.fullmatch(code) for p in allow):
            continue
        else:
            bad.append("ilverify: [%s] %s %s" % (code or "?", where, rest))
    if proc.returncode not in (0, 2) and not bad:
        bad.append("ilverify: exited with %d:\n%s" % (proc.returncode, text))
    return bad, notes


def run_peverify(args):
    proc = subprocess.run(
        [args.peverify, "/nologo", "/il", args.assembly],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300,
    )
    text = proc.stdout.decode("utf-8", errors="backslashreplace")
    bad = []
    for line in text.splitlines():
        m = PEVERIFY_ERROR_RE.match(line.strip())
        if m and not any(a in m.group("rest") for a in PEVERIFY_ALLOWED):
            bad.append("peverify: %s %s" % (short(m.group("where")), m.group("rest").strip()))
    return bad


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--ilverify")
    p.add_argument("--runtime-dir", action="append", default=[],
                   help="directory of reference assemblies (repeatable)")
    p.add_argument("--allow", help="file of allowed ILVerify error-code regexes")
    p.add_argument("--peverify", help="also run PEVerify (Windows)")
    p.add_argument("--skip", action="store_true", help="no verifier available; succeed")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("assembly")
    args = p.parse_args()

    if args.skip:
        print("msil-verify: skipped (no ilverify configured)")
        return 0
    allow = load_allow(args.allow) if args.allow else []
    bad, notes = run_ilverify(args, allow)
    if args.peverify:
        bad += run_peverify(args)
    for n in notes:
        print(n)
    for b in bad:
        print(b, file=sys.stderr)
    if bad:
        print("msil-verify: %d disallowed verifier error(s) in %s"
              % (len(bad), os.path.basename(args.assembly)), file=sys.stderr)
        return 1
    if args.verbose:
        print("msil-verify: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```
`msil/utils/ilverify-allowed.txt`:
```
# ILVerify error codes that are expected in backend output (Design.md 1:
# "unverifiable, full-trust IL"). One full-match regex per line. Everything
# else, in particular the stack-shape codes below, fails the test:
#   StackUnderflow StackOverflow PathStackDepth PathStackUnexpected
#   StackUnexpected UninitStack MethodFallthrough ReturnEmpty ReturnMissing
#   BadJumpTarget BadBranch LocallocStackNotEmpty UnrecognizedLocalNumber
#   UnrecognizedArgumentNumber UnknownOpcode
# Add a code here only with a comment naming the construct that produces it.

# localloc, cpblk, initblk, calli unmanaged, ldind/stind through native int
Unverifiable
# .locals without 'init' (passes-frame.md 6.6)
InitLocals
# ldind.*/stind.* on a native int address
StackByRef
# unmanaged pointer types in signatures or locals
UnmanagedPointer
```
(`glob` is imported but unused in the prototype; remove it.)

### A.11 `.github/workflows/msil-ci.yml`
```yaml
# CI for the MSIL fork (llvm/docs/MSIL/Design.md). Only workflows named
# msil-*.yml are meant to run in this repository; the inherited llvm-project
# workflows are disabled with .github/msil/disable-upstream-workflows.sh.
name: MSIL CI

permissions:
  contents: read

on:
  push:
    branches:
      - 'msil-main'
      - 'msil-milestone*'
  pull_request:
    branches:
      - 'msil-*'
      - 'msil/**'
    paths-ignore:
      - 'llvm/docs/**'
  schedule:
    # Nightly, on the default branch only (GitHub runs schedules from there).
    - cron: '17 5 * * *'
  workflow_dispatch:
    inputs:
      full:
        description: 'Also run check-llvm and check-clang'
        type: boolean
        default: false

concurrency:
  group: ${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: ${{ github.event_name == 'pull_request' }}

env:
  DOTNET_CLI_TELEMETRY_OPTOUT: '1'
  DOTNET_NOLOGO: '1'
  FULL_CHECK: ${{ github.event_name == 'schedule' || inputs.full == true }}
  # Pinned tool versions (see msil/utils/tools/msil-tools.proj).
  ILVERIFY_VERSION: '10.0.12'

jobs:
  linux:
    name: Linux x86_64 (.NET 8 and 10)
    if: github.repository_owner == 'alethic'
    runs-on: ubuntu-24.04
    timeout-minutes: 330
    steps:
      - name: Checkout
        uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false
          fetch-depth: 1

      - name: Free disk space
        run: |
          sudo rm -rf /usr/local/lib/android /opt/ghc /usr/local/.ghcup /usr/share/swift
          df -h /

      - name: Install build tools
        run: |
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends ninja-build clang lld

      - name: Set up .NET
        uses: actions/setup-dotnet@v5 # TODO: pin to a commit SHA like the other actions
        with:
          dotnet-version: |
            8.0.x
            10.0.x

      - name: Cache NuGet packages and dotnet tools
        uses: actions/cache@55cc8345863c7cc4c66a329aec7e433d2d1c52a9 # v6.1.0
        with:
          path: |
            ~/.nuget/packages
            ~/.dotnet/tools
          key: msil-dotnet-tools-${{ runner.os }}-${{ hashFiles('msil/utils/tools/msil-tools.proj') }}-${{ env.ILVERIFY_VERSION }}

      - name: Fetch ilasm (NuGet) and ILVerify (dotnet tool)
        run: |
          dotnet restore msil/utils/tools/msil-tools.proj -p:MsilToolsRid=linux-x64
          # NuGet extraction is not guaranteed to keep the executable bit.
          chmod +x ~/.nuget/packages/runtime.linux-x64.microsoft.netcore.il*asm/*/runtimes/linux-x64/native/il*asm
          dotnet tool update --global dotnet-ilverify --version "$ILVERIFY_VERSION"
          echo "$HOME/.dotnet/tools" >> "$GITHUB_PATH"

      - name: Set up sccache
        uses: hendrikmuhs/ccache-action@f09c25b45002a07be2955cbe52e8cee55643f89d # v1.2.24
        with:
          key: msil-linux
          max-size: 2G
          variant: sccache

      - name: Configure
        run: >
          cmake -G Ninja -S llvm -B build
          -C msil/cmake/caches/MSIL.cmake
          -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
          -DLLVM_USE_LINKER=lld
          -DCMAKE_C_COMPILER_LAUNCHER=sccache
          -DCMAKE_CXX_COMPILER_LAUNCHER=sccache
          -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
          "-DLLVM_LIT_ARGS=-v --time-tests --xunit-xml-output=${{ github.workspace }}/build/lit-results.xml"

      - name: Build
        run: >
          ninja -C build
          clang llc FileCheck not count split-file llvm-config llvm-mc
          TargetParserTests MCTests MSILTests

      - name: Unit tests (MSIL-related)
        run: build/bin/llvm-lit -sv build/test/Unit "--filter=MSIL|TripleTest|TargetRegistry"

      - name: check-msil (.NET 8, -O0 and -O2, native oracle)
        run: ninja -C build check-msil
        env:
          LIT_OPTS: --param msil_dotnet_runtime=8 --param msil_native_oracle=required

      - name: Execution tests (.NET 10, full-opt JIT)
        run: >
          build/bin/llvm-lit -sv build/tools/msil/test
          --param msil_dotnet_runtime=10
          --param msil_dotnet_env=DOTNET_TieredCompilation=0

      - name: Nightly extras (all opt levels, upstream regression)
        if: env.FULL_CHECK == 'true'
        run: |
          build/bin/llvm-lit -sv build/tools/msil/test --param msil_opt_levels=O0,O1,O2,O3,Os,Oz
          ninja -C build check-llvm check-clang

      - name: Upload test outputs
        if: failure()
        uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7.0.1
        with:
          name: msil-linux-test-output
          path: |
            build/lit-results.xml
            build/tools/msil/test/**/Output/
            build/test/CodeGen/MSIL/Output/
          retention-days: 7

  windows:
    name: Windows x64 (.NET 8 and .NET Framework 4.8)
    if: github.repository_owner == 'alethic'
    runs-on: windows-2022
    timeout-minutes: 330
    defaults:
      run:
        shell: pwsh
    steps:
      - name: Keep LF line endings
        run: git config --global core.autocrlf false

      - name: Checkout
        uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false
          fetch-depth: 1

      - name: Set up the MSVC environment (LIB/INCLUDE for the native oracle)
        uses: ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756 # v1.13.0
        with:
          arch: x64

      - name: Set up .NET
        uses: actions/setup-dotnet@v5 # TODO: pin to a commit SHA
        with:
          dotnet-version: |
            8.0.x
            10.0.x

      - name: Install ninja and ILVerify
        run: |
          choco install ninja --no-progress
          dotnet tool update --global dotnet-ilverify --version $env:ILVERIFY_VERSION
          "$env:USERPROFILE\.dotnet\tools" | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append

      - name: Set up sccache
        uses: hendrikmuhs/ccache-action@f09c25b45002a07be2955cbe52e8cee55643f89d # v1.2.24
        with:
          key: msil-windows
          max-size: 3G
          variant: sccache

      - name: Configure
        run: >
          cmake -G Ninja -S llvm -B build
          -C msil/cmake/caches/MSIL.cmake
          -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
          -DCMAKE_C_COMPILER_LAUNCHER=sccache
          -DCMAKE_CXX_COMPILER_LAUNCHER=sccache
          -DCMAKE_POLICY_DEFAULT_CMP0141=NEW
          -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
          -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
          "-DLLVM_LIT_ARGS=-v --time-tests --xunit-xml-output=${{ github.workspace }}/build/lit-results.xml"

      - name: Build
        run: >
          ninja -C build
          clang llc FileCheck not count split-file llvm-config llvm-mc
          TargetParserTests MCTests MSILTests

      - name: Unit tests (MSIL-related)
        run: python build/bin/llvm-lit.py -sv build/test/Unit "--filter=MSIL|TripleTest|TargetRegistry"

      - name: check-msil (.NET 8 + .NET Framework, native oracle, PEVerify)
        run: ninja -C build check-msil
        env:
          LIT_OPTS: --param msil_dotnet_runtime=8 --param msil_native_oracle=required --param msil_run_netfx=1

      - name: Upload test outputs
        if: failure()
        uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7.0.1
        with:
          name: msil-windows-test-output
          path: |
            build/lit-results.xml
            build/tools/msil/test/**/Output/
            build/test/CodeGen/MSIL/Output/
          retention-days: 7
```
Before M1.4 there is no `MSILTests` target. Drop it from the build lists until `llvm/unittests/Target/MSIL` exists. Also consider adding `LLVM_ENABLE_MACHINE_VERIFIER: '1'` to the job `env`.

### A.12 `.github/msil/disable-upstream-workflows.sh`
```bash
#!/usr/bin/env bash
# Disable every GitHub Actions workflow inherited from llvm/llvm-project in this
# fork, leaving only .github/workflows/msil-*.yml active. This changes repository
# settings, not files, so it never conflicts with upstream merges. Re-run it after
# each upstream sync: newly added upstream workflows start out enabled.
#
#   .github/msil/disable-upstream-workflows.sh [owner/repo]     (needs gh auth, actions:write)
set -euo pipefail
repo="${1:-alethic/llvm-msil}"
gh workflow list --repo "$repo" --all --limit 500 --json id,path,state \
  --jq '.[] | select(.state == "active") | select(.path | test("^\\.github/workflows/msil-") | not) | "\(.id) \(.path)"' |
while read -r id path; do
  echo "disabling $path"
  gh workflow disable "$id" --repo "$repo"
done
```

### A.13 `msil/utils/tools/msil-tools.proj` (evaluated, not restored)
```xml
<!--
  Fetches the IL tools the MSIL tests need on hosts without .NET Framework
  (Linux, macOS). Nothing is built; `dotnet restore` only downloads:

    dotnet restore msil/utils/tools/msil-tools.proj -p:MsilToolsRid=linux-x64

  ilasm then lives at
    $NUGET_PACKAGES/runtime.<rid>.microsoft.netcore.ilasm/<ver>/runtimes/<rid>/native/ilasm
  which msil/test/lit.cfg.py finds on its own. ILVerify is a dotnet tool:
    dotnet tool install (global) dotnet-ilverify, version MsilILVerifyVersion
-->
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <MsilToolsRid Condition="'$(MsilToolsRid)' == ''">linux-x64</MsilToolsRid>
    <!-- Keep in step with the runtime CI tests on (Design.md 7.1: net8.0). -->
    <MsilILAsmVersion Condition="'$(MsilILAsmVersion)' == ''">8.0.0</MsilILAsmVersion>
    <MsilILVerifyVersion>10.0.12</MsilILVerifyVersion>
    <IsPackable>false</IsPackable>
    <DisableImplicitNuGetFallbackFolder>true</DisableImplicitNuGetFallbackFolder>
  </PropertyGroup>
  <ItemGroup>
    <PackageDownload Include="runtime.$(MsilToolsRid).Microsoft.NETCore.ILAsm" Version="[$(MsilILAsmVersion)]" />
    <PackageDownload Include="runtime.$(MsilToolsRid).Microsoft.NETCore.ILDAsm" Version="[$(MsilILAsmVersion)]" />
  </ItemGroup>
</Project>
```
(An XML comment may not contain `--`; the first draft failed with MSB4025 for that reason.)

### A.14 `msil/utils/update-expected.py`
```python
#!/usr/bin/env python3
"""Create or check <stem>.expected files for MSIL execution tests.

The oracle is a native build of the same source with the just-built clang for
the host triple, run through msil-run.py --native, so the transcript format is
identical to what the lit tests compare against.

    update-expected.py --clang build/bin/clang msil/test/M1/03-memory/*.c
    update-expected.py --clang build/bin/clang --check msil/test/M1      (CI)

Windows: run from a VS x64 developer prompt (link.exe and LIB are needed).
Only <stem>.expected is written; a hand-maintained <stem>.<os>.expected
override is reported and left alone.
"""

import argparse
import os
import platform
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TEST_ROOT = os.path.join(os.path.dirname(HERE), "test")


def default_target():
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64",
            "aarch64": "aarch64"}.get(platform.machine().lower(), "x86_64")
    return {"Windows": arch + "-pc-windows-msvc", "Darwin": arch + "-apple-darwin"}.get(
        platform.system(), arch + "-unknown-linux-gnu")


def collect(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, files in os.walk(p):
                dirs[:] = [d for d in dirs if d not in ("Inputs", "Output")]
                for f in sorted(files):
                    if f.endswith(".c"):
                        yield os.path.join(root, f)
        else:
            yield p


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--clang", required=True)
    ap.add_argument("--target", default=default_target())
    ap.add_argument("--cflags", default="-ffreestanding",
                    help="must match %%{msil_cflags} of the tests' directory")
    ap.add_argument("--opt", default="-O0")
    ap.add_argument("--check", action="store_true", help="compare, do not write")
    ap.add_argument("tests", nargs="+")
    args = ap.parse_args()

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for src in collect(args.tests):
            stem = os.path.splitext(src)[0]
            osname = platform.system().lower()
            if os.path.exists(stem + "." + osname + ".expected"):
                print("skip (has %s override): %s" % (osname, src))
                continue
            exe = os.path.join(tmp, os.path.basename(stem) + ".exe")
            cc = [args.clang, "--target=" + args.target, args.opt] + args.cflags.split() + [
                "-I", os.path.join(TEST_ROOT, "Inputs"), src, "-o", exe]
            if subprocess.call(cc) != 0:
                print("FAIL (native compile): %s" % src)
                failures += 1
                continue
            run = [sys.executable, os.path.join(HERE, "msil-run.py"), "--native", exe]
            if args.check:
                rc = subprocess.call(run[:-1] + ["--expect", stem, exe])
                print(("ok: " if rc == 0 else "MISMATCH: ") + src)
                failures += rc != 0
            else:
                out = subprocess.run(run, stdout=subprocess.PIPE, check=True).stdout
                with open(stem + ".expected", "wb") as f:
                    f.write(out.replace(b"\r\n", b"\n"))
                print("wrote %s.expected" % stem)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
```
Extending it to `.ll` tests (`-Wno-override-module`) is a small follow-up.

### A.15 `msil/test/README.md` (outline)
1. What runs (the pipeline diagram from the top of `lit.cfg.py`) and what makes a test UNSUPPORTED.
2. Adding a program test: put `<name>.c` with `// RUN: %{msil_c}` in the right `M1/NN-*` directory, run `update-expected.py`, review the `.expected`, and run the directory.
3. Parameters (§6.3 table) and features.
4. The transcript format and per-OS overrides.
5. Verification policy and the allow list.
6. Reproducing a failure from `lit -v` output.

---

## Appendix B: ILVerify 10.0.12 `VerifierError` codes (read from `ILVerify.dll` by reflection)
`None UnknownOpcode MethodFallthrough FallthroughException FallthroughIntoHandler FallthroughIntoFilter LeaveIntoTry LeaveIntoHandler LeaveIntoFilter LeaveOutOfFilter LeaveOutOfFinally LeaveOutOfFault Rethrow Endfinally Endfilter BranchIntoTry BranchIntoHandler BranchIntoFilter BranchOutOfTry BranchOutOfHandler BranchOutOfFilter BranchOutOfFinally ReturnFromTry ReturnFromHandler ReturnFromFilter BadJumpTarget PathStackUnexpected PathStackDepth ThisUninitStore ThisUninitReturn LdftnCtor StackUnexpected StackUnexpectedArrayType StackOverflow StackUnderflow UninitStack ExpectedIntegerType ExpectedFloatType ExpectedNumericType StackObjRef StackByRef StackMethod UnrecognizedLocalNumber UnrecognizedArgumentNumber ExpectedTypeToken TokenResolve ExpectedMethodToken ExpectedFieldToken Unverifiable StringOperand ReturnPtrToStack ReturnVoid ReturnMissing ReturnEmpty ExpectedArray ValueTypeExpected TypeAccess MethodAccess FieldAccess ExpectedStaticField InitOnly CallVirtOnValueType CtorExpected CtorSig ArrayByRef ByrefOfByref CodeSizeZero TailCall TailByRef TailRet TailRetVoid TailRetType TailStackEmpty MethodEnd BadBranch Volatile Unaligned CallAbstract TryNonEmptyStack FilterOrCatchUnexpectedStack FinOrFaultNonEmptyStack DelegateCtor DelegatePattern BoxByRef EndfilterStack DelegateCtorSigI DelegateCtorSigO CatchByRef ThrowOrCatchOnlyExceptionType LdvirtftnOnStatic CallVirtOnStatic InitLocals CallCtor ExpectedValClassObjRefVariable ReadOnly Constrained UnsatisfiedMethodInst UnsatisfiedMethodParentInst UnsatisfiedFieldParentInst UnsatisfiedBoxOperand ConstrainedCallWithNonByRefThis ReadonlyUnexpectedCallee ReadOnlyIllegalWrite TailCallInsideER BackwardBranch NewobjAbstractClass UnmanagedPointer LdftnNonFinalVirtual ThisMismatch InterfaceImplHasDuplicate InterfaceMethodNotImplemented LocallocStackNotEmpty`

Note `ExpectedNumericType`: `conv.u` on an `ldsflda` (RVA-field) address produces it (E1). If the backend ever takes the address of an RVA field directly instead of using `segment_base + offset` (Design 4.1), it must either add this code to the allow list with a comment, or avoid the pattern.
