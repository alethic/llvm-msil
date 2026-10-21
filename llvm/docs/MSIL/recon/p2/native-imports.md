# Phase 2 recon: native imports (P/Invoke resolution from native libraries)

Key: `native-imports`. Date: 2026-10-21. Base: branch `msil`, HEAD `1ea64fe9fc7c`.
Scope: Design.md 5.2 step 3 and 5.3 (P/Invoke resolution from native export tables), replacing
Phase 1's fixed import table (plan decision D14). Supporting files (sources, inputs, trimmed logs):
[native-imports-files/](native-imports-files/). Scratch (not in the repository):
`C:\llvm-msil-scratch\p2\native-imports`.

Every LLVM/lld API, file and line below was checked in this checkout. Runtime claims come from the
experiments in section 3 (Windows 11 26200: .NET Framework 4.8, .NET 8.0.32, .NET 10.0.13; WSL
Ubuntu 26.04 with glibc 2.43: .NET 8.0.31, .NET 10.0.13). Anything not checked is marked
*unverified*.

## 0. Summary

Findings:

1. **llvm::object already reads everything msil-ld needs**, and a 600-line prototype resolver
   (`nisym`, [nisym.cpp](native-imports-files/nisym.cpp)) built against the shared build's libraries
   handles all of it: ELF shared objects (`.dynsym`, DT_SONAME, DT_NEEDED, GNU versions with the
   hidden bit, IFUNC, TLS), GNU linker scripts (`libc.so`, `libm.so`), COFF short import members
   (ucrt.lib, kernel32.lib, ws2_32.lib ordinals, data/const imports, EXPORTAS), COFF weak-alias
   objects (oldnames.lib), PE DLL export tables (with data-export detection) and IFS text stubs.
   Loading glibc libc+libm or the Windows CRT import libraries takes about 0.1 s per process.
