# MSIL backend: status of Phase 1

State after milestone M19 (2026-10-25, branch `msil`, with upstream llvm-project main at
`64f50ffd4f1c` merged). Phase 1 (milestones M0-M10) is the "first milestone" of
[Design.md](Design.md) section 12: one C file compiled with `clang -S` to ILAsm text,
assembled with `ilasm`, run on .NET. M11 followed up the first upstream merge. M12, the first
milestone of Phase 2, fixed the backend items every Phase 2 path needs (aliases, weak
references, empty `asm` statements with operands, inline `__int128` division, shared frame slots
for structs passed by value) and gave the WSL legs a native Linux oracle and a private .NET
runtime. M13 landed the shared plumbing of Phase 2 without changing any IL: WebAssembly's import
attributes and `#pragma comment(lib)` on MSIL triples, the driver's new error text for object
files, the lld flavor `msil` with a stub `msil-ld`, linker options in the bitcode symbol table
of MSIL modules, and the first headers of the ECMA-335 library and of CIL objects. M14 made
programs of several files work: `clang -c` wrote LLVM bitcode (since M19 `clang -flto -c`
does), clang has an MSIL toolchain
that links with `msil-ld`, and `msil-ld` links bitcode files and archives by lld's rules, with
LTO, into the ILAsm text of the whole program, which `ilasm` assembles (section 2, "Programs of
several files"). M15 made `msil-ld` bind every function that no file of the program defines to
the native libraries that a native link of the same program reads (import libraries, DLLs, ELF
shared objects with symbol versions, GNU ld scripts), so linked programs call the C library, the
Win32 API and native libraries of their own through P/Invoke, and every reference that cannot
bind is a named link error (section 2, "Native libraries and P/Invokes"); `clang -S` and `llc`
keep the frozen standalone import table of D14. M16 added the writer half of the ECMA-335
library in `llvm/lib/CLI`, which builds metadata, IL and method bodies, and two C# tools that
check it from outside, among them `mdcanon`, the canonical listing that later milestones compare
assemblies with (section 2, "The ECMA-335 library and the metadata oracles"). M17 completed the
library for Phase 2: a PE writer with the envelope of the C# compiler's images (executables,
libraries and the netmodules that CIL objects will be), a reader of PE images and metadata, the
codecs of the link-info record that CIL objects will carry, and a dumper, with two tools:
`llvm-cil-dump`, whose canonical listing is now the one the tests compare (`%msil_canon`), and
`llvm-cil-copy`, which rewrites an ilasm image through the reader and the writer, so the first
images written without ilasm verify and run on .NET 8, .NET 10, .NET Framework 4.8 and WSL;
M16 and M17 changed no IL. M18 made the backend emit binary CIL: every instruction it emits is
one MC instruction of one CIL opcode, which the ILAsm printer prints and a table-driven encoder
encodes, byte for byte as ilasm does on every method of the test corpus, so the text and the
CIL objects of M19 come from one stream of instructions. The text changed in two ways only:
the shortest instruction forms, short branches included (plan Q26), and `nomangle` in every
P/Invoke (section 2, "Binary CIL"). M19 made `clang -c` (clang-cl `/c`) and `llc
-filetype=obj` write CIL objects: ECMA-335 netmodules that hold the backend's binary CIL, a
link-info record that says what the symbol table, sections and relocations of a native object
say, and a COFF symbol table that `llvm-nm`, `llvm-ar` and GNU `nm` and `ar` read. `msil-ld`
links them from M20, so until then a program of several files is compiled with `-flto -c`, and
`clang -S` and llc write the same text as before (section 2, "CIL objects"). The linking of CIL
objects, `msil-ld`'s own assembly images and native data imports (M20), the binding of the .NET
base class library (M21) and libraries (M22) follow in M20-M23 (plan section 6). How to build
and use it is in
[GettingStarted.md](GettingStarted.md); the decisions referred to as D1-D38 are in section 3 of
[ImplementationPlan.md](ImplementationPlan.md), and its section 12 records every later
amendment.

## 1. Design.md section 12 exit criteria

