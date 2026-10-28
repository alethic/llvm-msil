# MSIL execution tests

This directory holds the end-to-end tests of the MSIL backend
(`llvm/docs/MSIL/Design.md`): C or LLVM IR is compiled for an MSIL triple,
assembled with ilasm, verified, force-JIT-compiled, run on .NET, and its
transcript is compared with a checked-in expected file. Since M14, programs
of several files, archives and stock-clang bitcode are also linked by
`msil-ld` (`M2/`, `%{msil_prog}`), since M15 they call native libraries
through the P/Invokes that `msil-ld` binds, and since M20 `msil-ld` links
them from CIL objects (`clang -c`) and writes their image itself, with no
ilasm, which the `obj` pipeline also does for every one-file test of `M1/`.
Since M21 `msil-ld` binds the .NET base class library against reference
assemblies, which `--param msil_references` gives every MSIL link (the
reference legs) and the tests of `M2/05-refs/` give their own links.
Since M22 tests also build library assemblies with `msil-ld -shared`
(`%{msil_shlibs}`) and link programs, other libraries and C# programs
(`%msil_csc`) against them (`M2/04-shared/`). Since M23, the Phase 2 exit,
CIL objects are the default pipeline: with no parameter every test builds
with `clang -c` (or `llc -filetype=obj`) and runs `msil-ld`'s image, and
`clang -S` plus ilasm is the oracle route of `--param msil_pipeline=asm`;
`--param msil_also_run` runs every verified image on further .NET runtimes;
and `Programs/` holds the exit corpus, nine third-party programs of several
files and c-testsuite's libc-free tests, built from a cache that
`msil/utils/corpus/fetch.py` fills outside the repository.
It is an LLVM external
project (`-DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil`,
set by `msil/utils/build/msil-configure.ps1` and `msil/cmake/caches/MSIL.cmake`),
so it needs no upstream CMake change. Plan section 5.5 has the rules every test
follows.

## What runs

Under the default pipeline `obj` (since M23), `%{msil_c}` and `%{msil_ll}`
build their one source as a program of one file, as `%{msil_prog}` does
(below): `clang -c` (an IR file `llc -filetype=obj`), then `msil-ld`'s
image `prog.exe`, which is verified, force-JIT-compiled and run like every
image here. Under `--param msil_pipeline=asm`, the Phase 1 route that the
`asm-oracle` leg keeps, for every level of `--param msil_opt_levels`
(default `O0,O2`), `%{msil_c}` runs:

```text
prog.c --clang --target=<triple> -S-->  %t.<L>.il  --ilasm -dll-->  %t.<L>.dll
  --> msil-verify.py   ILVerify with ilverify-allowed.txt, PEVerify on Windows,
                       then both again on a neutralized copy (ildasm + ilasm)
  --> msil-jitall.py   JIT-compile every method (if jitall exists); the entry
                       point does not run, but <Module>::.cctor (data segment
                       and C constructors) does, where the program would run
  --> msil-run.py      run from a private directory, under a timeout and a memory
                       cap, and compare the transcript with prog[.<os>].expected
                       (the stem is %{msil_expect}, default %S/<test stem>)
  [--> also run]       msil-jitall.py and msil-run.py again on each runtime of
                       msil_also_run (M23), against the same expected file
  [--> round trip]     ildasm, ilasm and ildasm again (the .NET tools); both
                       listings must match (apart from the MVID, the image
                       base, the numbers in ildasm's I_<rva> data labels and,
                       since M21, an ldc.r4 float32 signaling NaN that ilasm
                       quieted), and the reassembled image runs against the
                       same .expected (msil_roundtrip=1)
  [--> native oracle]  the same source built natively must give the same transcript
  [--> .NET Framework] ilasm -exe and run on .NET Framework 4.8
```

