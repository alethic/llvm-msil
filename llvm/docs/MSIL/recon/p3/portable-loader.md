# P-0 recon: loader envelope for the portable triple (key `loader`)

Read-only recon for P-1a. Repository the repository, branch `msil`, HEAD `c3460446ac09` (M25). Nothing in
the tree was changed; all probes, images, scripts and logs are under `<scratch>/p0/loader/`
(this run's reproducible set is `v2/`; the cut-off earlier attempt's `out*/`, `logs/` and `hosts/` were
reused only for sources, and every result below was re-measured in `v2/` except where noted).

## 0. Summary

1. **An IL-only PE32 image with machine I386 and only `COMIMAGE_FLAGS_ILONLY` (Roslyn's AnyCPU), written
   with lld/MSIL's current envelope (no mscoree import, no IAT, no `_CorExeMain` stub,
   `AddressOfEntryPoint` 0, no `.reloc`), loads and runs as a 64-bit process on every 64-bit host
   measured:** Windows .NET 8.0.33 and 10.0.14 (`dotnet exec`), .NET Framework 4.8 started directly as
   `x.exe` and through `Assembly.LoadFrom` in a 64-bit Framework process, and WSL Ubuntu .NET 8.0.31 and
   10.0.13. The same image also loads into every 32-bit process measured (x86 `dotnet`, a 32-bit Framework
   host, a direct start of a `32BITREQUIRED` copy). **Roslyn's mscoree stub and `.reloc` are not needed on
   any runtime** (the stub variant behaves identically). So P-1a does not grow: the writer only switches
   the optional header to PE32 and the machine to I386.
2. **The AnyCPU image runs silently in a 32-bit process, with wrong results.** A real `msil-ld` output
   (C `long long` arithmetic, emitted as `native int` by decision D2) returns 48 in a 64-bit process and
   **0** in a 32-bit one (.NET 8/10 x86 and Framework 4.8 x86), with no diagnostic. The width guard is
   therefore mandatory, not cosmetic.
3. **The guard must run in `<Module>::.cctor`, not in `'<msil>Main'`.** A library loaded by a host never
   runs `'<msil>Main'`; measured: with the guard only in `'<msil>Main'`, the DLL ran unguarded in a 32-bit
   process on Framework 4.8 and .NET 10. The module initializer ran before every other member of the
   module on all three runtimes (before a type with its own `.cctor`, before a type that touches no
   global), and a failure is sticky: every later access to any type of the module throws
   `TypeInitializationException` again.
4. **The guard should `throw`, not `Environment.FailFast`.** On .NET Framework 4.8, `FailFast` prints
   nothing to the console (64-bit and 32-bit alike): the process ends with exit code `0x80131623` and the
   message goes only to the Windows Application event log (event 1025). A thrown
   `PlatformNotSupportedException` is printed by all runtimes for an EXE ("Unhandled Exception" on
   Framework, "Unhandled exception." on .NET), and a host that loads the library gets a catchable
   `TypeInitializationException` whose inner exception carries the message. `Environment.Exit` prints
   cleanly but kills a host process that only loaded a library.