| Criterion | How Phase 1 meets it |
|---|---|
| About 25 small C programs compile at `-O0` and `-O2`, run, and match native clang's output, as a lit suite in CI | `msil/test/M1/11-programs/` has 20 programs (fib, sieve, quicksort over a global array, mergesort, n-queens, hanoi, matrix multiplication, crc32, a string table with relocations, a bytecode VM dispatching through a function-pointer table, fnv1a, string functions, bignum addition, life, fixed-point mandelbrot, collatz, a struct-heavy simulation, linked lists and binary trees in static pools, K&R code). With the struct and function-pointer programs of `08-fnptr/`, `09-structs/` and `10-calls/` and the other execution tests in `msil/test/M1/` (143 in all: 99 C programs, 43 IR files and one test that compiles a C program with `-g`), each test runs at `-O0` and `-O2` and its transcript (stdout and exit status) must equal the one a native clang build produces (on Windows linked with the compiler-rt builtins of the Visual Studio LLVM, for the `__int128` helpers a native build calls; for the Linux triple in WSL, a native Linux build linked on Windows and run in WSL, the E7 oracle of GettingStarted.md section 7); the gate also runs every C program of `msil/test/M1` at `-O1`, `-O3`, `-Os` and `-Oz` and every IR test that uses `%{msil_ll}` (40 of the 43) at `-O1` and `-O3` (the `more-levels` leg, on .NET 8); the three trap tests `01-int/trap-call-before-div.ll`, `01-int/trap-call-before-rem.ll` and `03-control/trap.ll`, like the `-g` test, have explicit RUN lines that pin `-O0` and `-O2` in every leg. The gate runs the suite on .NET 8 (tiered and with `DOTNET_TieredCompilation=0`), .NET 10, .NET Framework 4.8, and in WSL on a private .NET 10 for `x86_64-unknown-linux-gnu-msil` (tiered and optimizing JIT), assembles the programs with the Framework ilasm, the modern Windows ilasm and the Linux ilasm, and round-trips them through the modern ildasm and ilasm (Design.md 5.5). It also runs the upstream regression tests of the 55 upstream files the fork changes (327 lit tests, all of `TargetParserTests` and `MCTests`, and `LLDAsLibAllTests`; the upstream-hooks check of GettingStarted.md section 7). Since M18 it also requires that the backend's binary encoding of every method of the C programs of `msil/test/M1`, at `-O0` and `-O2`, equal what ilasm assembles from the same text (the `encoder-parity` leg; section 2, "Binary CIL"). Since M19 it also requires that the CIL object `clang -c` writes of each C program of `msil/test/M1` at `-O0` and `-O2`, and of each program of `11-programs` for the Linux triple, list exactly like ilasm's object of its object-form text, link-info record included, come out the same from two directories, open in `mdcanon` and both ildasms, agree with its record, and give `llvm-nm`, `llvm-ar` and, in WSL, GNU `nm` and `ar` the symbols of plan 5.8.6 (the `obj-text` and `obj-text-wsl` legs; section 2, "CIL objects"). CI (`.github/workflows/msil-ci.yml`) runs the same scripts as the local gate; it has a manual trigger only until the repository owner enables more |
| FileCheck tests on the emitted `.il` | 132 llc and MIR tests in `llvm/test/CodeGen/MSIL` (125 `.ll`, among them `lto-module-flags.ll` for the module flags of `msil-ld`, `imports-linked.ll` for the linked import mode of M15 and the 12 `object-*.ll` tests of M19, which write CIL objects with `llc -filetype=obj`, read them back with `llvm-cil-dump`, `llvm-nm` and `llvm-ar` and compare them with ilasm's objects of their object-form text, and 7 `.mir`; both x86_64 triples, the aarch64 triples where the ABI differs, ilasm round trip where a module is complete; since M18 they pin the shortest forms and `nomangle`) and one test there of the bitcode symbol table (`irsymtab-linker-options.ll`), the 9 MC tests of `llvm/test/MC/MSIL` (M18: `llc -show-mc-encoding` for every operand kind, branches at the short-form boundaries, `switch` entries and the synthesized `.cctor` and `'<msil>Main'`, the shortest forms, the BCL members, every operand class in both spellings of `-M object-form`, `nomangle` on four triples with its ImplMap flags 0x0201 read back from ilasm's image, and the clean error of `llvm-mc -filetype=obj`), 22 clang tests in `clang/test/CodeGen/MSIL` (including `design-milestone1.c`, Design.md 12's literal command on fib, and, since M19, `emit-obj.c`, whose `cc1 -emit-obj` writes CIL objects and which reads the headers and record of the objects of `-c` and clang-cl `/c`), the driver tests in `clang/test/Driver/msil-toolchain.c` and, for the MSIL toolchain of M14, `msil-link.c` (with M15's `--ucrtbase-imports` on windows-msvc), `msil-lto.c` and `msil-header-search.c`, and, for M19, `msil-object.c` (`-c` and clang-cl `/c` write CIL objects: their cc1 jobs and errors, and the bytes from two directories, `llvm-nm` letters and object-form text of what they write), the 47 `msil-ld` tests in `lld/test/MSIL` (`dispatch.test` of M13, the 25 of M14, ten of which M15 updated to its rules, and the 21 `native-*.test` of M15, whose ELF shared objects, import libraries and DLLs `yaml2obj`, `llvm-mc`, `ld.lld`, `lld-link` and `llvm-lib` make; `dispatch.test` and `dispatch-coff.test` check which program names reach `msil-ld` and that the other lld flavors, COFF's included, dispatch as before; all but `dispatch-coff.test` also run with `LLD_IN_TEST=2`), the 14 tests of the ECMA-335 tools `llvm-cil-dump` and `llvm-cil-copy` in `llvm/test/tools` (M17; their inputs are `yaml2obj` copies of ilasm images), and unit tests in `llvm/unittests/Target/MSIL` (`MSILImportsTest` pins the standalone import table and the linked import mode; M18's suites `MSILCILRecords`, `MSILBCLMembers`, `MSILBranchForms`, `MSILMCCodeEmitter` and `MSILAsmBackend`, 42 tests, pin the 219 CIL records against `CILOpcodes.def`, the BCL list, the choice of branch forms, the bytes and fixups of every record and the asm backend, and `MSILTargetStreamerTest` the streamer and both spellings of every operand class) and, for the ECMA-335 library (its headers and the link-info record of M13, its writer half of M16, its reader, PE writer, codecs and dumper of M17), `llvm/unittests/CLI` (`CLITests`, 18 suites; at least 170 tests, the M17 gate's floor) |
| `ilverify` reports no stack-shape errors | replaced by the verification policy of section 4 (decision D21, approved by the user 2026-10-12), enforced in every gate |

## 2. What works

### Toolchain and output

- `clang --target=<msil triple> -S` and `llc` write ILAsm text; `ilasm -dll -pe64 -x64` (the
  .NET Framework 4.8 ilasm or the NuGet ilasm 10.0.12) assembles it. One C file (one LLVM module)
  is one assembly, and so is a program of several files that `msil-ld` links (M14, below).
  Every optimization level works.
- Since M18 the text uses the shortest instruction forms, short branches (`br.s`, `blt.s`, ...)
  wherever the target is in reach included, and every P/Invoke is `pinvokeimpl("<library>"
  nomangle cdecl)`; `llc -show-mc-encoding` prints the binary encoding of every instruction
  ("Binary CIL" below).
- The module references only `mscorlib` (D10), so one assembly runs on .NET 8, 9 and 10 and, for
  Windows triples, on .NET Framework 4.8 (as an `.exe`). The gate runs .NET 8, 10 and Framework
  4.8; .NET 9 was only covered by the planning experiments.
- C functions become static methods of `<Module>`: `public` for external linkage, `assembly`
  otherwise. A generated `'<msil>Main'(string[])` is the entry point. It calls `main(void)`, or
  `main(int, char **)` with a NULL-terminated `argv` built from
  `Environment.GetCommandLineArgs()` (`argv[0]` is the `.dll` path under `dotnet x.dll`); any
  other signature of `main` is an error (D27).
- The assembly is named after the source file's stem, or `-mllvm -msil-assembly-name=<name>`
  (D28); in a program that `msil-ld` links, after the output file (M14, below).
- `-c` (clang-cl `/c`) writes a CIL object since M19, and so do `llc -filetype=obj` and
  `clang -cc1 -emit-obj` ("CIL objects" below). `-flto -c` and `-emit-llvm -c` write LLVM
  bitcode, and without `-S` or `-c` clang compiles to bitcode and links with `msil-ld` (M14,
  below), which links CIL objects only from M20. Before M19 `-c` wrote bitcode as `-flto -c`
  does, `-fno-lto -c` stopped with the driver error `CIL object files are not supported for
  MSIL target '<triple>' yet; use -flto to write LLVM bitcode objects, or -S to write ILAsm
  text` (M13's text; before M14 every `-c` and every link stopped there), and `llc
  -filetype=obj` with `MSIL object file emission is not implemented; use -S (ILAsm text)`.
  Assembler input (`-x assembler`, `.s` and `.S` files) and an assemble step (`-save-temps` or
  `-save-temps=obj` with `-c`, `-via-file-asm`, clang-cl's `/FA` and `/Fa`, which an explicit
  `-flto` ignores) stop with `assembling ILAsm text is not supported for MSIL target
  '<triple>'; compile C or LLVM IR sources instead, or use -S to write ILAsm text`, and
  `-fno-integrated-as -c` with `there is no external assembler that can be used on this
  platform`. `llvm-mc -filetype=obj` and `clang -cc1as -filetype obj`, which crashed or
  asserted before M18, stop with `assembly parsing is not supported for MSIL`.
- `msil-ld` is lld's flavor `msil` (M13): `msil-ld` (a copy of `lld`), `lld -flavor msil` and
  every program name with an `msil` component (such as `x86_64-unknown-linux-gnu-msil-ld`)
  start it, and the other flavors (`ld.lld`, `lld-link`, `ld64.lld`, `wasm-ld`) dispatch as
  before. M13's stub, which linked nothing, is a linker since M14 (below).
- `#pragma comment(lib, "<name>")` (M13) is kept on every MSIL triple, as the native triple
  spells it in `!llvm.linker.options`: `-l<name>` on the Linux triples, `/DEFAULTLIB:<name>.lib`
  on the Windows ones. The bitcode symbol table of an MSIL module carries these options, as it
  does for COFF, and `msil-ld` links the libraries they name (M15, "Native libraries and
  P/Invokes" below); `-S` output does not use them. The Linux MSIL triples
  accept only the `lib` kind and ignore the others with clang's warning, as ELF triples do; the
  Windows MSIL triples accept every kind, as MSVC triples do. Before M13 the pragma was dropped
  silently on the Linux triples.
- Bitcode of an MSIL triple and bitcode of its native triple are compatible
  (`Triple::isCompatibleWith`, M13), so the IR linker links them without the warning
  `Linking two modules of different target triples`.
- Unsupported input ends in an error that names the function or feature, with exit status 1,
  never a crash.
- `-mllvm` passes backend options from clang as llc takes them, and llc's stop points
  (`-stop-after=`, `-stop-before=`) work at every MSIL pass from `msil-isel` on: llc stops
  there, and the MIR it prints passes the machine verifier. The MIR of instruction selection
  reads back and compiles on with `-start-after=msil-isel` to the same IL as the full pipeline
  (GettingStarted.md section 8). Reading MIR back has three limits, all from upstream code;
  none of them changes the IL of a normal compile:
  - MIR with a floating-point infinity or NaN constant reads back at no stop point,
    `msil-isel` included: `CONST_F32` and `CONST_F64` print it as `float +inf`,
    `double -inf` or `double +qnan`, which the MIR parser rejects
    (`expected a floating point literal`), an upstream MIR printer and parser mismatch that
    WebAssembly shows too.
  - For a function that takes block addresses (computed goto), the IL of
    `-start-after=msil-isel` lacks the `@tmpN: // Address of block that was removed by CodeGen`
    labels that the full pipeline prints; every other line is the same. IndirectBrExpand, which
    runs before instruction selection, replaces each `blockaddress` with the block's number, so
    the IR inside the MIR takes no block address any more, while the full pipeline still prints
    an unused label for each block whose address the input IR took.
  - MIR printed at `branch-folder` or a later stop point does not read back yet when a `switch`
    became a jump table (`llc -run-pass=none` asserts `Cannot create an empty jump table!`:
    BranchFolder empties the table the MSIL `switch` does not reference, an upstream MIR
    printer and parser issue that WebAssembly shows too).

### Programs of several files (M14)

How to compile, link, assemble and run such a program, and every option, is in
GettingStarted.md section 3 ("Programs of several files").

- **clang's MSIL toolchain.** Every Linux, windows-msvc and windows-gnu MSIL triple gets
  `MSILToolChain` over its native toolchain (Linux, MSVC or MinGW), which keeps the native
  header search, predefines and ABI; any other MSIL triple (Darwin, FreeBSD, Cygwin, ...) ends
  with `unknown target triple '<triple>'`, also for a command that only links, and never runs a
  native linker. `-flto -c` writes LLVM bitcode (`-emit-llvm-bc -flto=full`), as plain `-c`
  did until M19 (it writes CIL objects since then, "CIL objects" below) and as every compile
  of a command that also links still does until M20, and a link runs
  `msil-ld` (never a native linker) with the triple's C library (`-lc -lm`, `-lucrt -lkernel32
  -loldnames --ucrtbase-imports`, ...), the LTO level of `-O` and the `-L`, `-l` and `-Wl`
  options in command-line order; the default output is `a.exe`, and `-nostartfiles` is
  accepted silently, because MSIL has no crt files. On the `-S` command line only
  `-mrelocation-model static` (instead of PIC) and the missing `-funwind-tables=2` change (on
  the aarch64 triples also two target
  features, `-fmv` no longer off and `+outline-atomics`, from the compiler-rt runtime default),
  and the IL is the same: 495 `-S` compiles of the 99 C programs of `msil/test/M1` (both
  x86_64 triples at `-O0` and `-O2`, `aarch64-unknown-linux-gnu-msil` at `-O2`) gave identical
  IL and diagnostics.
- **What `msil-ld` does.** It links LLVM bitcode files and archives of them, in command-line
  order, by lld's rules: archive members are lazy (also through `-l`, `--start-lib`/`--end-lib`;
  `--whole-archive` loads them all), and, as in lld, every member that defines a runtime
  library-call name of the target is extracted before LTO (a limit, below). Symbols resolve by a
  port of lld/ELF's resolver (plan 5.9.5) in either input order: strong, weak and common
  definitions, weak undefined references, aliases, and COMDAT groups (the first copy wins; the
  selection kinds `exactmatch`, `largest` and `samesize` are an error naming the group). LTO
  compiles the merged program, in which only `main` and the `-u` symbols stay visible, and
  `--lto-emit-asm` writes its ILAsm text, which `ilasm -exe` assembles into one image for .NET
  and .NET Framework. The assembly is named after the output (`--assembly-name` or `-soname`
  name another), through the module flag `msil-assembly-name`. A function that no input
  defines binds to a native library of the link (M15, "Native libraries and P/Invokes" below),
  and a symbol that binds nowhere is `undefined symbol: <name>`. (In M14 the backend bound such
  functions through the Phase-1 import table, D14, as it still does for a module that has
  `msil-ld`'s module flag `msil-lto` without M15's `msil-linked`.) Bitcode from stock clang for
  the native triple links with one warning per link (plan Q12; its limits are in
  GettingStarted.md section 3); every input must have the link's architecture, OS and
  environment. The link's triple (`--target`, else the first bitcode input's with `-msil`
  appended) must be one of D6's, x86_64 or aarch64 on Linux, windows-msvc or windows-gnu: any
  other, such as that of stock bitcode for Darwin or i686, stops the link with `unsupported
  target triple '<codegen triple>' for MSIL`. Diagnostics
  use lld's texts (`duplicate symbol`, `undefined symbol`, `undefined hidden symbol`, `symbol
  type mismatch`, `TLS attribute mismatch`, `unable to find library`, ...), and `-Map`,
  `--why-extract`, `--reproduce`, `--trace` and `-y` work as in lld. The same inputs give the
  same output byte for byte (the `det` leg).
- **What `msil-ld` does not do yet.**
  - Write the assembly image itself (M20): every link needs `--lto-emit-asm` and ilasm, and
    any other LTO link, ThinLTO bitcode and `--lto-partitions` above 1 stop with `ThinLTO,
    --lto-partitions and LTO without --lto-emit-asm need CIL object output, which this msil-ld
    does not have; use --lto-emit-asm (clang: -Wl,--lto-emit-asm) to write ILAsm text`.
  - Import native data (M20), or call native variadic functions, native functions with
    aggregate parameters or results, and native functions that take C function pointers
    (Phase 3): "Native libraries and P/Invokes" below.
  - Link CIL objects (M20; clang writes them since M19) or .NET assemblies (M21, M22): such an
    input is `<file>: linking CIL object files and .NET assemblies is not supported yet`.
  - `-shared` (`-shared is not supported yet`; libraries are M22's), an entry point other than
    `main` (M20), `--gc-sections` (M20) and reference assemblies (`--reference`, M21); `-r` is
    never supported.
  - Leave out an archive member that nothing references when it defines a runtime library-call
    name of the target (`fmod`, `floor`, `memcpy`, ...). As lld does, `msil-ld` extracts every
    such bitcode member, so its constructors run and its other definitions and references join
    the link, which can give `duplicate symbol` or `undefined symbol` errors that a native
    static link without LTO would not give. Native LTO through lld behaves the same (plan
    5.9.13; narrowing the names is left for phase 3).
- **LTO and argument promotion.** LTO makes every function but `main` and the `-u` symbols
  internal, so the optimizer may change the parameters of the others.
  `MSILTTIImpl::areTypesABICompatible` keeps ArgumentPromotion from turning a pointer parameter
  into a by-value `half` or `bfloat` one (`_Float16`, `__bf16`), which can need the phase-3
  helpers of section 3, so a program such as `msil/test/M1/02-fp/i128-fp-conv.c` links through
  `msil-ld` at `-O2` as it compiles with `-S` (plan D33 as amended for M14).

### Native libraries and P/Invokes (M15)

How to link against native libraries, with examples, is in GettingStarted.md section 3
("Native libraries and P/Invokes"); plan 5.10 has the contract.

- **What `msil-ld` reads.** Every native library on the command line or found by `-l`, through
  `llvm::object`, in the object format of the triple's OS only. On the Linux triples: ELF
  shared objects (the `.dynsym` definitions with their symbol versions) and GNU ld scripts
  (`GROUP`, `INPUT`, `AS_NEEDED` and `-l` inside them, found as lld/ELF finds them, and an
  absolute path under `--sysroot` when the script lies inside the sysroot, as glibc's `libc.so`
  needs). On the Windows triples: COFF import libraries (short import members with lld-link's
  name types, and MinGW's long-format members), the weak-alias objects of `oldnames.lib` and
  the export tables of native DLLs. Also archives of native objects, whose definitions are only
  named in an error. A library of the other format, given on the command line, by `-l:<file>`
  or by a script, is an error, `<path>: ELF file cannot be imported for target <triple>` on a
  Windows triple (lld-link rejects an ELF input) and `<path>: PE/COFF file cannot be imported
  for target <triple>` on a Linux one (ld.lld rejects a DLL and ignores COFF import members);
  `-l` passes over such a candidate (`--verbose`: `skipping ` and that text) and goes on with
  the next one. A file's machine and ELF class must match the triple (`<file>: ELF machine
  EM_AARCH64 does not match target <triple>`, and the COFF and PE forms). Every file is read
  through the driver, so `--reproduce` captures it.
- **Names (plan Q13, 5.10.11).** A P/Invoke names the module as a native link records it,
  verbatim: the ELF `DT_SONAME` (`libc.so.6`, `libm.so.6`), else the file name; for an import
  library the DLL name of the import member (`api-ms-win-crt-stdio-l1-1-0.dll` from `ucrt.lib`,
  `KERNEL32.dll`, `ucrtbase.dll`); for a DLL that `msil-ld` reads (on the command line, by `-l`
  or by `-l:`) the DLL's file name as found (`native.dll`, `Renamed.DLL`), never the name in
  its export directory, which differs for a DLL renamed after it was linked; never a bare
  `libc`. The entry is the export name: `#<n>`
  for an import by ordinal only (`htons` from `ws2_32.lib` is `WS2_32.dll` `#9`), the
  `EXPORTAS` name, the target of an oldnames alias (`strdup` is `_strdup`). The
  `--lto-emit-asm` text declares each bound function as
  `.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-string-l1-1-0.dll" as
  "_strdup" nomangle cdecl) native int 'strdup'(native int) cil managed preservesig {}`:
  `assembly`, where `-S` prints its standalone imports `public`, and `nomangle` since M18
  (ImplMap flags 0x0201, Design.md 5.3's `ExactSpelling`: the runtime looks the entry up by
  exactly that name; before M18 it printed `cdecl` alone).
- **Bindability (5.10.10).** A P/Invoke binds what `dlsym` and `GetProcAddress` reach: an ELF
  default-version (`@@`) or unversioned definition that is neither thread-local nor
  `GLIBC_PRIVATE` (an IFUNC, such as glibc's `strlen`, is a function: the loader runs its
  resolver), or a COFF import or DLL export of the right kind (an export in a section that is
  not code is data). A call never binds a COFF `DATA` or `CONST` import (for lld-link such a
  member defines only `__imp_<name>`) or a DLL's data export: lookup goes on with the later
  libraries, as in lld-link, and `'<name>' in <module> is data, but <file> calls it as a
  function` is reported only when no later library binds the name. Among ELF libraries the
  first definition wins whatever its type, as with `ld.so`. A definition that cannot
  be bound is kept for the error that names it when nothing else binds the symbol: only a
  hidden version (`symbol 'f' in libfoo.so.1 has only the non-default version f@V1, which a
  P/Invoke cannot bind`), thread-local data (`symbol 'errno' in libc.so.6 is thread-local data
  and cannot be imported; use <errno.h>`, the advice for `errno` only: `h_errno` or a library's
  own `__thread` variable gets the text without it), a `GLIBC_PRIVATE` symbol, and machine
  code in a native archive (`symbol 'atexit' is defined only in native object code
  (<path>/libc_nonshared.a(atexit.oS)); MSIL cannot link machine code`).
- **Resolution order.** Objects and archive members first, by lld/ELF's rules (section 2);
  then the native libraries in command-line order, where the first bindable definition wins
  and two libraries that define one name are no error. A native definition is bound late: it
  never preempts a definition of the program and never extracts an archive member, so a
  program's own `malloc` replaces the C library's for the program's code only (native code
  keeps calling its own). A weak undefined symbol that nothing defines stays null (M12).
  Anything else is `undefined symbol: <name>`.
- **Checked after LTO.** Bitcode symbol tables carry neither the import attributes nor the
  library calls that the optimizer adds (`printf` -> `puts`), so `msil-ld` binds and reports
  on the merged module after LTO, before code generation, as lld/ELF and lld/wasm report
  undefined symbols after LTO. Every error is followed by `>>> referenced by
  <file>:(<function>)` lines (`>>> referenced by ld-temp.o` for a name that only the optimizer
  introduced), and only live references count: a call that LTO removed is not reported. A
  hidden, protected or discarded undefined symbol is still reported before LTO. `msil-ld`
  writes the result into the module, as the module flag `msil-linked`, the attributes
  `msil-import-module` and `msil-import-name` on each bound declaration and the named metadata
  `!msil.imports` for the library functions that code generation may call; in that linked
  mode the backend takes every P/Invoke from these annotations and never consults D14
  (5.10.13). A library call that nothing bound is `library call '<name>' was not resolved by
  msil-ld`, and a `__` runtime helper keeps its phase-3 error.
- **Default libraries (Q14, plan 5.10.4).** The driver's default libraries now bind: `-lc -lm`
  on the Linux triples (from a `--sysroot`, such as the E7 sysroot of GettingStarted.md section
  7), `-lucrt -lkernel32 -loldnames --ucrtbase-imports` on windows-msvc (from `LIB` or the
  detected Windows SDK), `-lmsvcrt -ladvapi32 -lshell32 -luser32 -lkernel32` on windows-gnu
  (from the mingw-w64 import libraries of a `--sysroot` or `-L` directory).
  `--ucrtbase-imports` is an import library built into `msil-ld` (Windows targets only; on
  others `--ucrtbase-imports needs a Windows target, not <triple>`) that binds the 11
  functions native programs take from vcruntime, which `ucrt.lib` lacks: `memcpy`, `memset`,
  `memmove`, `memcmp`, `memchr`, `strchr`, `strrchr`, `strstr`, `wcschr`, `wcsrchr` and
  `wcsstr`, to `ucrtbase.dll`, which exports all of them. A `-lvcruntime` of yours comes
  earlier on the line and wins. `msil-ld` itself has no default libraries.
- **Libraries the program names (5.10.6).** `#pragma comment(lib, "<name>")` and the other
  linker options of bitcode inputs (`/DEFAULTLIB:`, `-l`, `-l:`, the dependent libraries of
  stock ELF bitcode, `/alternatename:`, `/failifmismatch:`) are read from each input when it is
  loaded, from an archive member when it is extracted, and each library is read once. A request
  for an MSVC C or C++ runtime library (`libcmt`, which clang names in every windows-msvc
  object, `msvcrt`, `ucrt`, `vcruntime`, ...) is ignored with a `--verbose` note, because the
  default libraries supply the C runtime; another option is ignored with a warning; a library
  that is not found is `unable to find library from dependent library specifier: <name>` with
  `>>> requested by <file>`. `--no-dependent-libraries` drops them all.
- **`-l` search (5.10.9).** The `-L` directories in order; the first that holds an acceptable
  candidate (`lib<n>.a`, `<n>.lib` and `<n>.dll` on windows-msvc; the MinGW names
  `lib<n>.dll.a` and `<n>.dll.a` too on windows-gnu; `lib<n>.a` and `lib<n>.so` on Linux, where
  `<n>.dll` and `lib<n>.dll` count only as M22's .NET libraries, so `-lfoo` in a directory
  holding `foo.dll`, `foo.lib` and `libfoo.so` takes `libfoo.so`, as ld.lld does) wins, and
  inside it a static library (with bitcode members) wins over an import library or a shared
  one: Design.md 5.2's static-first rule, the opposite of GNU ld's. An archive of machine code
  only is skipped (`--verbose`: `skipping native static archive <path> (no CIL or bitcode
  members)`), and so is a .NET assembly, with a warning that names `--reference` (managed
  libraries are M22's). `-Bstatic` accepts only static libraries until `-Bdynamic`, `-l:<file>`
  takes that file whatever its kind (one of the other object format is the error above), and
  an empty archive (glibc's `libpthread.a` and `libdl.a` since 2.34) is a static library that
  defines nothing, so the `-lpthread` of `-pthread` links against such a glibc.
- **Reporting.** `--print-imports` prints one line per P/Invoke of the output, sorted,
  `<symbol> -> <module>!<entry> [function|data, <version>|-]` (`puts -> libc.so.6!puts
  [function, GLIBC_2.2.5]`), the library functions that code generation calls (`floor`,
  `sqrt`, ...) included, then for each ELF module `<module>: version floor <version>`, the
  highest default version bound in it, those of the library functions that code generation
  calls included (`-` when none is versioned), also when the link fails. A P/Invoke itself
  has no version: at run time it binds the default version of the library that runs it.
  `-Map` lists the native libraries under `Inputs:` and the bound symbols under `Symbols:` as
  `NativeFunction` or `NativeData` rows with the library as the input, and
  `--trace-symbol=<sym>` prints `<library>: shared definition of <sym>`.
- **Tests.** The 21 `native-*.test` of `lld/test/MSIL` cover each reader, each search rule,
  each rejected kind of definition and each error text of plan 5.10.14 on synthetic libraries
  (yaml2obj, llvm-mc, `ld.lld`, `lld-link` and `llvm-lib /def:` make them). The programs of
  `msil/test/M2/03-native/` call the host's C library through the default libraries (string,
  memory, conversion and `malloc` functions, glibc's IFUNCs and its versioned libm functions,
  `kernel32.lib`, `htons` by ordinal, an oldnames alias, the 11 functions of
  `--ucrtbase-imports`) and a `native.dll` or `libnative.so` that the test builds and ships
  next to the program, and run on .NET 8 and Framework 4.8 and in WSL on .NET 10 against the
  native oracles; `msil/test/M2/09-hosted/` links a hosted glibc program against glibc's real
  `libc.so` and `libm.so` scripts (the optional `hosted` leg, GettingStarted.md section 7).
- **Not done yet.**
  - Native data (M20, plan 5.10.12): an undefined variable that a native library defines is
    `importing native data symbol 'environ' (libc.so.6) is not supported yet`, also an
    `extern` variable with the import attributes; a function that the program declares and
    calls but that the library defines as data is `'<name>' in <module> is data, but <file>
    calls it as a function`.
  - Native variadic functions (`printf`, phase 3): `native function 'printf' (libc.so.6) is
    variadic; calls of native variadic functions are not supported for MSIL yet (phase 3)`.
  - Native functions that take or return a struct by value, an `__int128`, a vector or a
    `_Float16` (phase 3): `native function '<name>' (<module>) takes or returns a value that a
    P/Invoke cannot pass natively yet (phase 3)`.
  - Native `long double` functions on x86_64 and aarch64 Linux (glibc and musl) and x86_64
    MinGW (phase 3): `long double` is `double` in MSIL (section 3), but the native one there is
    the x87 80-bit or the IEEE binary128 format, which the native ABI passes in memory, in
    `ST(0)` or in Q registers. A live call of, or reference to, a function of plan 5.10.14's
    `long double` list that native lookup binds is therefore a link error after LTO, `native
    function 'sqrtl' (libm.so.6) takes or returns a value that a P/Invoke cannot pass natively
    yet (phase 3)`, with `>>> referenced by` lines. The list holds the C99 and C11 `<math.h>`
    and `<complex.h>` `l` functions (`sqrtl`, `expl`, `powl`, `fabsl`, `frexpl`, `ldexpl`,
    `modfl`, `nanl`, `nexttowardl`, `cabsl`, ...), `nexttoward` and `nexttowardf` (a `long
    double` parameter), `strtold`, `wcstold` and glibc's `l` extensions (`sincosl`, `exp10l`,
    `significandl`, ...). It also covers glibc's TS 18661-3 `_Float64x` and `_Float128`
    functions, every name in which `f64x` or `f128` is followed by the end of the name or by
    `_` (`sqrtf64x`, `expf64x`, `strtof64x`, `strtof64x_l`, `wcstof64x`, `f32addf64x`,
    `sqrtf128`, `strtof128`, `__isinff128`, ...): with clang, glibc's `<bits/floatn.h>` makes
    `_Float64x` the source's `long double` (`double` in MSIL) while the native function passes
    the x87 or binary128 value, and a P/Invoke has no form of the native `_Float128` on any
    triple. Such a name is never bound quietly; a declaration with an explicit `import_module`
    is exempt (unchecked). windows-msvc (x86_64 and aarch64) and `aarch64-w64-windows-gnu`,
    where the native `long double` is `double` too, bind them. Call the `double` function
    instead (`sqrt` for `sqrtl` or `sqrtf64x`, `strtod` for `strtold`): it is the same function
    in an MSIL program.
  - `atexit` (phase 3, which intercepts it): glibc defines it only in machine code
    (`libc_nonshared.a`) and the UCRT does not export it, so it binds nowhere.
  - Native functions that take a C function pointer (phase 3, P3-B's native-callable thunks):
    native code that calls a function pointer of the program would enter managed code without
    a reverse-P/Invoke transition. Every live call of, or reference to, a function of plan
    5.10.14's callback list that native lookup binds is therefore a link error after LTO,
    `native function 'qsort' (libc.so.6) takes a C function pointer; passing functions to
    native code needs phase 3 thunks`, also when the call passes no function
    (`signal(SIGPIPE, SIG_IGN)`). The list has 46 names: `CreateThread`, `__sysv_signal`,
    `_beginthread`, `_beginthreadex`, `_lfind`, `_lfind_s`, `_lsearch`, `_lsearch_s`,
    `_onexit`, `at_quick_exit`, `atexit`, `bsd_signal`, `bsearch`, `bsearch_s`, `call_once`,
    `ftw`, `ftw64`, `lfind`, `lsearch`, `nftw`, `nftw64`, `on_exit`, `pthread_atfork`,
    `pthread_create`, `pthread_key_create`, `pthread_once`, `qsort`, `qsort_r`, `qsort_s`,
    `scandir`, `scandir64`, `scandirat`, `scandirat64`, `sigaction`, `signal`, `sigset`,
    `ssignal`, `sysv_signal`, `tdelete`, `tdestroy`, `tfind`, `thrd_create`, `tsearch`,
    `tss_create`, `twalk`, `twalk_r` (a name counts by its entry too, so `lfind` through the
    alias of `oldnames.lib` is `_lfind`). It also names glibc's other spellings of these
    functions, which its headers select through an asm label (`__REDIRECT`), so that standard C
    code references them instead: `signal` is `__sysv_signal` without `__USE_MISC`
    (`-std=c99`, `-std=c11` or `-std=c17`, `-ansi`, or `_POSIX_C_SOURCE` alone), and `ftw`,
    `nftw`, `scandir` and `scandirat` are `ftw64`, `nftw64`, `scandir64` and `scandirat64` with
    `_FILE_OFFSET_BITS=64`; `bsd_signal`, `scandirat`, `sigset`, `ssignal` and `sysv_signal`
    take a handler or a callback as well. The error names the symbol the program references
    (`native function '__sysv_signal' (libc.so.6) takes a C function pointer; ...`). A
    declaration with an explicit `import_module` is exempt, unchecked, and is the workaround
    for a call that passes no function of the program.
  - **Known gap until P3-B:** a function address passed to any other native function, such
    as a native library's own callback API or a Win32 callback (a window procedure,
    `EnumWindows`, `SetConsoleCtrlHandler`), is not diagnosed: the program links, and fails at
    run time when native code calls the function.
  - The `Imports:` section of the map, which shows the P/Invoke tokens, comes with M20.

### The link-semantics matrix (M14 and M15, LTO)

`msil/test/M2/08-semantics/` holds the LTO rows of plan 5.9.16, built from the recon's probe
programs. Each case either prints the transcript of the same program built natively (by clang
and lld for the native triple) or stops with a named diagnostic, at `-O0` and `-O2`; none ends
in an exception at JIT or load time. The gate runs them on .NET 8 and .NET Framework 4.8 (the
`net8` leg, and `lto`) and in WSL on .NET 10 for the Linux triple (`wsl-quick`); the native
transcripts are the same on both operating systems. M14 wrote 20 of the 23 tests, M15
`pragma-comment-lib` and `native-callback`, and M19 `knr-two-arities`.

| Case | What it links | Native | MSIL |
|---|---|---|---|
| `zoo` | two objects: a weak definition and the strong one that overrides it, a weak undefined function, COMDAT string literals (windows-msvc), common symbols, a function alias in a table, constructor priorities across objects, pointers to the other object in data, `static` names in both | 63 | 63 |
| `zoo-archive` | `zoo/a.c` with an archive of `b.c`, `c.c` and `d.c`: a weak undefined reference extracts no member | 63 | 63 |
| `zoo-defined` | `a.c`, `b.c`, `c.c` and the archive: the weak undefined function is defined | 223 | 223 |
| `p1-weak-alias` (5 tests) | a weak alias of a `static` function (musl's idiom) and a data alias, against a strong definition in either order, without one, and from a file without `main` | 144; 114 without the strong definition | the same |
| `p2-knr` | calls through declarations without a prototype across objects: an extra argument, a K&R `float` parameter | 23 | 23 |
| `knr-two-arities` (M19) | a declaration without a prototype, `int f();`, called as `f(1, 2)` and, on a path that never runs, as `f(1)`, against the definition `int f(int a, int b)` in the other file | 3 | 3 (LTO; the object row comes with M20, where `f(1)` gets the missing-argument adapter and its warning) |
| `sig-mismatch` | a prototype `f(int, int)` against the definition `f(int)` | 41 | 41 |
| `p4-selectany` | `__declspec(selectany)` data in two objects (Windows triples) | 15 | 15 |
| `p5-tls` | a `_Thread_local` variable used across objects | 12 | `thread-local variable @tcount is not supported for MSIL yet` (phase 3) |
| `p6-ctors` | constructors with priorities in two objects | `BCDM` | `BCDM` |
| `p6-dtors` | destructors with priorities | no transcript (Windows faults at exit; the Linux oracle runs no `.fini_array`) | `global destructor @d150 is not supported for MSIL yet`, and the same for `@d120` (phase 3) |
| `p9-weak-common` (both orders) | a weak definition and a common symbol of one name: the common one wins | 1 | 1 |
| `type-mismatch` | a function and a variable of one name | links, with undefined behaviour | `symbol type mismatch: value` |
| `duplicate-strong` | two strong definitions | link error | `duplicate symbol: shared` |
| `undefined` | a function that nothing defines | link error | `undefined symbol: missing_fn` |
| `weak-undef-guarded` | functions and a variable declared weak and defined nowhere, used only when their address is not null | every address null, 4 | the same |
| `pragma-comment-lib` (M15) | `#pragma comment(lib, "native")` in one of two files names a native library that the test builds (`native.dll` with its import library, or `libnative.so`); the link gets only its directory | a string from the library's data, 42, 20; exit status 12 | the same |
| `native-callback` (M15) | the C library's `qsort` with a comparator of the program | the sorted array | `native function 'qsort' (<module>) takes a C function pointer; passing functions to native code needs phase 3 thunks` (until P3-B), with the module `qsort` binds to: `api-ms-win-crt-utility-l1-1-0.dll` or `libc.so.6` |

The plain multi-file cases of plan 5.9.16 are tests of `msil/test/M2/00-link/`
(`static-same-name`, `inline-comdat`, `byval-sret-xtu`, `knr-def-xtu`, `main-not-first`,
`data-only-tu`, with `two-tu`, `weak-strong`, `common` and `ctor-priority`), and they print the
native transcript, as do the archive tests of `01-archive/`, the stock-clang bitcode tests of
`02-lto/` and the native-library programs of `03-native/`. The other rows of the matrix (named
sections, `__start_`/`__stop_` symbols, call adapters, data imports, `--gc-sections`,
libraries) need M20 or M22, and the object rows of every case M20. Of `p7-flags`, M19 pins
what the objects say (`llvm/test/CodeGen/MSIL/object-p7-flags.ll`: the call flags of
declarations without a prototype, with `sret` and with `byval` parameters, `dllimport`,
undefined data and the linker options); `msil-ld` links it from M20.

### Triples (D6)

| Triple | clang | llc | Executed by the gate |
|---|---|---|---|
| `x86_64-pc-windows-msvc-msil` | yes | yes | Windows: .NET 8 (tiered and optimizing JIT), .NET 10, .NET Framework 4.8; programs that `msil-ld` links (M14) on .NET 8 and Framework 4.8, with their P/Invokes bound from the Windows SDK's and Visual C++'s import libraries (M15) |
| `x86_64-unknown-linux-gnu-msil` | yes | yes | WSL Ubuntu: a private .NET 10 (tiered and optimizing JIT) against a native Linux build (the E7 oracle), also assembled by the Linux ilasm; programs that `msil-ld` links (M14), linked on Windows with the E7 sysroot's `libc.so.6` and `libm.so.6` as `--sysroot` (M15) |
| `x86_64-unknown-linux-musl-msil`, `x86_64-w64-windows-gnu-msil` | yes | yes | FileCheck and ilasm only |
| `aarch64-unknown-linux-gnu-msil`, `aarch64-unknown-linux-musl-msil`, `aarch64-pc-windows-msvc-msil`, `aarch64-w64-windows-gnu-msil` | yes (native AArch64 DataLayout and ABI) | yes | FileCheck and ilasm (`-pe64 -arm64`) only |
| `aarch64-apple-darwin*-msil` | `unknown target triple` (phase 4) | accepted, untested | no |
| `i686-*-msil`, `x86_64-*-gnux32-msil` | `unknown target triple` (i686: phase 5) | `unsupported target triple ... (Phase 1 supports x86_64 and aarch64 with 64-bit pointers)` | no |
| `x86_64-pc-win32-msil` (normalizes to `x86_64-pc-windows-msil`) | `unknown target triple`: Windows MSIL triples name `msvc` or `gnu` | accepted as windows-msvc | no |

clang uses the native target's ABI, data layout and predefines and adds `__MSIL__`; `long double`
is `double` on every MSIL triple, which differs from native on Linux and x86_64 MinGW (section 3).
Linux MSIL triples do not define `__ELF__` (section 6, item 5).

On the aarch64 triples clang classifies aggregates as AAPCS64 does natively. Since upstream
`de7794fcf781` (merged with `64f50ffd4f1c`), a non-HFA aggregate argument smaller than 8 bytes is
passed as an integer of its exact size (`i8` ... `i56`, formerly `i64`), so 1-4-byte structs are
`int32` IL parameters and 5-7-byte ones stay `native int`; results did not change. Both sides of
every call come from the same clang, no Phase-1 import takes an aggregate, and a native function
that takes or returns one is a link error (M15, section 2), so nothing can disagree.
`clang/test/CodeGen/MSIL/aarch64-aggregate-abi.c` and
`llvm/test/CodeGen/MSIL/struct-args-aapcs64.ll` pin these shapes on all four aarch64 triples and
assemble them with `ilasm -arm64`; execution stays x86_64-only (D6).

### C and LLVM IR

- Integers of every width: 32-bit values are `int32`; pointers and 64-bit integers are
  `native int` (D2; `int64` is never emitted). `__int128` arithmetic, shifts and compares are
  expanded inline, and since M12 so are `__int128` division and remainder by any divisor, the
  conversions between `__int128` and `float` or `double`, saturating ones included, and the
  signed and unsigned conversions from `__bf16` to `__int128` (of a `__bf16` read from memory:
  a `__bf16` parameter or return value itself still names a helper, section 3) (LLVM's
  ExpandIRInsts, as NVPTX and AMDGPU use it; plan section 12, M12 rows on Q36), so no
  compiler-rt helper is called. The expansion is large: a division or remainder by a variable
  is about 600-1,050 IL instructions per site (626-761 at `-O2` and 783-955 at `-O0` for a bare
  one; up to 1,053 in backlog recon 2.2's test functions), one by a constant that is not a
  power of two about 460-650 (exact division too, which no longer multiplies by the inverse),
  one by a power of two a few dozen, and a conversion to floating point 350-570. Phase 3 may
  replace it by one helper per assembly or by `Int128`/`UInt128` behind a switch (P3-B).
- `float` and `double`: arithmetic, remainder (`frem` is CIL `rem`), all comparisons NaN-exact,
  every integer conversion correctly rounded on every runtime (D17, and a round-to-odd step for
  int64 to float32), saturating conversions, `llvm.minimum`/`maximum`/`minimumnum`/`maximumnum`
  as compares and selects (`fmin` and `fmax` are math-library calls), and constrained (strict)
  operations as their plain forms. `fabs`, `copysign` and the `isnan` family use inline integer
  expansions.
- Control flow: branches, loops, `switch` (CIL `switch` for jump tables up to 4096 entries,
  compare chains otherwise; D35), computed `goto`, irreducible control flow, traps
  (`ldnull; throw`).
- Calls: direct calls, calls through function pointers (managed `calli`), K&R definitions and
  calls without a prototype (MSILAddMissingPrototypes), calls through a mismatched function type
  (MSILFixFunctionBitcasts wrappers), structs passed by value (copied by the caller) and returned
  through `sret`, on the SysV and Win64 ABIs (and the AArch64 ABIs on the aarch64 triples, FileCheck
  and ilasm only). The caller's copies share frame slots (M12): the K-th by-value struct argument
  of every call in a function is copied, before each call, into the function's K-th byval slot,
  which is as large and as aligned as the largest such argument, so ten calls of a 64-byte struct
  need 64 bytes of frame, not 640, and two by-value arguments of one call never share a slot.
  Only the SysV x86_64 triples pass structs `byval` (in memory); the Win64 and AArch64 ABIs pass
  large structs by reference to a copy that clang makes.
- Memory: loads and stores of every scalar width, `volatile` and unaligned accesses (`volatile.`
  and `unaligned.` prefixes), `memcpy`/`memset`/`memmove` (`cpblk`, `initblk`, and
  `System.Buffer::MemoryCopy` when the copy may overlap or the length is variable; D15). Static
  frames: one `localloc` per function for address-taken locals, local arrays and over-aligned
  objects (D13).
- Global variables: initialized and zero-initialized data, string literals, pointers to globals and
  functions in initializers (relocations), constructors (`__attribute__((constructor))`) in
  priority order, linkonce/weak/comdat definitions as ordinary ones (D12, D26). Everything lives
  in one unmanaged data segment that `<Module>::.cctor` allocates and fills.
- Unused inline functions: a `linkonce` or `linkonce_odr` function definition that nothing uses
  outside its own body is dropped before it is compiled or diagnosed, and so are the local
  (`static`) functions and other such definitions that only dropped bodies used (M12). A header's
  inline functions that the program does not call therefore cost nothing: the UCRT `stdio.h`
  defines `sprintf` and its helpers inline, and at `-O0` clang emits them, with calls of
  functions that are not imports. Global variables are never dropped, and neither is a local
  function that nothing referenced in the first place.
- Function addresses: each function whose address is used gets one `'<msil>fn_<name>'` field that
  the module initializer sets with `ldftn`, so a function has a single address on every runtime
  (section 12, M9 row).
- Aliases (M12): `__attribute__((alias("f")))`, `static`, `weak` and array aliases, and the
  aliases that rustc makes of identical functions. Every use of an alias of a defined function,
  or of a global variable at any constant offset, becomes its aliasee, through chains of aliases
  and in code, initializers and the constructor list alike, so `&alias == &aliasee`.
  A function alias with external, `weak` or `weak_odr` linkage also gets a `public` forwarding
  method under its own name, which calls the aliasee (a plain `call`, never `jmp`, which the
  verifiers reject), so the name stays a method and `main` may be an alias. Other aliases (an
  offset into a function, a function-typed alias of a variable, an alias of an ifunc, a
  `thread_local` alias of a variable that is not thread-local, such as
  `extern __thread int a __attribute__((alias("g")))` on a plain `int g`) and ifuncs stay errors
  (section 3).
- Undefined weak symbols (M12): a `__attribute__((weak))` declaration that nothing defines is
  null, as a native static link makes it: `if (f)`, `&weak_var == 0` and initializers that take
  its address see null. A direct call of such a function runs a trap stub
  `'<msil>undef_<name>'` (`ldnull`, `throw`: an unhandled `System.NullReferenceException`,
  where a native call of address 0 crashes), and so does a constructor entry that names it. A
  weak declaration of a Phase-1 import (`puts`, a math function, an `"msil-import-module"`
  declaration) stays a P/Invoke, and so does, in `msil-ld`'s text, a weak declaration that a
  native library binds (M15); a weak thread-local declaration
  (`extern __thread int v __attribute__((weak))`) keeps the `thread_local` error (section 3).
- Aliases, undefined weak symbols, the dropped inline functions and the section error of
  section 3 are rules for a whole program, which every `-S` and llc output is, and so is the
  `--lto-emit-asm` text of `msil-ld` (M14), which applies them after resolving the symbols of
  all inputs. CIL objects (M19) keep aliases and weak symbols as object symbols for `msil-ld`
  to resolve ("CIL objects" below; plan 5.8.10).
- Fences: `atomic_thread_fence` calls `Interlocked.MemoryBarrier`; `atomic_signal_fence` and the
  empty barrier `asm volatile("" ::: "memory")` are compiler-only barriers (D36).
- Empty `asm` statements with operands (M12): the `DoNotOptimize` idioms of benchmark harnesses
  and Rust's `black_box`, such as `asm volatile("" : : "r"(v) : "memory")`, with inputs of any
  kind (`"r"`, `"m"`, `"r,m"`, `"g"`, `"i"`), read-write operands (`"+r"`, `"+m"`, `"+rm"`;
  `"+r"` on a `_Bool` and on a local register variable such as `register int x asm("eax")`
  included), outputs tied to an input (`"=r"(x) : "0"(y)`, also with alternatives, as `"=r,r"`
  with `"0,0"`) and memory outputs (`"=m"`). clang emits them exactly as for the native triple,
  and the backend turns each into the compiler barrier above plus a use of every input that
  keeps its computation alive (`llvm.fake.use`, for integer inputs of up to 64 bits, pointers,
  `float` and `double`). An `asm` with instructions, an output not tied to an input (`"=r"`,
  `"=rm"`), a condition-flag output (`"=@ccz"` on x86, `"=@cceq"` on AArch64; read-write
  `"+@ccz"` and tied ones too, because native code sets it from the flags after the statement),
  an output in memory tied to an input of another floating-point type (`"=rm"` on a `float`
  with a `double` `"0"`, which native x86 cannot compile either), `asm goto` and Microsoft
  `__asm` stay errors (section 3). Each accepted output holds what native x86 code leaves in it:
  - A register output tied to an input is that input: zero-extended for a `_Bool` `"+r"`, and
    converted between pointer and integer for a pointer tied to an integer of its width, as the
    native register holds it. A tie written with alternatives (`"=r,r"` with `"0,0"`) is read
    from the first alternative that has one.
  - An output that allows a register and memory, tied to an input (`"=rm"`, `"=g"` or `"=X"`
    with `"0"`, read-write `"+rm"`; also `"=r"` on a non-scalar such as `_Complex int`), which
    clang passes in memory, receives its input after the statement, as native code does when it
    takes the register. A narrower output gets the input's low bytes (a `char` output tied to an
    `int` input gets its low byte).
  - With alternatives that include memory (`"=r,m"` or `"=m,r"` with `"0,0"`, `"+r,m"`), native
    x86 takes the memory alternative, so the memory keeps its contents and the output its old
    value, as for a memory output tied to an input (`"=m"` with `"0"`). On the AArch64 triples
    `"Q"` is a memory alternative too: `"=r,Q"` with `"0,0"` keeps the memory's contents, as
    native AArch64 code does.
- Fixed-length generic vector types are scalarized by the legalizer (scalable vectors cannot be
  and are an error, section 3). `-g` is accepted, but no debug information is written.

### The standalone import table (D14)

`clang -S` and llc compile one module without a linker, so a function that is not defined in the
module can only be a P/Invoke from this table, the Phase-1 import table; any other undefined
function is a compile error. Every declaration is `public` and `pinvokeimpl("<library>" nomangle
cdecl) ... preservesig` (`nomangle` since M18) with a blittable scalar signature. The table is
used by `-S` and llc only, and it is frozen until the end of Phase 2 (D14 as amended): no entry
is added or removed. M15's gate requires the IL and diagnostics of every C and IR test input
that the previous commit tracks, on four triples, to equal those of that commit's build byte for
byte (`il-snapshot.py`, GettingStarted.md section 7); M18's gate requires the same up to the
shortest forms and `nomangle` (`--normalize forms`), and M19 and M20 repeat the check. The text
that `msil-ld` links never consults it: since M15 every function that no file defines binds to
the native libraries of the link ("Native libraries and P/Invokes" above), and the module flag
`msil-linked` tells the backend so. In M14 the table also decided what a linked program could
call, and a module that has M14's flag `msil-lto` without `msil-linked` still gets that
behaviour.

| Triple (x86_64 and aarch64) | `puts`, `putchar` | C math library |
|---|---|---|
| `*-linux-gnu-msil` | `libc` | `libm.so.6` (glibc keeps libm separate; CoreCLR maps only `libc`) |
| `*-linux-musl-msil` | `libc` | `libc` |
| `*-windows-msvc-msil` | `ucrtbase` | `ucrtbase`: every function below except `frexpf`, `ldexpf` and `hypotf`, which the UCRT implements inline in its headers (`frexpf` and `ldexpf` from `llvm.frexp`/`llvm.ldexp` are computed in double) |
| `*-windows-gnu-msil` | `msvcrt` | `msvcrt`: only `sqrt`, `floor`, `ceil`, `fmod`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `exp`, `log`, `log10`, `pow`, `ldexp`, `frexp`, `modf` and their `f` variants except `ldexpf` and `frexpf` (what `msvcrt.dll` exports) |

The math functions are `sqrt`, `cbrt`, `floor`, `ceil`, `trunc`, `round`, `rint`, `nearbyint`,
`fmod`, `remainder`, `fma`, `fmin`, `fmax`, `fdim`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`,
`atan2`, `sinh`, `cosh`, `tanh`, `exp`, `exp2`, `expm1`, `log`, `log2`, `log10`, `log1p`, `pow`,
`ldexp`, `frexp`, `modf`, `hypot` and their `f` variants. In LLVM IR, a function declaration with
the attribute `"msil-import-module"="<library>"` (and optionally `"msil-import-name"="<entry>"`)
is imported from that library.

Since M15 the float32 `llvm.ldexp` and `llvm.frexp` are computed in double when the target's
runtime library has no float32 version of the call (windows-msvc) or the triple is windows-gnu
(`msvcrt.dll` exports no `ldexpf` or `frexpf`), whatever this table says; the IL is the same as
before on every triple, and in `msil-ld`'s text the same rule decides whether `ldexpf` or `ldexp`
must bind.

In C, WebAssembly's import attributes write these IR attributes on the MSIL triples (M13):
`__attribute__((import_module("<library>")))` and `__attribute__((import_name("<entry>")))`, or
`[[clang::import_module("<library>")]]` and `[[clang::import_name("<entry>")]]`, on a function
declaration. `import_module` makes the declaration a P/Invoke of that library, and `import_name`
sets its entry point (default: the C name); `__has_attribute(import_module)` is 1. `import_name`
alone makes no other function an import in `-S` and `llc` output (the function is still
`not a Phase-1 import`), but on a Phase-1 import (`puts`, `putchar`, the math functions above)
it renames the entry point imported from that function's Phase-1 library: `cbrt` declared with
`import_name("my_cbrt")` becomes `pinvokeimpl("libm.so.6" as "my_cbrt" nomangle cdecl)` on
glibc. In a program that `msil-ld` links (M15), `import_module` is kept as given, unchecked, and
`import_name` alone makes `msil-ld` look the entry point up in the native libraries of the link
and take the module from the library that defines it. Bitcode symbol tables carry no import
attributes, so when two files declare one function with different attributes the first
declaration's attributes win, unchecked, until M20. On an `extern` variable declaration they
give the variable the same attributes, for the data imports of `msil-ld` (M20; until then
`importing native data symbol '<name>' (<module>) is not supported yet`); an external variable
is still an error in `-S` output. On a definition they
have no effect. For an attribute written on the definition itself clang warns `import module
cannot be applied to a function with a definition` (`import name ...`, `... a variable with a
definition`), and only when it generates IR, not with `-fsyntax-only`; an attribute that the
definition inherits from an earlier declaration is dropped without a diagnostic, as on
WebAssembly.

### The ECMA-335 library and the metadata oracles (M16, M17)

`llvm/lib/CLI` (the LLVM component `CLI`, library `LLVMCLI`, namespace `llvm::cli`; plan 5.7) is
the one library that reads and writes ECMA-335 metadata for the Phase 2 tools: the backend's
binary CIL (M18) and CIL objects (M19), `msil-ld`'s output image (M20) and the tools
`llvm-cil-dump` and `llvm-cil-copy` (M17). M13 added its format headers
(`llvm/include/llvm/BinaryFormat/CLI.h`, `CLITables.def`, `CILOpcodes.def`), handles and the
metadata table schema; M16 added the writer half:

- **Blobs, signatures and custom attributes.** Compressed integers and a blob reader whose first
  error sticks; one signature model for types, methods, fields, properties and locals, which
  encodes, decodes (every error names its offset and cause), compares and hashes by content,
  remaps the TypeRefs it names, and prints the signature text of the canonical listing;
  custom-attribute blobs, with the `uint8[]` framing that carries a CIL object's link-info
  record (M17, M19).
- **IL.** The opcode table built from `CILOpcodes.def` (all 219 opcodes); an IL decoder and a
  token rewriter (`msil-ld`'s renumbering, M20); an IL builder that picks the shortest
  instruction forms, relaxes branches to a fixed point, computes `.maxstack` (with exception
  handlers and dead code) and rejects malformed bodies; and tiny and fat method bodies with
  small or fat exception sections.
- **Metadata.** The `#Strings`, `#Blob`, `#US` and `#GUID` heaps and `MetadataBuilder`, which
  numbers rows in the order later milestones rely on (plan 5.8.3, R6), groups members by their
  type, sorts the sorted tables, pads Field, MethodDef and Param past exactly 0xFFFF rows (a
  count that the runtimes reject), reports the CLR's limits as errors that name the entity
  (65,524 methods and 65,534 fields per type, names of at most 1,023 bytes, no NUL in a name, a
  method without a body), and writes the metadata root with bytes that depend only on the
  calls.

M17 completed it for Phase 2:

- **PE writer.** A PE32+ image (x86_64 and aarch64 machines) in the envelope of the C# compiler's
  images, as an executable, a library or a netmodule (a module without an Assembly row, the
  shape of a CIL object, M19): one `.text` section with the CLI header, the IL, the metadata,
  an optional debug directory and the mapped field data, and no import table, `.reloc` section
  or native entry stub. For CIL objects it adds marker sections and a COFF symbol table (plan
  5.8.6), so that symbol tools such as `llvm-nm` list them (M19). The MVID and the time
  stamp are a content ID, a SHA-256 hash of the whole file, so the same input always gives the
  same bytes.
- **Reader.** A PE image reader and a lazy, zero-copy metadata reader for all 45 tables, the
  heaps, method bodies, field data and the lookups that the linker will need (owners, nested
  types, custom attributes, P/Invoke maps, layouts, properties). Every index, offset and length
  of the input is checked: a malformed file is an error that names the problem, never a crash,
  and a large assembly such as `mscorlib.dll` is decoded only as far as it is used.
- **Link-info and symbol-table codecs.** The record that every CIL object carries in a custom
  attribute (M19; plan 5.8.8: symbols, segments, relocations, COMDAT groups, constructors,
  signatures and linker options, with its version rules and every consistency check), its
  validation against the object's metadata, the COFF symbol view built from it and its text
  form; and the symbol-table attribute of the libraries of M22.
- **Dumper.** Canonical listing v1 with the six normalizations of plan 5.12.6, and listings of
  the headers, of every table row and of the link-info record.

M16 and M17 changed no IL: the backend links the library from M18 on (its opcode table, IL
builder and signatures; "Binary CIL" below), `msil-ld` from M20. `CLITests` checks it in 18
suites (at least 170 tests, the M17 gate's floor) whose expected bytes come from ECMA-335, the
recon's measurements and Python prototype, ilasm, and C# compiled in scratch: among them the IL
builder rebuilds ilasm's 371-byte `main` of the recon's sample module instruction by
instruction, the 0xFFFF padding at 0xFFFE, 0xFFFF and 0x10000 rows, every limit, R6 order, the
same bytes from two identical builds, content IDs checked against `System.Reflection.Metadata`,
a module's envelope against a netmodule of the C# compiler, a writer-reader round trip of every
table the writer supports, wide heap and table indexes, public key tokens, and every link-info
entry, flag, check and printed line.

Two tools in `llvm/tools` use the library (GettingStarted.md section 6):

- `llvm-cil-dump` prints canonical listing v1 (`--canonical`, the default) with every option of
  `mdcanon`, the PE, CLI and metadata headers (`--headers`), every table row (`--tables`) and a
  CIL object's link-info record (`--link-info`). Its canonical listing is lit's `%msil_canon`
  from M17 on, and it must print the same bytes as `mdcanon` below: M17's gate compares the two
  on every C program of `msil/test/M1` at `-O0` and `-O2` and on its copy, with the default
  options and with every normalization on, and their statistics and full listings on the .NET
  Framework `mscorlib.dll` and .NET 8's `System.Runtime.dll` and `System.Private.CoreLib.dll`,
  the listings also on the `System.Runtime.dll` of the .NET 8 reference pack.
- `llvm-cil-copy` reads an IL-only image and writes it again with `MetadataBuilder` and the PE
  writer, renumbering every token: the end-to-end check of the library before `msil-ld` writes
  images (M20). The copy takes its kind and machine from the input (a DLL with an entry point
  becomes an executable), has one row for duplicate TypeRef and MemberRef rows, and keeps a CIL
  object's link-info record but not its COFF symbol view; an input that `MetadataBuilder`
  cannot rebuild (a table outside its writer set, a class whose base type is a later row, a
  lone surrogate in a user string, events, ...) is a named error (GettingStarted.md section 6).
  The copy of every C program of `msil/test/M1` at `-O0` and `-O2` lists exactly
  like its ilasm image and is the same byte for byte when made twice, and the copies of the 20
  programs of `msil/test/M1/11-programs` at `-O2` pass the verification of section 4, get no
  PEVerify metadata message that the ilasm image does not get, survive the ildasm and ilasm
  round trip, and run with the expected transcript on .NET 8, five of them also on .NET 10,
  .NET Framework 4.8 and WSL .NET 10.

The C# tools in `msil/utils/mdtools/` (GettingStarted.md section 7) check the library and its
users from outside, over System.Reflection.Metadata:

- `mdcanon` prints *canonical listing v1* (plan 5.7.10): an assembly's metadata and IL as text
  without anything that depends on the image layout (tokens, heap offsets, RVAs, header forms,
  the MVID). It was lit's `%msil_canon` in M16; from M17 that is `llvm-cil-dump --canonical`,
  which prints the same bytes, and `mdcanon` stays the independent second implementation that
  `cli-corpus-check.py --mode dumpers` and `msil/test/harness/canon-agree.test` compare it
  with. The equivalence oracles of M19-M23 compare listings: an object against the ilasm build
  of its text (M19), `msil-ld`'s image against `-S` and ilasm (M20). Its six options of
  plan 5.12.6 remove exactly the differences those comparisons allow (the assembly hash
  algorithm, ilasm's parameter rows, ilasm's `InitLocals` on bodies without locals, ilasm's
  quieting of float32 signaling NaNs, the P/Invoke module names and access).
- `check-cil-defs.py` finds `CILOpcodes.def` equal to the 218 opcodes of
  `System.Reflection.Emit.OpCodes` plus `no.`, which that table lacks.
- `cli-corpus-check.py --mode ilasm` shows that the listing does not depend on the assembler:
  the 20 programs of `msil/test/M1/11-programs` at `-O0` and `-O2`, assembled by the Framework
  ilasm and by the NuGet ilasm 10.0.12, give 40 of 40 equal listings, although every pair of
  images differs.

### Binary CIL (M18)

From M18 the backend emits CIL instructions and nothing else: each instruction that the
AsmPrinter emits is one MC instruction of one CIL opcode, and the ILAsm printer and a binary
encoder both read it, so the text and the binary CIL of M19's objects are two encodings of one
stream (plan M18; how to see the encoding is in GettingStarted.md section 3).

- **One record per opcode.** `llvm/lib/Target/MSIL/MSILInstrCIL.td` holds one record
  `CIL_<Opcode>` for each of the 219 opcodes of `CILOpcodes.def`, with the opcode's value, its
  ILAsm text as the mnemonic and the operand its kind takes. The machine instructions of
  instruction selection keep their records but no longer reach the streamer: the lowering
  expands each into CIL instructions (a 64-bit constant into `ldc.i4` or `ldc.i8` and `conv.i`,
  a global address into `ldsfld` plus an offset and `add`, `memmove` into `conv.i8`, `dup` and
  a `call` of `Buffer::MemoryCopy`, a jump table into `switch` and `br`, a trap into `ldnull`
  and `throw`, memory flags into `unaligned.` and `volatile.` prefixes, ...). The module
  initializer `<Module>::.cctor` and `'<msil>Main'` are built with the library's IL builder, in
  Phase 1's instruction order and with Phase 1's `.maxstack`.
- **The shortest forms (plan Q26).** Phase 1 already printed the short forms of 32-bit
  constants and of argument and local indices. Since M18 branches use their short forms
  (`br.s`, `brtrue.s`, `blt.s`, ...) where the target is in reach, and 64-bit constants
  (`ldc.i4.m1` then `conv.i`), global address offsets and the `.cctor` and `'<msil>Main'`
  bodies use the shortest `ldc.i4` forms. The branch forms of a method are chosen once, before
  it is emitted: every branch starts short, and a branch whose displacement from the next
  instruction leaves -128..127 is widened to its long form, until nothing changes; `switch`
  entries stay 32-bit. `msil-ld` will never relax branches (plan 5.8), so the text and the
  bytes always agree.
- **`nomangle`.** Every P/Invoke, in `-S`, llc and `msil-ld`'s text alike, is
  `pinvokeimpl("<module>" [as "<entry>"] nomangle cdecl)`: ImplMap flags 0x0201, Design.md
  5.3's `ExactSpelling`, so the runtime binds exactly the entry name and never tries another
  spelling of it. The D14 table itself did not change.
- **The encoder.** `MSILMCCodeEmitter` encodes each instruction from the opcode table: one byte
  or `FE` and one byte, then the operand, little-endian, with floating-point constants as their
  exact bits (signaling NaNs included). A metadata token is four zero bytes and a fixup that
  names the operand's class (`fixup_cil_method_token`, `fixup_cil_field_token`,
  `fixup_cil_sig_token`, and the type, `tok` and string kinds for later milestones), which M19's
  object writer turns into a token; a branch is a PC-relative fixup on its label, and so is
  each `switch` entry. `llc -show-mc-encoding` prints the bytes and fixups of every instruction
  as comments, and the text with those comments still assembles with ilasm while no instruction
  has more than 191 fixups: `MCAsmStreamer` prints fixup i as `char('A' + i)`, so entry 191 of
  a `switch` (the 192nd fixup) prints as a NUL byte, later ones also as LF (201) and CR (204),
  and ilasm cannot assemble the text of a `switch` of more than 191 targets (jump tables have up
  to 4096 entries, D35); plain `-S` text is not affected. Since M19 the CIL object writer
  writes these bytes into CIL objects and fills each token from the object's own metadata
  rows ("CIL objects" below).
- **Operand classes.** Each token operand records what it names, from the IR alone: a function
  defined in the module, or a declaration with `import_module` or `import_name` (plan 5.10.7);
  any other function, library calls included; the address field `'<msil>fn_<name>'` of a
  function; a global variable defined in the module (`common` ones included), or another one;
  the signature of a `calli`; a member of the .NET base class library. They print as Phase 1's
  text (`int32 'f'(int32)`, `native int 'g'`, `native int '<msil>fn_f'`, `void
  [mscorlib]System.Threading.Interlocked::MemoryBarrier()`), and llc's `-M object-form` prints
  the spelling of M19's CIL objects instead (plan 5.8.4): `int32
  [msil.extern]'<msil>Symbols'::'f'(int32)` for an undefined function, `native int
  [msil.extern]'<msil>Symbols'::'x'` for an undefined variable, `native int
  [msil.extern]'<msil>FnAddr'::'f'` for a function address, and the scope `[msil.bcl]` for a
  BCL member. A function or variable defined in the module prints alike in both.
- **The BCL list.** `llvm/include/llvm/CLI/BCLMembers.def` is the one list of base class
  library types and members that generated code uses (plan 5.11.7): `System.ValueType` (the
  base of the data image's value type) and the five members of D10 with their owner types,
  `Marshal::AllocHGlobal`, `Marshal::StringToHGlobalAnsi`, `Environment::GetCommandLineArgs`,
  `Buffer::MemoryCopy` and `Interlocked::MemoryBarrier`, all required and all in `mscorlib`.
  Every BCL name the backend prints comes from it, and M20-M22 add the members they need; M21's
  reference binder binds them by it.
- **The encoder equals ilasm.** `cli-corpus-check.py --mode encoder` (the `encoder-parity` leg,
  GettingStarted.md section 7) compiles each of the 99 C programs of `msil/test/M1` at `-O0`
  and `-O2` with `llc -show-mc-encoding`, assembles the same text with the .NET Framework
  ilasm, rebuilds each method's bytes from the encoding comments and compares them with
  ilasm's: 2,819 of 2,819 method bodies of the 198 modules are equal, with the same
  `.maxstack`, and each of their 24,099 token fixups names the member that ilasm's token names;
  their 23,316 branch and `switch` fixups resolve to ilasm's displacements. A float32 signaling
  NaN constant, which ilasm quiets (section 3, "Limits"), counts apart as `snan-quiet`; the
  corpus has none. A `switch` of more than 191 targets (see "The encoder") is found in the
  listing before it is parsed or assembled and fails its program and level with a message that
  names the `switch` and its target count, instead of ilasm's generic failure; the corpus's
  largest `switch` has 68 targets.
- **The text did not change otherwise.** The IL snapshot of M18's build, compared with that of
  M17's with `il-snapshot.py --normalize forms` (GettingStarted.md section 7), has the same IL
  and diagnostics for all 2,712 compiles of the 452 inputs of `msil/test/M1` and
  `llvm/test/CodeGen/MSIL` on four triples; 1,861 of the `.il` files differ only in the
  shortest forms and `nomangle`. The FileCheck tests of `llvm/test/CodeGen/MSIL`,
  `clang/test/CodeGen/MSIL` and `lld/test/MSIL` and the `msil-basic` test of
  `update_llc_test_checks` (83 files) changed only to follow those forms (rewritten, also
  inside FileCheck's regular expressions, and the checks that pinned the absence of a short
  form or `nomangle` deleted) and in comments, which `check-il-forms-diff.py` checks.

### CIL objects (M19)

How to write and read them, with examples, is in GettingStarted.md section 3 ("CIL objects");
plan 5.8 is the contract.

- **What writes them.** `clang -c` and clang-cl `/c` (one `cc1 -emit-obj` job: no assembler,
  no bitcode, no link; `<name>.o`, with clang-cl `<name>.obj`), `clang -cc1 -emit-obj` and
  `llc -filetype=obj`, on every MSIL triple of D6: x86_64 and aarch64, Linux, windows-msvc and
  windows-gnu. `-flto -c`, `-flto=thin -c` and `-emit-llvm -c` still write bitcode.
- **What an object is** (plan 5.8.2-5.8.7). An ECMA-335 netmodule in the envelope of the C#
  compiler's images: a PE32+ image for the triple's machine (0x8664 or 0xAA64) with
  characteristics 0x2022 and one `.text` section that holds the CLI header, the method bodies
  and the metadata. It has no entry point, no Assembly row, no `.cctor`, no `'<msil>Main'`, no
  data image and no FieldRVA row, so it cannot run, and nothing in it runs when it is loaded.
  The Module row is named after the base name of the source file (`main.c`; `<msil>module`
  when the module has none), never after `-o`. Each function the file defines is a static
  method of `<Module>` with M18's binary CIL (`public`, or `assembly` for a `static`
  function; bodies never set `InitLocals`), and each variable it defines is a `static
  initonly native int` address field that the linker will set. A function or variable that
  the file does not define is a member of `[msil.extern]'<msil>Symbols'`, called or loaded
  with the call's own signature; the address of a function is a field of
  `[msil.extern]'<msil>FnAddr'`, so a function keeps one address in the whole program; and
  the types of the base class library sit in the unbound scope `[msil.bcl]`, which `msil-ld`
  binds (M21). An undefined function declared with `import_module` gets one P/Invoke stub on
  that module per signature it is called with (`import_name` alone: on the placeholder module
  `msil.unresolved`, for `msil-ld` to bind natively under the import name), and one with its
  declared signature when the object only takes its address. Rows come in a fixed order (plan
  5.8.3, R6), the one ilasm gives the same object written as text, so both number every token
  alike.
- **The link-info record v1** (plan 5.8.8) sits in a custom attribute of the Module row and
  says what the symbol table, sections and relocations of a native object say: the triple, the
  ABI version and the data model (the sizes of `void *`, `long`, `wchar_t` and `long double`:
  8,8,4,8 on the Linux triples, 8,4,2,8 on the Windows ones); one symbol per global, in a fixed
  order (variables, functions, aliases, then the library calls of code generation), with its
  kind, binding (GLOBAL, WEAK, LOCAL or COMMON), visibility, `dllimport` and `dllexport`, its
  `llvm.used`, alias and import flags and its metadata row; one data segment per variable the
  file defines, named by its `section` or else `.data`, `.rodata` or `.bss`, with its bytes,
  alignment and flags (C strings, read-only, zero, kept by `llvm.used`); the data relocations
  (the address of a variable or of a function in 64 or 32 bits, and the difference of two
  addresses); COMDAT groups; the constructors of `llvm.global_ctors` in array order with their
  priorities; what a call's IL signature does not say (no prototype, an `sret` result, `byval`
  parameters with their sizes); and the linker options of `#pragma comment(lib)` and
  `!llvm.dependent-libraries`. `llvm-cil-dump --link-info` prints it, one line per entry. M19
  writes version 1.0 and its tests pin it, but this commit does not freeze it: M20's commit
  does, after `msil-ld` has linked the link-semantics matrix with it, and until then a gap that
  M20 finds changes v1.0 in place (plan 5.8.9).
- **The COFF symbol view** (plan 5.8.6). A COFF symbol table at the end of the file, one
  symbol per record symbol in record order, and three empty marker sections `.data`, `.rdata`
  and `.bss` let symbol tools read an object like a native one: `llvm-nm` prints `T` for a
  function (aliases included), `D`, `R` or `B` for a variable in a writable, read-only or zero
  segment, `C` for a common symbol and `U` for an undefined one, weak or not (never `W`), in
  lower case for a local symbol; `llvm-ar rcs` indexes the defined global symbols and the
  commons; GNU `nm` and `ar` agree (in WSL); `llvm-objdump -t` and `llvm-readobj --symbols`
  list the symbols. `msil-ld` never reads the view, and a copy by ilasm or `llvm-cil-copy`
  drops it.
- **What object mode changes** (plan 5.8.10). Whole-program output (`-S`, llc text and
  `msil-ld`'s `--lto-emit-asm` text) is the same as before byte for byte; in an object the
  rules that need the whole program become symbols for `msil-ld`:

  | C or IR | In a CIL object | In `-S` and llc text |
  |---|---|---|
  | a function the file does not define (another file's, `malloc`, a library call such as `memcpy`, a `__` helper) | an UNDEFINED symbol, called through `'<msil>Symbols'` | an error unless the D14 table imports it |
  | an `extern` variable the file does not define | an UNDEFINED data symbol; with `import_module` or `import_name`, IMPORT with both names | `external global variable ... (phase 2 linker)` |
  | a `weak` (or `linkonce`) definition, an MSVC string literal, `__declspec(selectany)` data | WEAK, in its COMDAT group if it has one (selection `any` or `nodeduplicate`) | an ordinary definition |
  | a `weak` declaration | UNDEFINED WEAK | null, and a trap stub for calls (M12) |
  | a `common` variable | COMMON, with its size and alignment | a zero-initialized definition |
  | an alias of a definition (of a function at offset 0, of a variable at a constant offset) | an ALIAS symbol with its own linkage and visibility | the aliasee, plus a forwarding method (M12) |
  | `section` on a variable | the name of its segment | an error until M20 |
  | `llvm.used` | NO_STRIP, and its segment is kept (RETAIN) | nothing |
  | constructors | INIT entries in array order, with their priorities | calls in `<Module>::.cctor`, by priority |
  | `#pragma comment(lib)`, `!llvm.dependent-libraries` | LINKER_OPTIONS | ignored |
  | visibility, `dllimport`, `dllexport` | the symbol's flags | ignored |
  | a call of a declaration without a prototype (`int f();`) | one MemberRef per IL signature of its calls, each marked NO_PROTOTYPE, and no wrapper | Phase 1's prototype from the calls, and wrappers |

  Phase 1's two prototype passes stop at undefined functions in object mode: a declaration
  without a prototype keeps its type and attribute, and no call of an undefined function gets
  a wrapper, so a K&R function called as `f(1, 2)` and as `f(1)` has two MemberRefs, and
  `msil-ld` will check each call against the definition (M20). Phase 1 derived one prototype
  from the first call and could lose an argument (`knr-two-arities` in the matrix above). Two
  calls with one IL signature that pass their arguments differently (a `byval` struct or an
  `sret` result in one, a plain pointer in the other) give the warning `calls to '<f>' pass
  incompatible arguments; the object records the first call's layout`. Thread-local
  variables, `llvm.global_dtors`, ifuncs, module `asm` and variadic functions stay the errors
  of section 3. Object mode adds three errors: `alias '<a>' must
  point to a definition plus a constant offset for MSIL`, `comdat '<c>' uses selection kind
  '<k>', which MSIL does not support` (`largest`, `exactmatch`, `samesize`), and `symbol
  '<name>' starts with '<msil>', which MSIL reserves for the names it generates`.
- **Object-form text.** `clang -S -mllvm -msil-object-form` (llc: `-msil-object-form`)
  writes the object as ILAsm text instead of the runnable module: a header comment, the three
  placeholder `.assembly extern` lines, the methods with the operand spelling of `-M
  object-form` (M18), the fields and stubs, then `.module` and the record as a `.custom` blob.
  ilasm turns it into an object whose canonical listing (`llvm-cil-dump --canonical
  --params=drop --initlocals=ignore-without-locals --snan=quiet`, which ignores ilasm's
  parameter rows, its `InitLocals` on bodies without locals and its quieting of float32
  signaling NaNs) and record equal those of `clang -c`'s object; only the COFF view is
  missing. That comparison is the tests' oracle; `msil-ld` never reads the text.
- **Determinism.** The MVID and the time stamp are a hash of the file, and an object depends
  only on the IR and the options, so two compiles in different directories with different
  `-o` names write the same bytes.
- **Not linkable yet.** `msil-ld` links CIL objects from M20 and until then rejects them,
  `<file>: linking CIL object files and .NET assemblies is not supported yet`; a
  compile-and-link command therefore still compiles to bitcode, and a program of several
  files is compiled with `-flto -c` (GettingStarted.md section 3). Native librarians and
  linkers refuse CIL objects as they refuse other netmodules: `lib.exe` with `LNK1302: only
  support linking safe .netmodules; unable to link pure .netmodule`, `llvm-lib` with `not a
  COFF object, bitcode, archive, import library or resource file`, and `lld-link` and `ld.lld`
  with `unknown file type`. `llvm-ar` and GNU `ar` archive them.
- **Reading them.** `llvm-cil-dump` (`--headers`, `--tables`, `--canonical`, `--link-info`),
  both ildasms, System.Reflection.Metadata (`mdcanon`), and the symbol tools above.
- **Tests.** The 12 `object-*.ll` tests of `llvm/test/CodeGen/MSIL` write objects with `llc
  -filetype=obj` and check them through `llvm-cil-dump`: the envelope (`object-envelope.ll`),
  every symbol row and the data model (`object-symbols.ll`), aliases, data segments and
  relocations, COMDAT groups, constructors, calls (the K&R case in both call orders), import
  stubs, the errors of object mode, the object-form text, the COFF view with `llvm-nm` and
  `llvm-ar`, and the matrix case `p7-flags`; most of them also compare the object with ilasm's
  object of its object-form text. They require the lit feature `llvm-cil-dump`, which the
  directory's `lit.local.cfg` sets when the build has that tool, so a tree without it reports
  them UNSUPPORTED (under a gate's `msil_require_verify=1` it is a configuration error).
  `clang/test/Driver/msil-object.c` checks `-c` and `/c`, their jobs and errors, that two
  compiles from different directories give the same bytes, and the `llvm-nm` letters and
  object-form text of what they write; `clang/test/CodeGen/MSIL/emit-obj.c`, which requires
  `llvm-cil-dump` too, checks `cc1 -emit-obj` and the headers and records of the objects that
  `-c` and `/c` write; and `msil/test/M2/08-semantics/knr-two-arities.test` runs through LTO
  until M20. The non-lit legs `obj-text` and `obj-text-wsl` (GettingStarted.md section 7) check
  every C program of `msil/test/M1` at `-O0` and `-O2`, and the programs of `11-programs` for
  the Linux triple too: the object of `clang -c` must list exactly like ilasm's object of its
  object-form text, record included, and have the same `--link-info` lines; two compiles from
  different directories must give the same bytes; `mdcanon` and both ildasms must open
  it; its envelope must be the one above; the consistency rules C1-C5 and C7 of plan 5.8.5
  must hold between its metadata and its record, and between ilasm's object and the same
  record; and `llvm-nm`, the `llvm-ar` index and, in WSL, GNU `nm` and `ar` must list the
  symbols the record gives.

## 3. What Phase 1 does not do

| Feature | Phase 1 | Planned |
|---|---|---|
| Linking CIL objects, `msil-ld`'s own assembly images, ThinLTO | `clang -c` writes CIL objects since M19 (section 2), but `msil-ld` links only bitcode and archives of it, into ILAsm text (`--lto-emit-asm`, section 2), which ilasm assembles: a CIL object given to it is `<file>: linking CIL object files and .NET assemblies is not supported yet`, so a program of several files is compiled with `-flto -c`, and any other LTO link (without `--lto-emit-asm`, ThinLTO bitcode, `--lto-partitions` above 1) is `msil-ld`'s error `ThinLTO, --lto-partitions and LTO without --lto-emit-asm need CIL object output, which this msil-ld does not have; use --lto-emit-asm (clang: -Wl,--lto-emit-asm) to write ILAsm text`. In M14-M18 `-c` wrote bitcode and `-fno-lto -c` was the driver error `CIL object files are not supported for MSIL target '...' yet; use -flto to write LLVM bitcode objects, or -S to write ILAsm text`; before M14 every `-c` and every link was that error, and before M13 its text was `emitting object files is not yet supported for MSIL target ...` | phase 2 (P2-A, P2-B): `msil-ld` links CIL objects from M20, when it also writes the PE image itself and ThinLTO works |
| Writing an assembly image without ilasm | `llvm/lib/CLI` reads and writes assemblies (M16 and M17, section 2), and `llvm-cil-dump` and `llvm-cil-copy` use it; since M18 the backend encodes every instruction as binary CIL, and since M19 CIL objects carry those bytes (section 2), but every image that runs, from `clang -S` or from `msil-ld`, still comes from ilasm | M20: `msil-ld` writes the image |
| Any other external function (`malloc`, `strlen`, `exit`, ...) or external variable (`environ`, `stderr`, ...) that no file of the program defines | `-S` and llc: `... is not a Phase-1 import` and `external global variable ... (phase 2 linker)`; in a CIL object (M19) both are undefined symbols. `msil-ld` binds such functions to the native libraries of the link (M15, section 2), and a variable that a native library defines is `importing native data symbol '<name>' (<module>) is not supported yet`; a symbol that nothing defines is `undefined symbol: <name>`, and a `weak` declaration that nothing defines is null instead (section 2) | native data: M20 (plan 5.10.12) |
| Variadic functions: definitions and calls | error; in a program that `msil-ld` links, a call of a native variadic function (`printf`) is `native function 'printf' (<module>) is variadic; calls of native variadic functions are not supported for MSIL yet (phase 3)` | phase 3 (Design.md 4.7) |
| The `va_arg` instruction in LLVM IR (other front ends, hand-written IR; clang expands C's `va_arg` itself) | error: `the va_arg instruction is not supported for MSIL yet (phase 3 variadics)`. One of a struct or array type is reported before instruction selection, which crashed on it (as it does natively), and its function gets no method (M12) | phase 3 (Design.md 4.7) |
| `thread_local` / `_Thread_local` | error: `thread-local variable @<name> is not supported for MSIL yet`, also for a `weak` declaration that nothing defines (which is not made null, unlike other weak declarations) | phase 3 (Design.md 4.4) |
| Dynamic `alloca`, VLAs, `stacksave`/`stackrestore`, `__builtin_frame_address` | error (`... phase 3 shadow stack`) | phase 3 (Design.md 4.2) |
| `setjmp`/`longjmp` | error | phase 3 (Design.md 4.6) |
| Atomics (`_Atomic`, `__atomic_*`, `__sync_*`) | error (only fences work) | phase 3 |
| Global destructors, `atexit` | error. `atexit` binds to no native library: glibc defines it only in machine code (`symbol 'atexit' is defined only in native object code (.../libc_nonshared.a(atexit.oS)); MSIL cannot link machine code`), and the UCRT does not export it | phase 3 (Design.md 4.1), which intercepts `atexit` |
| `long double` | is `double` (8 bytes, 53-bit mantissa) on every MSIL triple, not an error. Differs from native on x86_64 Linux (glibc and musl) and x86_64 MinGW (`x86_64-w64-windows-gnu`), where it is the 80-bit x87 format in 16 bytes, and on aarch64 Linux, where it is IEEE binary128 in 16 bytes; matches native on windows-msvc (x86_64 and aarch64) and `aarch64-w64-windows-gnu`. So where it differs, a native `long double` function (`sqrtl`, `expl`, `powl`, `strtold`, `nexttoward`, ...) that `msil-ld` binds is the link error `native function '<name>' (<module>) takes or returns a value that a P/Invoke cannot pass natively yet (phase 3)` (section 2, "Native libraries and P/Invokes"); windows-msvc and `aarch64-w64-windows-gnu` bind them | permanent (Design.md 3; section 6, item 14); calls of native `long double` functions: phase 3 |
| Conversions between floating point and `__int128` under strict floating point (constrained intrinsics: `-ffp-model=strict`, `#pragma STDC FENV_ACCESS ON`), conversions between `_Float16` and `__int128` and from `__int128` to `__bf16` (helpers `__extendhfsf2`, `__truncsfhf2`, `__extendbfsf2`, `__truncsfbf2`), `_Float16`/`__bf16` arithmetic, `__bf16` parameters, call arguments and return values even without arithmetic (`__truncsfbf2`, `__extendbfsf2`; `_Float16` ones compile), C `_Complex` multiplication and division | error naming the runtime helper (`__fixdfti`, `__floattidf`, `__extendhfsf2`, `__muldc3`, ...). `__int128` division, the plain conversions and those from `__bf16` to `__int128` are inline since M12 (section 2). Under `msil-ld`'s LTO, argument promotion never turns a pointer into a `_Float16` or `__bf16` parameter (section 2, "LTO and argument promotion") | phase 3: BCL types where available, helpers in the output assembly otherwise |
| `__float128`, `x86_fp80`, rounding-mode changes, floating-point environment access | error (CIL rounds to nearest and has no FP exceptions) | |
| Inline and module-level assembly, other than empty `asm` statements with or without operands (section 2): instructions, outputs not tied to an input, condition-flag outputs (`"=@cc<cond>"`, `"+@cc<cond>"`, tied or not), an output in memory tied to an input of another floating-point type, `asm goto`, Microsoft `__asm`, file-scope `asm` | error: clang's `inline assembly is not supported for the MSIL target '...'` (`Microsoft-style inline assembly ...`, `file-scope assembly ...`), llc's `inline assembly is not supported for MSIL` | never (Design.md 3) |
| `section` on a global variable (`__attribute__((section(...)))`, `__declspec(allocate(...))`), such as a constructor registered in `.init_array` or `.CRT$XCU` | `-S` and llc: error, `global variable @<name> in section "<section>" is not supported for MSIL yet (phase 2 CIL objects)`, one per variable in module order. Before M12 the section was dropped silently, so such a constructor never ran. A CIL object (M19) keeps the section as its segment's name for `msil-ld` (section 2). A section on a function is ignored, and clang's own sectioned globals (`llvm.metadata`) are not affected | phase 2: from M20 whole-program output lays out named sections as a one-object link does, and `msil-ld` lays out the segments of objects |
| Aliases other than those of section 2 (an offset into a function, a function-typed alias of a variable, an alias of an ifunc, a `thread_local` alias of a variable that is not thread-local) and ifuncs (from LLVM IR only: clang ignores `__attribute__((ifunc(...)))` on the MSIL triples with an `unknown attribute` warning) | error: `alias @<name> is not supported for MSIL`, `ifunc @<name> is not supported for MSIL`. A CIL object (M19) keeps every alias of a definition plus a constant offset (offset 0 for a function) as an alias symbol for `msil-ld`; any other alias there is `alias '<name>' must point to a definition plus a constant offset for MSIL` | ifuncs stay errors |
| Function pointers passed to native code (callbacks) | function pointer values are managed; no Phase-1 import takes a callback. In a program that `msil-ld` links, a native function of plan 5.10.14's callback list (`qsort`, `bsearch`, `signal`, `pthread_create`, `CreateThread`, ...; section 2) is a link error, `native function '<name>' (<module>) takes a C function pointer; passing functions to native code needs phase 3 thunks`. **Known gap:** a function address passed to any other native function (a native library's own callback API, a Win32 callback) is not diagnosed and fails at run time | phase 3 thunks (P3-B; Design.md 4.5) |
| Structs by value, `__int128`, vectors and `_Float16` in P/Invoke signatures | none of the standalone imports takes one; a natively bound function that does is `native function '<name>' (<module>) takes or returns a value that a P/Invoke cannot pass natively yet (phase 3)` | phase 3 |
| Exports (`-shared`), assembly-to-assembly linking | no; `msil-ld` reports `-shared is not supported yet` | phase 2 (P2-C, M22), Design.md 2.3 |
| The `Imports:` section of `msil-ld`'s map | `-Map` lists the native libraries and the natively bound symbols, but no P/Invoke tokens (there are none before `msil-ld` writes the image) | M20 |
| BCL intrinsics (`BitOperations`, `Math`, `MathF`, `Vector128`, ...) | none: bit operations, `fabs` and `copysign` use inline expansions, math calls are P/Invokes (D16) | phase 3 codegen switches |
| The codegen switches for newer .NET APIs of Design.md 7.1 (`DisableRuntimeMarshalling`, `Math.FusedMultiplyAdd`, `UnmanagedCallersOnly` thunks, ...) | none exist; Phase-1 output already is what their defaults will give: code that runs on .NET Framework 4.8 and modern .NET alike | phase 3 (P3-A) |
| Architecture-specific intrinsics (`llvm.x86.*`, `llvm.aarch64.*` from `immintrin.h`, `arm_neon.h`) | error naming the intrinsic: `intrinsic '<name>' is not supported for MSIL` (`llvm.x86.sse42.crc32.32.32` from `_mm_crc32_u32`, `llvm.x86.rdtsc` from `__rdtsc`, ...). Intrinsics that clang lowers to generic IR (`_mm_add_ps`, `_mm_popcnt_u32`, ...) still compile | phase 4 SIMD (Design.md 3.2) |
| SVE and SME scalable vector types (`__SVInt32_t`, `svint32_t`, `__SVCount_t`; in IR `<vscale x N x T>`, `target("aarch64.svcount")`, `llvm.vscale`), reachable on the aarch64 MSIL triples with `-march=...+sve` or `__attribute__((target("sve")))` | error naming the type or intrinsic: `scalable vector type '<vscale x 4 x i32>' is not supported for MSIL` | phase 4 SIMD (Design.md 3.2) |
| Debug information (Portable PDB) | `-g` accepted, nothing emitted | phase 4 |
| Darwin and i686 triples | clang: `unknown target triple '<triple>'`, for a Darwin triple also when the command only links; `msil-ld`: `unsupported target triple '<codegen triple>' for MSIL` (stock-clang bitcode of such a triple, or such a `--target=`) | phases 4 and 5 |
| Stack protector (`-fstack-protector*`, `/GS`), control-flow guard, patchable function entries | ignored with a warning (clang-cl's implicit `/GS` is not passed) | |
| Basic block sections, late function splitting, basic block address map, static data partitioning (`-fbasic-block-sections=`, `-fsplit-machine-functions`, `-fbasic-block-address-map`, `-fpartition-static-data-sections`) | `-fsplit-machine-functions`, `-fbasic-block-sections=all` and `=list=`, `-fbasic-block-address-map` and `-fpartition-static-data-sections` are a driver error (`unsupported option '...' for target '...'`), because MSIL is not ELF; `-fbasic-block-sections=none` is accepted, and `-fbasic-block-sections=labels` (a deprecated alias) passes the driver as `-fbasic-block-address-map` and ends in the backend's warning `basic block address map is not supported for MSIL; ignored`. Passed to cc1 or llc directly (`-fsplit-machine-functions`, `-mllvm -function-splitting=`, `--basic-block-sections=`, ...), the options are ignored, the address map with that warning: the IL is the same as without them | never: CIL methods have no sections |

### Limits

- An IL identifier is at most 1023 bytes, because both ilasms reject longer names; a longer C
  name is a compile error.
- `memset` with a variable length is limited to 4 GiB (`initblk` takes a 32-bit size).
- A P/Invoke records only the native library's name (M15): a library of your own must be
  where the runtime's loader finds it when the program runs, next to the program or on the
  system's search path, and on another system the same name may be another build of the
  library (a P/Invoke has no symbol version and binds the running library's default one).
- ilasm sets the quiet bit of float32 signaling-NaN constants (`ldc.r4`), with both ilasm versions;
  float64 constants are exact. The backend's encoder (M18) keeps the bits (`ldc.r4` of
  0x7FA00000 encodes as `22 00 00 A0 7F`), so the encoder parity check counts such a constant
  as `snan-quiet` rather than as a mismatch. CIL objects (M19) carry the encoder's bytes, so
  their constants are exact and the object oracle compares with `--snan=quiet`, but every
  image that runs is still ilasm's until `msil-ld` writes its own (M20).
- In `-S` and llc output, a prototype-less imported function whose address is used but which
  is never called directly cannot get a signature and is an error; whether the optimizer turned
  the indirect call into a direct one decides, so such code can fail at `-O0` and compile at
  `-O2`. Declare imports with a prototype (section 12, M9 row). A CIL object (M19) takes such
  an address through `'<msil>FnAddr'` without an error and has no stub for the function; from
  M20 `msil-ld` reports a native binding of it (plan 5.10.13).
- On Linux, as for native programs, only the low 8 bits of the exit status survive.

## 4. Verification policy

Design.md 12 asks that "`ilverify` reports no stack-shape errors". ILVerify alone cannot check that:
it stops at the first error of a method and cannot verify any method that contains `calli`. The
user approved this replacement on 2026-10-12 (decision D21, and its refinement in plan section 12,
rows 'D21 and M0.b item (7) refined' and the user confirmation after it). A module passes when all
of these are clean:

1. **MSILStackCheck**, the last machine pass of the backend, always on: it recomputes the stack
   depth before and after every instruction, checks that every branch and join agrees on it and
   that no block falls off the end of a method, and computes the exact `.maxstack`. A violation is
   an internal compiler error, never a module.
2. **jitall** (`msil/utils/msil-jitall.py`): every method is JIT-compiled with
   `RuntimeHelpers.PrepareMethod` on the runtime under test (in WSL for Linux triples), under a
   timeout and a memory cap. `main` does not run; `<Module>::.cctor` does, as it would before
   `main`.
3. **ILVerify** (`dotnet-ilverify` 10.0.12) with the allow-list `msil/utils/ilverify-allowed.txt`:
   `Unverifiable` (localloc, cpblk, initblk), `InitLocals` (`.locals` without `init`), `StackByRef`
   only as `[found Native Int] Expected ByRef on the stack.`, and `UnmanagedPointer`. Every other
   code fails.
4. **PEVerify** (Windows): every message fails except the unverifiable-by-design ones and exactly
   `[found Native Int][expected unmanaged pointer] Unexpected type on the stack.` (the native-int
   addresses passed to `System.Buffer::MemoryCopy`, D15). Two shapes that D12 and D2 always
   produce are notes only when the second pass runs: the RVA image copy in `<Module>::.cctor`
   (`Cannot modify an imaged based (RVA) static`, at that exact `ldsflda`) and a function address
   used as a `native int` (`ldftn`).
5. **A neutralized second pass** (`msil-verify.py`): an allowed construct hides the errors behind
   it, so the assembly is disassembled with ildasm, every allowed construct is replaced by
   something verifiable with the same stack effect (`localloc`, `ldftn` and the image `ldsflda`
   become `native int` zeros; `cpblk`, `initblk`, `ldind`, `stind` and `calli` become calls of
   checking stubs), reassembled, and verified again by ILVerify and PEVerify with no allowance but
   the `MemoryCopy` message.

Stack *types* are checked only by the verifiers (the JIT accepts most type errors silently), and
mixing of `native int` and `int64` only by PEVerify, so that check runs on Windows. Every gate
passes `--param msil_require_verify=1`, which makes a missing ILVerify, ildasm, jitall, PEVerify
(Windows), ilasm or runtime an error instead of a skipped step (D38), since M16 also a missing
`mdcanon`, and since M17 a missing `llvm-cil-dump`, the two canonical-listing tools of section 2
(also when an empty `--param msil_mdcanon=` or `msil_cil_dump=` switched one off). On top of
verification, every program runs and its transcript is compared with the native build's, under
Tier-0 and fully optimizing JIT.

## 5. Runtime differences

What differs between the runtimes Phase 1 targets, and how the backend deals with it:

| Behaviour | .NET 8 | .NET 10 | .NET Framework 4.8 | Phase 1 |
|---|---|---|---|---|
| `conv.r.un` of a 64-bit value | rounds twice | correct | rounds twice | never emitted for 64-bit sources; unsigned 64-bit to FP uses a branch-free sequence of signed conversions (D17) |
| `conv.r4` of an int64 that the JIT constant-folds | correct | correct | folded as `(float)(double)x`, rounds twice | the value is first rounded to odd at bit 11, so both paths give the correctly rounded float |
| Floating point to integer out of range (C undefined behaviour) | x86 "indefinite" value | saturates | indefinite | programs must not depend on it; `llvm.fptosi.sat`/`fptoui.sat` are lowered explicitly |
| Alignment of a constant-size `localloc` of 32 bytes or less | only 8 for 16 and 32 bytes in Tier-0 code, and for 8 bytes in optimized code | same as .NET 8 | only 8 for 8 to 32 bytes | frames assume 8 and realign when an object needs more, over-allocating `MaxAlign - 8` bytes (D13) |
| `localloc` zeroed under `.locals init` | yes | yes | no | `.locals` is never `init`; C does not expect zeroed frames |
| Two `ldftn` of one function in different methods | equal | equal | can differ (precode vs. compiled code) | one address field per function, set once in `<Module>::.cctor` (M9) |
| Overlapping constant-size `cpblk` | not what `memmove` gives | same as .NET 8 | a third result | `cpblk` only for `memcpy`, where overlap is undefined; `memmove` uses `Buffer.MemoryCopy` |
| Inconsistent stack depth at a join | accepted | the JIT hung and grew past 40 GB | not measured | MSILStackCheck rejects such code at compile time |
| `Math.Min(-0.0, +0.0)` | -0.0 | -0.0 | +0.0 | `Math.Min`/`Max` are not used; min/max are compares and selects (D16) |
| Main-thread stack on Windows | 1.5 MB (the reserve of `dotnet.exe`) | same | 4 MB (ilasm's default for a 64-bit `.exe`; `ilasm -stack=` sets it) | see below |

- **Deep recursion.** `dotnet x.dll` on Windows runs `main` on the host's main thread, whose
  stack is 1.5 MB, while native Linux programs get 8 MB by default (`ulimit -s`, which the Linux
  `dotnet` host's main thread also uses), and a CIL frame (the `localloc` frame plus the JIT's own
  frame) is larger than the native one, at `-O0` especially: about 19,000 frames of an
  `-O0`-shaped recursion fill the .NET 8 main thread on Windows (plan M9.d, review experiment
  `recon/artifacts/review-cil/deep.il`). A stack overflow terminates the .NET process and cannot be
  caught, so a program that recurses deeply natively may fail under .NET.
- **Traps and undefined behaviour** surface as .NET exceptions where native code gets a signal: a
  trap is an unhandled `System.NullReferenceException`, integer division by zero a
  `System.DivideByZeroException`. On Windows an unhandled exception exits with `0xE0434352`.
- **Tiered compilation.** Short programs run almost entirely as Tier-0 code, which is why the
  gate repeats the suite with `DOTNET_TieredCompilation=0`; no difference in results is known.

## 6. Design.md updates

The 14 Design.md updates this section proposed were applied to [Design.md](Design.md) on
2026-10-21, with the user's approval, together with the Phase 2 plan, the Phase 2 decisions
(recon `p2/decision-brief.md` section 3) and the consistency fixes of recon `p2/backlog.md`
section 7. Item 4 was amended: a `DllImport` names the library as a native link records it (the
ELF `DT_SONAME`, the exact Windows DLL file name), always `ExactSpelling` and `CDecl`, never a bare
name and never `libc` (Design.md 5.3). Where each item went, for the references above:

| Items | Design.md |
|---|---|
| 1, 2, 3, 4, 8, 13 | the "As built (Phase 1)" note at the end of section 12; item 4 also 5.3, item 8 also 4.5 |
| 5, 10, 11, 14 | section 3 (predefines, inline assembly, triple table, `long double`) |
| 6 | 4.2 |
| 7 | section 10, risk 9 |
| 9 | 2.3 |
| 12 | 5.5 |