The native oracle (`--param msil_native_oracle=1` or `required`) builds the
test for the native triple with the same clang and `%{msil_cflags}` (and a C
test's `%{msil_copts}`): on
Windows linked by the MSVC linker with the compiler-rt builtins of the Visual
Studio LLVM (`msil_native_rtlib`, for the `__int128` helpers `__udivti3`,
`__fixdfti`, ... that a native build calls), on Linux by the system linker
with `-lm`, and with `msil_runner=wsl` by the *E7 Linux oracle*: compiled on
Windows for `x86_64-unknown-linux-gnu` (`-fno-pic -c`), linked by `ld.lld`
with `msil/utils/linux-oracle/start.c` against the distribution's
`libm.so.6`, `libc.so.6` and `libgcc_s.so.1` (copied into
`msil_linux_sysroot` by `msil/utils/make-linux-sysroot.py`; `--as-needed`), and
run inside WSL by `msil-run.py --native --runner wsl`. WSL needs no compiler,
headers or root for it. `llvm/docs/MSIL/GettingStarted.md` section 7 has the
details and the host setup.

jitall runs on the host with the local runner and inside WSL (on the WSL .NET
runtime the programs use: the private one of `msil/utils/msil-wsl-dotnet.sh`,
`$HOME/.dotnet-msil`, in every gate leg) with `msil_runner=wsl`: the module
initializer of a linux-triple program calls libc and libm, which exist only
there. ILVerify, PEVerify, ildasm and ilasm run on the host, except that
`--param msil_wsl_ilasm` makes `%ilasm` the Linux ilasm inside WSL (the
neutralized pass of msil-verify.py still reassembles with the host ilasm).

`%msil_canon` (M16) prints the canonical listing of an assembly: canonical
listing v1 of plan 5.7.10, the text of its metadata and IL without tokens,
heap offsets, RVAs, header forms or anything else that depends on how the
image was laid out, so two images of the same program compare equal whoever
wrote them. From M17 it is `llvm-cil-dump --canonical` (the tree's
`llvm-cil-dump`, `%msil_cil_dump`; feature `msil-cil-dump`), which runs on the
host, also with `msil_runner=wsl`. In M16, and still when there is no
`llvm-cil-dump` (but under `msil_require_verify=1` that is a configuration
error), it is `mdcanon` (`msil/utils/mdtools/mdcanon.cs`, also
`%msil_mdcanon`), which lit builds on configuration and runs on the host's
dotnet; the two print the same bytes. Its options (`--order=decl`,
`--forms=canon`, `--scopes=strip` and the six normalizations of plan 5.12.6,
such as `--params=drop`) remove the differences that a comparison allows.
`harness/canon-listing.test` pins it, `harness/canon-agree.test` compares the
two dumpers, and the oracles of later milestones compare with it.

`%{msil_ll}` is the same for hand-written IR run through `llc` (`O0`..`O3`
only; `.ll` files carry no target triple or datalayout). `llc` does no IPO, so
values passed to `noinline` functions stay opaque even at `-O2`.

Since M18 the `.il` that these steps assemble uses the shortest instruction
forms (short branches where the target is in reach, `ldc.i4.<n>` and
`ldc.i4.s`) and `nomangle cdecl` in every P/Invoke (plan Q26 and 5.10.11),
which the ilasm of each leg (the Framework's, the NuGet Windows one, the Linux
one) must accept; the tests themselves did not change, because they compare
transcripts, not IL.

`%{msil_prog}` (M14) builds a program of several files with
`msil/utils/msil-prog.py` (`%msil_prog_build`) in a fresh `%t.<L>.d` per level,
then checks and runs `prog.exe` as `%{msil_c}` does. Since M20 it compiles CIL
objects (`clang -c`, the pipeline `obj`) under `msil_pipeline=asm` and `obj`,
and bitcode (`clang -flto -c`, the pipeline `lto`) under `msil_pipeline=lto`;
every compile of both the MSIL and the native build gets `%{msil_cflags}
%{msil_copts}` (M23):

```text
%{msil_shlibs}      first (M22), in order: clang -shared, each against the
                      libraries built before it-->            shlib/<NAME>.dll
                      (?NAME: -Wl,--interface-only-->          shlib-iface/<NAME>.dll)
%{msil_srcs}        --clang -c (obj) / -flto -c (lto)-->       obj/<stem>.o
%{msil_stock_srcs}  --clang --target=<native> -flto -c-->      stock/<stem>.o
%{msil_archives}    --compiled as %{msil_srcs}, then
                      llvm-ar rcs-->                           ar/<NAME>/lib<NAME>.a
  --clang -<L> %{msil_ldflags} <objects> -Lar/<NAME> -l<NAME> %{msil_libs}
     %{msil_native_libs} shlib/<NAME>.dll... -o prog.exe (clang runs msil-ld,
     which also reads the driver's default libraries and writes the
     image)-->                                                 prog.exe
     (msil_link_output=il, lto only: -Wl,--lto-emit-asm -o prog.il, then
     ilasm -exe, as every link did in M14-M19)
  --> msil-verify.py, msil-jitall.py, msil-run.py   as for %{msil_c}, each
                        with --dep-dir <workdir>/shlib (msil-verify.py also
                        --also-verify-deps) --dep %{msil_deps} -- before the
                        program; msil-run.py compares with %{msil_expect}
  [--> also run]        msil-jitall.py and msil-run.py of the same prog.exe on
                        each runtime of msil_also_run (M23)
  [--> round trip]      of prog.exe (msil_roundtrip=1)
  [--> equivalence]     lto with msil_equiv=1: O4 (below) on equiv/prog.exe,
                        a -Wl,--no-gc-sections link, against ilasm of
                        equiv/prog.il, the same link's --lto-emit-asm text
  [--> native oracle]   msil-prog.py --mode native: the same sources, archives,
                        %{msil_native_libs} and %{msil_shlibs} built for the
                        native triple (clang -fuse-ld=lld; the E7 recipe in
                        WSL, with -rpath $ORIGIN), run with the same --dep
                        files and its own shlib/ directory
  [--> .NET Framework]  the same prog.exe, run by the Framework
```

`prog.exe` is an EXE image, so the one file runs under `dotnet exec` and on the
.NET Framework. Stock sources are compiled by the tree's clang for the native
triple (with `-fno-ms-volatile` on windows-msvc), as stock-clang bitcode (plan
Q12); natively every source is an ordinary one. The MSIL link reads the
driver's default libraries of the triple (M15): on a Windows triple the import
libraries of `LIB` (a VS developer prompt, or `msil-lit.ps1`), on a Linux
triple the E7 sysroot, which `%{msil_ldflags}` names as
`--sysroot=<msil_linux_sysroot>` (it is empty on Windows triples; it was
`-nodefaultlibs` in M14, when the default libraries only had to exist). A
test's own native libraries go in `%{msil_native_libs}`, which both builds
link, and the files a run needs next to the program (a test-built
`native.dll` or `libnative.so`) in `%{msil_deps}`, which `msil-verify.py`,
`msil-jitall.py` and every `msil-run.py` run copy there (`--dep`). On Linux,
locally and in WSL, `msil-run.py` also puts that directory first in
`LD_LIBRARY_PATH`, because a native data import (M20) loads its library with
`dlopen` of the bare name, which never searches the program's directory
(Status.md section 3, "Limits"; natively the E7 recipe's `-rpath $ORIGIN`).

`%{msil_shlibs}` (M22; default empty) names the shared libraries of a test,
`[?]NAME=SRC[,SRC...][;...]`, which `msil-prog.py --shlibs` builds before
anything else, in that order, each linked against the latest build of every
other name, with the program's `--cflags` and `--ldflags` (`%{msil_ref_flags}`
included; no `%{msil_libs}` or `%{msil_native_libs}`); a `SRC` written
`stock:SRC` is a stock source. `?NAME` builds only the interface of `NAME`
(`-Wl,--interface-only`, into `shlib-iface/`), for a cycle: `?b=b.c;a=a.c;b=b.c`
links `b`'s interface, then `a` against it, then the real `b` against `a`. The
program links every real library after its archives. Natively the same
specification gives, on Windows, `-shared -fuse-ld=lld` DLLs with their import
libraries (an interface is the same link with `-Wl,/force:unresolved`, of
which only the import library is kept), every compile with
`-fms-runtime-lib=dll`, so that all DLLs share one C runtime as MSIL
assemblies do; and, for the E7 oracle and the Linux driver, `-fPIC` objects
linked `-shared -Bsymbolic` into `shlib/lib<NAME>.so` (`-soname lib<NAME>.so`;
no interfaces: the dynamic loader resolves cycles), with `-rpath $ORIGIN` on
the program; `-Bsymbolic` gives the CLR's semantics, in which a library's own
references never bind to the program. The build directory always has a
`shlib/` directory, empty without libraries, which every run, jitall and
verification of the composite gets as `--dep-dir`; `msil-verify.py` verifies
each library in it too (`--also-verify-deps`). `%{msil_det}` compares the
libraries as well. `%{msil_shlibs}` with `msil_link_output=il` is a usage
error of `msil-prog.py`, because a library has no `--lto-emit-asm` text, so
the tests that set it (`04-shared/` but `csharp-consumer`, and the three
`08-semantics/p2c-*` rows) say `UNSUPPORTED: msil-link-output=il` and are
UNSUPPORTED under `msil_pipeline=lto` with `msil_link_output=il`.
`%msil_csc SRC --out EXE [--reference DLL]... [--refs net8|net48]
[--unsafe]` (feature `msil-csc`) compiles a C# program with the newest .NET
SDK's csc (`msil/utils/jitall/build-jitall.py --csharp`): `net8` (the default)
against the newest 8.x reference pack, writing `<stem>.runtimeconfig.json`,
`net48` against the .NET Framework 4.8 reference assemblies and their
`Facades`; `$MSIL_REF_NET8` and `$MSIL_REF_NET48` name other packs, and lit
passes both variables to tests when they are set.

`--param msil_pipeline` (feature `msil-pipeline=<p>`) picks how `%{msil_c}`
and `%{msil_ll}` build: `obj` (M20, the default since M23) builds the one
source as `%{msil_prog}` does through CIL objects, a C file with `clang -c`
and an IR file with `llc -filetype=obj` (`%{msil_llc}`), into `msil-ld`'s
image (every leg that does not name another pipeline: `net8`, `more-levels`,
`wsl-net10`, ...); `asm` is the Phase 1 flow above (the `asm-oracle`,
`modern-ilasm` and `wsl-ilasm` legs, and the default before M23); `lto`
builds the one C source as `%{msil_prog}` does through `clang -flto -c` (the
`lto` legs), while `%{msil_ll}` stays on llc text. Under the default, a
missing `msil-ld` or `llvm-ar` takes away the feature `msil-exec` with a
note, so the execution tests are UNSUPPORTED; an explicit `obj` or `lto` and
`msil_require_link=1` make it a configuration error. The feature
`msil-prog-pipeline=<p>` names the pipeline that `%{msil_prog}` actually uses:
`obj` under `asm` and `obj` since M20 (`lto` under every pipeline in
M14-M19), `lto` under `lto`. A check that depends on how `%{msil_prog}` builds
(an object-only or LTO-only diagnostic or warning, `--lto-emit-asm` text, a
`--check-prefix=OBJ` or `LTO` block) is guarded by
`%if msil-prog-pipeline=<p> %{ ... %}` or `REQUIRES: msil-prog-pipeline=<p>`,
never by `msil-pipeline=<p>`, which decides only `%{msil_c}` and `%{msil_ll}`
(whose `ASM` checks stay under `%if msil-pipeline=asm`).
`--param msil_link_output` is `pe` (the default since M20: `msil-ld` writes
the image) or `il` (`lto` only: `msil-ld` writes `--lto-emit-asm` text, which
`ilasm -exe` assembles; under `asm` and `obj` it is a configuration error,
because CIL objects have no text link).

`--param msil_equiv=1` (feature `msil-equiv`, M20) adds the equivalence
oracles of plan 5.12.6, which compare `msil-ld`'s images canonically
(`%msil_canon`) with ilasm's images of the text that the same compiler
writes. Under `obj`, `%{msil_c}` and `%{msil_ll}` also link the objects again
with `-Wl,--no-gc-sections` into `equiv/prog.exe` and build the `-S` (or llc)
text of the same source with the same assembly name into `asm/prog.il`, which
ilasm assembles into `asm/prog.exe`: O3 requires the two images to list alike
(`%msil_equiv --oracle O3`), and O6 requires the PEVerify `/md` messages of
the image that runs to be among those of `asm/prog.exe` (`msil-verify.py
--md-baseline`; skipped with a note where PEVerify is missing). Under `lto`
with `pe`, `%{msil_prog}` runs O4: `equiv/prog.exe`, a `--no-gc-sections` LTO
link, against ilasm's image of `equiv/prog.il`, the same link's
`--lto-emit-asm` text. A test that a `skip` line of `equiv-allowed.txt`
covers (for the composite's triple and level, when the line names them)
builds neither file and prints `<oracle> skipped: <reason>`; the rest of the
test runs as usual. Since M21 a `skip O6` line works the same way for O6:
the composite passes `msil-verify.py --md-baseline` the file and the test's
`--test`, `--triple` and `--level` (`--equiv-skip`), which prints `O6
skipped: <reason>` for a covered test and compares as before for every
other one. `msil_equiv=1` under `asm`, or under `lto` with
`il`, is a configuration error. `--param msil_scale_tests=1` (feature
`msil-scale-tests`, M20) enables the tests at the CLR's per-type limits
(`M2/00-link/spill-limits.test`), which only `lit-legs.ps1`'s `net8` leg sets
(its `obj` leg before M23).

`--param msil_also_run=<entry>[,<entry>...]` (M23, feature `msil-also-run`)
repeats, after the main run of every composite, `msil-jitall.py` and
`msil-run.py` of the same verified image once per entry: `<major>` on
Microsoft.NETCore.App `<major>.x` of the runner, with its own runtimeconfig,
and `<major>+tc0` also with `DOTNET_TieredCompilation=0`; each compares with
the same expected file and writes `<t>.<L>.also-net<major>[-tc0].out`. The
leg's `msil_dotnet_env` applies to these runs too, and the .NET Framework run
stays `msil_run_netfx=1`. An entry of another form, one named twice and, under
`msil_require_exec=1`, a runtime that the runner lacks are configuration
errors (without it the entry is left out with a note). `net8` passes
`10,8+tc0`, `wsl-net10` `8,10+tc0`, and `lto`, `programs`, `programs-lto`
and `refs-net8` `10` (`8` in the WSL legs), so one verified build serves
.NET 8, .NET 10 and the optimizing JIT.

A test with explicit RUN lines gets the same runs from the substitution
`%{msil_also_run_check}` (M23): it REDEFINEs the placeholder
`%{msil_also_run_image}` to its image (the default,
`msil-also-run-image-not-set`, makes a test that forgets it fail) and appends
`%{msil_also_run_check}` after its main run's FileCheck (the expansion begins
with `&&`). Per `msil_also_run` entry, in the parameter's order, it expands to
one `msil-jitall.py` (where `msil-jitall` holds), one `msil-run.py` into
`<image>.also-net<major>[-tc0].out` and one `FileCheck --match-full-lines %s`
of that transcript, with the runtimeconfig and environment of the
composites' runs, and without `msil_also_run` to nothing;
`harness/also-run.test` pins both forms. The three trap tests of `M1/`
(`01-int/trap-call-before-div.ll`, `01-int/trap-call-before-rem.ll` and
`03-control/trap.ll`, which have no native oracle) use it in place of
their own `DOTNET_TieredCompilation=0` and .NET 10 runs, so they follow each
leg's folded runtimes (`net8` `10,8+tc0`, `wsl-net10` `8,10+tc0`,
`lit-legs.sh`'s `net8` also `10+tc0`).

`%{msil_copts}` (M23, default empty) is a test's extra compile flags, which
`%msil_prog_build` passes inside `--cflags='%{msil_cflags} %{msil_copts}'` to
every compile of both builds of `%{msil_prog}` and `%{msil_det}`, and which
every build of `%{msil_c}` gets after `%{msil_cflags}` on every pipeline (the
`asm` route's `-S` compile and native oracle, and the `asm` reference image
of O3 and O6 under `msil_equiv=1`, included); `%{msil_ll}` takes none,
because llc has no C flags (its native oracle compiles the IR). It may not
contain a single quote. `%{msil_expect}` (M23) is the stem of the expected file of
every composite's runs (main, native, .NET Framework, round trip and
`msil_also_run`), by default `%S/%{s:stem}`, so a directory of tests can
share one file (c-testsuite's `exit0.expected`). `%{msil_cflags}` stays a
placeholder inside the composites, so a directory may redefine it too
(c-testsuite uses `-std=gnu17 -w`).

The exit corpus (M23): `--param msil_corpus_dir=<dir>` names the cache that
`msil/utils/corpus/fetch.py` fills (default `$MSIL_CORPUS_CACHE`, else
`C:\llvm-msil-scratch\corpus-cache` on Windows and `~/.cache/msil-corpus`
elsewhere; an empty value, or a path with a blank, disables the corpus).
The feature `msil-corpus` holds when `<cache>/msil-corpus.stamp` was written
for the checked-in `msil/utils/corpus/manifest.json` (its SHA-256 over the
raw bytes, which `msil/.gitattributes` keeps LF on every host), and
`msil-corpus-<name>` for every tree the stamp lists with the manifest's
tree hash whose directory `<cache>/<name>-<ref>` exists; `%{msil_corpus}` is
the cache with `/` separators, for `%{msil_srcs}` and `%{msil_archives}`.
Without `msil-corpus` lit notes why and `Programs/` is UNSUPPORTED; with
`--param msil_require_corpus=1` (the corpus legs) that is a configuration
error naming `python msil/utils/corpus/fetch.py --cache <dir>`, and so is an
entry the stamp lacks unless `--param msil_corpus_skip=<name>[,<name>...]`
names it, whose tests are then UNSUPPORTED with a note (a name the manifest
does not have is a configuration error).

`--param msil_references` (M21; feature `msil-ref=<name>`) is `none` (the
default: `msil-ld` binds through its built-in `[mscorlib]` table), `net48`,
`net8`, `net10` or the path of a reference assembly or of a directory of
them (feature `msil-ref=path`). Anything but `none` makes
`%{msil_ref_flags}` `-Wl,--reference=<dir>`, a placeholder inside
`%{msil_ldflags}`, so it reaches the MSIL side of every link (`%{msil_prog}`,
and `%{msil_c}` and `%{msil_ll}` under `obj` and `lto`) and never the native
oracle; a test that needs references of its own `REDEFINE`s
`%{msil_ref_flags}` (empty, or `-Wl,--reference=%{msil_ref_dir_<pack>}`).
lit finds the three packs at configuration time, prints `msil: reference
packs: ...`, and sets the feature `msil-refpack=<name>` and
`%{msil_ref_dir_<name>}` (quoted, so `/<file>` may follow it) for each one
it finds: `net48` is `$MSIL_REF_NET48`, else the .NET Framework 4.8
reference directory (`Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8`
under `%ProgramFiles(x86)%`), else `build/.NETFramework/v4.8` of the NuGet
package `microsoft.netframework.referenceassemblies.net48` at
`MsilNet48RefAssembliesVersion` of `msil/utils/tools/msil-tools.proj`;
`net8` and `net10` are `$MSIL_REF_NET8` and `$MSIL_REF_NET10`, else
`ref/net<N>.0` of the newest `Microsoft.NETCore.App.Ref` `<N>.x` pack of the
dotnet installation. A path that names a .NET pack (a path that is, or is a
directory holding, `System.Runtime.dll`, such as
`<dotnet>/packs/Microsoft.NETCore.App.Ref/<v>/ref/net<N>.0`) is handled as
`net<N>`, `<N>` being the major version of that `System.Runtime`'s Assembly
row (5 or more: the .NET Framework facade's `System.Runtime` and .NET Core
3's are 4.x, and such a path is any other path); lit prints `msil:
references: path (<path>), a .NET <N> pack`. Any other path (the .NET
Framework's reference directory, a directory of your own) keeps PEVerify. A
`System.Runtime.dll` whose version lit cannot read, a directory one of
those variables names without the pack's core library (`mscorlib.dll`,
`System.Runtime.dll`), a pack that `msil_references` names but lit does not
find, a path that does not exist, any value other than `none` under
`msil_pipeline=asm` (no link), `msil_run_netfx=1` with `net8`, `net10` or a
path to a .NET pack (such images do not load on the .NET Framework), and a
programs' runtime older than the pack's .NET (`net10` with
`msil_dotnet_runtime=8`, say) are configuration errors, and so is
`msil_pipeline=lto` with `msil_equiv=1` under `net8`, `net10` or a path to a
.NET pack: O4 would compare the `--lto-emit-asm` text, which is never bound
(plan 5.11.8), with an image whose data-import helper calls `NativeLibrary`.
With references, O3 and O4 also allow the entry `scopes`, and with `net8`,
`net10` or a path to a .NET pack `%msil_verify` passes the pack as
`--runtime-dir` and `--skip-peverify "bound to .NET <n> references"`:
ILVerify checks the image against the assemblies it is bound to, and
PEVerify, which cannot load such an image, is skipped with that line (O6
with it).

Most gate and CI legs run the default levels `O0,O2`. The `more-levels` leg
runs `M1/` again with `--param msil_opt_levels=O1,O3,Os,Oz` on .NET 8 in every
gate run and in both CI jobs (the gate and the Windows job on
`x86_64-pc-windows-msvc-msil`, the Linux job on
`x86_64-unknown-linux-gnu-msil`), so every `%{msil_c}` test runs at all six
levels and every `%{msil_ll}` test at `O0`..`O3`. The `utc` leg is not part
of this suite: `msil/utils/run-utc-tests.py` runs six tests of
`llvm/test/tools/UpdateTestChecks/update_llc_test_checks` (`msil-basic`,
`x86-basic`, `x86-mir-only`, `x86-asm-mir-mixed`, `x86-constant-pool`,
`asm-show-inst`) without lit, which reports them UNSUPPORTED on Windows
(`llvm/test/tools/UpdateTestChecks/lit.local.cfg` sets the `llc-binary`
feature they require only when a file named exactly `llc` exists). Neither is
the `encoder-parity` leg (M18, Windows only; since M23 one of the `-Full` legs):
`msil/utils/mdtools/cli-corpus-check.py --mode encoder` compiles every C
program of `M1/` that has an `.expected` file at `O0` and `O2` with
`clang -S -emit-llvm` and `llc -show-mc-encoding`, assembles the same text
with the .NET Framework ilasm, and requires the bytes that the encoding
comments give for each method to equal ilasm's (code bytes, the token at each
fixup and `.maxstack`), for at least 2,600 methods (2,819 at M18); its log is
`build/lit-results/encoder-parity.log`. Nor are the `obj-text` and
`obj-text-wsl` legs (M19): `obj-text` (Windows only) runs
`cli-corpus-check.py --mode object` over every C program of `M1/` with an
`.expected` file at `O0` and `O2`, and over `M1/11-programs` for
`x86_64-unknown-linux-gnu-msil`. It compiles each with `clang -c` into a CIL
object twice, from two directories, and with
`-S -mllvm -msil-object-form`, which the .NET Framework ilasm assembles into
an object too, and requires the two compiles to give the same bytes, the
object to list exactly like ilasm's (`llvm-cil-dump --canonical
--params=drop --initlocals=ignore-without-locals --snan=quiet`, the
link-info record included) with the same `--link-info` lines, `mdcanon` and
both ildasms to open it, the netmodule envelope, the consistency rules C1-C5
and C7 between its metadata and its record (and between ilasm's object and
the same record), and the `llvm-nm` letters and `llvm-ar` index of its COFF
symbol view that the record gives. `obj-text-wsl` (`-Wsl`) runs
`--mode gnu-tools` at `O0` and `O2` over `M1/`, and over `M1/11-programs`
for `x86_64-unknown-linux-gnu-msil`: GNU `nm` and `ar` inside WSL must read
those objects as `llvm-nm` does. Their logs are
`build/lit-results/obj-text.log` and `obj-text-wsl.log`.

Every child process (ilasm, ildasm, ILVerify, PEVerify, jitall, dotnet, native
programs) runs under a wall-clock timeout (60 s) and a cap on committed memory
(4096 MiB): a Windows Job Object, or `RLIMIT_DATA` in a new session on POSIX.
The .NET 10 JIT was seen growing past 40 GB on bad IL
(`llvm/docs/MSIL/recon/cil-runtime.md`, e15).

What makes a test UNSUPPORTED: `M1/` needs the feature `msil-exec` (clang with
the MSIL target, a .NET runtime, and ilasm under `asm` or `msil-ld` and the
tree's `llvm-ar` under the default `obj`); `harness/` needs `msil-ilasm` and
`msil-dotnet`, and `harness/canon-listing.test` and `harness/canon-agree.test`
also `msil-cil-dump` and `msil-mdcanon`. Without
them `check-all` still passes. With `--param msil_require_verify=1` (every
gate) a missing ILVerify, ildasm, jitall, mdcanon, `llvm-cil-dump` (M17),
PEVerify (Windows), ilasm or .NET runtime for the selected runner is a
configuration error instead, also when an empty `--param msil_<tool>=` or
`$MSIL_<TOOL>` switched it off, so a gate cannot pass with every harness test
UNSUPPORTED; so is a failed mdcanon build. `--param msil_require_exec=1`
(every gate leg) does the same for `msil-exec`: `M1/` without clang, the MSIL
target, ilasm or a runtime is a configuration error instead of UNSUPPORTED.
`M2/` also needs the feature
`msil-ld` (the tree's `msil-ld`), its archive tests `msil-ar` (the tree's
`llvm-ar`), and most tests of `M2/05-refs/` the reference packs they link
against (`msil-refpack=net48`, `net8` or `net10`) and `msil-cil-dump`;
`--param msil_require_link=1` (every gate leg) makes a missing
`msil-ld` or `llvm-ar` a configuration error, and so do `msil_pipeline=lto` and
`obj`. `msil_equiv=1` needs `llvm-cil-dump` or `mdcanon` for `%msil_canon` (a
configuration error otherwise), and the tests of `M2/07-equiv/` require the
feature `msil-cil-dump` or `msil-mdcanon`. `M2/04-shared/csharp-consumer.test`
needs the feature `msil-csc` (the newest .NET SDK's csc), and
`--param msil_require_csc=1` makes a missing csc a configuration error.
The compile-only tests of `M2/09-hosted/` need only clang with the MSIL
target, and the feature `msil-glibc-sysroot` (glibc) or `msil-ucrt-headers`
(UCRT) of their C library; the `hosted` leg names the glibc sysroot, so there
a missing one is a configuration error. `Programs/` needs what `M2/` needs and
the feature `msil-corpus`, and each test the trees it builds from
(`msil-corpus-<name>`), so an entry left out of the cache on purpose is
UNSUPPORTED without a change to the tests; `msil_require_corpus=1` (the
corpus legs) makes a missing cache or entry a configuration error unless
`msil_corpus_skip` names it.

## Directories

`M1/` holds the milestone-1 tests (Design.md section 12), one directory per
capability in the order the milestones added them, so the lowest-numbered
failing directory names the earliest broken capability:

| Directory | Since | Covers |
|---|---|---|
| `00-smoke/` | M3 | return values, `argc`, leaf methods |
| `01-int/` | M4 | integer arithmetic of every width, `__int128` (its division inline since M12), compares, select, direct calls |
| `02-fp/` | M5 | `float`/`double` arithmetic, NaN compares, conversions and their rounding (to and from `__int128` since M12), min/max |
| `03-control/` | M6 | branches, loops, `switch` (dense, sparse, 64-bit), irreducible control flow, computed `goto`, traps |
| `04-frames/` | M7 | address-taken locals, local arrays, over-aligned frames, recursion with frames |
| `05-memory/` | M7 | loads and stores of every width, unaligned and volatile accesses, `memcpy`/`memset`/`memmove`, fences, bit builtins |
| `06-output/` | M8 | `puts`, `putchar` and the C math library through P/Invoke |
| `07-globals/` | M8 | globals, string tables, relocations, constructors, BSS; aliases and undefined weak symbols (M12) |
| `08-fnptr/` | M9 | function pointers, dispatch tables, callbacks, pointers to imports |
| `09-structs/` | M9 | structs by value and returned, bit-fields, unions, nested aggregates |
| `10-calls/` | M9 | K&R calls, many arguments, mutual and deep recursion; shared byval slots, empty `asm` with operands (`black_box`) and a Rust-shaped IR module (M12) |
| `11-programs/` | M10 | the Design.md section 12 program corpus: 20 complete programs (fib, sieve, quicksort, a string table with relocations, a function-pointer VM, ...) |

`M1/` has 143 tests: 99 C programs, 43 IR files (40 of them use
`%{msil_ll}`; the three trap tests pin their levels) and one `.test` that
compiles a C program with `-g`.

`M2/` (Phase 2, from M14) holds programs of several files that `msil-ld`
links (`%{msil_prog}`), in the tiers of plan 5.12.5:

| Directory | Since | Covers |
|---|---|---|
| `00-link/` | M14 | two or more files: calls and data across them, `static` functions and variables of one name, weak and strong definitions, inline functions (COMDATs), common symbols, constructor priorities across files, structs by value and returned across files, K&R definitions across files, `main` in a later file, a file with data only; since M20 more than 32 data relocations, unaligned, 4-byte and to functions, through the relocation loop (`reloc-loop`, whose 4-byte sites come from IR in an archive), and 65,600 functions and 65,600 variables at `-O0` past the per-type limits, into spill classes (`spill-limits`, `REQUIRES: msil-scale-tests`; since M21 in two generated files of 32,800 each, because one CIL object cannot pass the limits, and with its own verification steps under a 1,200-second limit, because verifying the 6.8 MB image takes minutes) |
| `01-archive/` | M14 | archives: lazy members, members with data only, members of one name in two archives, two archives that need each other (no group needed, as in lld), members that define `puts` and `fdim` and win over the C library |
| `02-lto/` | M14 | stock-clang bitcode for the native triple, also inside an archive, and `msil-ld`'s once-per-link warning about it; since M20, with `-flto` forced, LTO images (`lto-pe`, which also checks that the link runs `msil-ld` alone and writes a PE32+ EXE), ThinLTO (`thinlto`), `--lto-partitions=2` (`lto-partitions`), CIL objects mixed with MSIL bitcode, positional and in one archive (`mixed-objects-bitcode`), signature mismatches inside one file (`intra-tu-mismatch`: a warning exactly once at `-O0` and `-O2` for an `int` argument of a `long long` parameter, which the compile step cannot fold, and the integer-versus-floating-point error of a file compiled at `-O0`; plan 5.9.8 as clarified for M20), calls of one definition with one IR call type, each decided by its own `signext`, `zeroext` or `byval` attribute in both link orders at `-O0` and `-O2` (`lto-call-attrs`, Linux triples: `unsigned char` 200 and `signed char` -56 reach an `int` parameter widened as natively, with no diagnostic, and a `byval` struct call and a call one argument short stop the link with the error and the adapter warning, each exactly once), and `sret` and `byval` mismatches whose IR call type equals the definition's, which stop the link with plan 5.9.8's errors, each exactly once, at `-O0` and `-O2`, as the object path does (`lto-same-type-mismatch`, the `byval` case on Linux triples only) |
| `03-native/` | M15 | native libraries: C library functions outside the standalone table (`strlen`, `strcmp`, `atoi`, `malloc`, ...) and a program without output, glibc's versioned libm functions and its IFUNCs (Linux triple), `kernel32.lib`, `htons` imported by ordinal, `strdup` through an oldnames alias and the functions of `--ucrtbase-imports` (Windows triple), a `native.dll` or `libnative.so` that the test builds and ships (`%{msil_native_libs}`, `%{msil_deps}`), `-Wl,--print-imports` and the linked text's P/Invokes, and the named errors for an undefined function and for `-Bstatic` (with `OBJ` lines for the object path since M20); since M20 native variables: those of a test-built library, read and written (`data-import-testlib`), glibc's `stderr`, `environ` and `optind` (`data-import-glibc`, Linux) and `msvcrt.dll`'s `_iob` (`data-import-msvcrt`, Windows) |
| `04-shared/` | M22 | library assemblies (`%{msil_shlibs}`) and what links against them: a library and its program, recon E7 (`lib-and-prog`: calls, exported and `extern` data, both kinds of cross-assembly relocation, pointer identity both ways, the library's constructor before `main`, a program function that the library calls), one address per library function on the .NET Framework too (`framework-identity`, recon E2), cycles of two and three libraries through interface libraries, with complete data in each (`cycle-pair`, recon E4b, and `cycle-three`), initialization before the program, dependencies first, an otherwise unused library included (`init-order`, recon E4c), the heap, a `FILE *` and `errno` shared by a library and its program (`c-state`), two libraries built from the same code keeping a copy each (`archive-in-two-libs`), a library missing at run time and one that lost a function, which stop with `System.IO.FileNotFoundException` before `main` and with `System.MissingMethodException` in the calling method (`missing-library`, `missing-member`: a transcript, then `not %msil_exec --expect` on the broken deployment), a C# program compiled by `%msil_csc` for .NET 8 and the .NET Framework 4.8 against a library with `--export-class=Contoso.Lib.Native` (`csharp-consumer`, FileCheck), `%{msil_det}` with libraries and an interface (`lib-det`), the ildasm and ilasm round trip of a library, after which a program links against the copy (`roundtrip-library`), a library found by `-L` and `-l`, and the static archive that `-l` prefers (`lsearch-managed`), and, since M23, a 40-byte struct returned through `sret` and passed by value across the assembly boundary and a K&R call that promotes a `float` to `double` (`struct-abi`). Sources use an `API` macro (`__declspec(dllexport)` on Windows, default visibility elsewhere) and import data with `__declspec(dllimport)` on Windows, as the native oracle needs |
| `05-refs/` | M21 | reference binding with the real reference packs: `mscorlib-safe` (`%msil_check_mscorlib_safe` on every runtime of the leg: the 14 entries of `msil-ld --print-bcl-table`, and a probe that `NativeLibrary::Load` through `[mscorlib]` fails), `p1prog` (the recon's program, which uses every BCL member of Phase 1, in every leg, with the AssemblyRef rows that leg must give), `rows-none`, `rows-net8` and `rows-net10` (the rows without references and with each .NET pack, in first-use order, checked by `Inputs/refrows.py`; with the scopes stripped the listings equal the link's without references; the runs on the pack's .NET, with ILVerify against the pack and PEVerify skipped, and the negative runs: `System.IO.FileNotFoundException` on the .NET Framework and, for the .NET 10 binding, `System.MissingMethodException` on .NET 8 (the .NET 8 host cannot load `<Module>`'s .NET 10 references and reports the entry point missing; the .NET 10 run of the same image is the positive control); a run on a given .NET goes through `Inputs/net<N>.runtimeconfig.json`), `net48-equals-none` (equal canonical listings), `positional-pack` (positional `.dll` references give images byte-identical to every `--reference` form), `facade-only` and `two-corlibs` (the core-library errors, through `%{msil_prog_fail_O2}`), `dataimport-nativelibrary` (`03-native/`'s data import through `NativeLibrary` with the .NET 8 pack, on .NET 8 and in WSL on .NET 10, and M20's helper with the .NET Framework's) `lto-text-mscorlib` (`-S` and `--lto-emit-asm` text keep `[mscorlib]`, an LTO image binds to the pack, and `--lto-emit-asm` still rejects a set without a core library) and, since M23, `netcore-dll-name` (a program bound to the .NET 8 pack, linked by the clang driver as `prog.dll` without a warning, verified against the pack, and run with the `prog.runtimeconfig.json` the test writes, on Windows also as `dotnet prog.dll`) |
| `06-det/` | M14 | determinism: `%{msil_det}` builds a program twice and compares every object, archive and output: `det-lto` (bitcode with `-flto` forced, also with `--lto-partitions=4`), and since M20 `det-obj` and `det-archive` (CIL objects, archives and `msil-ld`'s image) |
| `07-equiv/` | M20 | explicit equivalence checks with their own RUN lines (feature `msil-cil-dump` or `msil-mdcanon`): O3 on one C file (`o3-single`), above the relocation-loop threshold from C and from IR (`o3-reloc-loop`) and with named sections, `__start_`/`__stop_` and a constructor-table entry (`o3-sections`, whose `-S` image also runs), and O4 on the zoo (`o4-zoo`, without its weak undefined function and its alias, whose whole-program forms O4 cannot compare) and with sections and constructors (`o4-sections-ctors`); each also has a composite against its native transcript |
| `08-semantics/` | M14 | the link-semantics matrix of plan 5.9.16 (`llvm/docs/MSIL/Status.md` section 2), through LTO and, since M20, through CIL objects: native transcripts, or named diagnostics for duplicate, undefined and mismatched symbols, thread-local variables and destructors; since M15 a library named by `#pragma comment(lib)` (`pragma-comment-lib`) and `qsort` with a C comparator, a named error until phase 3 (`native-callback`); since M19 a declaration without a prototype called with two argument lists (`knr-two-arities`); since M20 `__start_`/`__stop_` (`p3-start-stop` on Linux, `p3-start-stop-windows` as the named error), a constructor in `.init_array` or `.CRT$XCU` (`p8-section-ctor`, also through `clang -S` and ilasm) and its order against a constructor of equal key (`ctor-slot-order`), common symbols of different sizes (`common-largest`), unaligned pointers in data (`unaligned-reloc`), the call adapters and their warnings (`int-ptr-adapter`, `missing-arg-adapter`, `weak-def-sig-mismatch`), the signature error (`float-int-mismatch`), `--gc-sections` against `used` and `retain` data (`gc-used-retain`), entries of a named section between two markers that only pointer arithmetic reaches, which the default `--gc-sections` link keeps as a native link does (`section-bracket`; `obj` pipeline only, because under `-flto` LTO drops them first, as native LTO does), narrow unsigned results called through `int` prototypes, with no warning (`narrow-result-mismatch`), narrow arguments passed to wider parameters, with the adapter warning for the `long long` ones (`narrow-arg-mismatch`; since M21 it builds the program through objects and through LTO in every leg, on the leg's triple at `-O0` and `-O2`, checks each pipeline's warnings and requires the two transcripts to be equal), the import attributes against a test-built library (`import-attrs`), and `OBJ` checks of the object path on the existing diagnostic and adapter rows; since M22 the library rows (`%{msil_shlibs}`): a function that the program and its library both define, each calling its own, with `msil-ld`'s warning (`p2c-bsymbolic`), a cyclic pair with constructors (`p2c-cycle`) and weak library definitions against the program's strong ones (`p2c-weak-def`), and constructors that take glibc's `argc`, `argv` and `envp` (`initarray-args`: the program on Linux triples, the named error on Windows triples); since M23 the P3 rows, each pinning its Phase 2 diagnostic until phase 3 brings its transcript: a `.fini_array` slot (`fini-array`, Linux) and a `.CRT$XTU` slot (`crt-xt`, Windows), `global destructors are not supported yet: <s> in <file>`; `atexit` (`atexit-order`, `undefined symbol: atexit`); a variadic definition called from another object (`variadic-def-xtu`, the definition's error from `clang -c` under `obj` and from `msil-ld` under `lto`); and `matrix.txt`, one line per row and variant of plan 5.9.16 with its tests, native result, pipelines, triples and Phase 2 result, which `matrix-coverage.test` checks with `Inputs/check-matrix.py` (its self-test and the repository) |
| `09-hosted/` | M15 | the optional hosted leg (plan Q16), compile-only: glibc's 38 C and POSIX headers and the UCRT's headers with `-fsyntax-only`, a hosted glibc program linked to text against glibc's real `libc.so` and `libm.so` scripts, `atexit`, which glibc defines only in `libc_nonshared.a`, as a named error, and the spellings that glibc's headers give functions of the callback list (`__sysv_signal`, `ftw64`, `nftw64`, `scandir64`) as named errors too. Its `lit.local.cfg` needs only clang with the MSIL target |

`M2/` has 127 tests (12, 5, 9, 15, 14, 12, 3, 5, 47 and 5); 96 compare a
transcript (`08-semantics/initarray-args` only on Linux triples, where
Windows triples pin its named error), 17 pin a named diagnostic, the three of
`06-det/` and `04-shared/lib-det` check determinism,
`08-semantics/matrix-coverage` checks `matrix.txt` against the tests,
`04-shared/csharp-consumer` FileChecks a C# program's output,
`03-native/print-imports.test` checks `msil-ld`'s
`--print-imports` and linked text, `05-refs/mscorlib-safe.test` runs the
probe of `check-mscorlib-safe.py`, `05-refs/lto-text-mscorlib.test` checks
text, rows and a diagnostic, and the five of `09-hosted/` only compile and
link (two of them to a named link error). `05-refs/netcore-dll-name` also
requires `dotnet-runtime-8` and `msil-cil-dump`; `08-semantics/fini-array`
requires a Linux triple and `crt-xt` a Windows one. In `04-shared/`,
`csharp-consumer` requires `msil-csc` (its .NET Framework half also
`msil-netfx` and a Windows triple), `roundtrip-library` `msil-roundtrip`
(so it runs with `msil_roundtrip=1`: in the `roundtrip` leg and M22's
gate) and `msil-cil-dump`, and `lsearch-managed` `msil-ar`. The tests of `05-refs/` other than
`p1prog` and `mscorlib-safe` require the packs they bind to
(`msil-refpack=<name>`), and `rows-none` the leg without references
(`msil-ref=none`). Every transcript but three is the same on
Windows and on the E7 Linux oracle; the exceptions are
`08-semantics/section-bracket.linux.expected` (without the `mysec` line,
exit status 82: only the Windows program also walks an MSVC `$` group),
`08-semantics/narrow-arg-mismatch.linux.expected` (with the `si(-56)` and
`sl(-56)` lines, exit status 32: only the Linux program makes the
`signed char` calls) and `08-semantics/initarray-args.linux.expected` (the
constructors got `argc`, `argv` and `envp`; its `.expected` file is the
native Windows build with both constructors compiled out, which no MSIL leg
compares against). The library tests whose data hold another assembly's
addresses keep one transcript for both oracles, although native MSVC code
cannot hold another DLL's address in a static initializer: their sources set
those pointers in a constructor only for the native Windows build
(`STATIC_XRELOCS=0`), and every MSIL build and the Linux oracle use the static
initializers, which `msil-ld` turns into cross-assembly relocations.
`08-semantics/p4-selectany` (`__declspec`) and `p3-start-stop-windows`,
and `03-native/kernel32`, `ordinal-import`, `oldnames-alias`,
`ucrtbase-imports` and `data-import-msvcrt` require a Windows triple;
`02-lto/lto-call-attrs` (Windows triples put no extension attribute on a
`char` argument and pass a 32-byte struct by reference, not `byval`),
`03-native/ifunc` and `data-import-glibc` and `08-semantics/p3-start-stop`
a Linux one, and `08-semantics/initarray-args` runs its program on Linux
triples only. Three tests have a part for one triple only:
`02-lto/lto-same-type-mismatch` defines and checks its `byval` case only
for a Linux triple, because Windows triples pass both structs by
reference, `08-semantics/section-bracket` links its `$` group (`b.c`) only
for a Windows triple, and `08-semantics/narrow-arg-mismatch` calls its
`signed char` functions and checks their adapter warning only for a Linux
one, because Win64 leaves the upper bits of a `char` argument undefined.

`Programs/` (M23) is the Phase 2 exit corpus (plan 5.12.9). Its sources are
third-party code that the repository never holds: each test builds from the
trees that `msil/utils/corpus/fetch.py` put into the cache (`%{msil_corpus}`,
"Parameters and features" below), and its `lit.local.cfg` sets
`config.suffixes = [".test"]`, so the drivers, support files and shims are no
tests.

| Directory | Covers |
|---|---|
| `support/` | the support archive of every program (`sup_arena.c`, `sup_malloc.c`, `sup_memcmp.c`, `sup_memcpy.c`, `sup_memset.c`, `sup_strlen.c`, `sup_out.c`, with `sup.h` and `gen.h`): an arena allocator, the C library functions that a freestanding MSIL build lacks (defined only under `__MSIL__`; the native build uses the C library's) and output through `puts` |
| `shim/` | `assert.h`, `math.h`, `stdio.h`, `stdlib.h` and `string.h`, which both builds use instead of a C library's headers (`-isystem`) |
| `zlib/`, `tinf/`, `monocypher/`, `coremark/`, `lz4/`, `blake2/`, `tweetnacl/`, `kissfft/`, `jsmn/` | one program each: its driver(s) (CoreMark also `core_portme.{h,c}`), `<name>.test` and `<name>.expected`. The test `REQUIRES: msil-corpus-<name>` (tinf also `msil-corpus-zlib`), `REDEFINE`s `%{msil_srcs}` (the drivers), `%{msil_archives}` (one archive per library of the program, from `%{msil_corpus}/<name>-<ref>/...`, and `sup` from `support/`) and `%{msil_copts}` (the include directories, the program's defines, `-Werror=implicit-function-declaration -w`), and runs `%{msil_prog}`. Every path is written `%S/../<dir>/...`, so `det/` can repeat the lines verbatim. The drivers return their error count, clamped to 255 (jsmn's has no self-check and returns 0), so every transcript ends `[exit 0]`; there is no `.linux.expected` file, because the Windows and E7 oracles agree |
| `det/` | `<name>.test` for each program: the program's `REQUIRES:` and `REDEFINE:` lines, `check-recipe.py`, which fails when they differ from the program's test (continuation lines joined as lit joins them), then `%{msil_det}`; `check-recipe.test` is its self-test |
| `c-testsuite/` | c-testsuite's libc-free tests in a lit test format of the directory's own (`msil_ctest_format.py`): `libc-free.txt` lists the names under a header with the rule (the recon's regular expression of C library calls) and the count (150), and `lit.local.cfg` recomputes the list from the cached tree and stops on a difference, a malformed line or a listed test whose `.c.expected` is not empty. Each name is a lit test `MSIL :: Programs/c-testsuite/<name>` that runs `%{msil_prog}` on `tests/single-exec/<name>.c` with `%{msil_cflags}` redefined to `-std=gnu17 -w` and `%{msil_expect}` to `exit0` (`exit0.expected` is `[exit 0]`). A line `<name> diag <text>  # <P3 item>`, allowed only for a construct that phase 3 implements, runs `%{msil_prog_fail_<L>}` at each level of `msil_opt_levels` into `FileCheck diag.check -DDIAG=<text>` with the `LLVM ERROR` and `PLEASE submit` implicit-check-nots; the one such entry, `00140` (a variadic definition, P3-B), also requires `msil-prog-pipeline=obj`, because LTO deletes the unused definition at `-O2` |

`Programs/` has 169 lit tests: the nine programs, their nine `det` tests,
`det/check-recipe.test` and the 150 tests of c-testsuite. The corpus legs run
them (`programs`, `programs-lto`, `ctest` and `det`, and the WSL twins of all
but `ctest`); `net8` filters them out (`--filter-out=MSIL :: Programs/`).

`harness/` holds the self-tests of the runner, the verifier, jitall,
`update-expected.py`, `il-snapshot.py`, `%msil_canon`, the equivalence
oracles and lit's configuration: 112 tests, 16 of them in `prog/`, 13 in
`legs/`, 3 in `pipeline/`, 5 in `pipeline-obj/` and 11 in `equiv/`.
M23 added `default-pipeline.test` (with no `msil_pipeline` or
`msil_link_output`, the features `msil-pipeline=obj`,
`msil-prog-pipeline=obj` and `msil-link-output=pe`, and `%{msil_c}` as the
object route through `msil-prog.py --pipeline obj --link-output pe`, with no
ilasm; without `msil-ld` a note and UNSUPPORTED execution tests),
`also-run.test` (two entries add two jitall runs and two runs with the right
runtimeconfig and environment, a transcript that differs on one extra
runtime fails, an unknown or missing runtime is a configuration error, and
`%{msil_also_run_check}` expands to those runs with the parameter and to
nothing without it), `corpus-param.test` (fake caches from `Inputs/make-fake-corpus.py`: no
cache, a stale stamp, a valid stamp, a stamp without one entry, fatal under
`msil_require_corpus=1` unless `msil_corpus_skip` names it), `copts-expect.test`
(`%{msil_copts}` in every compile of both builds of `%{msil_prog}` and of
`%{msil_det}`, and a `%{msil_c}` test's flags under `obj` with
`msil_equiv=1`, the `asm` reference image of O3 and O6 included, and under
`asm`, in the `-S` compile and the native oracle; `%{msil_expect}` in every
run), `det-manifest.test` (`msil/utils/ci/det-manifest.py --self-test`),
`prog/update-expected-m23.test` (`update-expected.py --corpus-dir`, the
placeholders, and the walk that skips c-testsuite and the drivers of a
`.test`-only directory), and in its fix loop `ctest-lists.test` (the list
errors of c-testsuite's `libc-free.txt`, a `diag` entry without a P3 item, a
wrong count, a name listed twice, no rule, and the discovery: in nested lits
over a copy of the directory with a fake cache, one test per listed name,
while a non-empty `.c.expected`, a listed name the tree lacks and a
libc-free test the list lacks are configuration errors),
`legs/ci-legs-corpus.test` and `legs/ci-legs-corpus-sh.test` (the CI corpus
paths of `lit-legs.ps1` and `lit-legs.sh` against a stand-in build
directory: `ctest` under `MSIL_CORPUS_SKIP=coremark,c-testsuite` prints its
skip line, writes a SKIP row and runs no lit, a corpus leg passes
`msil_require_corpus=1`, `msil_corpus_skip` and `msil_corpus_dir`, and a
removed leg fails naming the leg that covers it) and `corpus-fetch.test`
(`msil/utils/corpus/fetch.py --self-test`). The tests that assumed the `asm` default
(`pipeline/prog.c`, `prog-o2.c`, `irprog.ll`, `jitall-runs`, `verify-runs`,
`wrong-expected`, `legs/roundtrip-composites` and runs of
`prog/lit-config.test` and `ref-flags.test`) now require
`msil-pipeline=asm` or pass `--param msil_pipeline=asm` to their nested lit
runs.
M22 added `shlibs.test` (a nested lit run of `prog/Inputs/nested/shlibs.test`
with stand-in tools that record the build order, the interfaces, the
libraries each link gets, `--dep-dir` on every run, jitall and verification,
and the native commands per OS), `prog/shlibs-commands.test` (every
`--shlibs` recipe of `msil-prog.py`, MSIL and native, `--det`, the usage
errors and `--equiv-skip` with libraries; `prog/deps.test` and
`prog/update-expected.test` cover `--dep-dir` and the native library builds of
`update-expected.py`), `csc.test` (`%msil_csc` compiles a two-line C# program
against an ilasm library for net8 and, under `msil-netfx`, net48, and both
run, while a C# error fails with csc's message) and `verify-data-cctor.test`
(pass 1 of `msil-verify.py` accepts the RVA image copy in a library's
`'<msil>Data'::.cctor` under the same listing check as in `<Module>::.cctor`,
and pass 2 neutralizes it, for the library alone and as a program's
`--dep-dir` with `--also-verify-deps`; a copy from another RVA field still
fails, naming the library, and a type error right after the copy fails in the
neutralized pass).
M21 added `ref-flags.test` (nested lit runs of the suite
`prog/Inputs/nested/refs/` with a stand-in clang and `msil-ld` that records
each link: `msil_references` reaches the MSIL link of `%{msil_prog}` and
`%{msil_c}` and never the native oracle's, its features and substitutions,
the runtimes of `%msil_check_mscorlib_safe`, which include the leg's own,
a path to a .NET pack, `System.Runtime.dll` or its directory, handled as
`net<N>`, and the configuration errors, `msil_equiv=1` under `lto` with a
.NET pack among them), `skip-peverify.test` (`msil-verify.py
--skip-peverify`: PEVerify runs in neither pass nor in O6, the reason is
printed, and an empty reason or `--roundtrip` with it is a usage error),
`prog/o6-skip.test` (a `skip O6` line that covers the test skips O6, and
lines for another oracle, test, triple or level leave it failing) and
`prog/roundtrip-snan.test` (`--roundtrip` accepts a quieted float32
signaling NaN `ldc.r4`, as bytes or as `float32(0x...)`, and rejects any
other change of the operand) and, since M22, `prog/roundtrip-moved-class.test`
(`--roundtrip` accepts a top-level class block that a `.custom` of the
`.assembly` block names, in practice a library's `'<msil>SymbolTable'`
class, at another position in the second listing, and rejects any other
moved class, a changed or missing block and a moved class of an EXE).
`canon-listing.test` (M16; from M17 it pins `llvm-cil-dump`, so it also needs
`msil-cil-dump`) assembles `Inputs/canon-listing.il` (an assembly
with a hash algorithm, a field with RVA data, a P/Invoke, methods with and
without `.locals`, a named and an `[opt]` parameter, a branch, a switch, a
non-ASCII `ldstr`, a try/catch and custom attributes) and checks every line of
three listings of `%msil_canon`: the default one, `--order=decl
--forms=canon`, and the five plan 5.12.6 normalizations that an ilasm image
exercises (`--assembly-hash=ignore --params=drop
--initlocals=ignore-without-locals --pinvoke-module=strip
--pinvoke-access=ignore`). Its second input, `coverage.il` (split from the
test, with a UTF-8 BOM prepended for ilasm), is an `-exe` with the rest of
the listing: the entry point, nested, interface and explicit-layout types, a
generic method, a literal field, a property, escaped and non-ASCII names in
UTF-8 order, general arrays, float and negative operands, MethodSpec, TypeSpec
and nested TypeRef tokens, an escaped `ldstr`, and filter and finally clauses.
Its default listing is checked line by line, `--scopes=strip` for `[*]` scopes
without `assemblyref` lines, and `--stats-only` for the table, heap and image
lines (their numbers as regexes), which `--stats` appends to the listing.
`%msil_canon --headers`, which `mdcanon` refuses (exit status 2), must print
the default listing and then the headers, so the expectations fail unless
`%msil_canon` is `llvm-cil-dump --canonical`. Last, `%msil_mdcanon --out-dir`
must write both default listings' bytes.
`canon-agree.test` (M17) assembles six inputs of `Inputs/` (`canon-listing.il`,
`calli-ok.il`, `cctor-puts-windows.il`, `d12-image.il`, `exit-42.il` and
`fp-special-constants.il`) and a seventh kept in the test (`canon-edges.il`,
split-file) with `%ilasm -dll` and requires the same bytes from `%msil_canon`
(`llvm-cil-dump`) and `%msil_mdcanon --out-dir` for each, with the default
options, with `--scopes=strip --order=decl --forms=canon --stats` and with the
six plan 5.12.6 normalizations; the same `--headers` check on `exit-42.dll`
comes first, so the comparisons never set `mdcanon` against itself.
`canon-edges.il` holds what plan section 12's row '5.7.10 clarified
(canonical listing v1 ...)' settles and ilasm can write, which the M1 corpus
lacks: two `.implements` and two nested `.try` clauses out of name and text
order, a field and a method each named `a` and `a$` (raw and quoted names sort
differently), a static property with a `.other` accessor, an event, a
MethodImpl, a DeclSecurity row, a custom attribute on a parameter and a
`switch` without targets; its default listing is also checked for that row's
orders and for the lines it leaves out.
`legs/roundtrip-data-labels.test` (M17) pins how `msil-verify.py
--roundtrip` (`%msil_roundtrip`) compares the two ildasm listings: it numbers
ildasm's RVA data labels (`.data cil I_<rva>`, `.field ... at I_<rva>`) in
order of first appearance, because where the data lies is layout (the PE
writer of `llvm/lib/CLI` puts it elsewhere than ilasm). A round trip whose
data moves passes, while a changed data byte, two fields that swap their data
labels, or a renamed member named like a label (`I_0000ABCD`) still fail.
`dep-local.test` and `dep-wsl.test` (M15) ship a
managed and a native dependency with a program through `--dep` to
`msil-verify.py`, `msil-jitall.py` and `msil-run.py`, and `--dep-dir` to
`msil-run.py` (local and .NET Framework runners, and WSL), and check that a
missing one fails. `il-snapshot.test` (M15) pins the comparison of
`msil/utils/il-snapshot.py --compare` on the fixture trees of
`Inputs/il-snapshot/`: exit status 1 naming a changed `.il`, a changed `.err`,
a missing and an extra file, and 2 for usage errors. `harness/prog/` (M14)
pins the expansions of `%{msil_prog}`, `%{msil_c}` under each pipeline,
`%{msil_ldflags}` (unchanged by M21 under `msil_references=none`;
`ref-flags.test` pins it with references) and, since M15,
`%{msil_native_libs}` and `%{msil_deps}`
(`deps.test`), and the commands of `msil-prog.py` (every leg and level,
archive specifications, `--det` catching a planted difference, the usage and
configuration errors, `update-expected.py` on `%{msil_prog}` tests, one of
which builds its own native library) with stand-ins for clang, `msil-ld`,
`llvm-ar` and `ld.lld`, so it needs neither a compiler nor a linker; since
M20 it pins `%{msil_prog}` under `asm` as the `obj` pipeline's
(`--pipeline obj --link-output pe`), the feature `msil-prog-pipeline=obj`
under `asm` and `obj` and `=lto` under `lto`, and O4 under `lto` with `pe`
(`equiv-lto.test`). `harness/pipeline-obj/` (M20) pins every expansion of
`%{msil_c}` and `%{msil_ll}` under `obj`, with and without `msil_equiv=1`
and at other levels, and of `%{msil_prog}` there, with stand-in tools that
write CIL objects, text and a deterministic image. `harness/equiv/` (M20)
checks `msil-equiv.py` and `equiv-allowed.txt`: for each of the seven
entries a pair of images that differ only in what the entry removes, which
must compare unequal with every other entry allowed and equal with it too;
`no-gc-sections.test` (an unreferenced function and variable: the default
image differs from ilasm's, the `--no-gc-sections` image does not; it needs
the tree's `msil-ld`); `unknown-name.test` (an `--allow` that is not an
entry exits 2); and `skip.test` (O3, O4 and directory `skip` lines, lines
qualified by a triple, a level or both, which cover only that triple and
level, malformed qualifiers, which exit 2, and the real file's crc32.c line,
which covers the Windows triple at `-O2` only), and `o6-md-baseline.test`
(O6 itself: `msil-verify.py --md-baseline` passes an image whose PEVerify
`/md` messages are among the baseline's and fails one with a message the
baseline lacks).

## Running the tests

```text
pwsh -NoProfile -File msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" msil/test/harness
python build/bin/llvm-lit.py -sv msil/test/M1/00-smoke
ninja -C build check-msil-exec       (this suite)
ninja -C build check-msil-m1-02-fp   (one directory; check-msil-m2-00-link, ...)
ninja -C build check-msil            (every MSIL test in the tree, lean dependencies)
python build/bin/llvm-lit.py -sv --param msil_pipeline=lto msil/test/M1/11-programs
python build/bin/llvm-lit.py -sv --param msil_equiv=1 --param msil_also_run=10 msil/test/M1/11-programs
python build/bin/llvm-lit.py -sv --param msil_pipeline=asm msil/test/M1/00-smoke   (the -S route)
python msil/utils/corpus/fetch.py && python build/bin/llvm-lit.py -sv msil/test/Programs/zlib
```

`check-msil` runs this suite and the MSIL lit tests outside `msil/`:
`llvm/test/CodeGen/MSIL` (with the CIL object tests `object-*.ll` of M19),
`clang/test/CodeGen/MSIL`, `clang/test/Preprocessor/init-msil.c`, the driver
tests `clang/test/Driver/msil-toolchain.c`, `msil-link.c`, `msil-lto.c`,
`msil-header-search.c`, `msil-object.c` (M19: `clang -c` writes CIL
objects) and `msil-shared.c` (M22: `-shared`, `/LD` and `/LDd` link
libraries), when lld is built `lld/test/MSIL` (the `msil-ld`
tests), `llvm/test/MC/MSIL` (M18: the encodings, fixups and operand
spellings of `llc -show-mc-encoding` and `-M object-form`), and
`llvm/test/tools/llvm-cil-dump` and `llvm/test/tools/llvm-cil-copy` (M17),
whose tools `check-msil` builds; `check-llvm` and
`check-llvm-tools-llvm-cil-dump` (or `-copy`) do not, so run those two
directories alone only after `ninja -C build llvm-cil-dump llvm-cil-copy`
(their `lit.local.cfg` reports the tests UNSUPPORTED while a tool is missing).
The CIL object tests `object-*.ll` of `llvm/test/CodeGen/MSIL` and
`clang/test/CodeGen/MSIL/emit-obj.c` (M19) read objects with `llvm-cil-dump`
too: they require the lit feature `llvm-cil-dump`, which
`llvm/test/CodeGen/MSIL/lit.local.cfg` sets when the tool is built, so
`check-llvm`, `check-clang` and `check-llvm-codegen-msil`, which do not build
it, report them UNSUPPORTED on a tree without it (under
`msil_require_verify=1` a missing tool is a configuration error).
This suite depends on `lld` (which makes `msil-ld`), `llvm-ar`,
`llvm-nm` (both also read the COFF view of CIL objects, M19), `llvm-cil-dump`
(`%msil_canon`), `llvm-cil-copy`, `yaml2obj`
(which makes the tools' test images) and, since M18, `llvm-mc` (which
`llvm/test/MC/MSIL` runs) besides clang, llc and the lit tools, and
`check-msil` also on `llvm-as`, `llvm-bcanalyzer`, `llvm-lib` and
`llvm-dlltool`, which build the inputs of `lld/test/MSIL`, and on
`llvm-readobj`, which with `yaml2obj` and `llvm-mc` makes and inspects the
native libraries of its native-library tests (M15). `llvm-lit` run directly
builds nothing: build the tools first (`msil-build.ps1 -Standard` builds every
tool the tests use).

Source paths work because `llvm-lit` maps `msil/test/lit.cfg.py` to the
generated `build/tools/msil/test/lit.site.cfg.py`. At configuration time
`lit.cfg.py` runs `msil/utils/jitall/build-jitall.py`, which builds jitall into
`build/tools/msil/jitall` when it is missing or out of date, and
`msil/utils/mdtools/build-mdtools.py`, which builds `mdump` and `mdcanon` into
`build/tools/msil/mdtools` the same way (M16; both need a .NET SDK and the
`Microsoft.NETCore.App.Ref` 8.x targeting pack, and download nothing). The
`check-msil-m1-<dir>` targets are created by CMake, so a new directory gets
its target at the next configure.

`pwsh -File` reads an argument that starts with `-` and contains a `:` as
`-Name:value`, so `msil-lit.ps1 -LitArgs "-sv --param msil_ilasm=D:/..."`
fails with "Missing an argument for parameter 'LitArgs'". Bind the value
with a colon (`-LitArgs:"-sv --param msil_ilasm=D:/..."`) or start it with a
blank (`-LitArgs " -sv --param ..."`); msil-lit.ps1 splits it at blanks.

The phase gate runs this suite in several legs, each a lit run with
`--param msil_require_verify=1 --param msil_require_exec=1 --param
msil_require_link=1` plus the parameters of its leg; since M23 they are the
leg set of the Phase 2 exit (plan 5.12.11), with CIL objects as the default
pipeline. On Windows: `net8` (every MSIL FileCheck directory and this suite
but `Programs/`, through objects on .NET 8 and the .NET Framework 4.8 against
the native oracle, with `msil_equiv=1`, `msil_also_run=10,8+tc0`,
`msil_scale_tests=1` and `msil_require_csc=1`), `more-levels` (M1 at `O1`,
`O3`, `Os` and `Oz`), `asm-oracle` (M1 and `harness/pipeline` through
`clang -S` and the .NET Framework ilasm, with the native oracle and the
ildasm round trip), `modern-ilasm` (the .NET ilasm), `roundtrip` (`O2` on
`msil-ld`'s images: `M1/11-programs`, `M2/07-equiv` and
`M2/04-shared/roundtrip-library.test`), `lto` (`M1/11-programs` and `M2/`
through LTO with O4 and `msil_also_run=10`), `det` (`M2/06-det` and
`Programs/det`), the reference legs `refs-net48`, `refs-net8` (with
`M2/04-shared` and the library rows of the matrix, and `msil_also_run=10`)
and `refs-net10`, and the corpus legs `programs`, `programs-lto` (with O4)
and `ctest`; with `-Wsl`, for the Linux triple on the private WSL runtimes
with the E7 oracle required: `wsl-net10` (M1, M2 and the harness through
objects, with `msil_equiv=1` and `msil_also_run=8,10+tc0`), `wsl-ilasm` (the
Linux ilasm), `lto-wsl` (`M1/11-programs`, `M2/00-link`, `M2/02-lto` and
`M2/08-semantics` through LTO, without O4, which `lto` runs over these
paths), `programs-wsl`, `programs-lto-wsl` (with O4), `det-wsl` and the
optional `hosted` leg (`M2/09-hosted` with `msil_glibc_sysroot` given, M15); `-Quick`
adds `wsl-quick` and `-Full` `all-levels`, `lto-m1` and `encoder-parity`.
Six legs are not lit runs: `expected` (`update-expected.py --check` of `M1/`
and `M2/`), `expected-linux` (`update-expected.py --check --os linux`, the E7
oracle), `utc`, `encoder-parity` (M18, above), and `obj-text` and
`obj-text-wsl` (M19, above). The legs `obj`, `obj-wsl`, `net10`, `net8-tc0`
and `wsl-net10-tc0` of earlier milestones are gone: objects are the default,
and the other runtimes are `msil_also_run` entries; `-Leg` of one of them
fails naming its successor. The corpus legs pass `msil_require_corpus=1`,
`msil_corpus_dir` from `$MSIL_CORPUS_CACHE` and `msil_corpus_skip` from
`$MSIL_CORPUS_SKIP`; while that names `c-testsuite`, `ctest` prints `ctest:
skipped (c-testsuite is in MSIL_CORPUS_SKIP)` and is a SKIP row (the CI's
default, until the CI licences of CoreMark and c-testsuite are settled).
`llvm/docs/MSIL/GettingStarted.md` section 7 lists every leg with its
parameters and the old leg names with the legs that replaced them. A lit leg
passes only when lit exits 0 and at least one test passed.
`lit-legs.ps1` writes a leg's lit arguments one per line into
`build/lit-results/<leg>.params.rsp` and passes `@<file>` to lit, so a
parameter value may contain blanks. `pwsh -NoProfile -File
msil/utils/run-m1-gate.ps1 [-CorpusCache <dir>] [-FetchCorpus]` runs every
leg in order through the same `msil/utils/ci/` scripts as `msil-ci.yml`,
after a `host-setup` step that installs the private WSL runtimes, the E7
sysroot and the glibc sysroot of the `hosted` leg (all idempotent, no root)
and a `corpus` step (`fetch.py --check` of all ten entries, clearing
`MSIL_CORPUS_SKIP`), with a `determinism` step after the legs
(`msil/utils/ci/det-manifest.py` in two work directories), and prints one
summary row per step and leg; `msil/utils/ci/lit-legs.ps1 -List` names the
legs, and each leg leaves its lit log and xunit file in
`build/lit-results/<leg>.{log,xml}`.

## Adding a test

1. Put `<name>.c` (or `.ll`) in the right `M1/<NN-area>/` directory: the
   lowest-numbered failing directory names the earliest broken capability.
   The first comment line says what the test covers, the second is the RUN line:

   ```c
   // Recursion and output through puts. Exit status fib(10) = 55.
   // RUN: %{msil_c}
   #include "msil_test.h"
   ```

   Every C test uses `%{msil_c}` (since M7 added frames, plan D32). Use a
   per-level form (`%{msil_c_O2}`) only with a comment saying why; any other
   RUN line needs a comment too.
2. Generate the expected transcript from a native build with the just-built
   clang, review it, and run the directory:

   ```text
   python msil/utils/update-expected.py msil/test/M1/02-fp/<name>.c
   python msil/utils/update-expected.py --check msil/test/M1      (what CI checks)
   python msil/utils/update-expected.py --os linux msil/test/M1/02-fp/<name>.c
   python msil/utils/update-expected.py --check --os linux msil/test/M1
   ```

   On Windows run it from a VS x64 developer prompt (the native link needs
   `LIB`); its MSVC builds link the compiler-rt builtins (`--rtlib`, default
   `auto`, as `msil_native_rtlib`). `--os linux` is the E7 Linux oracle on a
   Windows host (it needs the host setup of GettingStarted.md section 7) and
   the host's own build on Linux. Written with `--os linux`,
   `<name>.linux.expected` appears only when the Linux transcript differs
   from `<name>.expected` (a stale one is removed); review it and say in the
   test why it differs. `-j N` sets the number of parallel native builds
   (default one per CPU). `.expected` files are never edited by hand; a
   `<name>.windows.expected` override is the only exception and carries a
   comment in the test explaining it.

Rules (plan 5.5, D14, D32, D37): UB-free GNU C17 (`%{msil_cflags}` pins
`-std=gnu17`, so a change of clang's default dialect cannot break the corpus)
that behaves the same natively at every level; `main` always returns
explicitly (`-ffreestanding` turns off its implicit `return 0` on Linux
triples, and `-Werror=return-type` makes falling off it an error there; MSVCRT
triples keep the implicit return); the exit status is 0..255 (bitmasks use at
most 8 bits, checksums are masked with `& 0xff`); no `long`, `long double`,
varargs, printing of addresses or of floating-point values; no library calls
other than the Phase-1 imports (D14: `puts`, `putchar` and the C math library;
`M1/06-output/` tests them), and output goes through `msil_test.h`. Every
Phase-1 capability exists since M9, so a new test may use all of them
(`MSIL_EXPECT(cond)`, the `put_*` helpers, frames, globals, function
pointers). The capability table of plan 5.5 only limited what each milestone's
tests could use; that is why tests in `01-int/` and `02-fp/` accumulate
results branch-free with `MSIL_CHECK(acc, bit, cond)` and `return acc;`.

A test of several files goes in `M2/<NN-area>/` as a `.test` file (plan
5.12.5): its sources are `split-file` parts, `REDEFINE:` lines name them, and
one RUN line is `%{msil_prog}`:

```text
# Archive members are linked only when they resolve an undefined symbol.
# REQUIRES: msil-ar
# RUN: rm -rf %t && split-file %s %t
# REDEFINE: %{msil_srcs} = %t/main.c
# REDEFINE: %{msil_archives} = help=%t/used.c,%t/unused.c
# RUN: %{msil_prog}
#--- main.c
#include "msil_test.h"
int helper(void);
int main(void) { put_i64(helper()); return 7; }
#--- used.c
int helper(void) { return 100; }
#--- unused.c
...
```

- `%{msil_srcs}` (default `%s`) are the MSIL sources, `%{msil_stock_srcs}` the
  stock-clang ones, `%{msil_archives}` the archives as
  `NAME=SRC[,SRC...][;NAME=...]` (each `lib<NAME>.a`, linked in that order
  after the objects as `-L<dir> -l<NAME>`; a `stock:SRC` member is compiled as
  a stock source) and `%{msil_libs}` further arguments of the MSIL link only.
  The values sit inside single quotes on the command line: no `'` in them,
  and no `,` or `;` in a path.
- A test that builds an archive says `REQUIRES: msil-ar`; one that needs a
  Windows triple (`__declspec`, a Win32 function) says
  `REQUIRES: msil-triple-windows`.
- Unlike `M1/`, whose tests also compile with `clang -S`, an `M2/` test may
  call any non-variadic native function that binds through the driver's
  default libraries (M15; `03-native/`), and `%{msil_native_libs}` adds
  libraries to both builds (`-lws2_32`, say). A test that needs a native
  library of its own builds it with RUN lines before its composite (plan
  5.12.5), and ships it with the program:

  ```text
  # RUN: rm -rf %t && split-file %s %t
  # RUN: %if msil-triple-windows %{ %clang_native -O2 -c %t/native.c -o %t/native.obj && %lld_link -dll -noentry -nodefaultlib %t/native.obj -out:%t/native.dll %} %else %{ %clang_native -O2 -fPIC -c %t/native.c -o %t/native.o && %ld_lld -shared -soname libnative.so %t/native.o -o %t/libnative.so %}
  # REDEFINE: %{msil_srcs} = %t/main.c
  # REDEFINE: %{msil_native_libs} = -L%t -lnative
  # REDEFINE: %{msil_deps} = %if msil-triple-windows %{%t/native.dll%} %else %{%t/libnative.so%}
  # RUN: %{msil_prog}
  ```

  `lld-link -dll` also writes the import library `native.lib`, which
  `-lnative` finds on Windows triples; `%{msil_deps}` may use `%if` and
  wildcards, and `update-expected.py` runs the RUN lines before the first
  composite (other than `rm -rf %t && split-file %s %t`) itself, with the
  oracle's tools (`03-native/native-lib.test`).
- Common symbols use `__attribute__((common))`, not `-fcommon`, which the
  native oracle would not get. A test that must use LTO on every pipeline
  (a ThinLTO or partition case) adds `-flto` (or `-flto=thin`) to a
  `REDEFINE:` of `%{msil_cflags}`, which the native oracle gets too; prebuilt
  objects or bitcode go in `%{msil_libs}`.
- A case that must stop with a named diagnostic runs
  `%if msil-prog-pipeline=obj %{ %{msil_prog_fail_O0} 2>&1 | FileCheck %s
  --check-prefix=OBJ --implicit-check-not="LLVM ERROR"
  --implicit-check-not="PLEASE submit" %}` and the same with `lto` and
  `--check-prefix=LTO` (and with `_O2`), because the object path reports
  `msil-ld`'s texts, which name the objects, and LTO the code generator's or
  the merged module's; it has no `.expected` file. A check of `msil-ld`'s own
  output (a warning, say) runs `%msil_prog_build` under the same guards.
  `msil-ld`'s LTO applies the call table before its optimizer as well as in
  code generation, so LTO's signature warnings and errors are the same at
  every level (`llvm/docs/MSIL/Status.md`, the matrix), and the matrix pins
  them, like the object path's, at `-O0` and `-O2`.
- `update-expected.py` writes the `.expected` file as for `M1/`
  (`python msil/utils/update-expected.py msil/test/M2/00-link/<name>.test`):
  it splits the test into a temporary directory, reads the `REDEFINE:` lines
  and builds the program natively with `msil-prog.py --mode native` (`--ar`
  and `--split-file` name the tools; by default the ones next to `--clang`),
  with `%{msil_native_libs}` and, for the run, the `%{msil_deps}` files next
  to the program. Tests whose only composites are `%{msil_prog_fail_<L>}` or
  `%{msil_det}` have no transcript and are skipped.
- A test of libraries (M22) goes in `M2/04-shared/` (a row of the matrix in
  `M2/08-semantics/`) and names them in `%{msil_shlibs}` ("What runs"):

  ```text
  # RUN: rm -rf %t && split-file %s %t
  # REDEFINE: %{msil_shlibs} = zl=%t/zl.c
  # REDEFINE: %{msil_srcs} = %t/main.c
  # RUN: %{msil_prog}
  ```

  Export with an `API` macro (`__declspec(dllexport)` on Windows,
  `__attribute__((visibility("default")))` elsewhere) and declare imported
  variables `__declspec(dllimport)` on Windows, as the native oracle needs
  (MSIL ignores it). Native MSVC code cannot put another DLL's address in a
  static initializer, so a test whose data hold such addresses sets them in a
  constructor for the native Windows build only (`#if defined(_WIN32) &&
  !defined(__MSIL__)`). A cycle names an interface first
  (`?b=b.c;a=a.c;b=b.c`). The order in which unrelated native libraries
  initialize differs between Windows and glibc, so a test of initialization
  order chains its libraries, and a constructor of a cycle records what it
  saw rather than printing it. `update-expected.py` builds the libraries
  natively too, and a test outside `04-shared/` that builds or links a
  library needs a `skip O4` line in `msil/utils/equiv-allowed.txt` (below),
  because a library has no `--lto-emit-asm` text.
- `update-expected.py` decides `%if` only in `REDEFINE:` values and in the RUN
  lines before the first composite, never around a composite: it builds and
  runs every `%{msil_prog}` test with each oracle. A test whose composite one
  triple guards (`%if msil-triple-linux %{ %{msil_prog} %}`, as
  `08-semantics/initarray-args` does) therefore needs a source that builds and
  runs natively on both operating systems, and gets an `.expected` file even
  when no MSIL leg compares against it. A test for one triple only says
  `REQUIRES:`, which `update-expected.py` honours.
- A test that ends in an unhandled exception (`04-shared/missing-member`)
  calls `fflush(0)` first: C `stdout` still in the C library's buffer is lost
  when the runtime ends the process.
- A program of the exit corpus (M23) goes in `Programs/<name>/` (above): its
  sources come from `%{msil_corpus}` (a new corpus entry needs a line in
  `msil/utils/corpus/manifest.json` with its hashes and licence, which
  `fetch.py` checks), its flags go in `%{msil_copts}`, it `REQUIRES:
  msil-corpus-<name>`, and `det/<name>.test` repeats its `REQUIRES:` and
  `REDEFINE:` lines exactly. Write its `.expected` file with
  `update-expected.py --corpus-dir <cache> msil/test/Programs/<name>`. A
  c-testsuite test that a construct of phase 3 stops gets a `diag` line in
  `libc-free.txt`; any other failure of one is a compiler or linker bug, never
  a reason to drop it from the list.

## Parameters and features

| `--param` | Default | Meaning |
|---|---|---|
| `msil_triple` | host: `x86_64-pc-windows-msvc-msil` or `x86_64-unknown-linux-gnu-msil`; `x86_64-unknown-linux-gnu-msil` with `msil_runner=wsl` | triple of `%clang_msil` and `%llc_msil` |
| `msil_opt_levels` | `O0,O2` | levels `%{msil_c}` / `%{msil_ll}` run (`O0,O1,O2,O3,Os,Oz`; `%{msil_ll}` skips `Os` and `Oz`); the `more-levels` leg passes `O1,O3,Os,Oz` |
| `msil_dotnet_runtime` | `8` (`10` with `msil_runner=wsl`), else the newest | Microsoft.NETCore.App major the programs run on; a missing one is an error |
| `msil_dotnet_env` | empty | `VAR=VALUE[;VAR=VALUE...]` for programs and jitall, e.g. `DOTNET_TieredCompilation=0` (D38) |
| `msil_native_oracle` | `0` | `1`: also build natively and compare when possible; `required`: an error when it is not possible (CI). With `msil_runner=wsl` it is the E7 Linux oracle (feature `msil-linux-oracle`), for `x86_64-unknown-linux-gnu-msil` only |
| `msil_native_rtlib` | `auto` | the compiler-rt builtins the Windows native oracle links for MSVC triples (`__udivti3`, `__fixdfti`, ...): `auto` takes the newest `clang_rt.builtins-<arch>.lib` of the Visual Studio LLVM (`%VCINSTALLDIR%Tools\Llvm\x64\lib\clang\*\lib\windows`), else of the LLVM whose `clang-cl` or `lld-link` is on `PATH`; a path names one; empty or `none` disables it. Missing under `msil_native_oracle=required`, it is a configuration error |
| `msil_linux_sysroot` | `$MSIL_LINUX_SYSROOT`, else `C:\llvm-msil-scratch\linux-sysroot` if it exists (Windows), else `/` (Linux) | the Linux libraries of the E7 oracle, in the layout of `msil/utils/make-linux-sysroot.py`; a named directory without `usr/lib/x86_64-linux-gnu/libc.so.6` and `libm.so.6` is a configuration error; empty disables it |
| `msil_run_netfx` | `0` | `1`: also `ilasm -exe` and run on .NET Framework 4.8 (Windows, local runner) |
| `msil_runner` | `local` | `wsl`: run programs in WSL (`wsl.exe -d Ubuntu`), on .NET 10 by default |
| `msil_wsl_dotnet` | the private runtime if installed, else the distribution's `dotnet` (with a note) | with `msil_runner=wsl`: the dotnet host inside WSL that runs the programs and jitall. `private` is `$HOME/.dotnet-msil/dotnet`, which `msil/utils/msil-wsl-dotnet.sh` installs; when it is not installed that is a configuration error naming the script. An absolute WSL path names another host. Every WSL gate leg passes `private` |
| `msil_require_verify` | `0` | `1` (every gate): a missing ILVerify, ildasm, jitall, mdcanon, `llvm-cil-dump` (M17), (Windows) PEVerify, ilasm or .NET runtime of the runner is a configuration error, even when an empty override disabled it; so is a failed `build-mdtools.py` run |
| `msil_require_exec` | `0` | `1` (every gate leg): `M1/` without the feature `msil-exec` is a configuration error, not UNSUPPORTED |
| `msil_roundtrip` | `0` | `1`: `%{msil_c}` and `%{msil_ll}` also round-trip every assembly through the .NET ildasm and ilasm at the version msil/utils/tools/msil-tools.proj pins (MsilILAsmVersion) from the NuGet global-packages folder (a missing one is a configuration error naming the version) (`msil-verify.py --roundtrip`; the listings must match) and run the reassembled image against the same `.expected` (Design.md 5.5; not the Framework ildasm, which prints infinite constants as `inf`) |
| `msil_wsl_ilasm` | unset | with `msil_runner=wsl`: `%ilasm` runs this Linux ilasm (a Windows path) inside WSL through `msil-run.py --tool --runner wsl`, which stages an executable copy; `nuget` picks the newest NuGet `runtime.linux-x64.Microsoft.NETCore.ILAsm` |
| `msil_ilasm`, `msil_ildasm`, `msil_dotnet`, `msil_ilverify`, `msil_peverify` | auto | tool path; empty disables the tool; `msil_ilasm=nuget` picks the newest NuGet `runtime.<rid>.Microsoft.NETCore.ILAsm` |
| `msil_jitall` | `<build>/tools/msil/jitall` | jitall directory; empty disables jitall |
| `msil_mdcanon` | `<build>/tools/msil/mdtools`, which lit builds on configuration with `msil/utils/mdtools/build-mdtools.py` (only when a source changed; a failed build is a warning, and a configuration error under `msil_require_verify=1`) | the directory of `mdcanon.dll` and `mdcanon.runtimeconfig.json` (M16, plan 5.12.2): a named one is used as it is, and one without those files is a configuration error; empty disables mdcanon. The host's dotnet runs it, also with `msil_runner=wsl` (feature `msil-mdcanon`; `%msil_mdcanon`, `%msil_canon`) |
| `msil_timeout`, `msil_mem_limit_mb` | `60`, `4096` | limits for every child process |
| `msil_wsl_distro` | `Ubuntu` | WSL distribution of `msil_runner=wsl` |
| `msil_pipeline` | `obj` (since M23; `asm` before) | how `%{msil_c}` and `%{msil_ll}` build (feature `msil-pipeline=<p>`): `obj` (M20) as a one-source `%{msil_prog}` through `clang -c` (`llc -filetype=obj`) and `msil-ld`'s image; `asm` with `clang -S` (llc) and `ilasm -dll`, the oracle route; `lto` as a one-source `%{msil_prog}` through `clang -flto -c` and `msil-ld` (M14), `%{msil_ll}` on llc text. `%{msil_prog}` uses `obj` under `asm` and `obj`, `lto` under `lto` (feature `msil-prog-pipeline=<p>`) |
| `msil_link_output` | `pe` | what `msil-ld` writes for `%{msil_prog}` (feature `msil-link-output=<x>`): `pe`, the program image (M20); `il`, ILAsm text (`--lto-emit-asm`) that `ilasm -exe` assembles, only under `lto` (a configuration error under `asm` and `obj`; the default in M14-M19) |
| `msil_equiv` | `0` | `1` (M20, feature `msil-equiv`): the composites also run the equivalence oracles, O3 and O6 under `obj`, O4 under `lto` with `pe` ("What runs" above; `%msil_equiv`); a configuration error under `asm` and under `lto` with `il`, and when neither `llvm-cil-dump` nor `mdcanon` is there |
| `msil_scale_tests` | `0` | `1` (M20, feature `msil-scale-tests`): the tests at the CLR's per-type limits run (`M2/00-link/spill-limits.test`); only `lit-legs.ps1`'s `net8` leg sets it (its `obj` leg before M23) |
| `msil_references` | `none` | M21, feature `msil-ref=<none\|net48\|net8\|net10\|path>`: the reference assemblies of every MSIL link, `%{msil_ref_flags}` (`-Wl,--reference=<dir>`; "What runs" above has the packs, `$MSIL_REF_NET48`, `$MSIL_REF_NET8` and `$MSIL_REF_NET10`, and the configuration errors); `none` binds through `msil-ld`'s built-in `[mscorlib]` table. With references, O3 and O4 allow `scopes`; with `net8`, `net10` or a path to a .NET pack (handled as `net<N>`), `%msil_verify` verifies against the pack and skips PEVerify, and `msil_pipeline=lto` with `msil_equiv=1` is a configuration error |
| `msil_ld`, `msil_ar`, `msil_nm` | `<build>/bin/msil-ld`, `llvm-ar`, `llvm-nm` | the tree's linker, archiver and symbol lister (also `$MSIL_LD`, `$MSIL_AR`, `$MSIL_NM`), never ones from `PATH`: the Visual Studio `llvm-ar` cannot index this LLVM's bitcode. A named path that does not exist is a configuration error; empty disables the tool |
| `msil_cil_dump` | `<build>/bin/llvm-cil-dump` | the tree's ECMA-335 dumper (M17, plan 5.12.2; also `$MSIL_CIL_DUMP`), never one from `PATH`: feature `msil-cil-dump`, `%msil_cil_dump`, and `%msil_canon` is `llvm-cil-dump --canonical` when it is found (else the `mdcanon` command). A named path that does not exist is a configuration error; empty disables it. Under `msil_require_verify=1` a missing `llvm-cil-dump` (the tree's never built, or a named path that does not exist) and a disabled one (`--param msil_cil_dump=` or an empty `$MSIL_CIL_DUMP`) are configuration errors that name `llvm-cil-dump` and `msil_require_verify=1`; without it `%msil_canon` falls back to `mdcanon` |
| `msil_require_link` | `0` | `1` (every gate leg): a missing `msil-ld` or `llvm-ar` is a configuration error, and so is an `M2/` that cannot run; `msil_pipeline=lto` requires both as well |
| `msil_require_csc` | `0` | `1` (M22; `lit-legs.ps1`'s `net8` passes it since M23): a missing csc of the newest .NET SDK (`%msil_csc`, feature `msil-csc`) is a configuration error instead of an UNSUPPORTED `csharp-consumer` |
| `msil_also_run` | empty | M23, feature `msil-also-run`: `<major>[+tc0][,...]`, more runtimes for jitall and the run of every composite's verified image ("What runs" above); a malformed or repeated entry, and under `msil_require_exec=1` a runtime the runner lacks, is a configuration error |
| `msil_corpus_dir` | `$MSIL_CORPUS_CACHE`, else `C:\llvm-msil-scratch\corpus-cache` (Windows) or `~/.cache/msil-corpus` | M23: the cache of `msil/utils/corpus/fetch.py` (features `msil-corpus`, `msil-corpus-<name>`, substitution `%{msil_corpus}`); empty disables the corpus |
| `msil_require_corpus` | `0` | `1` (M23, the corpus legs): a missing or stale stamp, or an entry the stamp lacks and `msil_corpus_skip` does not name, is a configuration error naming `fetch.py --cache <dir>` |
| `msil_corpus_skip` | empty | M23: comma list of manifest names left out on purpose (the legs pass `$MSIL_CORPUS_SKIP`); their tests are UNSUPPORTED with a note, and a name the manifest lacks is a configuration error |
| `msil_glibc_sysroot` | `$MSIL_GLIBC_SYSROOT`, else `C:\llvm-msil-scratch\glibc-sysroot` on Windows when it holds `usr/include/stdio.h`; empty disables it | a glibc sysroot with headers, as `msil/utils/make-glibc-sysroot.sh` makes it, for the hosted tests of `M2/09-hosted/` (feature `msil-glibc-sysroot`, substitution `%{msil_glibc_sysroot}`; M15). A value given explicitly, as the `hosted` leg gives it, without `usr/include/stdio.h` is a configuration error |

Tool search order: `--param msil_<tool>=PATH`, `$MSIL_<TOOL>`, the value CMake
found, well-known places (the .NET Framework ilasm, and the ildasm of the
Windows SDK's NETFX 4.x Tools next to PEVerify, on Windows), `PATH`, then the
NuGet global-packages folder (`dotnet nuget locals global-packages --list`)
for ilasm and ildasm (`runtime.<rid>.Microsoft.NETCore.ILAsm` / `.ILDAsm`),
`~/.dotnet/tools` for ILVerify and the Windows SDK for PEVerify.

Features: `msil-exec`, `msil-ilasm`, `msil-dotnet`, `dotnet-runtime-<N>` (of
the runner), `msil-ld` and `msil-ar` (the tree's `msil-ld` and `llvm-ar` were
found), `msil-pipeline=<asm|lto|obj>`, `msil-prog-pipeline=<obj|lto>` (the
pipeline `%{msil_prog}` uses: `obj` under `asm` and `obj` since M20, `lto`
under `lto`; see "What runs"), `msil-link-output=<pe|il>`, `msil-equiv` and
`msil-scale-tests` (M20), `msil-ref=<none|net48|net8|net10|path>` (the
value of `msil_references`) and `msil-refpack=<net48|net8|net10>` (that
reference pack was found; M21), `msil-csc` (the newest .NET SDK's csc was
found, for `%msil_csc`; M22), `msil-also-run` (`msil_also_run` names a
runtime; M23), `msil-corpus` and `msil-corpus-<name>` (the corpus cache's
stamp matches the manifest, and the tree `<name>` is in it; M23),
`msil-ilverify`, `msil-peverify`, `msil-ildasm`
(msil-verify.py runs its neutralized pass), `msil-jitall` (jitall.dll is
built and the runner has a runtime), `msil-mdcanon` (mdcanon.dll is built and
the host's dotnet has a runtime for it; M16), `msil-cil-dump` (the tree's
`llvm-cil-dump` was found, so `%msil_canon` is `llvm-cil-dump --canonical`;
M17), `msil-netfx`,
`msil-run-netfx`, `msil-wsl`, `msil-wsl-ilasm`, `msil-roundtrip`,
`msil-native-oracle`, `msil-native-rtlib` (the compiler-rt builtins were
found), `msil-linux-sysroot` (a complete `msil_linux_sysroot`),
`msil-linux-oracle` (E7 can run: the sysroot with `libgcc_s.so.1`,
`ld.lld`, clang with the X86 target and a compiled `start.o`),
`msil-registered-target`, `msil-ld-lld` and `msil-lld-link` (`%ld_lld` and
`%lld_link` were found, for tests that build native libraries; M15),
`msil-glibc-sysroot` (a glibc sysroot with headers, `msil_glibc_sysroot`;
M15), `msil-ucrt-headers` (a directory of `INCLUDE` holds `corecrt.h`; M15),
`msil-triple=<triple>`, and `msil-triple-windows` / `msil-triple-linux`. The
last two follow the effective triple, not the host: the WSL runner executes a
linux triple from Windows, so a Windows-only P/Invoke test says
`REQUIRES: msil-triple-windows`, not `system-windows`.

Substitutions: `%msil_triple`, `%clang_msil`, `%llc_msil`, `%clang_native`,
`%ilasm` (ilasm with `-nologo -quiet -pe64 -x64` under the limits; callers add
`-dll` or `-exe` and `-output=`), `%dotnet`, `%msil_exec` (msil-run.py),
`%msil_verify` (msil-verify.py, with `--ildasm`/`--ilasm` when ildasm was
found), `%msil_jitall` (msil-jitall.py, with `--runner wsl` under
`msil_runner=wsl`), `%msil_roundtrip` (`msil-verify.py --roundtrip` with the
.NET ildasm and ilasm), `%msil_mdcanon` (M16: `mdcanon` on the host's dotnet,
under the limits of `msil-run.py --tool`; add the options and files),
`%msil_cil_dump` (M17: the tree's `llvm-cil-dump` on the host under the same
limits), `%msil_canon` (the canonical listing a test compares:
`%msil_cil_dump --canonical` when the feature `msil-cil-dump` is present, from
M17, else the `%msil_mdcanon` command; a missing tool is a placeholder, so a
test that runs them needs `REQUIRES: msil-cil-dump` or `msil-mdcanon`),
`%msil_inputs`, `%{msil_cflags}` (`-std=gnu17 -ffreestanding
-Werror=return-type -I msil/test/Inputs`), `%{msil_native_ldflags}` (`-lm`
when the native triple is not Windows: glibc keeps the C math library out of
libc and clang does not add `-lm` for C; `"-l<path>.lib"` of the compiler-rt
builtins for MSVC triples when `msil_native_rtlib` found them; else empty;
the native-oracle legs put it after the source, and `update-expected.py`
follows the same rule), the E7 pieces `%{msil_linux_cc}` (clang
`--target=x86_64-unknown-linux-gnu -fno-pic`), `%{msil_linux_ld}` (`ld.lld
-dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed <start.o>`),
`%{msil_linux_libs}` (the sysroot's `libm.so.6`, `libc.so.6` and
`libgcc_s.so.1`), `%{msil_linux_exec}` (`%msil_exec --native --runner wsl
--wsl-distro <distro> --expect-os linux`) and `%{msil_e7_args}` (`--clang
<clang> --linux-sysroot <sysroot> --wsl-distro <distro> --ld-lld <ld.lld>`:
lit's own E7 configuration as `update-expected.py --os linux` options, so a
test that runs `update-expected.py` uses the clang, sysroot, WSL distribution
and ld.lld that enabled `msil-linux-oracle` and not the script's defaults; an
absent sysroot or ld.lld is a `*-not-found` placeholder), `%ld_lld` and
`%lld_link` (the tree's linkers, else `PATH`, else the Visual Studio LLVM's),
`%{msil_c}`, `%{msil_ll}`,
`%{msil_c_O0}` .. `%{msil_c_Oz}` and `%{msil_ll_O0}` .. `%{msil_ll_O3}`.
For programs of several files (M14): `%msil_ld` (`msil-ld` under
`msil-run.py --tool` limits), `%msil_ar` and `%msil_nm` (the tree's `llvm-ar`
and `llvm-nm`), `%msil_prog_build` (`msil-prog.py` with the tools, the triple,
`%{msil_cflags}`, `%{msil_ldflags}` and `%{msil_native_ldflags}`; add
`--level`, `--workdir` and the inputs), the placeholders `%{msil_srcs}`
(default `%s`), `%{msil_stock_srcs}`, `%{msil_archives}` and `%{msil_libs}`
(default empty), `%{msil_native_libs}` (default empty: link arguments of
both builds, `msil-prog.py --native-lib`; M15) and `%{msil_deps}` (default
empty: files that verification, the force-JIT and every run get next to the
program, passed as `--dep %{msil_deps} --`; M15), `%{msil_ldflags}` (the
MSIL link's flags: empty on Windows triples and
`--sysroot=<msil_linux_sysroot>` on Linux triples since M15,
`-nodefaultlibs` in M14; since M21 followed by `%{msil_ref_flags}`), the
placeholder `%{msil_ref_flags}` (M21: `-Wl,--reference=<dir>` under
`msil_references`, else empty; a test with references of its own
`REDEFINE`s it), `%{msil_ref_dir_net48}`, `%{msil_ref_dir_net8}` and
`%{msil_ref_dir_net10}` (M21: the quoted pack directory, else the
placeholder `msil-ref-dir-<name>-not-found`, so a test that uses one says
`REQUIRES: msil-refpack=<name>`), the composites `%{msil_prog}` (every level of
`msil_opt_levels`) and `%{msil_prog_O0}` .. `%{msil_prog_Oz}`,
`%{msil_prog_fail_O0}` .. `%{msil_prog_fail_Oz}` (`not` and the build of
that level only, for named diagnostics) and `%{msil_det}` (`msil-prog.py
--det` at `-O2` in `%t.det.d`), and `%{msil_glibc_sysroot}` (the quoted
glibc sysroot of the hosted tests, else `glibc-sysroot-not-found`; M15).
Since M20: `%msil_equiv` (`msil/utils/msil-equiv.py --canon '<%msil_canon>'
--allowed msil/utils/equiv-allowed.txt`; add `--oracle O3|O4`, `--test %s`,
the `--allow <name>` options of the comparison and the two images, and
`--ilasm '%ilasm'` when an operand is `--lto-emit-asm` text) and
`%{msil_llc}` (the tree's llc, which `%{msil_ll}` runs under `obj`).
Since M21: `%msil_check_mscorlib_safe` (`msil/utils/check-mscorlib-safe.py
--msil-ld <msil-ld> --ilasm <ilasm> --runtimes <runtimes>` with the limits
and the dotnet and WSL options of the run; the runtimes are, with the local
runner, `netfx` where the .NET Framework is installed, `net8` and `net10`
where installed, and always the leg's own runtime `net<N>`
(`msil_dotnet_runtime`, or the default one), so the list is never empty
where `msil-exec` holds (a host with only .NET 9 runs the probe on
`net9`), and with the WSL runner `wsl-net<major>` of the programs' runtime;
callers add `--out DIR` and optionally `--table FILE`).
Since M22: the placeholder `%{msil_shlibs}` (default empty; "What runs"),
which `%msil_prog_build` passes as `--shlibs='%{msil_shlibs}'` (an explicit
`--shlibs=` later on a command line overrides it), and `%msil_csc`
(`msil/utils/jitall/build-jitall.py --dotnet <dotnet> --csharp`; add `SRC
--out EXE`, `--reference DLL`, `--refs net8|net48` and `--unsafe`).
Since M23: `%{msil_corpus}` (the corpus cache with `/` separators and no
quotes, for `%{msil_srcs}` and `%{msil_archives}`; `msil-corpus-not-found`
without a cache), the placeholder `%{msil_copts}` (default empty; extra
compile flags of both builds of `%{msil_prog}` and `%{msil_det}` and of every
build of `%{msil_c}` on every pipeline, never of `%{msil_ll}`),
`%{msil_expect}` (the expected-file stem of every composite's runs, default
`%S/%{s:stem}`), and `%{msil_also_run_check}` with the placeholder
`%{msil_also_run_image}` (default `msil-also-run-image-not-set`) for tests
with explicit RUN lines: such a test REDEFINEs `%{msil_also_run_image}` to its
image and appends `%{msil_also_run_check}` after its main FileCheck, which
adds one jitall, one run and one `FileCheck --match-full-lines` per
`msil_also_run` entry and nothing without the parameter ("What runs"; the
three trap tests of `M1/` use it). No placeholder name is a substring of
another (lit's `REDEFINE:` rule).
Everything works in lit's internal shell.

## Transcripts

`msil-run.py` writes the program's stdout, then one status line. Every run of
CRs before an LF (`\r+\n`) becomes a single LF, in program output on every
runner (local, `--native`, `--netfx`, WSL) and in `.expected` files alike,
both in `msil-run.py` (`--out`, `--expect`) and in `update-expected.py`
(written files and `--check`). The rule is idempotent, so `puts("a\r")`
gives the transcript `a\n` on Windows (text-mode stdout writes `a\r\r\n`)
and on Linux (`a\r\n`), and the `.expected` file survives being committed
with `eol=lf` (`msil/.gitattributes`). The status lines are:

| Status | Meaning |
|---|---|
| `[exit N]` | exit status N with 0 <= N <= 255 |
| `[exit N: outside 0..255]` | any other exit status: Linux would truncate it, so it never matches (D37); Windows exception codes print as `0xC0000005` |
| `[unhandled exception: T]` | the CLR reported an unhandled exception of type T (same on every OS) |
| `[signal N]` | killed by signal N (POSIX) |
| `[timeout]` | still running after the timeout |
| `[memory limit]` | hit the memory cap |

`[no newline at end of output]` precedes the status when stdout does not end
in a newline. `<stem>.<os>.expected` wins over `<stem>.expected`; `<os>` comes
from the target triple, because the triple decides `sizeof(long)` and the
import libraries (lit passes it as `--expect-os`; msil-run.py's own default
is `linux` under `--runner wsl`, else the host's OS).

A program that the dotnet host or CoreCLR does not start at all (a missing
framework, an invalid runtimeconfig.json, 'Failed to create CoreCLR') is a
configuration error (exit status 2) on every OS, never a status line: Linux
keeps only 8 bits of the host's HRESULT (150 for a missing framework) and
CoreCLR exits with 137 there. An out-of-memory start-up failure is
`[memory limit]`.

## Verification policy (plan D21)

`msil-verify.py` runs two passes, and an error in either fails the test.

1. The assembly itself. An ILVerify error passes only when an entry of
   `msil/utils/ilverify-allowed.txt` matches its code and its message (`CODE:
   MESSAGE`, both full-match regular expressions): `Unverifiable` (localloc,
   cpblk, initblk), `InitLocals` (`.locals` without init), `StackByRef` only
   as `[found Native Int] Expected ByRef on the stack.` (an int32, int64 or
   float64 address fails, also on Linux without PEVerify) and
   `UnmanagedPointer` (ldarg/ldloc of an argument or local declared `void*`;
   no Phase-1 construct produces it, D2, but D21 lists it for phase-3 typed
   signatures). On Windows PEVerify fails on any message except the
   unverifiable-by-design ones and exactly `[found Native Int][expected
   unmanaged pointer] Unexpected type on the stack.` (the native int addresses
   of `System.Buffer::MemoryCopy` for MEMMOVE, D15). ILVerify cannot verify
   `calli` ('ImportCalli not implemented' is a note). Two PEVerify reports
   come from shapes that D12 and D2 always produce, and both need the ildasm
   listing of pass 2 (plan section 12, 'D21 and M0.b item (7) refined'):
   `Cannot modify an imaged based (RVA) static` passes only where the listing
   shows `ldsflda valuetype '<msil>image_t' '<msil>image'` or, since M20,
   `ldsflda valuetype '<msil>relocs_t' '<msil>relocs'` at the reported offset
   (the image copy and the relocation table of the loop in `<Module>::.cctor`,
   and since M22 the image copy in `'<msil>Data'::.cctor`, the first phase of a
   library; any other RVA field fails), and every PEVerify error in a method that
   contains `ldftn` (a function address used as a native int, which PEVerify
   types as a method pointer) is a note, because pass 2 verifies that method
   again. Without ildasm both fail.
2. A neutralized copy. ILVerify does not follow control flow out of a block
   with an error, and PEVerify stops at the first error of most kinds, so in
   pass 1 an allowed construct hides the errors behind it (a type error after
   the localloc frame or after the image ldsflda, say). msil-verify.py
   disassembles the assembly with ildasm, rewrites it so that nothing allowed
   is left (`.locals init`; localloc becomes a pop and a zero; `ldftn` and the
   `ldsflda` of the image and of the relocation table become `ldc.i4.0;
   conv.i`, a native int as the code uses them; cpblk, initblk, ldind, stind
   and calli become calls of `'<msil-verify>...'` stubs that take every
   address as native int and check the sizes, values and arguments too, for
   example `cpblk(native int, native int, int32)`; short branches become
   long), reassembles it with ilasm in a private directory and runs ILVerify
   and PEVerify on it with no allowance except PEVerify's MemoryCopy message.
   Offsets in its messages refer to the copy: `--keep-copy DIR` keeps it.

MSILStackCheck (in the backend) and jitall check stack depths, `.maxstack`
and what the JIT rejects, but not stack types, which the JIT mostly accepts
silently (plan section 9); only the verifiers check those, and native
int/int64 mixing only PEVerify, so that check exists on Windows only.
`--param msil_require_verify=1` (every gate) turns a missing ILVerify, ildasm
or (Windows) PEVerify into a configuration error.

Libraries (M22): `--dep-dir DIR` (repeatable) treats every file directly in
DIR as a `--dep` file, ILVerify references and copies next to the assembly
for PEVerify; a missing or empty directory adds nothing. `--also-verify-deps`
runs both passes on each managed file of those directories too, with the
other files and the `--dep` files as its dependencies, prefixes its messages
with the file name and prints `msil-verify: also verified <files>`; it is a
usage error with `--roundtrip`. The composites pass both with the build's
`shlib/` directory, so every library of `%{msil_shlibs}` is verified with its
program.

## The equivalence allow-list (`msil/utils/equiv-allowed.txt`)

The equivalence oracles of "What runs" (M20, plan 5.12.6) compare canonical
listings exactly, except for what an entry of `msil/utils/equiv-allowed.txt`
lets them ignore. Each entry is one line, `<name> <option>... #
<justification>`, whose options are `%msil_canon`'s (`llvm-cil-dump` and
`mdcanon` implement them alike); a comparison passes the names it allows
(`%msil_equiv --allow <name>`), and `msil-equiv.py` takes the options from the
file. A `skip <oracle>[@<triple>][:<level>] <test path> <reason>` line, whose
oracle is `O3` or `O4` (or, since M21, `O6`), names a test whose difference no
entry removes (a repository-relative path; one that ends in `/` covers a
directory): for O3 and O4 that test builds no equivalence image and
`%msil_equiv` prints `<oracle> skipped: <reason>`; for O6 `msil-verify.py
--md-baseline ... --equiv-skip <file> --test <path>`, which reads the line
with `msil-equiv.py`'s parser, prints `O6 skipped: <reason>` and compares no
PEVerify messages. Every other check of the test still runs, and a line
covers its own oracle only. A line that names an MSIL triple
(`@<triple>`) or a level (`:<level>`, one of `O0`, `O1`, `O2`, `O3`, `Os` and
`Oz`) covers the test only when the build has that triple and level: the
composites pass the test's `--triple` and `--level` to `msil-equiv.py` and
`msil-prog.py --equiv-skip`, which match alike. A line that names neither
covers every triple and level; a triple that does not end in `-msil`, another
level or another oracle is a usage error (exit 2; plan section 12, 'Plan
5.12.6 amended (`skip` lines)'). Lines that start with `#` are comments. A
new entry or `skip` line needs a justification here and in the file, and a
self-test in `harness/equiv/` for an entry.

Every line of the file:

| Line | Option | Allowed in | Why |
|---|---|---|---|
| `assembly-hash` | `--assembly-hash=ignore` | O3, O4 | `msil-ld` writes the Assembly row's hash algorithm as SHA1 (0x8004), as the C# compiler does; ilasm writes 0 without a `.hash algorithm` directive, which `-S` and `--lto-emit-asm` text never print |
| `ilasm-params` | `--params=drop` | O3, O4 | ilasm adds a Param row per parameter (`A_<n>` when unnamed, `args` on the entry point, `A_0` on P/Invoke stubs); the compiler and `msil-ld` write none, and the dropped rows have flags 0 and no marshalling or constant |
| `ilasm-initlocals` | `--initlocals=ignore-without-locals` | O3, O4 | both ilasms keep an explicit `.maxstack` below 8 on a small body without `.locals` and exception clauses by writing a fat header with `InitLocals` set; the compiler and `msil-ld` never set it (D13), and bodies with locals compare as they are |
| `snan-quiet` | `--snan=quiet` | O3, O4 | both ilasms quiet float32 signaling-NaN constants (`ldc.r4`), while the encoder and `msil-ld` keep their bits; float64 operands, constants and data bytes compare as they are |
| `pinvoke-module` | `--pinvoke-module=strip` | O3 | `-S` binds P/Invokes through the frozen D14 table, `msil-ld` through the native libraries with their own module names (Q13), so ImplMap scopes and ModuleRef rows differ; entry names, flags and signatures compare as they are |
| `pinvoke-access` | `--pinvoke-access=ignore` | O3 | `-S` prints standalone P/Invokes `public`, `msil-ld` writes its stubs `assembly`; only the member access of methods with `PinvokeImpl` is ignored |
| `scopes` | `--scopes=strip` | O3, O4, only when `msil_references` is not `none` (M21) | `-S` and `--lto-emit-asm` text print `[mscorlib]`, while the reference legs bind the base class library to the reference packs |
| `skip O3 msil/test/M1/07-globals/alias.c` | | | aliases: `-S` gives the function aliases `chain_alias`, `pub_alias` and `weak_alias` forwarding methods and replaces the data aliases by their aliasees, while the object's ALIAS symbols share the aliasee's method and give `counter_alias` and `table_alias` address fields of their own (plan 5.8.10) |
| `skip O3 msil/test/M1/07-globals/weak-undefined.c` | | | `extern_weak`: `-S` folds the `extern_weak` declarations to null with no field, while `msil-ld` gives the object's UNDEFINED WEAK symbols zero fields (`'<msil>fn_maybe'`, `weak_data`) and zero bytes in the image, which the code then loads (plan 5.8.10, 5.9.6 step 3) |
| `skip O6 msil/test/M1/07-globals/alias.c` | | | aliases: `msil-ld`'s image has the address fields `counter_alias` and `table_alias` of the object's ALIAS symbols, which the `-S` build replaces by their aliasees (plan 5.8.10), so PEVerify `/md` reports them (`0x8013129D` on `'<Module>'::'counter_alias'` and `'table_alias'`, at `-O0` and `-O2`) and not for the baseline, on a host where it reports every `<Module>` field of both images (M21; M20's post-commit `obj` leg) |
| `skip O6 msil/test/M1/07-globals/weak-undefined.c` | | | `extern_weak`: `msil-ld`'s image has the zero fields `'<msil>fn_maybe'` and `weak_data` of the object's UNDEFINED WEAK symbols, which the `-S` build folds to null with no field (plan 5.8.10, 5.9.6 step 3), so PEVerify `/md` reports them (`0x8013129D`, at `-O0` and `-O2`) and not for the baseline, on a host where it reports every `<Module>` field of both images (M21) |
| `skip O3 msil/test/M1/10-calls/rust-shaped.ll` | | | aliases: `llc`'s text gives the `unnamed_addr` alias `sum_b` a forwarding method that calls `sum_a`, while the object's ALIAS symbol shares `sum_a`'s method (plan 5.8.10) |
| `skip O3@x86_64-pc-windows-msvc-msil:O2 msil/test/M1/11-programs/crc32.c` | | | `unnamed_addr` C strings: at `-O2` on the Windows triple, `msil-ld` shares the equal STRINGS segments of the comdat string literal `'??_C@_0BB@JCEFLFJE@0123456789abcdef?$AA@'` and `put_hex.digits` (plan 5.9.9 rule 6), while `-S` keeps Phase 1's layout with one copy of each (plan 5.8.10), so the image's `.data` is 32 bytes smaller (1776 bytes against 1808) and every later field offset differs; at `-O0` `put_hex.digits` is not `unnamed_addr`, and on the Linux triple ConstantMerge folds the private literal into `put_hex.digits`, so there both sides are equal and O3 runs |
| `skip O4 msil/test/M1/07-globals/alias.c` | | | aliases: the LTO object keeps the data aliases `counter_alias` and `table_alias` as ALIAS symbols with address fields of their own, which the image stores and loads, while the `--lto-emit-asm` text replaces them by their aliasees (plan 5.8.10) |
| `skip O4 msil/test/M1/07-globals/weak-undefined.c` | | | `extern_weak`: `msil-ld` gives the LTO object's UNDEFINED WEAK symbols zero fields (`'<msil>fn_maybe'`, `weak_data`) and zero bytes in the image, which the code then loads, while the `--lto-emit-asm` text folds them to null with no field (plan 5.8.10, 5.9.6 step 3) |
| `skip O4 msil/test/M2/08-semantics/p1-weak-alias.test` | | | aliases: the LTO object keeps the data alias `arr_alias` as an ALIAS symbol with an address field of its own, which the image stores and loads, while the `--lto-emit-asm` text replaces it by its aliasee `base_arr` (plan 5.8.10) |
| `skip O4 msil/test/M2/08-semantics/p1-weak-alias-kept.test` | | | aliases: the LTO object keeps the data alias `arr_alias` as an ALIAS symbol with an address field of its own, which the image stores and loads, while the `--lto-emit-asm` text replaces it by its aliasee `base_arr` (plan 5.8.10) |
| `skip O4 msil/test/M2/08-semantics/p1-weak-alias-main.test` | | | aliases: the LTO object keeps the data alias `arr_alias` as an ALIAS symbol with an address field of its own, which the image stores and loads, while the `--lto-emit-asm` text replaces it by its aliasee `base_arr` (plan 5.8.10) |
| `skip O4 msil/test/M2/08-semantics/p1-weak-alias-main-kept.test` | | | aliases: the LTO object keeps the data alias `arr_alias` as an ALIAS symbol with an address field of its own, which the image stores and loads, while the `--lto-emit-asm` text replaces it by its aliasee `base_arr` (plan 5.8.10) |
| `skip O4 msil/test/M2/08-semantics/p1-weak-alias-reversed.test` | | | aliases: the LTO object keeps the data alias `arr_alias` as an ALIAS symbol with an address field of its own, which the image stores and loads, while the `--lto-emit-asm` text replaces it by its aliasee `base_arr` (plan 5.8.10) |
| `skip O4 msil/test/M2/08-semantics/weak-undef-guarded.test` | | | `extern_weak`: `msil-ld` gives the LTO object's UNDEFINED WEAK symbols zero fields (`'<msil>fn_hook'`, `'<msil>fn_other_fn'`, `weak_data`) that the guards load, while the `--lto-emit-asm` text folds them to null with no field and the guards to constants (plan 5.8.10, 5.9.6 step 3) |
| `skip O4 msil/test/M2/08-semantics/zoo.test` | | | `extern_weak`: `msil-ld` gives the LTO object's UNDEFINED WEAK symbol `maybe` the zero field `'<msil>fn_maybe'`, while the `--lto-emit-asm` text folds it to null with no field (plan 5.8.10, 5.9.6 step 3) |
| `skip O4 msil/test/M2/08-semantics/zoo-archive.test` | | | `extern_weak`: `msil-ld` gives the LTO object's UNDEFINED WEAK symbol `maybe` the zero field `'<msil>fn_maybe'`, while the `--lto-emit-asm` text folds it to null with no field (plan 5.8.10, 5.9.6 step 3) |
| `skip O4 msil/test/M2/00-link/spill-limits.test` | | | spill classes: `--lto-emit-asm` text has no per-type limit, so `msil-ld`'s image moves members into `'<msil>Spill<N>'` classes that the text does not have (plan 5.9.12) |
| `skip O4 msil/test/M2/00-link/reloc-loop.test` | | | prebuilt CIL objects: the composite links the CIL object `r4.o` in `libr4.a`, so its `--also-il` link stops with `'--lto-emit-asm cannot link CIL objects'` and there is no text to compare (plan 5.9.13) |
| `skip O4 msil/test/M2/02-lto/mixed-objects-bitcode.test` | | | prebuilt CIL objects: the composite links CIL objects (`objpart.o`, and `arobj.o` in `libmixedpre.a`) beside bitcode, so its `--also-il` link stops with `'--lto-emit-asm cannot link CIL objects'` and there is no text to compare (plan 5.9.13) |
| `skip O4 msil/test/M2/02-lto/lto-partitions.test` | | | `--lto-partitions=2`: the composite's link writes two partitions, so its `--also-il` link stops with `'ThinLTO and --lto-partitions cannot write ILAsm text'` and there is no text to compare (plan 5.9.13) |
| `skip O4 msil/test/M2/04-shared/` | | | library links (M22): `-shared libraries and links against them have no --lto-emit-asm text`; `msil-ld` stops with `--lto-emit-asm cannot write a -shared library` and, for a link with a library input, `--lto-emit-asm cannot import from a .NET library`, so `msil-prog.py --equiv-skip` builds neither the equivalence link nor the `--also-il` text |
| `skip O4 msil/test/M2/08-semantics/p2c-bsymbolic.test` | | | library links (M22), with the same reason |
| `skip O4 msil/test/M2/08-semantics/p2c-cycle.test` | | | library links (M22), with the same reason |
| `skip O4 msil/test/M2/08-semantics/p2c-weak-def.test` | | | library links (M22), with the same reason |

The `skip` lines are of four kinds (plan 5.12.6): 5.8.10's whole-program
differences (under O6 too, since M21, for the two tests whose image has
fields that the `-S` build lacks), where `-S`, llc and `--lto-emit-asm` text keep M12's alias
forwarding methods and data aliases replaced by their aliasees, fold
`extern_weak` declarations to null and keep Phase 1's layout of
`unnamed_addr` C strings, one copy each, while an object has ALIAS and
UNDEFINED WEAK symbols and `msil-ld` shares live STRINGS segments with equal
bytes inside one object too, by plan 5.9.9 rule 6 (O3 against `-S` or llc
text, O4 against `--lto-emit-asm` text); spill classes under O4; and O4 for
links that `--lto-emit-asm` cannot write as one text module (prebuilt CIL
objects or archives of them, ThinLTO and `--lto-partitions`); and, since M22,
O4 for the tests that build `-shared` libraries or link against them, which
have no `--lto-emit-asm` text at all (`04-shared/` as a directory, and the
three library rows of the matrix). Under `lto` with `msil_equiv=1` these
tests print `O4 skipped: <reason>` and run every other check (M22's gate
shows it on `04-shared/lib-and-prog` and `08-semantics/p2c-cycle`). Only crc32.c's
line names a triple and a level, because its difference exists only in the
Windows triple's `-O2` build; every other line covers every triple and level.

O3 allows `assembly-hash`, `ilasm-params`, `ilasm-initlocals`, `snan-quiet`,
`pinvoke-module` and `pinvoke-access`; O4 the first four (linked text and
`msil-ld` bind P/Invokes alike); both add `scopes` in the reference legs. The
object oracle of M19 (`cli-corpus-check.py --mode object`,
`llvm/test/CodeGen/MSIL/object-form-text.ll`) uses the options of
`ilasm-params`, `ilasm-initlocals` and `snan-quiet` directly.

## Reproducing a failure

`lit -v` prints every command it ran with the substitutions expanded. The
intermediate files are next to the test in the build tree:
`build/tools/msil/test/<dir>/Output/<name>.c.tmp.<L>.{il,dll,out}`, and the
native oracle's next to them (`.native.exe` and `.native.out`; under the E7
oracle `.native.o`, `.native.elf` and `.native.out`). Re-run one step by
copying its command line, for example `python msil/utils/msil-run.py
--show-stderr --dotnet ... --runtimeconfig
build/tools/msil/test/msil-net8.runtimeconfig.json <dll>` to see the program's
stderr, `python msil/utils/msil-jitall.py <dll>` to find the method the JIT
rejects, or `python msil/utils/msil-verify.py ... --keep-copy <dir> <dll>` to
see the neutralized copy that pass 2 of msil-verify.py verified.

A `%{msil_prog}` test builds each level in its own directory,
`build/tools/msil/test/M2/<dir>/Output/<name>.test.tmp.<L>.d/`: `obj/` holds
the CIL objects (bitcode under `lto`) and `stock/` the stock-clang bitcode,
`ar/<NAME>/` each archive and its members, `shlib/` the libraries of
`%{msil_shlibs}` (`shlib/<NAME>.dll`; their objects in `shlib-obj/<NAME>/`,
interfaces in `shlib-iface/` and `shlib-iface-obj/<NAME>/`; natively DLLs and
import libraries, or `lib<NAME>.so`), and `prog.exe` is `msil-ld`'s image
(with `msil_link_output=il`, `prog.il` is its text and `prog.exe` ilasm's
image); with `msil_equiv=1`, `equiv/` holds the `--no-gc-sections` image (and
under `lto` its text), and a one-file test under `obj` (in
`Output/<name>.c.tmp.<L>.d/`) keeps the reference `asm/prog.il` and ilasm's
`asm/prog.exe`, so `%msil_equiv` can be re-run on them; the transcripts are
`<name>.test.tmp.<L>.out` (and `.native.out`, `.netfx.out`, `.rt.out`, and
under `msil_also_run` `.also-net<major>[-tc0].out`), and
the native build is in `<name>.test.tmp.<L>.native.d/`. `msil-prog.py` prints
every command it runs (`msil-prog: <command>`), with the sources relative to
that directory, so a step can be re-run from there; `clang -###` on the link
line shows the `msil-ld` command, and `-Wl,-Map=<file>` (with the image's
layout, methods, fields and P/Invokes since M20), `-Wl,--why-extract=<file>`,
`-Wl,--print-gc-sections` or `-Wl,--trace` added to it show what `msil-ld`
linked and why; `-Wl,--print-imports` shows which native library each P/Invoke
binds to (M15). A run that needs `%{msil_deps}` copies them into the private
directory of that run, so re-run it with the same `--dep FILE... --` options
before the program (`msil-run.py --dep-dir DIR` copies every file of a
directory, as the composites do with `shlib/`). `llvm-cil-dump --link-info
shlib/<NAME>.dll` prints a library's symbol table, and `-Wl,--trace-symbol=<s>`
on the program's link shows which library binds `<s>`. `python
msil/utils/msil-prog.py --help` describes every option.