2. **Bindability is narrower than "the symbol is in .dynsym"**. A P/Invoke resolves through
   `dlsym`/`GetProcAddress` by bare name, so only the *default* version of a versioned ELF symbol is
   reachable (`matherr@GLIBC_2.2.5`, compat-only, gives EntryPointNotFoundException; `memcpy@@GLIBC_2.14`
   wins over `memcpy@GLIBC_2.2.5`). TLS (`errno@@GLIBC_PRIVATE`) and data symbols must never become
   P/Invoke functions: `pinvokeimpl("libc.so.6" as "errno")` resolves and then **crashes with
   SIGSEGV** (exit 139) when called. Symbols defined only in native static code (`atexit`,
   `at_quick_exit`, `pthread_atfork` in glibc's `libc_nonshared.a`; `atexit` and `mainCRTStartup` in
   MSVC's `msvcrt.lib`; `printf` in `legacy_stdio_definitions.lib`) cannot be linked at all.
   IFUNC symbols (`strlen`, `memchr`, `memcpy`) work as ordinary functions.
3. **DllImport names must be what a native link records, not bare names** (Design.md 5.3 needs a
   change). On WSL .NET 8/10 without `-dev` packages, `"libm"`, `"m"`, `"libz"`, `"z"`, `"libm.so"`
   and `"libz.so"` all fail with DllNotFoundException; `"libm.so.6"` and `"libz.so.1"` (DT_SONAME)
   load. `"libc"` and `"c"` load only because CoreCLR special-cases `libc` (inferred, 3.2). On Windows every
   spelling of an existing DLL loads on .NET 8/10, but **.NET Framework 4.8 does not append `.dll`
   to a dotted name** (`"my.dot"` fails, `"my.dot.dll"` loads), so keep the DLL name verbatim,
   suffix included. API-set names from ucrt.lib (`api-ms-win-crt-stdio-l1-1-0.dll`) load on all
   three Windows runtimes, as do ordinals (`"#9"` for `htons`).
4. **Exact entry names matter on Windows**: without `nomangle`, an entry `GetModuleHandle` (which
   does not exist) silently binds `GetModuleHandleA` on all three runtimes. msil-ld knows the exact
   export, so it should emit `nomangle` (ExactSpelling, ImplMap flag 0x0001).
5. **Data imports are cheap to support** because Phase 1 already reads every global through an
   address field (D12): `<Module>::.cctor` fills the field with the native export's address, from
   `NativeLibrary.Load`+`GetExport` (verified on .NET 8/10, Windows and Linux: glibc `stdout`,
   `environ`, `optind`) or from `kernel32!LoadLibraryW`+`GetProcAddress` (verified on .NET Framework
   4.8 and .NET 8: msvcrt `_iob`, the FILE array MinGW's headers use for `stdout`). Real headers make
   this common on glibc: a small `getopt`/`fputs(stderr)` program references `stderr`, `environ`,
   `optind` as data (section 5).
6. **What real headers ask for differs per libc** ([real-headers.txt](native-imports-files/real-headers.txt)):
   with UCRT headers, `printf` is an *inline variadic definition* over the non-variadic import
   `__stdio_common_vfprintf(..., va_list)`, `stdout` is `__acrt_iob_func(1)` and `errno` is
   `*_errno()`, so Windows needs no native variadic call and no data import for stdio; with glibc
   headers, `printf` is a native variadic import and `stdout`/`stderr` are data imports.
7. **The variadic and aggregate decisions move to link time.** With separate compilation the
   compiler no longer knows whether an undefined function is native, so the Phase-1 compile-time
   errors of the AsmPrinter (variadic, non-scalar signature) become link-time checks after
   resolution, and the object must carry the signature flags for that (section 6). Experiments show
   the bridge P3 will need is small: a 16/24-byte struct returned through a hidden pointer works as a
   plain first `native int` P/Invoke parameter on Win64 (`lldiv`) and SysV (`mk`), and a 24-byte
   `byval` struct works as an explicit-layout value-type parameter (SysV `sum`).
8. **Sysroot**: msil-ld should read the real sysroot (`.so` files and their linker scripts, or
   Windows import libraries), exactly like ld.lld/lld-link. Text stubs are not a drop-in: `llvm-ifs`
   **cannot convert glibc** (`Size Mismatch for _sys_siglist`, `Type Mismatch for log2f`: versioned
   duplicates; IFS has a "TODO: Add support for symbol versioning"). A usable glibc x86_64 sysroot
   subset (libc.so.6, libm.so.6, libmvec.so.1, ld-linux, the two linker scripts, libc_nonshared.a)
   is 4.5 MB. Tests should use yaml2obj-built ELF `.so` files and `llvm-lib /def` import libraries
   (templates in the supporting files; both verified with the resolver).
9. **`-l` lookup must classify archives**: a sysroot's `libc.a` or MSVC's `msvcrt.lib` is native
   machine code. Design.md 5.2's "libfoo.a first" order must skip archives without CIL or bitcode
   members, or `-lc` would pick glibc's `libc.a`.
10. **Windows default libraries**: ucrt.lib (14 API-set DLLs) lacks the `mem*`/`str*` subset, which
    native links take from vcruntime.lib (`VCRUNTIME140.dll`, a VC++ redistributable that a clean
    Windows or a .NET install does not guarantee). `ucrtbase.dll` exports all of the C ones (and 65
    of the 71 `VCRUNTIME140.dll` imports), so the toolchain can bind them there instead (user
    decision, section 13). MinGW's GNU-dlltool import
    libraries use the *long* import format (COFF objects with `.idata$N` sections) and need a reader
    of their own; their msvcrt flavor implements `__acrt_iob_func` (stdout) in native code.

Recommendations (each detailed below):

- Resolver inside msil-ld: `lld/MSIL/NativeLibraries.{h,cpp}` with one reader per format and one
  symbol index; resolution after objects, archives and managed assemblies; one P/Invoke MethodDef
  per (symbol, IL signature) with ImplMap `nomangle cdecl`, ModuleRef = DT_SONAME / DLL file name
  verbatim; diagnostics with the reason a symbol is not bindable (section 8).
- Data imports in Phase 2 through the address-field + `.cctor` accessor (or, minimally, a clear
  diagnostic); `errno` stays a function import (`__errno_location`, `_errno`).
- Keep the D14 table only as the import policy of a self-contained `llc`/`clang -S` module
  ("standalone mode"), so the Phase-1 lit tests (46 files with P/Invoke text) and the M1
  execution programs (135 C and IR files) keep working while the
  linked path is built; move the f32 `ldexp`/`frexp` promotion off the table (section 10).

## 1. Phase 1 today (D14)

### 1.1 The table and its users

- Decision D14 (ImplementationPlan.md:155) and its amendment "Windows import sets follow the DLL
  exports" (section 12 row, ImplementationPlan.md:2043). Status.md section 2 "The Phase-1 import
  table (D14)" (Status.md:120-138) is the user-facing description.
- `llvm/lib/Target/MSIL/MSILUtilities.h:88-112`: `std::optional<StringRef>
  MSIL::getPInvokeModule(const Triple &TT, StringRef Name, const Function *F = nullptr)` and
  `StringRef MSIL::getPInvokeEntry(StringRef Name, const Function *F)`.
- `llvm/lib/Target/MSIL/MSILUtilities.cpp:144-308`: `enum class CRuntime { Glibc, Musl, UCRT,
  MSVCRT }`, `MathImports[]` (36 double functions with `NotInUCRT`/`NotInMSVCRT` bits for the
  double and `f` variants), `getCRuntime` (Linux non-musl non-Android = glibc; MSVC environment =
  UCRT; GNU environment = MSVCRT), `findMathImport`, `getPInvokeModule` (252-299: the
  `msil-import-module` attribute wins; `puts`/`putchar` -> `libc`/`ucrtbase`/`msvcrt`; math ->
  `libm.so.6` on glibc, `libc` on musl, `ucrtbase`/`msvcrt` unless the DLL lacks it),
  `getPInvokeEntry` (301-308: `msil-import-name` or the name).
- Code that consults the table:
  - `MSILISelLoweringCall.cpp:196-255` (library-call policy for `ExternalSymbolSDNode` callees): a
    function of the module with the same IL name is called directly; otherwise
    `getPInvokeModule` must accept it, `__` names get "runtime helper '<name>' is not available for
    MSIL yet (phase 3)", anything else "library call '<name>' is not a Phase-1 import".
  - `MSILISelLowering.cpp:107-131`: the math operations are `Expand` (libcalls) and `FSIN`/`FCOS`
    `LibCall`; `FSINCOS` is split because `sincos` is not an import.
  - `MSILISelLowering.cpp:225-241`: float32 `FLDEXP`/`FFREXP` are promoted to f64 when the f32
    libcall "is not an import" and the f64 one is, by calling `getPInvokeModule` on the libcall's
    name. **This makes code generation depend on the table** (section 10.2, item 3).
  - `MSILAsmPrinter.cpp:462-485` (`emitStartOfAsmFile`): writes the UTF-8 BOM when an
    `msil-import-*` value is non-ASCII (D11 amendment).
  - `MSILAsmPrinter.cpp:681-702` (`hasNativeSignature`): only `void`/`i32`/`i64`/pointer/`float`/
    `double`, and no `byval`, `inalloca`, `preallocated`, `sret` or `nest` parameter.
  - `MSILAsmPrinter.cpp:704-723` (`reportNotImported`) and `725-807` (`emitPInvokeDeclarations`):
    one declaration per (IL name, signature); an undefined function defined in the module with the
    same name and signature is skipped; `F->isVarArg()` -> "undefined variadic function '<name>' is
    not supported for MSIL yet (phase 3)"; not in the table -> "undefined function '<name>' is not
    defined in this module and is not a Phase-1 import (puts, putchar, the C math library,
    msil-import-module); Phase 1 has no linker"; non-native signature -> "imported function '<name>'
    has a parameter or result that a P/Invoke cannot pass natively ...".
  - `MSILAsmPrinter.cpp:545-563` (`collectExternGlobals`): a referenced external variable is
    "external global variable '<name>' is not supported (phase 2 linker)", **but its address field
    is still emitted** so the code that reads it stays well formed. Section 5 builds on that.
  - `MCTargetDesc/MSILTargetStreamer.cpp:117-139` (`MSILTargetAsmStreamer::emitPInvoke`): prints
    `.method public hidebysig static pinvokeimpl("<lib>" [as "<entry>"] cdecl|stdcall) <ret>
    '<name>'(<params>) cil managed preservesig {}`. ImplMap flags are therefore `CallConvCdecl`
    (0x0200) only: no `nomangle`.
- IR attributes: `"msil-import-module"="<lib>"`, `"msil-import-name"="<entry>"` on a declaration.
  No C spelling exists (backlog item 10: CoreMark added them to the IR by hand).

### 1.2 Prototype fix-ups (M9) that interact with imports

- `MSILFixFunctionBitcasts.cpp` (header 13-47): calls whose type differs from the callee get a
  wrapper; a variadic-typed call of a non-variadic function (the SysV `isNoProtoCallVariadic` K&R
  shape) is made non-variadic; calls of variadic functions keep their callee so LowerCall diagnoses
  them (D19).
- `MSILAddMissingPrototypes.cpp` (header 13-41): clang marks body-less prototype-less declarations
  `"no-prototype"`; the pass derives the signature from the direct calls. A declaration used only
  by address is the error "the signature of prototype-less function '<name>' cannot be derived: its
  address is used, but it is never called directly; declare it with a prototype"
  (ImplementationPlan.md:2081, the M9 fix for zero-parameter P/Invokes). Whether valid C hits it
  depends on the optimization level.
- Phase 2 consequence: with separate compilation the signature of an undefined function comes from
  *this* object's calls only, and different objects can disagree (K&R callers, the legalizer's
  `sqrt(double)` next to a C call). Native libraries carry no signatures, so msil-ld must emit one
  P/Invoke per (symbol, signature) as Phase 1 does per module (`Declared` set in
  `emitPInvokeDeclarations`), and keep one canonical stub for the function's address field
  (`'<msil>fn_<name>'`, M9) so `&puts` compares equal across objects. A prototype-less import used
  only by address still has no signature source and stays an error (now at link time, unless
  another object defines the function).

### 1.3 What real headers produce

Phase-1 tests are freestanding (`msil/test/Inputs/msil_test.h` declares `puts` itself). With the
real headers ([real-headers.txt](native-imports-files/real-headers.txt), clang at HEAD, `-O1`):

| | Windows SDK UCRT headers (`x86_64-pc-windows-msvc-msil`) | glibc 2.43 headers (`x86_64-unknown-linux-gnu-msil`, `--sysroot`) |
|---|---|---|
| `printf("...", errno)` | `define linkonce_odr i32 @printf(ptr, ...)` (inline, comdat; `_CRT_STDIO_INLINE`, stdio.h:950) calling `declare i32 @__stdio_common_vfprintf(i64, ptr, ptr, ptr, ptr)` | `declare i32 @printf(ptr, ...)`: a native variadic import |
| `errno` | `declare ptr @_errno()` (errno.h:24 `#define errno (*_errno())`) | `declare ptr @__errno_location()` |
| `fputs("x\n", stderr)` | `fwrite` (optimizer) + `declare ptr @__acrt_iob_func(i32)` (corecrt_wstdio.h:37) | `fwrite` + `@stderr = external global ptr`: a data import |
| `getopt`, `optind`, `environ` | n/a | `declare i32 @getopt(...)`, `@optind = external global i32`, `@environ = external global ptr` |

Phase 1 rejects both programs (the variadic definition or call, every undefined function outside
the table, every external global).

## 2. Reading native libraries with llvm::object

### 2.1 APIs (verified in this tree)

| Need | API (file:line) |
|---|---|
| Classify a file | `identify_magic(StringRef)` -> `file_magic::{archive, elf_shared_object, coff_import_library, pecoff_executable, coff_object, tapi_file, macho_dynamically_linked_shared_lib, macho_dynamically_linked_shared_lib_stub, unknown}` (`llvm/include/llvm/BinaryFormat/Magic.h:26-56`); text (`unknown`) = linker script or IFS |
| Archives | `object::Archive::create`, `Archive::children(Error &)` (`Archive.h:416`), `Child::getAsBinary()` (`Archive.h:299`) |
| ELF dynamic symbols | `ELFObjectFileBase::getDynamicSymbolIterators()` (`ELFObjectFile.h:90`, impl 1501); typed symbol via `ELFObjectFile<ELFT>::getSymbol(DataRefImpl)` (`ELFObjectFile.h:459`) for `st_shndx`, type, binding, visibility |
| ELF versions | `ELFObjectFileBase::readDynsymVersions()` (`ELFObjectFile.h:117`, impl `ELFObjectFile.cpp:806-871`): one `VersionEntry{Name, IsVerDef}` per dynsym entry after the null one; **the `IsVerDef` field holds "is default"** (it is filled from `getSymbolVersionByIndex`'s `IsDefault`). `ELFFile::getSymbolVersionByIndex` (`ELF.h:1119-1141`): `VER_NDX_LOCAL`/`GLOBAL` -> unversioned; the `VERSYM_HIDDEN` bit -> not default. The raw versym (for the hidden bit of each entry) is `getSectionContentsAsArray<Elf_Versym>` of `SHT_GNU_versym` |
| DT_SONAME, DT_NEEDED | `ELFFile::dynamicEntries()` (`ELF.h:409`) + the string table of the `SHT_DYNAMIC` section's `sh_link` (`getSection`, `getStringTable`, `ELF.h:563`, `1332`) |
| lld's binding rule | `lld/ELF/InputFiles.cpp:1564-1745` (`SharedFile::parse`): the bare name is defined only when `ver == idx` (not hidden, line 1721); every versioned definition is also added as `name@ver` |
| COFF short import member | `object::COFFImportFile` (`COFFImportFile.h:36-86`): `getCOFFImportHeader()` -> `coff_import_header` (`COFF.h:550-562`: `OrdinalHint`, `getType()` = bits 0-1 code/data/const, `getNameType()` = bits 2-4), `getExportName()` (`COFFImportFile.cpp:76-99`: "" for ordinals; strips for NOPREFIX/UNDECORATE; EXPORTAS name). `isData()` is private. **The DLL name is not exposed**: parse it as `lld/COFF/InputFiles.cpp:1259-1300` (`ImportFile::parse`) does: 20-byte header, symbol name NUL, DLL name NUL [, EXPORTAS name NUL] |
| COFF weak aliases (oldnames.lib) | `COFFSymbolRef::isWeakExternal()` (`COFF.h:400`), `getWeakExternal()` (371) -> `coff_aux_weak_external{TagIndex, Characteristics}` (511); `COFFObjectFile::getSymbol(uint32_t)` (1158), `getSymbolName(COFFSymbolRef)` (1177); alias when `Characteristics == IMAGE_WEAK_EXTERN_SEARCH_ALIAS` |
| PE DLL exports | `COFFObjectFile::export_directories()` (`COFF.h:1128`), `ExportDirectoryEntryRef::{getDllName, getOrdinal, getExportRVA, getSymbolName, isForwarder, getForwardTo}` (`COFF.h:1300-1310`); data vs code from the containing section's `IMAGE_SCN_CNT_CODE` |
| IFS text stubs | `ifs::readIFSFromBuffer` (`IFSHandler.h:40`), `ifs::readELFFile` (`ELFObjHandler.h:28`); `IFSStub` has "TODO: Add support for symbol versioning" (`IFSStub.h`); `convertELFSymbolTypeToIFS` maps `STT_GNU_IFUNC` to `Unknown` (`IFSStub.cpp:120-134`). `LLVMInterfaceStub` is **not built** in the shared tree (no `.lib`): 3 sources |
| Mach-O (later) | `llvm/Object/TapiFile.h`, `TapiUniversal.h`; `MachO::InterfaceFile::getInstallName()` (`TextAPI/InterfaceFile.h:186`); `LLVMTextAPI.lib` is built |

### 2.2 ELF: glibc 2.43 (WSL Ubuntu 26.04)

Inputs copied from WSL: `libc.so.6` (2,190,608 bytes, 3,189 dynsym entries, 3,123 defined
global/weak default-visibility non-ABS), `libm.so.6` (1,372), `libmvec.so.1` (216),
`ld-linux-x86-64.so.2` (32), `libpthread.so.0` and `libdl.so.2` (placeholders only), `libz.so.1.3.1`
(88). Full output: [nisym-glibc.txt](native-imports-files/nisym-glibc.txt).

| Name | Library (DT_SONAME) | Kind and versions | Bindable by a P/Invoke? |
|---|---|---|---|
| `puts`, `printf`, `vprintf`, `malloc`, `qsort`, `exit` | libc.so.6 | FUNC `@@GLIBC_2.2.5` | yes (`printf` is variadic: P3-B) |
| `memcpy` | libc.so.6 | IFUNC `memcpy@@GLIBC_2.14` + FUNC `memcpy@GLIBC_2.2.5` (hidden) | yes, the 2.14 IFUNC |
| `strlen`, `strchr`, `memmove`, `time`, `gettimeofday` | libc.so.6 | IFUNC | yes (verified for `strlen`, `memchr`) |
| `stdout`, `stderr`, `stdin` | libc.so.6 | OBJECT, 8 bytes | data import (section 5) |
| `environ` / `__environ`, `optarg`, `optind`, `timezone`, `daylight`, `tzname` | libc.so.6 | OBJECT (`environ`, `timezone`, `daylight`, `tzname` WEAK) | data import |
| `errno` | libc.so.6 | **TLS** `errno@@GLIBC_PRIVATE` | no: never; `<errno.h>` gives `(*__errno_location ())` |
| `__errno_location` | libc.so.6 | FUNC | yes |
| `pthread_create`, `dlopen`, `dlsym` | libc.so.6 | `@@GLIBC_2.34` + hidden `@GLIBC_2.2.5` | yes (2.34 moved them from libpthread/libdl) |
| `stat`, `fstat` | libc.so.6 | WEAK FUNC `@@GLIBC_2.33` (`__xstat@@GLIBC_2.2.5` also exported) | yes; before 2.33 they existed only in libc_nonshared.a (glibc 2.33 release notes, from memory) |
| `atexit`, `at_quick_exit`, `pthread_atfork`, `__stack_chk_fail_local`, `__pthread_atfork` | **libc_nonshared.a** only | native static code | no |
| `sqrt` | libm.so.6 | WEAK FUNC `@@GLIBC_2.2.5` | yes; **not** in libc.so.6 (`"libc" as "sqrt"` fails) |
| `sqrtf` | libm.so.6 | `sqrtf@@GLIBC_2.43` + hidden `@GLIBC_2.2.5` | yes |
| `exp`, `log`, `pow`, `log2`, `exp2` | libm.so.6 | `@@GLIBC_2.29` + hidden `@GLIBC_2.2.5` | yes |
| `floor`, `sin`, `cos`, `fma`, `modf` | libm.so.6 | WEAK IFUNC | yes |
| `matherr`, `_LIB_VERSION` | libm.so.6 | compat-only (hidden) | **no**: EntryPointNotFoundException (verified, l15) |
| `signgam` | libm.so.6 | WEAK OBJECT | data import |
| `__libpthread_version_placeholder` | libpthread.so.0 | 12 hidden versions | no |
| `zlibVersion`, `deflate` | libz.so.1 | FUNC (zlib's version nodes `ZLIB_1.2.0` ... are ABS marker symbols) | yes |

Linker scripts (from `libc6-dev 2.43-2ubuntu2.4`, see section 7.1): `libc.so` is
`GROUP ( /usr/lib/x86_64-linux-gnu/libc.so.6 /usr/lib/x86_64-linux-gnu/libc_nonshared.a AS_NEEDED (
/lib64/ld-linux-x86-64.so.2 ) )`, and `libm.so` is `GROUP ( /usr/lib/x86_64-linux-gnu/libm.so.6
AS_NEEDED ( /usr/lib/x86_64-linux-gnu/libmvec.so.1 ) )`. `libpthread.a` and `libdl.a` are empty
8-byte archives. `libc_nonshared.a` defines exactly `__pthread_atfork __stack_chk_fail_local
at_quick_exit atexit pthread_atfork`. Resolving through these scripts with a sysroot works in the
prototype ([nisym-glibc-sysroot.txt](native-imports-files/nisym-glibc-sysroot.txt)): `-lc` gives
libc.so.6 + libc_nonshared.a + ld-linux (`__tls_get_addr@@GLIBC_2.3`, `_dl_argv@@GLIBC_PRIVATE`),
`-lm` gives libm.so.6 + libmvec.so.1 (`_ZGVbN2v_sin@@GLIBC_2.22`, IFUNC).

### 2.3 COFF import libraries (Windows SDK 10.0.26100.0, MSVC 14.51.36231)

Output: [nisym-windows.txt](native-imports-files/nisym-windows.txt). Counts from `llvm-readobj
--coff-imports` and [parse_readobj_imports.py](native-imports-files/parse_readobj_imports.py).

| Library | Archive members | Short import members | DLLs | Notes |
|---|---|---|---|---|
| `ucrt\x64\ucrt.lib` | 1,392 | 1,350 (1,348 code, 2 **const**: `_mbcasemap`, `_wctype`) | 14 API sets: conio 30, convert 122, environment 18, filesystem 65, heap 27, locale 20, math 290, multibyte 200, process 36, runtime 104, stdio 159, string 177, time 72, utility 30 (all `api-ms-win-crt-*-l1-1-0.dll`) | name type `name` for all |
| `um\x64\kernel32.lib` | 1,399 | 1,396 code | `KERNEL32.dll` | `HeapAlloc`, `AcquireSRWLockExclusive` are forwarders to NTDLL in kernel32.dll; GetProcAddress follows them |
| `um\x64\ws2_32.lib` | 201 | 196 (48 **ordinal**, 148 name) | `WS2_32.dll` | `socket` = `#23`, `htons` = `#9` |
| `um\x64\user32.lib` | | 801 (11 ordinal, some with no public name) | `USER32.dll` | |
| `VC\...\lib\x64\vcruntime.lib` | 113 | 102 code | `VCRUNTIME140.dll` 71, `VCRUNTIME140_1.dll` 3, `VCRUNTIME140_THREADS.dll` 28 | `memcpy memset memmove memcmp memchr strchr strrchr strstr wcschr wcsrchr wcsstr longjmp __C_specific_handler _purecall ...` |
| `oldnames.lib` | 228 | 0 | | machine-independent (`IMAGE_FILE_MACHINE_UNKNOWN`) COFF objects, each a weak external with `Search: Alias`: `strdup` -> `_strdup`, `open` -> `_open`, `fileno` -> `_fileno`, `getcwd` -> `_getcwd`, ... |
| `msvcrt.lib` | 1,334 | 0 | | 1,060 AMD64 + 274 machine-unknown objects: the CRT startup and static parts (`mainCRTStartup`, `atexit`, `_onexit`, `__security_check_cookie`), 169 undefined `__imp_` symbol references |
| `legacy_stdio_definitions.lib` | | 0 | | native `printf`, `vprintf`, `fprintf`, `sprintf`, `scanf` |

Name-by-name (`-lucrt -lvcruntime -loldnames -lkernel32 -lws2_32 -lmsvcrt`):

| Name | Result |
|---|---|
| `puts`, `__acrt_iob_func` | `api-ms-win-crt-stdio-l1-1-0.dll`, code |
| `malloc` | `api-ms-win-crt-heap-l1-1-0.dll` |
| `_errno`, `_crt_atexit`, `exit` | `api-ms-win-crt-runtime-l1-1-0.dll` |
| `sqrt`, `_hypotf` | `api-ms-win-crt-math-l1-1-0.dll` |
| `printf`, `frexpf`, `hypotf`, `ldexpf` | **undefined**: header inline functions (stdio.h:950; corecrt_math.h:735-745) |
| `memcpy`, `strchr` | `VCRUNTIME140.dll` (only vcruntime.lib has them) |
| `strdup`, `open`, `fileno` | weak alias (oldnames.lib) -> `_strdup` etc. in `api-ms-win-crt-*`; P/Invoke entry is the alias target |
| `atexit`, `mainCRTStartup` | only native code in msvcrt.lib: not bindable |
| `_mbcasemap` | const (data) import: not a function |
| `socket`, `htons` | `WS2_32.dll` ordinals `#23`, `#9` |
| `stdout`, `errno`, `environ`, `_environ` | undefined: macros over `__acrt_iob_func`, `_errno`, `__p__environ` |

An import library built with `llvm-lib /machine:x64 /def:` from
[coff-import.def](native-imports-files/coff-import.def) gives every member kind the resolver must
handle: name (`fn1`), `NONAME` ordinal (`fn2` -> `#5`), `DATA` (`dataobj`, `__imp_` only),
ordinal with name (`fn3`), and `renamed == realname` (name type EXPORTAS -> entry `realname`).

### 2.4 PE DLL export tables

`nisym` on `System32\ucrtbase.dll` (2,484 named exports), `msvcrt.dll` (1,330), `kernel32.dll`
(1,698), `ws2_32.dll` (196): ucrtbase exports 65 of the 71 `VCRUNTIME140.dll` imports of
vcruntime.lib, among them all 11 C memory/string functions (`memcpy memset memmove memcmp memchr
strchr strrchr strstr wcschr wcsrchr wcsstr`) and `longjmp` (the 6 it lacks are CRT-internal:
`__telemetry_main_*_trigger`, `__vcrt_*`), and
no `printf`/`vprintf`/`frexpf`/`hypotf`; **msvcrt.dll exports `printf`** but not `__p__environ`
(x64); data exports are detected from the RVA's section (`_iob`, `_environ`, `__argc`, `_timezone`
in msvcrt; `_mbcasemap` in both). A DLL built with `lld-link -dll -noentry` from a C file with a
`dllexport` function and variable reports `fn1` as code and `dataobj` as data, from both the DLL and
its import library.

### 2.5 GNU long-format import libraries (MinGW)

`mingw-w64-x86-64-dev 13.0.0-2ubuntu1` (built with GNU binutils dlltool): `libkernel32.a`,
`libmsvcrt.a`, `libucrt.a` contain **no** short import members. Each import is a COFF object
(`llvm-objdump -h -r -s -t` of member `libapi-ms-win-crt-stdio-l1-1-0s00134.o` of `libucrt.a`):

```
.text     8 bytes  ff25 00000000 9090    jmp *__imp_puts(%rip)  (REL32 __imp_puts)
.idata$7  4 bytes  ADDR32NB _head_lib64_libapi_ms_win_crt_stdio_l1_1_0_a   (-> DLL name)
.idata$5  8 bytes  ADDR32NB .idata$6     (IAT; an ordinal import has IMAGE_ORDINAL_FLAG64 instead, per the PE spec)
.idata$4  8 bytes  ADDR32NB .idata$6     (ILT)
.idata$6  8 bytes  86 00 'puts' 00 00    (hint 0x86, name)
symbols: puts (.text), __imp_puts (.idata$5), _head_... (undefined)
```

The DLL name lives in the archive's tail member (`libkernel32t.o`: `.idata$7` = `KERNEL32.dll\0`),
reached through the head member that defines `_head_<lib>`. A reader for this format is about
150-200 lines (follow `.idata$7` -> head symbol -> head `.idata$2` Name relocation -> tail string;
the head's relocation was not dumped, this follows the import directory layout);
the prototype does not have one and classifies these members as native code. In `libmsvcrt.a`,
`__acrt_iob_func` (the stdout of current mingw headers) is native code
(`lib64_libmsvcrt_common_a-acrt_iob_func.o`); in `libucrt.a` it is a real import. `llvm-dlltool`
and llvm-mingw produce short-format libraries instead.

### 2.6 The prototype (`nisym`)

[nisym.cpp](native-imports-files/nisym.cpp), built by [build-tools.ps1](native-imports-files/build-tools.ps1)
with clang-cl against `D:\llvm-msil\build\lib\*.lib` (no cmake or ninja in the shared tree; the 3
`lib/InterfaceStub` sources compiled locally). Usage: `nisym [--os=linux|windows]
[--sysroot=DIR] [-LDIR] [-lNAME] [FILE...] --query=a,b,c | --dump`. It prints, per name, every
definition in link order and `=>` the one a linker would bind, as
`pinvokeimpl("<module>" as "<entry>") [kind, name@@VER]` or `NOT BINDABLE: <reason>`. Per-file:
ELF shared object (sections 2.1-2.2 rules), linker script (minimal tokenizer: comments, GROUP,
INPUT, AS_NEEDED, `-l`, sysroot-relative absolute paths; it wrongly treats `OUTPUT_FORMAT`'s
argument as an input, harmless), archive (short import members, COFF weak aliases, native ELF/COFF
objects recorded as "native code only"), PE DLL, IFS. Timing: libc.so.6 + libm.so.6, or ucrt.lib +
kernel32.lib + oldnames.lib + vcruntime.lib, about 0.1 s wall per process including startup.

## 3. DllImport library naming: what actually loads

Experiments: [gen_probe.py](native-imports-files/gen_probe.py) writes one ILAsm program per OS in
which every case is its own method (`pinvokeimpl("<module>" as "<entry>" cdecl) ... preservesig`,
Phase 1's shape) called from `main` inside `try`/`catch`; assembled with Framework ilasm (`-exe
-pe64 -x64`), run directly on Framework and with `dotnet exec --runtimeconfig` on .NET. Logs:
[probe-windows.log](native-imports-files/probe-windows.log),
[probe-linux.log](native-imports-files/probe-linux.log).

### 3.1 Windows (identical on .NET Framework 4.8, .NET 8, .NET 10 unless noted)

| Module string | Entry | Result |
|---|---|---|
| `ucrtbase`, `ucrtbase.dll` | `abs` | OK |
| `api-ms-win-crt-utility-l1-1-0`, `...-l1-1-0.dll`, `API-MS-WIN-CRT-UTILITY-L1-1-0.DLL` | `abs` | OK |
| `api-ms-win-crt-string-l1-1-0` / `-math-` / `-stdio-` / `-runtime-` | `strlen`, `sqrt`, `__acrt_iob_func`, `_errno` | OK |
| `api-ms-win-crt-math-l1-1-0` (wrong set) | `abs` | OK: every API set resolves to ucrtbase.dll |
| `KERNEL32`, `KERNEL32.dll`, `kernel32` | `GetTickCount` | OK |
| `WS2_32` | `#9` (ordinal) | OK (returns `htons(0x1234)` = 13330) |
| `VCRUNTIME140`, `ucrtbase` | `memchr` | OK (both present on this machine) |
| `api-ms-win-crt-string-l1-1-0` | `_strdup` (oldnames target) | OK |
| `ucrtbase` | `frexpf`, `printf` | EntryPointNotFoundException |
| `libc`, `c` | `abs` | DllNotFoundException |
| `msvcrt` | `abs` | OK |
| `my.dot` (DLL file `my.dot.dll`) | `fn1` | **Framework: DllNotFoundException**; .NET 8/10: OK |
| `my.dot.dll` | `fn1` | OK everywhere |
| `kernel32.dll` | `GetModuleHandle` (no such export) | **without `nomangle`: binds `GetModuleHandleA`**; with `nomangle`: EntryPointNotFoundException |
| `api-ms-win-crt-utility-l1-1-0.dll` | `lldiv`, hidden return pointer passed as a plain first `native int` | OK: 9, 2 ([sret-win64.il](native-imports-files/sret-win64.il)) |

.NET Framework has no `NativeLibrary`; its P/Invoke loader is LoadLibrary-based, which appends
`.dll` only to a name with no extension at all, hence the dotted-name failure. Ordinals (`#N`) work
on Framework too. (Case `x01` of the log fails on all three runtimes because it declared
`msvcrt.dll`'s `__p__environ`, which x64 msvcrt.dll does not export: exactly the mistake a
resolver catches at link time. `data-win.il` in section 5.2 replaced it.)

### 3.2 Linux (WSL, glibc 2.43, no libc6-dev or zlib1g-dev; .NET 8 and 10 identical)

| Module string | Entry | Result |
|---|---|---|
| `libc`, `c`, `libc.so.6` | `abs` | OK |
| `libc.so` | `abs` | DllNotFoundException (the `-dev` file is a linker script, and absent) |
| `libm`, `m`, `libm.so` | `sqrt` | DllNotFoundException |
| `libm.so.6` | `sqrt`, `sqrtf` (2.43), `exp` (2.29) | OK |
| `libz`, `z`, `libz.so` | `zlibVersion` | DllNotFoundException |
| `libz.so.1` (DT_SONAME), `libz.so.1.3.1` (file) | `zlibVersion` | OK |
| `libpthread.so.0`, `libdl.so.2` | `pthread_self`, `dlerror` | OK: `dlsym` on a handle searches its DT_NEEDED (libc.so.6) |
| `libm.so.6` | `abs` | OK, same reason |
| `libm.so.6` | `matherr` (compat-only) | EntryPointNotFoundException |
| `libc.so.6` | `memchr`, `strlen` (IFUNC) | OK |
| `libc.so.6` | `fstat` (`@@GLIBC_2.33`), `__errno_location` | OK |
| `libc.so.6` | `atexit` (libc_nonshared.a only) | EntryPointNotFoundException |
| `libc` | `sqrt` | EntryPointNotFoundException (glibc keeps it in libm) |
| `libc.so.6` | `errno` (TLS) called as a function | **Segmentation fault, exit 139** (separate run, `probe-l26`) |
| `libbig.so.1` (ld.lld-built) | `mk`: 24-byte struct via IR `sret`, as a plain first `native int` | OK (21) ([abi-sysv.il](native-imports-files/abi-sysv.il)) |
| `libbig.so.1` | `sum`: 24-byte struct `byval`, as an explicit-layout value type `.size 24` | OK (42) |

`"libc"` works because CoreCLR maps that one name to the C library (inferred from these results:
`"c"` works through the `lib` prefix probe, while `"libm"`/`"m"` fail; the runtime source was not
read). Behavior on musl and macOS is *unverified* (musl's dynamic linker is known to treat
`libc.so`, `libm.so`, `libpthread.so` ... as itself; macOS CoreCLR maps `libc` to
`/usr/lib/libc.dylib`, from memory).

### 3.3 Naming rule for msil-ld

- **ELF**: the module string is the library's DT_SONAME verbatim (`libc.so.6`, `libm.so.6`,
  `libz.so.1`), or its file name as found when it has none, which is what `ld` records in DT_NEEDED.
  Runtime-only systems have no unversioned `libfoo.so` symlinks, so bare names fail.
- **PE/COFF**: the DLL name from the import member or the DLL's export directory, verbatim with its
  suffix (`api-ms-win-crt-stdio-l1-1-0.dll`, `KERNEL32.dll`, `WS2_32.dll`): what `link.exe` writes
  into the import table, and the only spelling that is safe on Framework for dotted names.
- **Mach-O** (later): the install name (`/usr/lib/libSystem.B.dylib`), from `LC_ID_DYLIB` or a
  `.tbd`'s `install-name` (*unverified* at runtime).
- **Entry**: the export name (EXPORTAS/undecorated per the import name type), `#<ordinal>` for
  ordinal-only imports, the alias target for oldnames aliases. Always with `nomangle`.
- An explicit `msil-import-module`/`msil-import-name` is used as given and not checked (wasm-ld's
  `import_module` semantics, section 8.3).
- Phase 1's choices are therefore not what msil-ld will produce: `"libc"` becomes `"libc.so.6"`,
  `"ucrtbase"` becomes the API-set DLL names (or `"ucrtbase.dll"` if the user prefers one module,
  section 13), and `"msvcrt"` becomes `"msvcrt.dll"`. All of these load (sections 3.1-3.2).

## 4. Symbol versions, IFUNC and the lost version floor

- `dlsym` (and therefore DllImport and `NativeLibrary.GetExport`) returns the default (`@@`) version
  or an unversioned definition; a hidden (`@`) version is unreachable. lld uses the same rule for
  the bare name (`ver == idx`). The resolver must apply it: bind the default definition, and when
  only hidden versions exist report "only the non-default version oldf@V1, which a P/Invoke cannot
  bind" instead of emitting a P/Invoke that fails at the first call.
- IFUNC (`STT_GNU_IFUNC`) symbols are functions for the resolver; `dlsym` runs the resolver.
- **Portability consequence (by reasoning; only the binding rule was verified):** a native link
  against glibc 2.43 records `GLIBC_2.43` for `sqrtf` and `GLIBC_2.34` for `pthread_create`, and
  the loader refuses to start on an older glibc. A P/Invoke records only `libc.so.6` +
  `pthread_create`, so the same assembly starts on glibc 2.31 and fails at the first call with
  EntryPointNotFoundException (there `pthread_create` is in libpthread.so.0, and `libc.so.6`'s
  handle does not search it). Advice for users is the native one (link against the oldest sysroot
  you target); msil-ld should print the version floor (the highest default version among bound
  symbols, per library) with a `--print-imports`-style option, and may record it in the assembly.
- The ELF readers must skip absolute version-marker symbols (`GLIBC_2.2.5@@GLIBC_2.2.5`,
  `ZLIB_1.2.0`): `st_shndx == SHN_ABS`.

## 5. Data imports (stdout, errno, environ)

### 5.1 Kinds the resolver sees

| Source | Kind | Phase 2 action |
|---|---|---|
| ELF `STT_OBJECT`/`STT_COMMON` (`stdout`, `environ`, `optind`, `signgam`) | data | data-import accessor (5.2) or diagnostic |
| ELF `STT_TLS` (`errno`) | TLS | error: "thread-local; use <errno.h>" |
| COFF short import `IMPORT_DATA` / `IMPORT_CONST` (`_mbcasemap`, a `.def` `DATA` export) | data | accessor or diagnostic |
| PE export in a non-code section (`_iob`, `_environ`) | data | accessor or diagnostic |
| A data symbol used as a callee | | error: "'stdout' in libc.so.6 is data, but a.o calls it as a function" (prevents the SIGSEGV of 3.2) |
| A function symbol used as data (its address in a table) | | the function's address field (M9), as today |

`errno` never needs a data import: UCRT, glibc and musl headers all expand it to a function call
(`*_errno()`, `*__errno_location()`). Whether the runtime itself clobbers `errno` between two
P/Invokes (for example `fopen` then `__errno_location`) is a phase-3 conformance question
(*unverified*; .NET's `SetLastError=true`/`Marshal.GetLastPInvokeError` captures `errno` on Unix).

### 5.2 The accessor (verified)

Phase 1 already compiles every global access to `ldsfld native int '<name>'` (D12), and keeps that
field for an undefined variable (`collectExternGlobals`). So a native data import needs no codegen
change: msil-ld initializes the field in the module initializer with the native address.

- Modern .NET ([data-modern.il](native-imports-files/data-modern.il)): a private helper
  `'<msil>dataimport'(string lib, string name)` = `NativeLibrary::Load(lib)` +
  `NativeLibrary::GetExport(handle, name)` (`[System.Runtime.InteropServices]`); `.cctor` stores
  the results in `stdout`, `environ`, `optind`; `main` reads `*stdout` and calls `fputs`/`fflush`
  through it. Output on WSL .NET 8 and 10: `optind = 1`, `environ[0] starts with SHEL`, the line
  written through glibc's `stdout`. The same program with msvcrt's `_iob` runs on Windows .NET 8/10.
  On Framework it fails in `.cctor` (no `System.Runtime.InteropServices` 8.0.0.0).
- .NET Framework (Windows triples only) ([data-win.il](native-imports-files/data-win.il)):
  `kernel32!LoadLibraryW` + `GetProcAddress` P/Invokes in the same `.cctor` shape; writing through
  `&_iob[1]` with msvcrt's `fputs` works on Framework 4.8 and .NET 8.
- Which helper to use follows P2-B's reference binding: `NativeLibrary` when a reference defines it,
  else the kernel32 pair on Windows triples, else an error naming the symbol. The three-argument
  `NativeLibrary.Load(string, Assembly, DllImportSearchPath?)` probes like DllImport (assembly
  directory first); the experiment used the one-argument overload (*unverified* difference for
  app-local libraries).
- Semantics: the address is the variable itself (no copy relocation), resolved eagerly at startup
  like a native data relocation; a missing library fails the module initializer
  (TypeInitializationException) instead of a lazy first-call failure.
- Cost: one helper method, two string literals and one call per data import in `.cctor`.

Recommendation: implement it in Phase 2 (P2-B, with the binding work), because glibc programs
reference `stderr`/`stdout`/`environ`/`optind` routinely (section 1.3) and the phase-3 conformance
programs (zlib's minigzip, Lua, SQLite's shell) write to `stderr`. The minimal alternative is the
error "data symbol 'stdout' in libc.so.6 cannot be imported yet".

## 6. Variadic, non-blittable and prototype-less imports in Phase 2

With objects, an undefined function can resolve to another object (our internal convention:
aggregates indirect, own variadic convention from P3), to a managed assembly (P2-C), or to a native
library (the native ABI). Only the last needs the native-signature checks, so they move from the
AsmPrinter (compile time) to msil-ld (after resolution). The object must carry, per undefined
function: the IL signature, `isVarArg`, `no-prototype` (and whether its signature was derived or is
unknown), `byval`/`sret`/`inalloca`/`nest` parameter attributes with the C type's size and
alignment, vector/i128/half types, address-taken, explicit `msil-import-*` values, and the
`dllimport` storage class (a hint only). Per undefined variable: name and `dllimport`.

Phase-2 diagnostics after a native resolution (texts are proposals):
- variadic: "native function 'printf' (libc.so.6) is variadic; calls of native variadic functions
  are not supported for MSIL yet (phase 3)". On UCRT this never fires for stdio (section 1.3).
- aggregate, vector, i128, half: "native function 'sum' (libbig.so.1) takes or returns a value that
  a P/Invoke cannot pass natively yet (phase 3)".
- prototype-less, address-only: "the signature of prototype-less function 'f' (libfoo.so.1) cannot
  be derived: ..." (the M9 text, now at link time; resolved silently when another object defines
  `f`).

What phase 3 can do (feasibility verified in section 3): an `sret` result on x86-64 is
ABI-compatible as a plain first pointer parameter on both Win64 and SysV, so x86-64 could accept
`sret` imports already (AArch64 passes it in X8, so it cannot; *unverified* on hardware); a `byval`
aggregate needs a small linker-generated IL bridge (`ldarg p; ldobj S; call pinvoke(valuetype S)`)
with an explicit-layout value type; a native variadic callee needs a bridge from the caller's own
argument buffer to the v-variant (Design.md 4.7). Because the call site cannot know at compile time
whether its callee is native, **P3-B's variadic lowering becomes a link-time decision** (the caller
uses the own convention and msil-ld bridges native callees), which P2-A's object format should not
preclude.

## 7. The sysroot problem, `-l` lookup and the driver

### 7.1 What msil-ld needs on disk

| Option | Content | Verdict |
|---|---|---|
| Real sysroot files | ELF `.so` (+ `libc.so`/`libm.so` linker scripts, `libc_nonshared.a`), Windows `.lib` import libraries | **Recommended.** Same files ld.lld and lld-link read; versions, IFUNC and kinds are exact. glibc x86_64 subset used here: 4.5 MB (`sysroot-glibc`, from WSL + `apt-get download libc6-dev`) |
| `llvm-ifs` text stubs (`.ifs`) | SONAME, symbols with type/size/weak, no versions | Not for glibc: llvm-ifs fails on libc.so.6 ("Size Mismatch for _sys_siglist", 520 vs 512, also with `--strip-size`) and libm.so.6 ("Type Mismatch for log2f", Func vs IFUNC/Unknown). Works for unversioned libraries (`libz.so.1.ifs`: 5.4 KB, 102 symbols; `--output-elf` stub: 4.9 KB, readable by the resolver). Needs upstream versioning support, or a filtered writer (default versions only, IFUNC as Func) |
| Stub `.so` files | ELF with only dynsym/versym/verdef/dynamic | Works (yaml2obj-built 1.3 KB stub, section 9.1), but nobody ships glibc stubs; generating them needs a tool (Zig generates glibc stubs from its `abilists` database, from memory, *unverified*) |
| Windows: import libraries | Windows SDK `ucrt.lib`, `kernel32.lib`; or generated from `.def` files with `llvm-lib /def:` / `llvm-dlltool` (mingw-w64's free `.def` files describe the same DLLs) | Recommended; this is Windows' stub format already |
| Windows: DLLs | `System32` export tables | Works on Windows hosts (2.4), but not for cross-builds; use as a fallback only |

### 7.2 `-l` lookup and archive classification

lld/ELF searches `lib<name>.so` then `lib<name>.a` per directory (`lld/ELF/DriverUtils.cpp:230-241`);
lld's MinGW driver searches `lib<name>.dll.a`, `<name>.dll.a`, `lib<name>.a`, `<name>.lib`,
`lib<name>.dll`, `<name>.dll` (`lld/MinGW/Driver.cpp:112-143`). Design.md 5.2 orders `libfoo.a`
(static) before a managed `foo.dll` before native libraries. In a real sysroot `libc.a`, `libm.a`
(glibc) and `msvcrt.lib`, `libcmt.lib` (MSVC) are native machine code, so the lookup must classify:
an archive counts as static only if it has CIL-object or bitcode members; a native-only archive is
skipped (noted in `--verbose`), and an archive with short or long import members is an import
library. Within a found native input, symbols defined only by native objects produce the "native
code only" diagnostic. `-Bstatic` then means "CIL archives only".

### 7.3 Default libraries per triple and how the driver passes them

| Triple | Default inputs (proposal) | Notes |
|---|---|---|
| `*-linux-gnu-msil` | `-lc -lm` | `-lm` always: the legalizer turns `llvm.sqrt`, `floor`, `fma`, `fmin`, ... into libm calls that native code would inline (D14's reason for including the math library). There is no DT_NEEDED, so an unused `-lm` costs nothing |
| `*-linux-musl-msil` | `-lc` | musl's libm is inside libc.so (*unverified* here: no Alpine) |
| `*-windows-msvc-msil` | `ucrt.lib`, `oldnames.lib`, `kernel32.lib`, plus the vcruntime C subset (11 `mem*`/`str*`/`wcs*` functions, all exported by ucrtbase.dll): either `vcruntime.lib` (`VCRUNTIME140.dll`, native fidelity, needs the VC++ redistributable) or a toolchain-generated import library binding them to `ucrtbase.dll` (user decision) | never `msvcrt.lib`, `libcmt.lib`, `legacy_stdio_definitions.lib` (native code) |
| `*-windows-gnu-msil` | `libucrt.a` (or `libmsvcrt.a`), `libkernel32.a` | needs the long-format reader (2.5); prefer the UCRT flavor (msvcrt flavor's `__acrt_iob_func` is native code) |

The driver (P2-B's `MSILToolChain<NativeTC>`) reuses the native toolchain's library paths:
`Linux::Linux` (`clang/lib/Driver/ToolChains/Linux.cpp:225`, multiarch paths under the sysroot at
346-347) and `MSVCToolChain::getSubDirectoryPath(SubDirectoryType::Lib)`,
`getUniversalCRTLibraryPath`, `getWindowsSDKLibraryPath` (`MSVC.h:92-124`), and passes
`--sysroot=` so linker scripts' absolute paths resolve inside it (GNU ld rule for scripts found in
the sysroot). `-nostdlib`/`-nodefaultlibs` drop the defaults. `#pragma comment(lib, ...)` reaches
the IR as `!llvm.linker.options`: `!{!"-lws2_32"}` on `x86_64-unknown-linux-gnu-msil` (clang's
`AddDependentLib` emits `llvm.dependent-libraries` only for `isOSBinFormatELF()`,
`clang/lib/CodeGen/CodeGenModule.cpp:3984-3995`) and `!{!"/DEFAULTLIB:ws2_32.lib"}` on the Windows
MSIL triples. The MSIL backend ignores both today; P2-A objects should carry them and msil-ld
should accept both spellings.

## 8. Resolver design inside msil-ld

### 8.1 Files and data structures

- `lld/MSIL/NativeLibraries.h/.cpp` (new): readers and the index. Depends on `LLVMObject` (and
  `LLVMInterfaceStub` only if IFS input is adopted; `LLVMTextAPI` for Darwin later).

```cpp
enum class NativeKind : uint8_t { Function, Data, TLS, NoType };
enum class NotBindable : uint8_t {
  None, HiddenVersion, ThreadLocal, PrivateVersion, NativeCodeOnly, DataImport };
struct NativeExport {
  StringRef Module;       // ModuleRef string: DT_SONAME / DLL file name / install name
  StringRef Entry;        // ImplMap import name; "#N" for ordinal-only imports
  NativeKind Kind;        // IFUNC -> Function; COFF IMPORT_DATA/CONST -> Data
  NotBindable Why;
  StringRef Version;      // ELF default version, for --print-imports and diagnostics
  InputFile *File;        // for "defined in" lines
};
class NativeIndex {       // first bindable definition per name, in link order
  DenseMap<CachedHashStringRef, SmallVector<NativeExport, 1>> Defs;
  DenseMap<CachedHashStringRef, StringRef> Aliases;   // COFF weak-external aliases
public:
  void addFile(MemoryBufferRef, StringRef Path, const Triple &);  // dispatch by identify_magic
  const NativeExport *lookup(StringRef Name, SmallVectorImpl<const NativeExport *> &Rejected);
};
```

- Readers, one function each (all exercised by the prototype): `readELFShared` (per ELFT;
  machine/class check against the triple), `readLinkerScript` (GROUP/INPUT/AS_NEEDED/`-l`,
  sysroot), `readCOFFShortImport` (machine check; ARM64EC/ARM64X members for aarch64 later),
  `readGNULongImport` (new, 2.5), `readCOFFAliasObject` (weak externals with search-alias),
  `readPEDll`, `readIFS` (optional). Native objects inside archives only feed `NativeCodeOnly`.
- `lld/MSIL/Symbols.h`: a `NativeFunctionSymbol` and a `NativeDataSymbol` kind next to
  defined/lazy/undefined/managed (the wasm-ld shape, where `UndefinedFunction` carries
  `importName`/`importModule`, `lld/wasm/Symbols.h:194-255`).

### 8.2 Order of resolution

1. Objects and archive members (CIL or bitcode), including the phase-3 helper archive, so
   `atexit`, `setjmp`, `__divti3` resolve to toolchain helpers before any native library.
2. Managed assemblies built by this toolchain (P2-C).
3. Native libraries, in command-line order; the first *bindable* definition wins (shared-library
   semantics: no duplicate-definition error between libraries). Unbindable definitions are kept
   for the diagnostic.
4. Weak undefined with no definition anywhere: address 0, no P/Invoke (`if (&f) f();`).
5. Anything left: lld's "undefined symbol: foo" with ">>> referenced by a.o".

Interposition does not reach native code: an object's own `malloc` replaces the P/Invoke for our
code only, while native `strdup` still allocates with libc's `malloc` (document it).

### 8.3 What is emitted for a native function

- One `MethodDef` per (symbol, IL signature): `static`, `pinvokeimpl` (MethodAttributes 0x2000),
  `hidebysig`, access `assembly` (Phase 1 uses `public`; imports are not part of the export class),
  MethodImpl `preservesig` (0x0080), no body.
- One `ImplMap` row: `MappingFlags = ExactSpelling (0x0001) | CallingConventionCDecl (0x0200)`
  (values from `System.Reflection.MethodImportAttributes`; `stdcall` 0x0300 for i686 later),
  `ImportName = Entry`, `ImportScope` = a `ModuleRef` per distinct module string.
- One canonical stub per symbol for its `'<msil>fn_<name>'` address field (declared prototype if
  any, else the first signature in input order).
- Explicit `msil-import-module`/`msil-import-name` symbols skip the native lookup (step 3) and use
  the given strings; two objects that disagree are an error, as in wasm-ld
  (`lld/wasm/SymbolTable.cpp:715-745`, "import module mismatch for symbol").

### 8.4 What is emitted for native data

The address field of the variable (already referenced by the code) plus a `.cctor` call of the
private `'<msil>dataimport'` helper (5.2), placed in its own initializer function called from
`<Module>::.cctor` before relocations and constructors, so FeatureIdeas idea 7 can later run it per
variant.

### 8.5 Diagnostics (proposed texts, lld style)

- `undefined symbol: foo` / `>>> referenced by a.o`
- `symbol 'atexit' is defined only in native object code (libc_nonshared.a(atexit.oS)); MSIL cannot link machine code`
- `symbol 'oldf' in libfoo.so.1 has only the non-default version oldf@V1, which a P/Invoke cannot bind`
- `symbol 'errno' in libc.so.6 is thread-local data and cannot be imported; use <errno.h>`
- `symbol '_dl_argv' in ld-linux-x86-64.so.2 is GLIBC_PRIVATE`
- `'stdout' in libc.so.6 is data, but a.o calls it as a function`
- `data symbol 'stdout' in libc.so.6 cannot be imported: no reference assembly defines System.Runtime.InteropServices.NativeLibrary` (only if 5.2 is implemented)
- the variadic, aggregate and prototype-less texts of section 6
- `libfoo.so: ELF machine EM_AARCH64 does not match target x86_64-unknown-linux-gnu-msil`
- `--verbose`: `skipping native static archive libc.a (no CIL or bitcode members)`

### 8.6 Reporting options

`--print-imports` (or a `-Map` section): `symbol -> module!entry [kind, version]`, per-library
version floor; `--trace-symbol=` like lld. Useful for tests (section 9) without disassembling.

## 9. Tests

### 9.1 lit tests for msil-ld (lld/test/MSIL/native-*.test)

lld's test dependencies already include `yaml2obj`, `obj2yaml`, `llvm-lib`, `llvm-dlltool`,
`llvm-ar`, `llvm-mc`, `llvm-readobj` (`lld/test/CMakeLists.txt`, `LLD_TEST_DEPS`); `llvm-ifs` is not
in the list. Inputs, all checked with the prototype:

- ELF `.so` from yaml2obj, no program headers, no code:
  [elf-libfoo-min.yaml](native-imports-files/elf-libfoo-min.yaml) (1,328 bytes once assembled):
  DT_SONAME, FUNC, OBJECT, IFUNC, TLS, `newf@@V2` + hidden `newf@V1`, hidden-only `oldf@V1`.
  Two yaml2obj details: a duplicate dynamic symbol name needs the unique suffix `'newf (1)'`
  (otherwise "repeated symbol name"), and DT_SONAME takes a numeric `.dynstr` offset (0x24 here,
  computed from yaml2obj's string table; pin it in the test with `llvm-readobj --dynamic-table`).
  `obj2yaml` of an ld.lld-built `.so` does not round-trip after editing (segment offset errors), so
  write these by hand.
- Versioned `.so` the lld way: [elf-libfoo-versioned.c](native-imports-files/elf-libfoo-versioned.c)
  + [.map](native-imports-files/elf-libfoo-versioned.map) with `.symver`, linked by `ld.lld -shared
  -soname libfoo.so.1 --version-script` (in lit: `llvm-mc` assembly, `REQUIRES: x86`).
- COFF import library from a `.def`: `llvm-lib /machine:x64 /def:` with
  [coff-import.def](native-imports-files/coff-import.def) (name, NONAME ordinal, DATA, EXPORTAS).
- oldnames-style alias object: [coff-weak-alias.yaml](native-imports-files/coff-weak-alias.yaml)
  (yaml2obj; `IMAGE_FILE_MACHINE_UNKNOWN` objects are rejected by `llvm-lib` ("unknown machine:
  0"), so archive them with `llvm-ar rcs`).
- PE DLL: `lld-link -dll -noentry` of a COFF object (yaml2obj or `llvm-mc`) with `/export:` (data
  and code); verified here with a clang-compiled object (section 2.4).
- GNU long-format member: yaml2obj COFF with `.idata$4/5/6/7` sections and the head/tail members
  (template: `obj2yaml` of the mingw-w64 members of 2.5).
- Linker scripts: a text `libc.so` with GROUP/AS_NEEDED and `--sysroot`, plus a native
  `libc_nonshared.a` (yaml2obj ELF REL object archived with `llvm-ar`).

Test list: `native-elf-resolve` (each kind and version case, ModuleRef = SONAME, `nomangle`),
`native-elf-nosoname` (module = file name), `native-elf-script` (GROUP, AS_NEEDED, sysroot),
`native-static-only` (atexit diagnostic), `native-search` (`-L`/`-l`, native `.a` skipped,
`-Bstatic`), `native-machine-mismatch`, `native-coff-importlib` (ordinal `#5`, EXPORTAS, DATA),
`native-coff-alias` (`strdup` -> `_strdup`), `native-coff-gnu-importlib`, `native-pe-dll`,
`native-order` (object beats native, first library wins, managed beats native),
`native-import-attr` (explicit module/name, mismatch error), `native-signatures` (two K&R
signatures -> two MethodDefs, one address field), `native-variadic`, `native-aggregate`,
`native-data` (accessor or diagnostic; data called as a function), `native-weak-undef`,
`native-linker-options` (`-lfoo` and `/DEFAULTLIB:foo.lib` from objects), `native-print-imports`.

### 9.2 Unit tests

The readers and `NativeIndex` can be unit-tested with in-memory inputs from
`llvm::yaml::yaml2ObjectFile` (`llvm/ObjectYAML/yaml2obj.h`), as `llvm/unittests/Object` does.

### 9.3 Execution tests

`msil/test`: link real programs against the host's libraries (Windows SDK `ucrt.lib`/`kernel32.lib`
on Windows; on the WSL leg, the system's `/lib/x86_64-linux-gnu` or a checked-in yaml2obj test
sysroot), and run them: puts/printf-free programs first, then a glibc `fputs(..., stderr)` program
for the data accessor, an ordinal import (`htons` via `ws2_32.lib`), an oldnames alias (`strdup`),
an IFUNC (`strlen`), and the version case (`exp`, `sqrtf`).

## 10. Migrating the Phase-1 tests and code that rely on D14

### 10.1 Inventory (this checkout)

| Files | Count | Dependency |
|---|---|---|
| `llvm/test/CodeGen/MSIL/*.ll` with `pinvokeimpl` | 30 | the D14 library strings in CHECK lines (`pinvoke-libs.ll`, `pinvoke.ll`, `libcall-policy.ll`, `fp-expand.ll`, `triple-parity.ll`, `globals.ll`, `ldftn-calli.ll`, ...) |
| tests with "Phase-1 import" diagnostics | 17 (11 `.ll`, `msil_test.h`, `lit.cfg.py`, 2 M1 C files, `M1/lit.local.cfg`, `README.md`) | the D14 error texts |
| tests using `msil-import-module` | 10 `.ll` | the attribute |
| `clang/test/CodeGen/MSIL/` | 3 (`asm-barrier.c`, `design-milestone1.c`, `no-proto-address.c`) | P/Invoke text, the M9 address-only error |
| `msil/test/harness` | 3 `.test` + 6 hand-written `.il` | hand-written `pinvokeimpl("ucrtbase")`/`("libc")`: independent of the backend |
| `msil/test/M1` programs | 135 `.c`/`.ll` | `puts`/`putchar`/math through `msil_test.h`, compiled with `clang -S` and assembled by ilasm |
| `llvm/unittests/Target/MSIL/MSILTargetStreamerTest.cpp` | 1 | `emitPInvoke` text only |

### 10.2 Plan

1. **Keep D14 as the "standalone module" import policy** of `llc` and `clang -S` (a module that is
   assembled by ilasm without a linker), frozen as it is: every test above keeps passing unchanged,
   and the ilasm round-trip oracle (Design.md 5.5) keeps working. Rename it in the code and docs
   ("standalone import table"), state that it is not used for objects, and have the object path
   (P2-A) emit undefined symbols instead. Whether `-S` in Phase 2 prints the standalone module or
   the object's text is P2-A's decision; this recon only needs the standalone mode to remain
   available (for example by default for `-S`, or behind an option).
2. The linked path gets its own tests (section 9) with the resolver's names (`libc.so.6`,
   `api-ms-win-crt-*.dll` or `ucrtbase.dll`, `nomangle`).
3. **`MSILISelLowering.cpp:225-241` must stop consulting the table**: base the float32
   `ldexp`/`frexp` promotion on `RuntimeLibcalls` (the MSVC triples already lack `ldexpf`/`frexpf`
   there) plus a small per-triple "the C runtime DLL lacks the float variant" list for windows-gnu
   (msvcrt.dll lacks them, RuntimeLibcalls says MinGW has them), or the object path's codegen would
   differ from the standalone path's.
4. `MSILISelLoweringCall.cpp:196-255` in object mode: let every non-`__` library call through as
   an undefined symbol (resolution and "undefined symbol" move to msil-ld; `exp10`, `roundeven`
   then resolve on glibc 2.39+/2.25+ and fail to link on UCRT); keep the `__` helper error until
   the phase-3 helper archive exists.
5. `MSILAsmPrinter::emitPInvokeDeclarations` and `hasNativeSignature`: standalone mode only; object
   mode records the signature flags of section 6 instead.
6. The M1 execution suite switches from `clang -S` + ilasm to `clang -c` + msil-ld when P2-B lands
   (one harness change in `msil/test/lit.cfg.py`'s `c_pipeline`/`ll_pipeline`), linking against the
   host libraries; expected outputs do not change. Keep one `-S` leg for the ilasm oracle.
7. Status.md's D14 section and Design.md 5.3 change with the naming rule (section 12).

## 11. FeatureIdeas interactions (not planned, only checked for preclusion)

- Idea 6 (enclosed model) needs "a closed-world check: every external symbol must resolve inside
  the enclosure or to an allowed host import". Keeping native resolution a separate final step with
  its own result kind (8.2) makes that a policy switch (`--no-native-imports` or an allow-list).
  Nothing here precludes it.
- Idea 7 (several triples in one assembly) needs "its own P/Invoke table" and "its own initializer,
  run on first use of that variant". P/Invokes are lazy, so per-variant MethodDefs are fine; the
  data-import accessor must not be hard-wired into the one eager `<Module>::.cctor` (it would load
  msvcrt.dll on Linux), hence 8.4's separate initializer function. SONAME/DLL names per triple fit
  per-variant tables.

## 12. Proposed Design.md updates (for the user; Design.md is user-owned)

- 5.3, second bullet, replace with: "Each `DllImport` names the library as a native link records
  it: the DT_SONAME of an ELF library (or its file name when it has none), the DLL file name from
  the import library or DLL, with its `.dll` suffix, and the install name of a Mach-O library.
  Bare names such as `"z"` load only where a development symlink `libz.so` exists, and .NET
  Framework does not append `.dll` to a dotted name. Entry points are exact (`ExactSpelling`)."
- 5.2 step 3: add "Only default-version ELF symbols can be bound (a P/Invoke resolves through
  `dlsym`); symbols that exist only in native static code (glibc's `libc_nonshared.a`, MSVC's
  `msvcrt.lib`) are link errors unless the toolchain provides them (`atexit`, 4.1). Native data
  (glibc `stdout`, `environ`) is imported through the global's address field, filled at startup."
- 5.2 `-lfoo` lookup: "`libfoo.a` counts only if it holds CIL or bitcode members; native archives
  are skipped."
- 4.7: note that whether a variadic callee is native is known only at link time.

## 13. Decisions for the user (with recommendations)

1. **vcruntime subset on windows-msvc**: (a) `vcruntime.lib` -> `VCRUNTIME140.dll` (like native
   MSVC; needs the VC++ redistributable on the target machine) or (b) a toolchain import library
   binding the 11 C functions to `ucrtbase.dll` (no extra dependency; verified exported).
   Recommend (b).
2. **UCRT module names**: (a) the API-set DLLs from ucrt.lib (link.exe fidelity; 14 possible
   ModuleRefs) or (b) one `ucrtbase.dll` (Phase-1 continuity). Both load on Framework 4.8 and .NET
   8/10. Recommend (a).
3. **Data imports in Phase 2**: accessor (5.2) or diagnostic until phase 3. Recommend the accessor.
4. **glibc library names**: `libc.so.6` (SONAME, uniform) or `libc` (Phase-1 spelling, CoreCLR
   special case). Recommend `libc.so.6`.
5. **D14 after Phase 2**: keep as the frozen standalone (`-S`/`llc`) policy or remove when the
   harness moves to msil-ld. Recommend keep.
6. **Text stubs for cross sysroots**: none in Phase 2 (real sysroot files), or extend IFS with
   versions (upstream work). Recommend none in Phase 2.

## 14. Experiments, installs and costs

Experiments (all in the scratch directory; logs in the supporting files):
1. `llvm-readobj`/`llvm-nm` (VS LLVM 22.1.3) on ucrt.lib, kernel32.lib, ws2_32.lib, user32.lib,
   vcruntime.lib, oldnames.lib, msvcrt.lib, legacy_stdio_definitions.lib, ucrtbase.dll: section 2.3.
2. `nisym` on glibc, the glibc sysroot scripts, the Windows libraries, System32 DLLs, yaml2obj and
   `.def` inputs, mingw-w64 libraries: sections 2.2-2.6, 7, 9.
3. DllImport probes: 24 cases on Framework 4.8, .NET 8, .NET 10 (Windows); 25 cases on WSL .NET 8
   and 10, plus the `errno`-as-function crash: section 3.
4. Dotted DLL name and `nomangle` probes on the three Windows runtimes: section 3.1.
5. Data-import accessors: NativeLibrary (.NET 8/10 Windows and Linux) and kernel32 (Framework,
   .NET 8): section 5.2.
6. ABI: Win64 `lldiv` (sret) on three runtimes; SysV `mk` (sret) and `sum` (byval value type) on
   WSL .NET 8/10: sections 3, 6.
7. `llvm-ifs` on glibc (fails) and libz (works): section 7.1.
8. clang at HEAD on real UCRT and glibc headers: section 1.3.
9. Side observation: two assemblies with the same simple name in one directory made `dotnet exec`
   run the other one's code (the host adds the app directory's assemblies to the TPA without a
   deps.json), as D28's private-directory rule anticipates.

Installs: none system-wide; no dotnet tools, NuGet or pip packages. Downloaded with `apt-get
download` (no root) and unpacked with `dpkg-deb -x` into the scratch directory: `libc6-dev
2.43-2ubuntu2.4`, `linux-libc-dev` (headers), `mingw-w64-x86-64-dev 13.0.0-2ubuntu1`. Built in
scratch against the shared build's libraries: `yaml2obj`, `obj2yaml`, `llvm-ifs` (with a local
`LLVMInterfaceStub`) and `nisym`: about 20 translation units plus 3 TableGen runs, under 3 minutes;
no cmake or ninja. Scratch use: 210 MB on C: (147 MB is the unpacked mingw-w64 package). D: free
space stayed at 19 GB.