5. **The guard needs no BCL member except the exception's constructor.** The width test is pure IL
   (`ldc.i4.m1; conv.i; conv.u8; ldc.i8 -1; bne.un`), measured correct on 32- and 64-bit; the byte-order
   test can be pure IL too (an `int32` local read through `ldind.u1`), at the price of one ILVerify
   `StackUnexpected` (the trailer's `.cctor` is unverifiable anyway: `cpblk`).
6. **Keep the current stack reserve (4 MB) in the PE32 header.** On a direct Framework start the OS loader
   takes the main thread's stack from the image: Roslyn's AnyCPU default 1 MB gives the 64-bit process a
   1 MB main stack (measured `0x100000`), 4 MB in a PE32 header gives 4 MB (`0x400000`), in 64-bit and in
   32-bit processes. Under `dotnet` the host's own stack applies (1.5 MB measured).

## 1. What lld/MSIL writes today (file:function citations)

The image is written by `llvm/lib/CLI/PEWriter.cpp : cli::writePEImage`, called from
`lld/MSIL/Writer.cpp : Writer::writeImage` (EXE/DLL), `llvm/lib/Target/MSIL/MCTargetDesc/MSILObjectWriter.cpp`
(the CIL object, a netmodule with the COFF symbol view) and `llvm/tools/llvm-cil-copy/llvm-cil-copy.cpp`.

| Item | Where | Today |
|---|---|---|
| Machine per triple | `PEWriter.cpp : PEOptions::get` | `x86_64` -> AMD64, `aarch64` -> ARM64; every other arch is the error "architecture '...' is not supported until phase 5" (comment: "PE32 images (i686) need the mscoree import, the entry stub and .reloc") |
| Machine check | `PEWriter.cpp : checkContent` | anything but AMD64/ARM64 is "machine 0x.... is not supported until phase 5 (only PE32+ images are written)" |
| Image base | `PEOptions::get` | `0x140000000` EXE, `0x180000000` library and module |
| Header layout | `PEWriter.cpp` file-scope constants | PE32+ only: optional header 112 + 16x8 = 240 bytes, `DataDirectoriesOffset` = opt + 112, `static_assert(SectionTableOffset == 0x188)` |
| COFF Characteristics | `writePEImage` | `IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE` (0x0022), plus `IMAGE_FILE_DLL` for a library or module (0x2022); never `32BIT_MACHINE`, never `RELOCS_STRIPPED` |
| Optional header | `writePEImage` | magic `PE32_PLUS`, linker 14.0, `AddressOfEntryPoint` 0 ("no entry stub"), OS/subsystem 4.0, `DllCharacteristics` 0x8540, stack 0x400000/0x4000, heap 0x100000/0x2000, alignments 0x2000/0x200 (`PEOptions` defaults, `llvm/include/llvm/CLI/PEWriter.h`) |
| Directories | `writePEImage` | only CLR (14) and, with debug entries, Debug (6): no import, IAT or base relocations |
| CorFlags | `PEOptions::CorFlags` | `COMIMAGE_FLAGS_ILONLY` |
| mscoree | nowhere | no `mscoree.dll` import and no `_CorExeMain`/`_CorDllMain` stub anywhere in `lld/MSIL` or `llvm/lib/CLI` |
| Copy tool | `llvm-cil-copy.cpp` (machine block before `PEContent C`) | AMD64/ARM64 inputs take `PEOptions::get` defaults; any other machine is passed on and refused by `checkContent` |
| Tests that pin it | `llvm/unittests/CLI/PEWriterTest.cpp` (`UnsupportedArchitecture`: riscv64, i686, armv7, wasm32 refused; `SizeOfOptionalHeader == 0xF0`; AMD64/ARM64 header tests), 35 lit/test files that match `machine=0x8664` (host triples; unaffected) |

So the current envelope is already Roslyn's x64 envelope; the only difference from Roslyn's AnyCPU envelope
is the PE32 optional header, the machine, the image base, and Roslyn's (unneeded, see 3) stub/IAT/`.reloc`.

## 2. How Roslyn emits AnyCPU (measured)

Built with `dotnet <sdk 10.0.403>/Roslyn/bincore/csc.dll -noconfig -nostdlib -r:<Framework64 v4>/mscorlib.dll
-deterministic -platform:<p>` (`v2/build-v2.sh`), parsed with `src/pe.py dump` and `src/opthdr.py`
(`v2/dump.txt`):

| `-platform:` | Format | Machine | COFF chars | CorFlags | Entry stub / import | `.reloc` | ImageBase | Stack (reserve/commit) |
|---|---|---|---|---|---|---|---|---|
| `anycpu` exe | PE32 | 0x014C | 0x0022 | 0x1 ILONLY | `FF 25 <abs IAT VA>` at RVA 0x2522, `mscoree.dll!_CorExeMain` | one block `00200000 0c000000 2435 0000` (HIGHLOW at 0x2524 = the stub's operand) | 0x400000 | 0x100000/0x1000 |
| `anycpu` dll | PE32 | 0x014C | 0x2022 | 0x1 | same with `_CorDllMain` | same | 0x10000000 | 0x100000/0x1000 |
| `anycpu32bitpreferred` | PE32 | 0x014C | 0x0022 | 0x20003 (ILONLY, 32BITREQUIRED, 32BITPREFERRED) | yes | yes | 0x400000 | 1 MB |
| `x86` | PE32 | 0x014C | 0x0102 (+32BIT_MACHINE) | 0x3 | yes | yes | 0x400000 | 1 MB |
| `x64` | PE32+ | 0x8664 | 0x0022 | 0x1 | **none**, entry 0 | **none** | 0x140000000 | 0x400000/0x4000 |

Common to all: linker 48.0, alignments 0x2000/0x200, OS/subsystem 4.0, `DllCharacteristics` 0x8540
(0x8560 with `-highentropyva+`, even on PE32), heap 0x100000/0x1000 on PE32, one Debug entry of type 16
(Reproducible, `-deterministic`), a `.rsrc` version resource, `BaseOfData` = the `.rsrc` RVA (0x4000).
ilasm (`ilasm -exe`/`-dll`, PE32 by default) writes the same stub and `.reloc` but sets `32BIT_MACHINE`
(chars 0x0102) on an AnyCPU image; `ilasm -pe64 -arm64` writes no stub at all.

So "AnyCPU" is: PE32 + I386 + `ILONLY` only, without `32BITREQUIRED`/`32BITPREFERRED`. The stub and
`.reloc` are Roslyn's PE32 habit (it drops them for x64/ARM64), not a loader requirement (section 3).

## 3. Measurements: which images load and run

### 3.1 Images (`v2/build-v2.sh`; probe source `src/probe.il`)

`probe.il` is P/Invoke-free and references only `mscorlib` (the facade on .NET). Its `<Module>::.cctor`
prints a line and runs `'<msil>WidthGuard'` (env `MSIL_GUARD_MODE` = `failfast` (default) | `throw` |
`exit`; `MSIL_GUARD_SITE=main` moves the guard into `'<msil>Main'`; `MSIL_GUARD_FORCE=1` makes it fail on a
64-bit process too). `'<msil>Main'` prints `IntPtr.Size`, `Environment.Is64BitProcess`,
`BitConverter.IsLittleEndian`, `Environment.Version` and returns 42. The DLL forms have no `.entrypoint`
(`v2/probe-lib.il`) and expose `Probe.Run()` and `Probe2.Pure()` (touches no global).

| Image | How | Header (from `v2/dump.txt`) |
|---|---|---|
| `pe32-stub.{exe,dll}` | `ilasm -exe/-dll` | PE32 I386, chars 0x0102/0x2102, ILONLY, mscoree stub + IAT + `.reloc` |
| `pe32-ilasmnostub.{exe,dll}` | `ilasm -stripreloc` | PE32 I386, stub + IAT but **no `.reloc`**, chars 0x0103/0x2103 (`RELOCS_STRIPPED`) |
| `pe32-stripped.{exe,dll}` | `pe.py strip` of `pe32-stub` | PE32, chars 0x0102/0x2102, no stub, no IAT, no `.reloc`, entry 0 |
| `pe32-pref32.exe` | `ilasm -32bitpreferred` | CorFlags 0x20003, stub, `.reloc` |
| `pe64-ilasm.{exe,dll}` | `ilasm -pe64 -x64` | PE32+ AMD64 with ilasm's x64 stub and `.reloc` |
| `pe64-stripped.exe` | `pe.py strip` | PE32+ AMD64 without stub/`.reloc` |
| **`pe64-writer.{exe,dll}`** | `llvm-cil-copy` of `pe64-ilasm` | **today's lld/MSIL envelope** (`writePEImage`): PE32+ AMD64, chars 0x0022/0x2022, base 0x140000000/0x180000000 |
| **`pe32-writer.{exe,dll}`** | `pe.py topE32` of `pe64-writer` | **proposed P-1a envelope**: the same bytes with a 224-byte PE32 optional header (section table at 0x178), I386, chars 0x0022/0x2022, base 0x400000/0x10000000, stack 0x100000/0x1000, `BaseOfData` 0, ILONLY, no stub/IAT/`.reloc`, entry 0 |
| `pe32-writer-32bm.exe` | `pe.py setflags` | + COFF `32BIT_MACHINE` (0x0122) |
| `pe32-writer-req32.exe` | `pe.py setflags` | CorFlags 0x3 (32BITREQUIRED) |
| `pe32-writer-pref32.exe` | `pe.py setflags` | CorFlags 0x20003 |
| `pe64-writer-arm64.exe`, `pe64-ilasm-arm64.exe` | `setflags` / `ilasm -arm64` | PE32+ ARM64 |
| `pe64-writer-i386.exe` | `setflags` | PE32+ with machine I386 (inconsistent; control) |
| `real/{pe64,pe32,req32}/{ptr,union,wide}.exe` | `clang --target=x86_64-pc-windows-msvc-msil -O1 -ffreestanding -nostdlib` (msil-ld), then `topE32`, then `setflags 3` | real msil-ld outputs: `ptr.c` (pointer table relocated by the trailer), `union.c` (pointer store into a union), `wide.c` (`long long` multiply and shift) |

### 3.2 Hosts (`v2/run2.ps1`, `v2/wsl-run2.sh`)

| Host | Command | Bitness |
|---|---|---|
| net8-x64 / net10-x64 | `"C:\Program Files\dotnet\dotnet.exe" exec --runtimeconfig src/net{8,10}.runtimeconfig.json x` | 64 |
| net8-x86 / net10-x86 | `"C:\Program Files (x86)\dotnet\dotnet.exe" exec --runtimeconfig ... x` | 32 |
| fx48-direct | `x.exe` (Windows OS loader + mscoree shim, Framework 4.8, `4.0.30319.42000`) | 64 for AnyCPU, 32 for `32BITREQUIRED` |
| fx48-ldr64 / fx48-ldr32 | `v2/hosts/loader-{anycpu,x86}.exe x` (Framework host, `Assembly.LoadFrom`, calls `Probe2.Pure` then `Probe.Run`) | 64 / 32 |
| net10-ldr64 / net10-ldr32 | the same loader under x64 / x86 `dotnet exec` | 64 / 32 |
| wsl-net8 / wsl-net10 | WSL Ubuntu (kernel 6.18.33.2, x86_64), `/usr/bin/dotnet exec` (apt's 8.0.31 and 10.0.13; `$HOME/.dotnet-msil` was only listed, not used) | 64 |

### 3.3 Results (exit codes; `v2/matrix.log`, `v2/wsl.log`)

42 = loaded and ran to the end (64-bit process; the probe printed `IntPtr.Size=8 Is64BitProcess=True
IsLittleEndian=True` on every such run). `0x80131623` = loaded and ran in a 32-bit process, where the
guard's `FailFast` fired (the 32-bit runs with the guard moved to Main printed `IntPtr.Size=4
Is64BitProcess=False IsLittleEndian=True`). `E0434352` = unhandled exception (load refusal, or the
`dotnet exec` of a DLL: "Entry point not found", expected). `99` = the loader caught a load error.

| Image | net8-x64 | net10-x64 | net8-x86 | net10-x86 | fx48-direct | fx48-ldr64 | fx48-ldr32 | net10-ldr64 | net10-ldr32 | wsl-net8 | wsl-net10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **pe32-writer.exe** (proposed) | **42** | **42** | ran 32 | ran 32 | **42** (64-bit) | 42 | ran 32 | 42 | ran 32 | **42** | **42** |
| **pe32-writer.dll** (proposed) | DLL | DLL | DLL | DLL | n/a | **42** | ran 32 | **42** | ran 32 | DLL | DLL (loader: **42**) |
| pe32-stub.exe (Roslyn-like, stub + `.reloc`) | 42 | 42 | ran 32 | ran 32 | 42 | 42 | ran 32 | 42 | ran 32 | 42 | 42 |
| pe32-stub.dll | DLL | DLL | DLL | DLL | n/a | 42 | ran 32 | 42 | ran 32 | DLL | DLL (loader: 42) |
| pe32-stripped.exe (ilasm, stub removed) | 42 | 42 | ran 32 | ran 32 | 42 | 42 | ran 32 | 42 | ran 32 | 42 | 42 |
| pe32-stripped.dll | DLL | DLL | DLL | DLL | n/a | 42 | ran 32 | 42 | ran 32 | DLL | DLL |
| pe32-ilasmnostub.exe (`RELOCS_STRIPPED`) | 42 | 42 | ran 32 | ran 32 | 42 | 42 | ran 32 | 42 | ran 32 | 42 | 42 |
| pe32-ilasmnostub.dll (`RELOCS_STRIPPED` DLL) | BadImage | BadImage | BadImage | BadImage | n/a | **99** | **99** | **99** | **99** | 134 BadImage | 134 BadImage |
| pe32-writer-32bm.exe (+`32BIT_MACHINE`) | 42 | 42 | ran 32 | ran 32 | 42 | 42 | ran 32 | 42 | ran 32 | 42 | 42 |
| pe32-writer-req32.exe | refused | refused | ran 32 | ran 32 | ran 32 | 99 | ran 32 | 99 | ran 32 | 134 refused | 134 refused |
| pe32-writer-pref32.exe | BadImage | BadImage | ran 32 | ran 32 | ran 32 | **42** | ran 32 | 99 | ran 32 | **42** | **42** |
| pe32-pref32.exe (ilasm) | BadImage | BadImage | ran 32 | ran 32 | ran 32 | 42 | ran 32 | 99 | ran 32 | 42 | 42 |
| pe64-writer.exe (today) | 42 | 42 | refused | refused | 42 | 42 | 99 | 42 | 99 | 42 | 42 |
| pe64-writer.dll (today) | DLL | DLL | DLL | DLL | n/a | 42 | 99 | 42 | 99 | DLL | DLL (loader: 42) |
| pe64-ilasm.exe / pe64-stripped.exe | 42 | 42 | refused | refused | 42 | 42 | 99 | 42 | 99 | 42 | 42 |
| pe64-writer-arm64.exe | refused | refused | refused | refused | Windows: "not compatible with the version of Windows" | 99 | 99 | 99 | 99 | 134 refused | 134 refused |
| pe64-writer-i386.exe (PE32+ I386) | BadImage | BadImage | BadImage | BadImage | "not a valid Win32 application" | **42** | 99 | 99 | 99 | **42** | **42** |

Messages behind the codes (verbatim from the logs): 64-bit image in a 32-bit .NET 10 process: "The
assembly architecture is not compatible with the current process architecture." (.NET 8 x86: "Could not
load file or assembly 'probe, ...'"); Framework: "An attempt was made to load a program with an incorrect
format." `32BITREQUIRED|32BITPREFERRED` on Windows .NET x64: "Format of the executable (.exe) or library
(.dll) is invalid." `RELOCS_STRIPPED` DLL: CoreCLR "Bad IL format. The format of the file ... is invalid.",
Framework "The module was expected to contain an assembly manifest." The ARM64 and PE32+-I386 direct
starts: `Start-Process` errors quoted in the table.

Readings:

- **The proposed envelope (`pe32-writer`) behaves exactly like Roslyn's AnyCPU (`pe32-stub`)** in every
  cell: same loads, same refusals, same bitness. The stub, the IAT and `.reloc` make no difference on any
  runtime, including the direct Framework start of an EXE (64-bit for AnyCPU; 32-bit WOW64 for a
  `32BITREQUIRED` or `32BITPREFERRED` copy, both without a stub).
- **COFF `IMAGE_FILE_32BIT_MACHINE` is ignored by every loader** (0x0122 runs 64-bit). Do not set it
  (Roslyn's AnyCPU does not).
- **Never set `IMAGE_FILE_RELOCS_STRIPPED` on a DLL**: every runtime refuses it (the EXE form is fine).
  lld/MSIL never sets it; a contract line keeps it that way.
- **`32BITPREFERRED` is not a reliable refusal**: Linux .NET runs such an image in its 64-bit process and a
  64-bit Framework host loads it as a library; only Windows .NET x64 refuses it. Relevant to P-4
  (msil32), not to msil64: msil32 needs the guard as much as msil64 does.
- **A PE32+ image with machine I386 is inconsistent across runtimes** (refused by Windows CoreCLR, loaded
  by Framework `LoadFrom` and by Linux CoreCLR). Never emit it.
- Probing artifact (not an envelope issue): on WSL .NET 10, `roslyn/r-anycpu.exe` failed with "Entry point
  not found in assembly 'r-anycpu'" because `r-anycpu.dll`, an assembly of the same simple name, sat in the
  same directory (.NET 8 picked the EXE). A first `v2/real` run hit the same class of artifact
  (`ptr-pe32.exe` whose assembly name `ptr-pe64` matched a sibling file) and was rebuilt in per-variant
  directories. See risk R5.

### 3.4 Real msil-ld outputs in the wrong process width (`v2/real.log`, `v2/wsl-extra.log`)

| Program | expected | pe64 (today) on 64-bit hosts | pe32 (AnyCPU) on 64-bit hosts (net8/10 x64, fx48-direct, WSL net10) | pe32 on net8-x86 / net10-x86 | req32 on fx48-direct (32-bit) |
|---|---|---|---|---|---|
| `ptr.c` | 42 | 42 | 42 | 42 | 42 |
| `union.c` | 42 | 42 | 42 | 42 | 42 |
| `wide.c` | 48 | 48 | 48 | **0** | **0** |

`wide.c` computes `(int)((a*b) >> 36) & 0xff` on two `volatile long long` globals. Its IL (ildasm of
`real/pe64/wide.exe`) loads them with `ldind.i` and multiplies as `native int` (decision D2): in a 32-bit
process `ldind.i` reads 4 bytes and the product loses its high word. `ptr.c` and `union.c` survive only by
the little-endian coincidence that the low 4 bytes of an 8-byte slot hold the pointer. Nothing reports
the error. This is the case for the guard.

## 4. The width guard: what the user sees (`v2/guard-modes.log`, `v2/pureil.log`, `v2/wsl.log`)

Run on `pe32-writer.exe`, `pe32-writer.dll` and `pe32-writer-req32.exe` in every 32-bit host, and forced
(`MSIL_GUARD_FORCE=1`) on the 64-bit hosts and WSL. Message text in all modes: "probe: this assembly was
compiled for msil64-unknown-dotnet and needs a 64-bit little-endian process, but this process has
IntPtr.Size = 4 and IsLittleEndian = True".

| Mode, site | .NET 8 (Windows x86) | .NET 10 (Windows x86; WSL forced) | Framework 4.8 EXE (direct, 32- or 64-bit) | Library in a host (Framework / .NET 10 loader) |
|---|---|---|---|---|
| `FailFast`, `.cctor` | "Process terminated. <message>" + stack, exit `0x80131623` | same, exit `0x80131623` (Linux: 134) | **nothing on the console**, exit `0x80131623`; message only in the Application event log (`.NET Runtime`, event 1025, "The application requested process termination through System.Environment.FailFast") | Framework: host killed, **nothing printed**; .NET 10: host killed, message printed |
| `throw PlatformNotSupportedException`, `.cctor` | "Unhandled exception. System.TypeInitializationException: The type initializer for '<Module>' threw an exception. ---> System.PlatformNotSupportedException: <message>", exit `0xE0434352` | "Unhandled exception. System.PlatformNotSupportedException: <message> at .cctor()" (no wrapper for the entry assembly), exit `0xE0434352` (Linux 134) | "Unhandled Exception: System.TypeInitializationException ... ---> System.PlatformNotSupportedException: <message>", exit `0xE0434352` | host catches `TypeInitializationException` (on .NET 10 inside `TargetInvocationException` from reflection) with the message as inner exception; host survives |
| `Console.Error` + `Environment.Exit(134)`, `.cctor` | message, exit 134 | message, exit 134 | message, exit 134 | message, **host process exits** with 134 |
| any mode, site `'<msil>Main'` | EXE: as above, stack shows `<msil>Main` | as above | as above | **guard never runs: the library executes in the 32-bit process** (Framework and .NET 10 loaders returned 42 with `IntPtr.Size=4`) |

Initialization order (`v2/init-order.log`, re-run with `hosts/loader2.exe` and `out3/probe3.dll` on
Framework 4.8, .NET 8.0.33 and .NET 10.0.14): calling a method of a type with its own `.cctor` (like
`'<msil>Data'`) ran `<Module>::.cctor` first; with the module initializer throwing, the type's own
`.cctor` never ran, the second call threw `TypeInitializationException` again, and a call into an
unrelated type with no static state (`Pure.Seven`) threw it too. Unforced, the order was `<Module>.cctor`
then `Data.cctor`. Both `'<msil>Main'` (EXE) and every export (library) are therefore covered by one
guard at the top of `<Module>::.cctor`.

Pure-IL guard (`v2/pureil/guard4.il`): no BCL member except
`PlatformNotSupportedException::.ctor(string)`:

```
.locals init (int32 probe)
ldc.i4.m1
conv.i          // sign-extends: all ones in native int
conv.u8         // zero-extends: 0x00000000FFFFFFFF on 32-bit, all ones on 64-bit
ldc.i8 -1
bne.un FAIL
ldc.i4.1
stloc.0
ldloca.s probe
ldind.u1        // 1 on a little-endian machine
brfalse FAIL
ret
FAIL: ldstr "..."  newobj PlatformNotSupportedException::.ctor(string)  throw
```

Measured: 42 on net8/net10 x64, Framework direct (64-bit) and WSL net8/net10; the exception with the
message on net8/net10 x86 and on Framework 32-bit. A first draft used `conv.u` instead of `conv.i` and
fired on 64-bit hosts too (`conv.u` zero-extends the `int32` -1): the sign-extension is load-bearing and
needs a unit test. ILVerify reports one error on it, `[StackUnexpected] ... [found Int32][expected
Byte]` at the `ldind.u1`; `BitConverter.IsLittleEndian` would avoid that but is a field, and
`llvm/include/llvm/CLI/BCLMembers.def` has only `CLI_BCL_TYPE` and `CLI_BCL_METHOD` entries (no field
kind). Big-endian runtimes could not be measured here (no s390x host); the test exists for .NET's
big-endian s390x builds.

## 5. Other measurements

- **Stack reserve** (`v2/stack/`): a Roslyn AnyCPU EXE calling `GetCurrentThreadStackLimits` (measurement
  only) reported a main-thread reserve of `0x100000` on a direct Framework start in the 64-bit process
  (Roslyn's PE32 default 1 MB); with `SizeOfStackReserve` patched to `0x400000` it reported `0x400000`,
  in the 64-bit process and in a 32-bit (`32BITREQUIRED`) copy. Under `dotnet exec` both reported
  `0x180000` (the host's). PE32's stack fields are 32-bit, so today's `PEOptions` defaults
  (0x400000/0x4000) fit; using Roslyn's PE32 values would cut C programs' main stack on Framework to a
  quarter.
- **Verifiers**: `ilverify` (with the .NET 8.0.33 shared framework as references) reports "All Classes and
  Methods ... Verified" on both `pe32-writer.exe` and `pe64-writer.exe`; PEVerify `/md` (NETFX 4.8 Tools)
  reports Verified with the same 4 warnings on `pe32-writer.exe`, `pe32-writer.dll` and `pe64-writer.exe`.
  `llvm-cil-dump --headers` reads the PE32 image ("Format: PE32 machine=0x014C exe").
- **ARM64**: not measured (no ARM64 runner). By the rule observed on x64 (AnyCPU = neutral), .NET on
  ARM64 Windows/Linux/macOS runs the image as an ARM64 process with the same LP64 little-endian layout.

## 6. What P-1a must change (proposal)

1. `llvm/include/llvm/CLI/PEWriter.h` and `llvm/lib/CLI/PEWriter.cpp`:
   - `PEOptions::get`: a case for the portable 64-bit arch (`Triple::msil64`, per `recon-target`): Machine
     `IMAGE_FILE_MACHINE_I386`, the PE32 format, ImageBase `0x400000` (Executable) and `0x10000000`
     (Library and Module, Roslyn's), CorFlags `COMIMAGE_FLAGS_ILONLY` only, StackReserve/Commit
     **kept at 0x400000/0x4000** (section 5), Heap 0x100000/0x1000 (Roslyn's PE32; immaterial),
     `DllCharacteristics` 0x8540 (no `HIGH_ENTROPY_VA`). `Triple::msil32`: a loud error naming P-4. The
     comment "PE32 images (i686) need the mscoree import, the entry stub and .reloc" is wrong by
     measurement and goes; i686 host triples stay refused until P5-A for their own reasons
     (`32BITREQUIRED` and the address-width work), not for the stub.
   - A format notion: either an explicit `PEOptions` member (for example `bool PE32Plus`) or "I386 means
     PE32". Explicit is better: `llvm-cil-copy` and tests can set it, and FeatureIdeas 7 (Machine and
     CorFlags independent of the triple) stays open.
   - `checkContent`: accept I386 with the PE32 format, `ILONLY` set and neither `32BITREQUIRED` nor
     `32BITPREFERRED` (until P-4 defines msil32's flags); keep refusing everything else; refuse a PE32+
     image with machine I386 (section 3.3).
   - `writePEImage`: the header constants become per-format: optional header 224 bytes (96 + 16x8),
     `DataDirectoriesOffset` = opt + 96, section table at `0x178` (the `static_assert(SectionTableOffset
     == 0x188)` becomes two asserts); magic `0x10B`; `BaseOfData` (u32) at opt+24 = the RVA of the first
     section after `.text` that has data, else 0 (0 measured accepted everywhere; Roslyn writes the
     `.rsrc` RVA); ImageBase as u32 at opt+28; stack/heap as four u32 at opt+72..87; `LoaderFlags` at
     opt+88; `NumberOfRvaAndSizes` at opt+92. `SizeOfHeaders` stays `alignTo(0x178 + 40*n, 0x200)` (0x200
     for one section, 0x400 for the CIL object's four sections). COFF Characteristics unchanged (0x0022 /
     0x2022: never `32BIT_MACHINE`, never `RELOCS_STRIPPED`). Still no import, IAT, stub or `.reloc`, and
     `AddressOfEntryPoint` 0. The content id, `.text` layout and COFF symbol view are unchanged.
2. Callers: `lld/MSIL/Writer.cpp : Writer::writeImage` needs no change (it passes `ctx.arg.target`);
   `MSILObjectWriter.cpp` makes msil64 CIL objects PE32 netmodules through the same `PEOptions::get`
   (`PEImage` already reads PE32, plan 5.7.8; lld/MSIL never calls `isPE32Plus`); `llvm-cil-copy.cpp`
   copies an I386 `ILONLY` input with the msil64 defaults instead of refusing it (optional, but it keeps
   the copy oracle usable on P-1a outputs).
3. The guard, in `llvm/lib/CLI/Trailer.cpp : cli::buildTrailer` (shared by msil-ld's
   `RowTrailerSink`/`Writer::run` and `MSILAsmPrinter::emitTrailer`, so `-S` gets it too):
   - a `TrailerInput` member (for example the required pointer width, set only for the portable triples);
   - when set, `<Module>::.cctor` is always emitted (today it exists only when `HasPhase1 || HasPhase2`),
     and the guard is its first code, before `emitPhase1` in a one-phase output and before `emitPhase2`
     in a two-phase one; `'<msil>Data'::.cctor` needs no copy (the module initializer runs first,
     section 4);
   - the check of section 4 (pure IL; the byte-order half is a user/plan choice, Q2), then `ldstr` of a
     link-time constant message (assembly name, triple, what to do) and `throw` of
     `System.PlatformNotSupportedException` (Q1). Not in `'<msil>Main'` (libraries) and not `FailFast`
     (silent on Framework).
   - `llvm/include/llvm/CLI/BCLMembers.def`: `CLI_BCL_TYPE(PlatformNotSupportedException, "System",
     "PlatformNotSupportedException", true)` and its `.ctor(string)` as `Required` (both exist in
     Framework 4.8's mscorlib, in `System.Runtime` and in `netstandard`). This corrects `recon-target`'s
     row for `MSILAsmPrinter.cpp : emitTrailer` and its question 2, which list `IntPtr::get_Size`,
     `BitConverter::IsLittleEndian` and `Environment.FailFast`: the first two are unnecessary and
     `FailFast` is the wrong primitive on Framework.
4. Tests (P-1a): `PEWriterTest` (an msil64 header test: magic 0x10B, `SizeOfOptionalHeader` 0xE0, machine
   0x14C, chars 0x22/0x2022, section table 0x178, bases 0x400000/0x10000000, stack 0x400000, entry 0, CLR
   directory only; msil32 refused; I386 + `32BITREQUIRED` refused; the existing i686 refusal kept with a
   new reason); a `Trailer`/lit test that the portable `.cctor` starts with the guard and that `conv.i`
   precedes `conv.u8`; the run legs of the recommendation (`.NET 8/10`, Framework x64 direct, WSL .NET 10)
   plus "a 32-bit process hitting the guard": `C:\Program Files (x86)\dotnet\dotnet.exe exec` of an EXE,
   and a 32-bit Framework host or a `CorFlags /32BITREQ+` copy for the Framework leg, each expecting exit
   `0xE0434352` and the message on stderr. Hosts exist on this machine: x86 .NET 8.0.33/10.0.14, the
   Framework 32-bit runtime, `CorFlags.exe` in `NETFX 4.8 Tools`.
5. Docs: `Design.md` section 2's bullet "Pointer size is fixed at compile time. Every triple names a CPU,
   so the PE's machine type is set ..." needs the portable exception (AnyCPU PE32 + guard); plan line 1821's
   comment and `PEWriter.h`'s "Any other architecture is an Error until phase 5 (PE32, the mscoree stub and
   .reloc)" need the measured reason.

## 7. Risks

- **R1 (low)**: no stub means the Windows OS loader's built-in IL-only handling starts Framework EXEs.
  Measured on Windows 11 (10.0.26200) only; Framework 4.8 also supports Windows 7 SP1/8.1, where Roslyn's
  x64 images (also stubless) have always run, so the risk is small but unmeasured.
- **R2**: the guard costs one always-present `<Module>::.cctor` on portable outputs and forces module
  initialization at first touch of any type, even in programs with no data. Negligible, but it changes
  the shape the M24-M35 trailer tests see on portable (only on portable).
- **R3**: big-endian and ARM64 runtimes unmeasured (no hosts).
- **R4**: if the byte-order check uses `ldind.u1` on an `int32` local, every portable output gets one
  more ILVerify error class (`StackUnexpected`); the gates' verification step must accept it on portable,
  or the check must use another route (Q2).
- **R5**: an assembly whose simple name equals a sibling file's (for example a user program named `c`
  next to the libc assembly `c.dll`, or `prog.exe` next to `prog.dll`) can be resolved to the wrong file by
  the .NET host (observed on WSL .NET 10). Not an envelope issue; a P-1b point (reserve or diagnose the
  libc assembly's name in msil-ld).
- **R6**: `FailFast` stays tempting for "fail loudly" elsewhere in the portable libc (abort, assert);
  on Framework it is silent on the console. The same finding applies to `abort()` design in P-2.

## 8. Questions

- **Q1. Guard failure primitive.** Options: (a) throw `PlatformNotSupportedException` from
  `<Module>::.cctor` (printed by every runtime for an EXE, catchable by a host that loads a library);
  (b) `Environment.FailFast` (silent on the console on Framework 4.8); (c) write to `Console.Error` and
  `Environment.Exit` (kills a host that only loaded a library). Recommendation: (a).
- **Q2. Byte-order half of the guard.** Options: (a) pure IL (`int32` local + `ldind.u1`, one ILVerify
  error); (b) add a field kind to `BCLMembers.def` and read `System.BitConverter::IsLittleEndian`
  (verifiable, one more table feature); (c) width only, no byte-order check. Recommendation: (a); the
  `.cctor` is unverifiable already (`cpblk`), and big-endian .NET exists (s390x).

## 9. Reproduction

```
bash <scratch>/p0/loader/src/build-v2.sh                     # all images into v2/
pwsh -NoProfile -File <scratch>/p0/loader/src/run2.ps1 > v2/matrix.log
pwsh -NoProfile -File src/run2.ps1 -Mode throw -Site main -Images pe32-writer.exe -Hosts net10-x86
MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash <scratch>/p0/loader/src/wsl-run2.sh
python -I src/pe.py dump <image>; python -I src/opthdr.py <image>
```

Files: `src/probe.il`, `v2/probe-lib.il`, `src/probe3.il`, `v2/pureil/guard4.il`, `real/{ptr,union,wide}.c`,
`roslyn/hello.cs`, `hosts/loader.cs`, `hosts/loader2.cs`, `v2/stack/stk.cs`; logs `v2/matrix.log`,
`v2/guard-modes.log`, `v2/real.log`, `v2/wsl.log`, `v2/wsl-extra.log`, `v2/pureil.log`, `v2/init-order.log`,
`v2/dump.txt` (all under `<scratch>/p0/loader/`). Tools used read-only: `<build>/bin`
`clang.exe`, `llvm-cil-copy.exe`, `llvm-cil-dump.exe`; Framework64 `ilasm.exe`; NETFX 4.8 Tools `ildasm.exe`,
`PEVerify.exe`; `ilverify`; SDK 10.0.403 `csc.dll`.

## Verifier corrections

Adversarial verification, 2026-11-01. New probes and logs are in `<scratch>/p0/loader-verify/`. Nothing in
the tree was changed. Each item says whether it **holds**, needs a **correction**, or is a **gap**.

### V1. Holds, now also measured for a beforefieldinit type and global methods (`init-bfi.log`, `init-bfi-unforced.log`)

The report's init-order evidence (`v2/init-order.log`) was written from a transcript: the .NET 8/10 rows read only
"(same three lines)", and its `Data` type was **not** `beforefieldinit`. The real `'<msil>Data'` of a TwoPhase output
is `beforefieldinit` (plan 5.9.10), and C functions are global methods of `<Module>`. So I re-measured with
`probe4.il`: a `beforefieldinit` `Data` with its own `.cctor`, a global method `gfn`, and an `Exp` class that reads
`Data::F` or calls `gfn`. The host `host4.exe` calls `Exp` **directly** (compile-time reference, no reflection), in both
orders, on Framework 4.8, .NET 8.0.33 and .NET 10.0.14 (x64).
- With no guard failure, `<Module>.cctor` ran first every time. The `beforefieldinit` `Data.cctor` ran lazily, at
  the first `Exp.Get`.
- With the guard forced, every call threw `TypeInitializationException` with the inner
  `PlatformNotSupportedException`, and `Data.cctor` never ran. The failure is sticky on all three runtimes.

Claim 3 and the "one guard covers every export" proposal therefore hold. Note that the forced rows of an empty
`MSIL_GUARD_FORCE=` were not usable, because an empty variable is non-null on Windows. Those rows were deleted from the
log and re-run with the variable unset.

### V2. Correction: Q2 option (a) fails the gates' verifier, and a verifiable method-only route exists (`ilverify-guards.log`, `msil-verify-guards.log`, `guardB-run.log`)

The report says the extra ILVerify `StackUnexpected` is cheap because "the `.cctor` is unverifiable anyway: cpblk"
(R4, Q2). That reasoning is wrong:
- `msil/utils/msil-verify.py` does not accept errors by method. It accepts only the codes in
  `msil/utils/ilverify-allowed.txt`: `Unverifiable`, `InitLocals`, `StackByRef [found Native Int]` and
  `UnmanagedPointer`. Pass 2 rewrites `cpblk` and allows nothing.
- Measured: `msil-verify.py --ilverify ilverify.exe --runtime-dir <.NET 8.0.33> guard4.exe` gives
  `1 disallowed verifier error(s) in guard4.exe` (`[StackUnexpected] ... [found Int32][expected Byte]`).
- In pass 2, the `ldind.u1` becomes `call '<msil-verify>'::'ldind.u1'(native int)`. The stack holds an `int32&`
  there, so the error comes back even where nothing else is allowed.
- Option (a) would therefore make **every** portable output fail the verify step of every gate and lit run. Fixing
  that needs a change to `ilverify-allowed.txt` and to the pass-2 rewrite. Unlike cpblk, that change has no
  design-level justification.
- The `conv.u` variant (`ldloca; conv.u; ldind.u1`, `guardC.il`) also fails: `[ExpectedNumericType] [found address
  of Int32]`.

There is a verifiable route the report missed:
- `ldc.i4.1; call uint8[] [mscorlib]System.BitConverter::GetBytes(int32); ldc.i4.0; ldelem.u1; brfalse FAIL`
  (`guardB.il`).
- It is a **method** row, so `BCLMembers.def` needs no field kind. `BitConverter::GetBytes(int32)` is in Framework
  4.8's mscorlib and in modern .NET.
- ILVerify reports "All Classes and Methods ... Verified", and `msil-verify.py` passes with no disallowed error.
- Runs: 42 on .NET 10 x64 and on a Framework direct start (64-bit). On .NET 10 x86 it throws
  `PlatformNotSupportedException` with the message.
- It fits the standing rule "prefer platform mechanisms".

**Q2 should offer this as option (d) and recommend it** over (a). The cost is one `CLI_BCL_TYPE(BitConverter)` row and
one `CLI_BCL_METHOD` row, appended under 5.13.7. R4 then goes away.

### V3. Correction: Trailer.cpp and BCLMembers.def are hot files, so the guard is a hook plus a new file

Plan 5.13.1 L1 lists `llvm/include/llvm/CLI/Trailer.h`, `llvm/lib/CLI/Trailer.cpp`, `RowTrailerSink`
(`lld/MSIL/Synthetic.cpp`), `MSILAsmPrinter.{h,cpp}` and `BCLMembers.def` as hot files. L2 says that a feature adds
only a hook of about 30 lines in a hot file. The feature's logic goes into a new file named for it. The whole-program
`TrailerInput` fill goes into `MSILTrailerTextSink.cpp` (L9). P-1a sits inside the Phase 3 program order (option A),
so these rules apply to it.

Section 6 item 3 asks for the guard emitter to be written "in `Trailer.cpp : cli::buildTrailer`", and the summary's
file list includes `lld/MSIL/Writer.cpp` and `MSILAsmPrinter.cpp`. Correct placement:
- The emitter goes in a new non-hot file, for example `llvm/lib/CLI/TrailerGuard.cpp`.
- `buildTrailer` gets a hook: the new `TrailerInput` field, "always make `<Module>::.cctor`", and one call at its top.
- The `BCLMembers.def` rows are appended under a milestone heading (5.13.7).
- The fill goes in `MSILTrailerTextSink.cpp` and the lld producer, not in `MSILAsmPrinter.cpp`.

recon-target's row for `TrailerDefs.cpp : cli::isOfferedIn` ("a `Portable` scope for the width guard if it is a
trailer definition") is not addressed by the report. The guard is not a callable trailer definition, so no TrailerDefs
row is needed. The report should say so explicitly.

### V4. Gap: M28's contract also claims the first instruction of `<Module>::.cctor`

M28 (plan 5.15.3, the `Trailer.cpp` hook of M28's WP text) pins this for `TwoPhase` with `SpilledData`:
"`<Module>::.cctor` exists and starts with `ldsfld` of `Sink.getDataField()` and `pop`, before the `ldtoken`s".
- That touch runs `'<msil>Data'::.cctor`, which is phase 1: allocation, `cpblk` and the `stind.i` relocations.
- If the touch comes before the guard, phase 1 runs in a wrong-width process before the guard fires. The writes stay
  inside the allocated block, so the damage is limited, but the guard is no longer first.
- The report's "the guard is first, before `emitPhase1`/`emitPhase2`" therefore needs to name this case.
- P-1r must re-pin M28's "starts with" wording for portable outputs: the guard comes first, then the touch. Without
  that re-pin, the two contracts conflict.
- M31's `ThreadAdopt` and M29's Framework subscription do not claim the first slot.

### V5. Gap: plan and Design.md contracts that say "PE32+" for CIL objects and images, and a checker that enforces it

Section 6 item 5 lists only Design.md's pointer-size bullet and the 5.7.7 comment. These further sites state PE32+ as
a contract or test it. Each needs an amendment, or a decision to keep msil64 CIL objects PE32+, which would need
another machine value, because PE32+ I386 is refused by Windows CoreCLR (3.3):
- plan 5.8.2 "**O1** PE32+ image. COFF Machine from the triple (x86_64 0x8664, aarch64 0xAA64)";
- plan 5.7.7 "PE32+, with the classic 128-byte DOS header";
- plan 5.7.10: the canonical header line `Format: PE32+ machine=0x8664`, and "llvm-cil-copy reads an IL-only PE32+
  image";
- plan section 10 Q1 (B2): "A CIL object is an ECMA-335 netmodule (PE32+ envelope, ...)";
- Design.md 5.1 Object files, "**Shape.** A PE32+ envelope";
- `msil/utils/mdtools/cli-corpus-check.py`. Its O1 check (`OBJECT_MACHINES = {"x86_64": 0x8664, "aarch64": 0xAA64}`,
  and "not a PE32+ dll") would flag every msil64 object.

`msil/utils/msil-verify.py : ilasm_flags_for` maps only 0x8664 and 0xAA64. A PE32 I386 output is rebuilt with plain
`ilasm -exe`/`-dll`, which writes PE32 with `32BIT_MACHINE`. That is harmless for verification (no change needed),
but worth a note.

Good news on the linker side, measured (`pe32-object.log`): a host-triple CIL object rewritten to PE32 I386 with
`pe.py topE32` is read by `llvm-nm`, gets an `llvm-ar` armap (`add`, `g`), and links with msil-ld both as an object
and as an archive member. The result runs and returns 42 on .NET 10. This supports the report's "`PEImage` already
reads PE32; lld/MSIL never calls `isPE32Plus`", which the report had not measured.

The same run shows a side point for recon-linkrules: msil-ld does not compare a CIL object's COFF machine with the
target. A PE32 I386 object linked silently into an x86_64 link, so only the link-info triple can stop a portable/host
mix. In addition, `lld/MSIL/NativeLibraries.cpp : isCompatibleCOFF` and `isCompatibleELF` return true for any
arch other than x86_64/aarch64. A native import library would therefore pass the machine check on msil64; portable
has to refuse native libraries elsewhere.

### V6. Correction: citation errors

- "Design.md section 2's bullet 'Pointer size is fixed at compile time ...'" (6 item 5 and the summary): that bullet
  is in Design.md **section 3** ("Target triples, data models and ABI").
- "plan line 1821's comment" (6 item 5): this is a bare line number. The standing rule requires citing the PEOptions
  struct comment in plan **5.7.7 PE writer (M17)**. The summary already says 5.7.7.
- "35 lit/test files that match `machine=0x8664`" (1): `git grep -l` finds **25** tracked files (54 occurrences),
  docs included. The point stands (host triples, unaffected), but the count is wrong.

### V7. Holds (re-checked)

- **PEWriter.** `PEWriter.cpp : PEOptions::get` (AMD64/ARM64 only; the "PE32 images (i686) need the mscoree import,
  the entry stub and .reloc" comment; "not supported until phase 5"), `checkContent` (AMD64/ARM64 only),
  `static_assert(SectionTableOffset == 0x188)`, `W16(P + 0, PE32_PLUS)`, and `PEWriter.h`'s phase-5 comment are all
  as cited.
- **PEWriter callers.** The callers are `Writer.cpp : Writer::writeImage` (`PEOptions::get`), `MSILObjectWriter.cpp`
  (`PEOptions::get(TT, Module)` plus the COFF view) and `llvm-cil-copy.cpp`. `PEWriterTest.cpp : UnsupportedArchitecture`
  lists i686 with the stub rationale, and the test pins `SizeOfOptionalHeader == 0xF0`.
- **Trailer and BCL rows.** `Trailer.cpp : buildTrailer` emits `<Module>::.cctor` only when
  `HasPhase1 || HasPhase2` (or phase 2 alone under TwoPhase). `BCLMembers.def` has only `CLI_BCL_TYPE` and
  `CLI_BCL_METHOD`.
- **Logs.** `v2/matrix.log` and `v2/wsl.log` rows for `pe32-writer.{exe,dll}` agree with table 3.3. `v2/real.log`
  shows `wide.exe` 48 on x64, 0 on net8/net10 x86, and refusal of the PE32+ form under x86. `v2/dump.txt` shows
  `pe32-writer.exe` as PE32, 014c, chars 0022, base 400000, entry 0, CLR directory only.
- **Stack reserve.** Re-run (`stack-rerun.log`): 1 MB gives 0x100000 and 4 MB gives 0x400000 on a direct Framework
  start, in a 64-bit process and in a 32-bit (`req32`) one. Under `dotnet exec` the result is 0x180000.
- **Stack reserve without the stub.** The report measured only Roslyn images that have the stub. I also measured a
  **stubless** copy (`pe.py strip`, entry 0): it gives 0x400000. The proposed envelope keeps the image's stack reserve.
- **Pure-IL width test.** `ldc.i4.m1; conv.i; conv.u8; ldc.i8 -1; bne.un` gives no ILVerify error. Only the
  byte-order half did (V2).

### V8. Additions to the risks

- **R7 (unmeasured).** On Windows on ARM64 without .NET Framework 4.8.1, Framework AnyCPU EXEs have historically
  started under x86 emulation (Windows 10 on ARM) or x64 emulation. In the x86 case an msil64 portable EXE started
  directly would hit the guard: loud, but a refusal on a 64-bit machine. Other 32-bit .NET runtimes (linux-arm,
  browser-wasm) also refuse msil64 loudly through the guard, which matches the user decision. It should be documented.
- **R2 addendum.** The always-present module initializer on portable means that every reflection use that
  instantiates a type of the assembly runs the guard first, including the private symbol-table attribute class read
  through `GetCustomAttributes`. That is harmless in a correct-width process.
- **Test placement.** The 32-bit guard leg is Windows-only (WSL has no 32-bit .NET). Per the user decision ("portable
  smoke leg in post-commit runs only; M27-M35 gates stay as written"), it belongs to P-1a's own gate and to the
  post-commit portable smoke leg, never to the M27-M35 gates. Section 6 item 4 should say so.
