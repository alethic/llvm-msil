# P2 recon: binding BCL types and members against reference assemblies (`bcl-binding`)

Status: recon report for Phase 2 planning (P2-B), 2026-10-20. Base: branch `msil` at `1ea64fe9fc7c`.
Scratch: `C:\llvm-msil-scratch\p2\bcl-binding\` (1.9 MB in all; no private LLVM build). Supporting files:
[bcl-binding-files/](bcl-binding-files/) (the resolver tool's source, query list, experiment generators, logs).
Installs: none (the resolver is a `net10.0` console app that uses the in-box System.Reflection.Metadata; no NuGet
package was added).

Inputs read: Design.md (all, in particular 2.3, 5, 6, 7.1, 8, 11), ImplementationPlan.md sections 1-5, 7 and the
section 12 rows of 2026-10-14/15, Status.md, `msil/orchestration/P2/inputs.md`, recon `asmprinter.md`,
`cil-runtime.md`, `isel.md` 5.9, FeatureIdeas.md. Every file, line and API named below was checked in this checkout;
behaviour of .NET was measured on this host (.NET Framework 4.8, .NET 6.0.36, 8.0.31, 10.0.12 on Windows; .NET 8.0.31
and 10.0.12 in WSL Ubuntu). Statements that rest on documentation or memory rather than a check here are marked
*unverified*.

## 0. Summary

1. **Phase 1 binds 6 BCL entities through one AssemblyRef** (`mscorlib` 4.0.0.0): `System.ValueType` (base of the
   RVA image type), `Marshal::AllocHGlobal`, `Marshal::StringToHGlobalAnsi`, `Environment::GetCommandLineArgs`,
   `Buffer::MemoryCopy`, `Interlocked::MemoryBarrier`, plus the implicit core-library element types. In a compiled
   program that is 1 AssemblyRef, 5 TypeRefs and 5 MemberRefs (section 1.2). Every one of them is **text** today
   (TableGen AsmStrings and AsmPrinter string literals); `MSILMemberRefExpr::BCLMember` exists but has no caller.
   P2-A's binary encoder therefore needs a structured, symbolic BCL operand.
2. **Where things are defined** (sections 2.2-2.3, tool output in the files): in the .NET Framework 4.8 reference set
   everything is in `mscorlib` (facades forward to it); in netstandard 2.0/2.1 everything is in `netstandard.dll`;
   in the .NET 6/8/10 reference packs the core library is `System.Runtime` and the Phase-1 members split over
   `System.Runtime`, `System.Runtime.InteropServices` and `System.Threading`. Definitions move between versions
   (`RuntimeInformation`: `System.Runtime.InteropServices.RuntimeInformation` 6.0 -> `System.Runtime` 8.0), and
   public key tokens differ per assembly (`b77a5c561934e089`, `b03f5f7f11d50a3a`, `cc7b13ffcd2ddd51`), so binding
   must be data-driven from the references, never a static name table (except for the no-reference fallback).
3. **Runtime facts** (section 3, 16 IL variants plus a real Phase-1 program, Windows and WSL): an `[mscorlib]`
   reference runs on Framework 4.8 and on .NET 6/8/10 because the runtime's own `mscorlib.dll` (a 4.0.0.0 facade with
   1,381 forwarders on .NET 8) forwards each Phase-1 type to `System.Private.CoreLib` in one hop. The same program
   bound like Roslyn binds against the .NET 8 pack (`System.Runtime` / `System.Runtime.InteropServices` /
   `System.Threading` 8.0.0.0) runs on .NET 8 and 10 and **fails on Framework 4.8** with
   `FileNotFoundException: ... 'System.Runtime, Version=8.0.0.0 ...'` (exit `0xE0434352`). Binding to the wrong
   facade fails at run time (`[System.Runtime]Marshal` -> `TypeLoadException`): implementation facades forward only
   their reference assembly's surface. CoreCLR ignores the public key token and requires `version >= requested`;
   Framework checks the token (except for `mscorlib`) and loads only versions it has (`netstandard` 2.0.0.0 and the
   4.x facades yes; 2.1.0.0 and 8.0.0.0 no). Custom modifiers are part of the binding: a MemberRef without the
   definition's `modreq` throws `MissingMethodException`.
4. **Roslyn** (section 4, 19 csc runs and its source): corlib = the unique reference that defines `System.Object` and
   has no AssemblyRefs (two candidates count as none: CS0518); lookup uses type *definitions* only, forwarders only
   produce CS1069/CS1070; a type defined in two references is CS0433, a missing member CS0117; AssemblyRefs are
   emitted only for defining assemblies actually used, with the 8-byte token, flags 0 and no hash. csc on .NET has no
   default reference (no references -> CS0518); the Framework-hosted csc.exe defaults to `mscorlib`.
5. **Proposed algorithm** (section 5): Roslyn's rules, plus exact signature matching that includes the return type,
   generic arity and calling convention, the definition's custom modifiers copied into the MemberRef, transitive
   binding of every type in a signature, assembly-qualified names for `System.Type` attribute arguments, identities
   copied from the defining assembly's `Assembly` row (never from a facade's AssemblyRef, which in the .NET 8 pack
   says version 0.0.0.0), *required* versus *optional-with-fallback* requests for missing members, and consistency
   checks for toolchain-built input assemblies (P2-C).
6. **No references** (section 5.10): recommended answer: a small built-in table of the **`[mscorlib]`-safe**
   members the default codegen uses (verified on Framework 4.8 and .NET 6/8/10 by following the runtimes' own
   forwarders), all bound to `mscorlib` 4.0.0.0; used only when no reference is given; anything outside it is an
   error that asks for `--reference`. This keeps Design.md goal 1 and Phase-1 behaviour (`clang foo.c -o foo.dll` runs
   everywhere) and is Roslyn-like in every case where references are given.
7. **Spelling** (section 6): msil-ld `--reference=<path>` / `--reference <path>` (double dash only, lld's `EEq`
   form; a file, or a directory meaning every assembly in it). No short form: `-r` is `--relocatable` in lld/ELF,
   wasm and MachO and is forwarded to the linker by clang, `-R` is `--rpath` in lld/ELF and `-R<remark>` in clang.
   The clang driver needs no new option in P2-B: positional `.dll` inputs already reach the linker (clang classifies
   unknown extensions as `TY_Object`, `clang/lib/Driver/Driver.cpp:3253-3265`) and `-Wl,--reference=...` works.
8. **A metadata reader in LLVM is needed** (section 7): for reference assemblies (this task), for P2-C's
   toolchain-built assemblies, and for objects if P2-A picks an assembly-shaped object. Scope for binding: 13 tables
   read (Assembly, AssemblyRef, TypeDef, TypeRef, NestedClass, ExportedType, MethodDef, Field, TypeSpec, MemberRef,
   CustomAttribute, GenericParam optional, Module for row sizes) plus #Strings and #Blob, but the column schema of all
   45 tables for row sizes. About 1,800-2,500 lines on top of `COFFObjectFile` (which already exposes the CLR runtime
   header data directory). The table schema, coded indexes, element types, flags and compressed-integer codecs should
   live in one shared `BinaryFormat` header and `.def`, used by the reader, the MC/PE writers and later the Portable
   PDB writer, as `BinaryFormat/Wasm.h` serves MC, Object and lld/wasm.

## 1. Inventory

### 1.1 Phase-1 BCL references in the backend

Found with `grep -rn 'mscorlib\|System\.' llvm/lib/Target/MSIL` (no BCL reference exists elsewhere in the compiler;
the `msil/` tree only has test inputs and harness tools that reference `mscorlib`).

| # | IL text emitted | Where | Why |
|---|---|---|---|
| A | `.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }` | `MCTargetDesc/MSILTargetStreamer.cpp:57-62` (`MSILTargetAsmStreamer::emitAssemblyRefs`), called from `MSILAsmPrinter.cpp:484` | D10: the only AssemblyRef |
| B | `.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType` | `MSILTargetStreamer.cpp:151` (`emitDataImage`), called from `MSILAsmPrinter.cpp:821` | D12: the value type that types the RVA field `'<msil>image'` holding the initialized data image |
| C | `call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)` | constant `AllocHGlobal`, `MSILAsmPrinter.cpp:171-173`; used by `emitModuleInitializer` (`:848`, the call at `:911`) and, as literal text, by `emitEntryPoint` (`:1007`, text at `:1074-1076`) | allocate the data segment in `<Module>::.cctor` (D12); allocate `argv` in `'<msil>Main'` (D27) |
| D | `call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)` | `MSILAsmPrinter.cpp:1087-1089` | each `argv[i]` (D27) |
| E | `call string[] [mscorlib]System.Environment::GetCommandLineArgs()` | `MSILAsmPrinter.cpp:1058-1060` | `argc`/`argv` (D27) |
| F | `conv.i8` / `dup` / `call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)` | AsmString of `defm MEMMOVE`, `MSILInstrMemory.td:148-151`; the node `MSILISD::MEMMOVE` is built in `MSILSelectionDAGInfo.cpp:83-99` | memmove and variable-length memcpy (D15) |
| G | `call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()` | AsmString of `defm MEMORY_BARRIER`, `MSILInstrMemory.td:165-167` (opcode `0x28`, CIL `call`), pattern for `atomic_fence` at `:177` | every non-singlethread fence (D36) |

Implicit core-library dependencies with no TypeRef row: the element types in signatures (`void`, `int32`,
`native int`, `float32`, `float64`, `string`, `string[]` of `'<msil>Main'`, `void*` of `MemoryCopy`). The runtime
resolves them to its core library whatever the AssemblyRefs say. P/Invokes (ImplMap + ModuleRef rows) are not BCL.

Representation today: `MSILMemberRefExpr::BCLMember` (`MCTargetDesc/MSILMCExpr.h:52-54`, "printed verbatim") and
`MSILMemberRefExpr::createBCL` (`MSILMCExpr.cpp:45`) exist, and `MSILInstPrinter.cpp:325` accepts a `BCLMember`
operand for `call`, but nothing creates one: rows F and G carry the call in the TableGen AsmString, rows C-E are
lines of `SynthesizedBody` (`MSILAsmPrinter.cpp:208-`), rows A-B are streamer text. **Consequence for P2-A:** the
CIL binary encoder cannot encode a text call; F and G need a BCL-member operand (or a per-opcode BCL id), and the
synthesized `.cctor`/`Main` bodies need a structured instruction list with BCL operands. A single
`.def` list of BCL members (namespace, type, member, signature, whether it is in the default table of 5.10) would
serve the text printer, the encoder, msil-ld's built-in table and tests.

### 1.2 What a compiled Phase-1 program contains

`bcl-binding-files/runtime/p1prog.c` uses every row above (a global data image with a relocation, `argv`, a
variable-length `memmove`, a fence and `puts`). Compiled with the shared build (`clang
--target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -S`), assembled with the Framework ilasm, its BCL metadata is
(dumped with the resolver's `refs` mode):

```text
AssemblyRef 1: mscorlib 4.0.0.0 pkt=b77a5c561934e089 flags=0
TypeRef System.Buffer, System.Threading.Interlocked, System.ValueType,
        System.Runtime.InteropServices.Marshal, System.Environment   -> [mscorlib]
MemberRef System.Buffer::MemoryCopy                       blob=00 04 01 0F 01 0F 01 0A 0A
MemberRef System.Threading.Interlocked::MemoryBarrier     blob=00 00 01
MemberRef System.Runtime.InteropServices.Marshal::AllocHGlobal       blob=00 01 18 18
MemberRef System.Environment::GetCommandLineArgs          blob=00 00 1D 0E
MemberRef System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi blob=00 01 18 0E
```

Rebound as msil-ld would bind it against the .NET 8 pack (`runtime/rebind.py`), the MemberRef signature blobs are
byte-identical; only the AssemblyRef rows (`System.Runtime`, `System.Runtime.InteropServices`, `System.Threading`,
all 8.0.0.0, `b03f5f7f11d50a3a`) and the ResolutionScope of the 5 TypeRefs change. For the Phase-1 set, binding is
therefore exactly "choose an AssemblyRef per TypeRef"; no IL byte and no signature changes.

### 1.3 Members Phase 3 (and later) is expected to use

From Design.md 3.2, 4.1-4.7, 5.3, 6, 7.1, plan P3-A/P3-B and `inputs.md`. "mscorlib-safe" means: a reference scoped to
`[mscorlib]` resolves on Framework 4.8 and on .NET 6, 8 and 10, measured by following each runtime's own forwarders
(section 2.4). The defining assembly per reference set is in section 2.2.

| Purpose (Design) | Members (ILAsm signatures) | mscorlib-safe |
|---|---|---|
| shadow stack, TLS block (4.2, 4.4, 6) | `void* NativeMemory::Alloc(native uint)`, `AllocZeroed(native uint)`, `AlignedAlloc(native uint, native uint)`, `Free(void*)`, `AlignedFree(void*)`; `void Marshal::FreeHGlobal(native int)`; `ThreadStaticAttribute::.ctor()` | NativeMemory no; FreeHGlobal, ThreadStatic yes |
| memcpy/memset alternatives (isel.md) | `NativeMemory::Copy(void*, void*, native uint)`, `Fill(void*, native uint, uint8)`, `Clear(void*, native uint)` (.NET 7+; absent in the .NET 6 pack) | no |
| atexit, global dtors (4.1) | `AppDomain AppDomain::get_CurrentDomain()`, `void AppDomain::add_ProcessExit(EventHandler)`, `EventHandler::.ctor(object, native int)`; `Environment::Exit(int32)` | yes |
| argv (D27) | `string[] Environment::GetCommandLineArgs()` | yes |
| native-callable function pointers (4.5) | `native int Marshal::GetFunctionPointerForDelegate(Delegate)` and `GetFunctionPointerForDelegate<T>(!!0)` (generic arity 1); `System.MulticastDelegate` (delegate base); `UnmanagedFunctionPointerAttribute::.ctor(CallingConvention)`; `CallConvCdecl`; `UnmanagedCallersOnlyAttribute::.ctor()` with field `Type[] CallConvs` | all yes except UnmanagedCallersOnly |
| P/Invoke switches (5.3, 7.1) | `DisableRuntimeMarshallingAttribute::.ctor()` (.NET 7+; absent in .NET 6), `SuppressGCTransitionAttribute::.ctor()`, `NativeLibrary::Load(string)`, `GetExport(native int, string)`; `Marshal::GetLastPInvokeError()` / `GetLastWin32Error()` (errno, *speculative*) | no (GetLastWin32Error yes) |
| atomics (P3-B) | `Interlocked::CompareExchange`/`Exchange` on `int32&`, `int64&`, `native int&`; `Add(int32&/int64&, ...)`; `MemoryBarrier()`; `Volatile::Read/Write` on `int32&`, `int64&`, `native int&` | yes |
| atomics on newer runtimes | `CompareExchange(uint32&, ...)`, `And`, `Or`, `MemoryBarrierProcessWide()` (.NET Core 3+/5+); `CompareExchange(uint8&/int16&, ...)`, `Exchange(uint8&, uint8)` (.NET 9+: only in the 10.0 pack here) | no |
| `pause` (inputs.md) | `Thread::SpinWait(int32)`; `X86Base::Pause()` (.NET 8+; absent in the 6.0 pack) | SpinWait yes |
| f16 (7.1) | `System.Half`, `float32 Half::op_Explicit(Half)`, `Half Half::op_Explicit(float32)` and `(float64)` (overloads that differ only in the return type), `BitConverter::HalfToInt16Bits`/`Int16BitsToHalf` | no |
| i128 division (7.1) | `System.Int128`/`UInt128` (.NET 7+; absent in 6.0), `.ctor(uint64, uint64)`, `op_Division`, `op_Modulus`, `op_RightShift(Int128, int32)`, `op_Explicit` to `uint64`/`float64` | no |
| math switches (7.1, D16) | `Math::FusedMultiplyAdd`, `CopySign`, `ScaleB`, `Log2`, `Cbrt` (Core 2.1+/3.0+), `MathF::*`; `Math::Sqrt`, `Floor`, `Round(float64, MidpointRounding)`, `Min`, `Abs`, `IEEERemainder`; `BitConverter::SingleToInt32Bits`, `Int32BitsToSingle`, `DoubleToInt64Bits` | old Math members and `DoubleToInt64Bits` yes; the rest no |
| bit operations (isel.md) | `BitOperations::PopCount/LeadingZeroCount/TrailingZeroCount/RotateLeft`, `BinaryPrimitives::ReverseEndianness` | no |
| setjmp/longjmp (4.6, inputs.md) | `Exception::.ctor()`, `IDictionary Exception::get_Data()`, `IDictionary::set_Item(object, object)` | yes |
| `llvm.debugtrap` (isel.md) | `Debugger::Break()` | yes |
| SIMD, arch intrinsics (3.2, phase 4) | `Sse42::get_IsSupported()`, `Sse42::Crc32(uint32, uint32)`, nested `Sse42/X64::Crc32(uint64, uint64)`, `Ssse3::Shuffle(Vector128<uint8>, Vector128<uint8>)`, `Arm.Crc32::ComputeCrc32`, `Vector128<T>` (metadata name ``Vector128`1``), generic `Vector128::Create<T>(!!0)` | no |

Binder requirements this list implies: nested types (`Sse42/X64`), generic type instances in signatures (TypeSpec),
generic methods (MethodSpec), overloads distinguished by return type (`Half::op_Explicit`), byref parameters,
attribute constructors and attribute named fields with `System.Type[]` values (section 4.4), and value types used in
locals and signatures (`valuetype System.Int128`), not only call targets.

## 2. Where each member is defined

### 2.1 Reference sets examined

Measured with the resolver's `info` and `stats` modes (`bcl-binding-files/stats.txt`).

| Set | Path | Assemblies | Metadata | Core library by Roslyn's rule | Facades in the set |
|---|---|---|---|---|---|
| net48 | `C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8` + `Facades` | 235 (127 define types, 108 only forward) | 23,290 KiB (all of the Framework; `mscorlib` alone 1,582 KiB) | `mscorlib` 4.0.0.0 `b77a5c561934e089`, 0 AssemblyRefs, 2,993 TypeDefs | `netstandard` 2.0.0.0 (2,417 forwarders), `System.Runtime` 4.1.2.0 (279), ... -> `mscorlib` |
| ns2.0 | `D:\packages\NuGet\netstandard.library\2.0.3\build\netstandard2.0\ref` | 113 (1 defines) | 1,562 KiB | `netstandard` 2.0.0.0 `cc7b13ffcd2ddd51`, 2,418 TypeDefs | `mscorlib` 4.0.0.0, `System.Runtime` 4.1.2.0 -> `netstandard` |
| ns2.1 | `C:\Program Files\dotnet\packs\NETStandard.Library.Ref\2.1.0\ref\netstandard2.1` | 121 (1 defines) | 1,730 KiB | `netstandard` 2.1.0.0, 2,595 TypeDefs | as ns2.0 |
| net6 | `C:\Program Files\dotnet\packs\Microsoft.NETCore.App.Ref\6.0.36\ref\net6.0` | 159 (109 define) | 2,685 KiB | `System.Runtime` 6.0.0.0 `b03f5f7f11d50a3a`, 799 TypeDefs | `mscorlib` 4.0.0.0 (1,255 forwarders), `netstandard` 2.1.0.0 (2,600) |
| net8 | `...\Microsoft.NETCore.App.Ref\8.0.31\ref\net8.0` | 163 (104 define) | 3,226 KiB; 3,963 TypeDefs, 7,011 ExportedTypes, 47,146 MethodDefs | `System.Runtime` 8.0.0.0, 891 TypeDefs | `mscorlib` (1,256), `netstandard` 2.1.0.0 (2,604) |
| net10 | `...\Microsoft.NETCore.App.Ref\10.0.12\ref\net10.0` | 167 (107 define) | 3,575 KiB | `System.Runtime` 10.0.0.0, 936 TypeDefs | `mscorlib` (1,260), `netstandard` (2,611) |

The reference assemblies checked (net8 `System.Runtime`, `System.Runtime.InteropServices`, `mscorlib`; net48
`mscorlib` and `Facades\System.Runtime`; ns2.0 `netstandard`) carry `[assembly: ReferenceAssembly]`; the
shared-framework implementation assemblies (`C:\Program Files\dotnet\shared\Microsoft.NETCore.App\8.0.31`) and the
Framework's own `mscorlib` do not, and with the shared framework as references the Roslyn rule picks
`System.Private.CoreLib` 8.0.0.0 (`7cec85d7bea7798e`) as the core library (resolver `refasm` and `info` modes).

### 2.2 Defining assembly per member

Full matrix (112 queries x 6 sets): [bcl-binding-files/matrix.md](bcl-binding-files/matrix.md); per-query detail with
forwarder lists: `resolve.txt`. Condensed:

| Members | net48 | ns2.0 | ns2.1 | net6 | net8 | net10 |
|---|---|---|---|---|---|---|
| `Object`, `ValueType`, `Environment`, `Buffer::MemoryCopy`, `Exception`, `AppDomain`, `EventHandler`, `ThreadStaticAttribute`, `MulticastDelegate`, `CallConvCdecl`, old `Math`/`BitConverter` members, `Debugger::Break` | mscorlib 4.0.0.0 | netstandard 2.0.0.0 | netstandard 2.1.0.0 | System.Runtime 6.0.0.0 | System.Runtime 8.0.0.0 | System.Runtime 10.0.0.0 |
| `Marshal::*` (AllocHGlobal, StringToHGlobalAnsi, FreeHGlobal, GetFunctionPointerForDelegate, GetLastWin32Error), `UnmanagedFunctionPointerAttribute` | mscorlib | netstandard | netstandard | System.Runtime.InteropServices 6.0.0.0 | ...InteropServices 8.0.0.0 | ...InteropServices 10.0.0.0 |
| `Interlocked::*` (int32/int64/native int), `Volatile::*` | mscorlib | netstandard | netstandard | System.Threading 6.0.0.0 | System.Threading 8.0.0.0 | System.Threading 10.0.0.0 |
| `Thread::SpinWait` | mscorlib | netstandard | netstandard | System.Threading.Thread 6.0.0.0 | ... 8.0.0.0 | ... 10.0.0.0 |
| `NativeMemory` Alloc/AllocZeroed/AlignedAlloc/Free/AlignedFree, `NativeLibrary`, `UnmanagedCallersOnlyAttribute` (+`CallConvs`) | — | — | — | System.Runtime.InteropServices 6.0.0.0 | 8.0.0.0 | 10.0.0.0 |
| `NativeMemory` Copy/Fill/Clear | — | — | — | type there, **no member** | System.Runtime.InteropServices 8.0.0.0 | 10.0.0.0 |
| `SuppressGCTransitionAttribute` | — | — | — | System.Runtime 6.0.0.0 | 8.0.0.0 | 10.0.0.0 |
| `DisableRuntimeMarshallingAttribute` | — | — | — | — | System.Runtime 8.0.0.0 | 10.0.0.0 |
| `Marshal::GetLastPInvokeError`; `Interlocked` uint32 CAS, `And`, `Or`; `Math::FusedMultiplyAdd/CopySign/ScaleB/Log2`; `BitConverter` Half bits; `GC::AllocateArray<T>` | type there, no member | no member | no member (`MemoryBarrierProcessWide` present in ns2.1) | System.Runtime(.InteropServices/.Threading) 6.0.0.0 | 8.0.0.0 | 10.0.0.0 |
| `Interlocked` uint8/int16 CAS, uint8 Exchange | no member | no member | no member | no member | no member | System.Threading 10.0.0.0 |
| `Math::Cbrt`, `MathF::Sqrt`, `BitConverter::SingleToInt32Bits`, `Span<T>` | no | no | netstandard 2.1.0.0 | System.Runtime 6.0.0.0 | 8.0.0.0 | 10.0.0.0 |
| `MathF::FusedMultiplyAdd` | — | — | no member | System.Runtime 6.0.0.0 | 8.0.0.0 | 10.0.0.0 |
| `System.Half` and its operators | — | — | — | System.Runtime 6.0.0.0 | 8.0.0.0 | 10.0.0.0 |
| `Int128`, `UInt128` | — | — | — | — | System.Runtime 8.0.0.0 | 10.0.0.0 |
| `BitOperations::*` | — | — | — | System.Runtime 6.0.0.0 | 8.0.0.0 | 10.0.0.0 |
| `BinaryPrimitives::ReverseEndianness` | — | — | netstandard 2.1.0.0 | System.Memory 6.0.0.0 | System.Memory 8.0.0.0 | System.Memory 10.0.0.0 |
| `X86.Sse42` (+ nested `X64`), `Ssse3`, `Arm.Crc32`, `Vector128<T>` | — | — | — | System.Runtime.Intrinsics 6.0.0.0 | 8.0.0.0 | 10.0.0.0 |
| `X86Base::Pause`, `Vector128::Create<T>` | — | — | — | no member | System.Runtime.Intrinsics 8.0.0.0 | 10.0.0.0 |
| `RuntimeInformation::IsOSPlatform` | mscorlib | netstandard | netstandard | **System.Runtime.InteropServices.RuntimeInformation 6.0.0.0** | **System.Runtime 8.0.0.0** | System.Runtime 10.0.0.0 |

Public key tokens of the defining assemblies (from their `Assembly` rows, SHA-1 of the public key, last 8 bytes
reversed): `mscorlib` `b77a5c561934e089` (the 16-byte ECMA key); `System.Runtime`, `System.Runtime.InteropServices`,
`System.Threading`, `System.Threading.Thread`, `...RuntimeInformation` `b03f5f7f11d50a3a`; `netstandard`,
`System.Memory`, `System.Runtime.Intrinsics` `cc7b13ffcd2ddd51`. None of the bound members carries `[Obsolete]` or
`[RequiresPreviewFeatures]` in the 8.0/10.0 packs (resolver output); several carry `[CLSCompliant(false)]`, which is
irrelevant to binding.

### 2.3 Forwarders: reference facades versus runtime facades

Reference facades forward to the reference pack's contract assemblies: in the .NET 8 pack `mscorlib` forwards
`Marshal` to `System.Runtime.InteropServices`, `Interlocked` to `System.Threading`, the rest of the Phase-1 types to
`System.Runtime` (`resolve.txt`). **The AssemblyRef rows those forwarders point through carry version 0.0.0.0**
(`mscorlib.dll` of the 8.0.31 pack: `System.Runtime 0.0.0.0`, `System.Runtime.InteropServices 0.0.0.0`,
`System.Threading 0.0.0.0`), while the pack's `netstandard.dll` forwarders say 8.0.0.0. A linker must therefore never
copy a forwarder's AssemblyRef; it takes the identity from the destination's own `Assembly` row.

Runtime facades follow a different path (resolver `trace` mode over the runtime directories;
`trace-core.txt`, `trace-netfx.txt`):

| Start | .NET 8.0.31 / 10.0.12 runtime | .NET Framework 4.8 (Framework64 + GAC facades) |
|---|---|---|
| `[mscorlib]` | `mscorlib` 4.0.0.0 -> `System.Private.CoreLib` (one hop, for `System.Object` and all 6 Phase-1 entities) | `mscorlib` defines everything |
| `[netstandard]` | `netstandard` 2.1.0.0 -> `System.Runtime(.InteropServices/.Threading)` -> `System.Private.CoreLib` | `netstandard` 2.0.0.0 -> `mscorlib` |
| `[System.Runtime]` | Object, ValueType, Environment, Buffer resolve; **Marshal, Interlocked: TypeLoadException** | `System.Runtime` 4.0.0.0 forwards Object, ValueType, Buffer only |
| `[System.Runtime.InteropServices]` | Marshal resolves; nothing else | 4.0.0.0 facade forwards Marshal |

### 2.4 The `[mscorlib]`-safe surface

[bcl-binding-files/mscorlib-safe.md](bcl-binding-files/mscorlib-safe.md) (from `tool/mscorlib_safe.py`) follows
`[mscorlib]` through each runtime's own forwarders for all 112 queries. Safe on all four runtimes: all of Phase 1,
`ThreadStaticAttribute`, `Marshal::FreeHGlobal`, `GetFunctionPointerForDelegate` (both), `GetLastWin32Error`,
`AppDomain::get_CurrentDomain`/`add_ProcessExit`, `EventHandler::.ctor`, `Environment::Exit`,
`UnmanagedFunctionPointerAttribute`, `CallConvCdecl`, `MulticastDelegate`, `Interlocked`
`CompareExchange`/`Exchange`/`Add` on int32/int64/native int, `Volatile` Read/Write, `Thread::SpinWait`, the old
`Math` members, `BitConverter::DoubleToInt64Bits`, `Exception::.ctor`/`get_Data`, `IDictionary::set_Item`,
`Debugger::Break`, `RuntimeInformation::IsOSPlatform`.

Two traps that the table makes visible:
- The runtime `mscorlib` facade forwards *types*, so `[mscorlib]System.Math::FusedMultiplyAdd` resolves on .NET 6/8/10
  (the type is forwarded and the member exists there) but not on Framework; "the type is forwarded" is not "the
  member exists everywhere".
- The runtime facades of .NET 8/10 forward more than older ones: `MathF`, `BinaryPrimitives` and `Span<T>` resolve
  through `[mscorlib]` on .NET 8 and 10 but not on .NET 6 or Framework; `NativeMemory`, `BitOperations`, `Half`,
  `Int128`, the intrinsics and the new attributes never do (matches recon `isel.md` 5.9).

### 2.5 The resolver tool

`bcl-binding-files/tool/` (`Program.cs`, about 600 lines, `bclresolve.csproj`, `queries.txt`). Build:
`dotnet build -c Release -o bin`. Modes:

- `info NAME=PATH[;PATH]...`: per set, assemblies that define or forward `System.Object`, their AssemblyRef counts,
  and the core library by Roslyn's rule. A directory adds every `*.dll` in it; non-metadata files are skipped.
- `resolve --queries F <sets>` / `matrix --queries F <sets>`: for each query of the form
  `[<ret>] Namespace.Type[/Nested]::Member[``N](<params>)` (or a field, or a bare type), the defining assembly, its
  version and token, the references that forward the type there, the chain when only forwarders are present
  (`FORWARDED-TO-MISSING(...)` when the destination is not in the set), ambiguity, the matched signature
  (return type included; base types searched), and `[Obsolete]`/`[RequiresPreviewFeatures]`/platform attributes.
- `trace --queries F --from ASM --dir DIR...`: follows the runtime binder's forwarder chain from one assembly through
  implementation assemblies and checks the member at the end.
- `refs <sets>`: AssemblyRef rows, TypeRef scopes and MemberRef signature blobs of compiled assemblies.
- `stats`, `refasm`, `modscan`: sizes and row counts, `[ReferenceAssembly]` detection, members with custom
  modifiers.

The resolution logic in `RefSet.Resolve`/`FindMember` is the algorithm of section 5 minus emission, so it can serve as
an executable model for msil-ld's tests (expected outputs for synthetic reference sets).

## 3. Runtime experiments

### 3.1 AssemblyRef variants

`runtime/gen.py` writes 16 variants of one program that calls rows C-G of 1.1 and defines a value type extending
`System.ValueType` (row B); only the scope of each member changes. Assembled with the Framework ilasm
(`-exe -pe64 -x64`), run as the `.exe` on Framework 4.8 and with `dotnet exec --runtimeconfig` on .NET 8.0.31 and
10.0.12 (`runtime/run.py`), and in WSL Ubuntu on .NET 8 and 10 (`runtime/wsl-run.sh`). 42 = success.
Full tables: `runtime/results.md`, `runtime/results-wsl.md`.

| Variant | Framework 4.8 | .NET 8 (Win, WSL) | .NET 10 (Win, WSL) |
|---|---|---|---|
| v01 all `[mscorlib 4.0.0.0 b77a...]` (Phase 1) | 42 | 42 | 42 |
| v02 bound per the net8 pack (`System.Runtime`/`...InteropServices`/`System.Threading` 8.0.0.0) | FileNotFoundException `System.Runtime.InteropServices, Version=8.0.0.0` | 42 | 42 |
| v03 bound per the net10 pack (10.0.0.0) | FileNotFoundException | FileNotFoundException `... Version=10.0.0.0` | 42 |
| v04 all `[netstandard 2.0.0.0 cc7b...]` | 42 | 42 | 42 |
| v05 `Marshal` scoped to `System.Runtime` 8.0.0.0 (wrong assembly) | FileNotFoundException | TypeLoadException `Could not load type 'System.Runtime.InteropServices.Marshal' from assembly 'System.Runtime, Version=8.0.0.0'` | same |
| v06 `mscorlib` with a wrong token | 42 | 42 | 42 |
| v07 `mscorlib` without a token | 42 | 42 | 42 |
| v08 `[mscorlib]NativeMemory::Alloc` | TypeLoadException | TypeLoadException (facade has no forwarder) | same |
| v09 Framework-era facades 4.0.0.0 (`System.Runtime`, `...InteropServices`, `...Extensions`, `System.Threading`) | 42 | 42 | 42 |
| v10 `mscorlib` 5.0.0.0 | 42 | FileNotFoundException `mscorlib, Version=5.0.0.0` | same |
| v11 net8 binding + `NativeMemory::Alloc` | FileNotFoundException | 42 | 42 |
| v12 `mscorlib` for all + `NativeMemory` from `...InteropServices` 8.0.0.0 | FileNotFoundException | 42 | 42 |
| v13 `System.Runtime.InteropServices` 8.0.0.0 with a wrong token | FileNotFoundException (token in the message) | 42 | 42 |
| v14 `System.Runtime.InteropServices` 4.0.0.0 with a wrong token | FileNotFoundException | 42 | 42 |
| v15 all `[netstandard 2.1.0.0]` | FileNotFoundException `netstandard, Version=2.1.0.0` | 42 | 42 |
| v16 net48-pack facade versions (`System.Runtime` 4.1.2.0, `System.Threading` 4.0.11.0, ...) | 42 | 42 | 42 |

Unhandled exceptions exit with `0xE0434352` on Windows (both runtimes) and 134 (SIGABRT) in WSL.

### 3.2 The real Phase-1 program

`runtime/p1prog-runs.txt`: `p1prog.exe` (Phase-1 output, `[mscorlib]`) prints `world` and exits 42 on Framework 4.8,
.NET 8 and .NET 10. `p1prog_net8.exe` (same IL, rebound to the net8 pack by `rebind.py`) exits 42 on .NET 8 and 10
and fails on Framework 4.8 before `main` with `Unhandled Exception: System.IO.FileNotFoundException: Could not load
file or assembly 'System.Runtime, Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a'` (exit
`0xE0434352`): loading `<Module>` needs `'<msil>image_t'`, whose base type is now `[System.Runtime]System.ValueType`.

### 3.3 Why: the binders

- **CoreCLR** (.NET 6+) binds framework assemblies from the host's trusted-platform-assembly list: the name must
  match, the available version must be at least the requested one (v03, v10), and the public key token is not
  checked (v06, v13, v14). The shared framework ships `mscorlib.dll` 4.0.0.0 and `netstandard.dll` 2.1.0.0 as
  implementation facades, so `[mscorlib]` (4.0.0.0) and `[netstandard]` (2.0 or 2.1) references bind, and their
  ExportedType forwarders lead to `System.Private.CoreLib` (2.3). A facade forwards only what its contract contains
  (v05, v08).
- **.NET Framework** binds strong-named references against the GAC: the token must match (v13, v14) except for
  `mscorlib`, which always binds to the running `mscorlib` (v06, v07, v10). It has `netstandard` 2.0.0.0 and the
  4.x facades (v04, v09; 4.8 also binds the 4.1.x facade versions of the net48 reference pack, v16), and nothing of
  version 2.1 or 8.0 (v02, v15).
- **Lazy failure:** the CLR loads an assembly when it compiles the first method that needs it. `runtime/v17-lightup.il`
  references `NativeMemory` from `System.Runtime.InteropServices` 8.0.0.0 in a method called only with a command-line
  argument: on Framework 4.8 it exits 42 without the argument and fails with FileNotFoundException with it; .NET 8
  runs both paths. So one assembly *can* light up modern APIs behind a run-time check, but that needs two reference
  sets at once, which section 5 does not propose (it contradicts Roslyn's model and the user's "capability from the
  references").

### 3.4 Custom modifiers are part of the binding

`runtime/m-with.il` and `m-without.il` call `Utf16StringMarshaller::GetPinnableReference(string)`, whose definition
returns `char& modreq([System.Runtime]System.Runtime.InteropServices.InAttribute)` (one of 7 methods of public types
with a custom modifier in the net8 `System.Runtime` (5) and `System.Runtime.InteropServices` (2); none in
`System.Threading` or `System.Runtime.Intrinsics`; found with `modscan`). With the modreq the call returns `'*'` (exit 42) on .NET 8; without it: `System.MissingMethodException:
Method not found: 'Char ByRef ...GetPinnableReference(System.String)'`. None of the members of 1.3 has a modifier
today, but the binder must copy the definition's signature, not synthesize one.

### 3.5 Verification tools and non-mscorlib bindings

`ilverify` (dotnet-ilverify 10.0.12, `-r` the 10.0.12 shared framework) reports the same allowed codes for
`p1prog.dll` and `p1prog_net8.exe`. **PEVerify cannot verify the net8-bound assembly**: it stops with
`[HRESULT 0x80070002] - The system cannot find the file specified.` after nine
`.NET Framework Internal error: 0x8013129d` metadata messages, and does no IL verification. The D21 policy's PEVerify
step (and the neutralized second pass) therefore only works for outputs bound to Framework-loadable references
(mscorlib, netstandard 2.0, 4.x facades). A gate leg that links with a .NET 8 pack must run PEVerify on a
mscorlib-bound build of the same program or skip PEVerify for that leg.

## 4. How Roslyn does it

### 4.1 Source (dotnet/roslyn `main`, read 2026-10-20)

- `CSharpCommandLineParser.cs`: `/r:` and `/reference:` (with `alias=path`), `/link:`/`/l:` (embed interop types),
  `/lib:` (search directories, not references), `/addmodule:` (netmodules). `/nostdlib[+|-]`; when it is off and an
  SDK directory is known, the parser inserts `Path.Combine(sdkDirectory, "mscorlib.dll")` as the first reference
  (around lines 520-530 at the time of reading; line numbers from a web view, *approximate*). `/sdkpath:` sets the
  directory, `/nosdkpath` clears it.
- `CommonReferenceManager.Binding.cs`, `IndexOfCorLibrary` (approximately lines 954-1025): a reference is a core
  library candidate when it is not linked (`/link`), has `AssemblyReferences.Length == 0`, contains no NoPIA local
  types, is not superseded by a higher version (scripting only), and `DeclaresTheObjectClass`. Exactly one candidate
  is the core library; **two or more mean none** ("If there is an ambiguous match, pretend there is no COR library");
  a compilation with no references is its own core library.

### 4.2 Experiments

`roslyn/run-roslyn.sh`, log `roslyn/roslyn-log.txt`, emitted rows `roslyn/refs.txt`. csc is
`C:\Program Files\dotnet\sdk\10.0.402\Roslyn\bincore\csc.dll` with `-noconfig`; `p1.cs` calls the Phase-1 members
from C#, `fma.cs` calls `Math.FusedMultiplyAdd`.

| Run | References | Result |
|---|---|---|
| r1 | all 163 files of the net8 pack (`-nostdlib`) | compiles; AssemblyRefs `System.Runtime`, `System.Runtime.InteropServices`, `System.Threading` 8.0.0.0 `b03f...`; **not** `mscorlib` or `netstandard`, though both were referenced |
| r2 | net48 dir + `Facades` (minus two non-metadata DLLs, which csc rejects with CS0009/CS1509) | compiles; only `mscorlib` 4.0.0.0 |
| r3 | netstandard 2.0 set | compiles; only `netstandard` 2.0.0.0 |
| r4 | net8 `mscorlib.dll` facade only | CS0518 `Predefined type 'System.Object' is not defined or imported` (no core library) |
| r5 | net8 `System.Runtime.dll` only | CS0234 `'Marshal' does not exist in the namespace 'System.Runtime.InteropServices' (are you missing an assembly reference?)` |
| r6 | net48 `mscorlib` + net8 `System.Runtime`, `...InteropServices`, `System.Threading` | CS0518 everywhere: two core-library candidates = none |
| r7 | net48 set, `Math.FusedMultiplyAdd` | CS0117 `'Math' does not contain a definition for 'FusedMultiplyAdd'` |
| r8 | none (`csc.dll` on .NET, no `-nostdlib`) | CS0518: no default reference on .NET |
| r9 | none, `-nostdlib` | CS0518 |
| r10 | net8 + net10 `System.Runtime` | CS0518 (two candidates) |
| r11 | none, VS 2026 `csc.exe` (Framework-hosted) | compiles; `mscorlib` 4.0.0.0 (by the parser code, the `mscorlib.dll` of its SDK directory, i.e. the Framework's) |
| r13 | net48 `mscorlib` + net8 `System.Runtime.InteropServices` | CS0433 `The type 'Marshal' exists in both 'System.Runtime.InteropServices, Version=8.0.0.0 ...' and 'mscorlib, Version=4.0.0.0 ...'` |
| r14 / r14b | net8 pack + net48 `mscorlib` (same identity as the pack's facade), in both orders | no diagnostic about the duplicate identity; csc silently kept one file, the later one in both orders (r14 failed with CS0518, r14b compiled against the facade) |
| r15 | net8 pack + net10 `System.Runtime.InteropServices` | CS0433 between the 8.0.0.0 and 10.0.0.0 definitions: no version unification |
| r17 | net8 pack, `Math.FusedMultiplyAdd` | compiles; `System.Runtime` 8.0.0.0 |
| r18 | net8 `System.Runtime` + net8 `mscorlib` facade | CS1069 `The type name 'Marshal' could not be found in the namespace 'System.Runtime.InteropServices'. This type has been forwarded to assembly 'System.Runtime.InteropServices, Version=0.0.0.0, ...' Consider adding a reference to that assembly.` (note the facade's 0.0.0.0) |

### 4.3 What Roslyn emits

AssemblyRef rows only for assemblies that define a used type, in first-use order, with `PublicKeyOrToken` = the
8-byte token (flag `PublicKey` clear), `Flags` 0, empty `HashValue`, culture neutral (`refs.txt`). TypeRefs are scoped
to the defining assembly; nested types are scoped to their enclosing TypeRef
(`DebuggableAttribute/DebuggingModes`). Roslyn adds assembly attributes msil-ld does not need
(`CompilationRelaxations`, `RuntimeCompatibility`, `Debuggable`, and for `/unsafe` `UnverifiableCode` and
`SecurityPermission`); `TargetFrameworkAttribute` comes from the SDK's generated source, not from csc. Whether the
absence of `TargetFrameworkAttribute` changes Framework compatibility quirks for an `.exe` is *unverified*; Phase 1
outputs have none and pass the Framework gate.

### 4.4 Type-valued attribute arguments

`roslyn/uco.cs` (`[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]`, net8 pack) produces the
blob in `roslyn/uco-blob.txt`: the type is serialized as the string
`System.Runtime.CompilerServices.CallConvCdecl, System.Runtime, Version=8.0.0.0, Culture=neutral,
PublicKeyToken=b03f5f7f11d50a3a`. Custom attribute blobs are therefore a second binding site: the binder writes the
assembly-qualified name of the bound definition into the blob (Phase 3's UnmanagedCallersOnly switch, if it sets
`CallConvs`).

### 4.5 Roslyn diagnostics and their msil-ld analogues

| Roslyn | Condition | msil-ld (proposed) |
|---|---|---|
| CS0518 | no unique core library | error naming the candidates (or "none: only facades given") |
| CS0433 | type defined in two references | error naming both identities and paths |
| CS1069 / CS1070 | type only forwarded, destination not referenced | error naming the forwarder and the destination (with the destination's version taken from the forwarding assembly, as Roslyn does, or "any version") |
| CS0234 / CS0246 | type not found | error naming the type and the reference set |
| CS0117 | member not found on the type | required: error listing the overloads present; optional: fallback (5.8) |
| CS0656 "Missing compiler required member" (*not run here*) | a well-known member the compiler needs is missing | the same as CS0117 for the members of the default codegen |
| CS1705 (*not run here*) | a referenced assembly references a higher version than given | for toolchain-built input assemblies (P2-C), 5.9 |

## 5. Proposed binding algorithm for msil-ld

### 5.1 Requests

Codegen (in the backend for objects, inside msil-ld for LTO) produces **requests**, each naming a managed entity with
no assembly: `Type` (namespace + name, nested types as `Outer/Inner`, generic arity in the name as in metadata,
`` Vector128`1 ``), `Method` or `Field` (declaring-type request, name, ECMA-335 signature blob whose class/valuetype
operands are other requests, generic method arity), `TypeSpec`/`MethodSpec` instances, `AttributeCtor`, and
`TypeName` (a `System.Type` value in an attribute blob). Each request has a strength: **required** (default) or
**optional** with a fallback (5.8). In an assembly-shaped object (P2-A options B, C or "a" of `inputs.md`) these are
literally TypeRef rows whose ResolutionScope is a reserved placeholder AssemblyRef, MemberRef/TypeSpec/MethodSpec
rows, and CustomAttribute rows; in a wasm-style container they are a "managed imports" section plus relocations on
the 4-byte tokens in IL. Either way binding = choosing an AssemblyRef for every TypeRef and rewriting MemberRef
signatures and attribute blobs.

### 5.2 Loading the references

1. Inputs: `--reference=<path>` (file or directory, 6.1) and positional `.dll` inputs that are not toolchain-built
   libraries (those carry P2-C's export attribute and are C-symbol providers, Design 5.2). Directories contribute
   every `*.dll` with CLI metadata (non-recursive); non-metadata files in a directory are skipped silently (the net48
   directory has two), an explicitly named non-metadata file is an error (Roslyn CS0009).
2. Open lazily: map the file, parse PE + CLI header + metadata root, index only the TypeDef names (with NestedClass)
   and ExportedType names, and the `Assembly` row. Member tables are decoded only for bound types. (The resolver loads
   and indexes the whole net48 directory, 235 files, in about 0.1 s above process start-up with SRM.)
3. Identity checks: the same path twice is ignored; two files with the same full identity (name, version, culture,
   token) keep the first and **warn** (Roslyn silently keeps one, r14); the same simple name with a different version
   or token is an **error** (Roslyn's CS0433 r15 would otherwise appear for every type of it).
4. Warn when a reference is not a reference assembly (no `[assembly: ReferenceAssembly]`) and binding would bind a
   definition in `System.Private.*` (binding to implementation assemblies ties the output to one runtime build;
   `mscorlib` of the Framework directory, which VS csc uses, is harmless).

### 5.3 The core library

Roslyn's rule: the unique reference that defines `System.Object` (top level, no base type) and has no AssemblyRefs.
None or several is an **error** for any link that binds at least one type (every link that has a module initializer
or `Main` does): "the references contain no core library (an assembly that defines System.Object and references no
other assembly); given: mscorlib 4.0.0.0 (facade, forwards System.Object to System.Runtime)", or "...mix two core
libraries: mscorlib 4.0.0.0 (...) and System.Runtime 8.0.0.0 (...)". The core library is used for that check and for
diagnostics; types are still bound by the general lookup (the core library's types are found there).

### 5.4 Type lookup

1. Candidates: public TypeDefs with the exact full name in any reference (nested: every enclosing type public or
   nested-public). Non-public definitions are ignored, so implementation assemblies cannot cause false ambiguity.
2. One candidate: bind. Several: **error** (CS0433), whatever the order of references. First-match-wins, as in `-L`
   search, would make the output depend on argument order and silently mix contracts.
3. None, but some reference forwards the name (a top-level ExportedType row whose Implementation is an AssemblyRef
   and whose flags have the forwarder bit `0x00200000`, SRM's `TypeAttributes.Forwarder`; nested forwarded types are
   ExportedType rows whose Implementation is the enclosing ExportedType and which do **not** carry the bit: 27 of
   1,256 rows in the net8 `mscorlib` facade, 71 of 2,604 in its `netstandard`, resolver `fwdflag` mode): follow the chain *within the
   reference set*. In a closed set (every pack measured here) the destination is a reference and step 1 already found
   it, so this step only produces the CS1069-style error when the destination is missing. Binding to the facade
   instead would emit `[mscorlib]`-style references for exactly the types the facade forwards, an implicit fallback
   that 2.4 shows to be runtime-dependent; do not.
4. Nothing: error.
5. Every type that appears in a bound member's signature (parameters, return, custom modifiers such as
   `InAttribute`, generic arguments) and every base type a request needs is bound by the same rules (transitively).

### 5.5 Member lookup

1. Search the bound TypeDef's MethodDefs (or Fields) by name, then compare the request's signature with each
   definition: calling convention (static/instance, `vararg`, generic flag), generic parameter count, parameter count,
   return type and parameter types, after mapping each definition TypeDefOrRef token to its full name (a TypeRef of the
   reference assembly is itself resolved by name). Custom modifiers are **ignored for matching** (codegen cannot know
   them) and **copied into the emitted signature** (3.4). If two definitions match after ignoring modifiers, error.
2. Not found: search base types (the runtime resolves a MemberRef on a derived type through the bases), but emit the
   declaring type as the parent, as Roslyn does.
3. Only public members (or `famorassem`/`family` for instance members of base types, never needed here).
4. Optionally warn for `[Obsolete]` and refuse `[RequiresPreviewFeatures]` members unless a switch allows them
   (none of today's inventory has either).

### 5.6 Emission

For each defining assembly actually used, one AssemblyRef row, in first-use order (deterministic). ECMA-335 II.22.5
layout: `MajorVersion`, `MinorVersion`, `BuildNumber`, `RevisionNumber` (2 bytes each), `Flags` (4 bytes, 0),
`PublicKeyOrToken` (#Blob index; blob `08` + the 8 token bytes in order, e.g. `08 B0 3F 5F 7F 11 D5 0A 3A`), `Name`,
`Culture` (#Strings, empty = neutral), `HashValue` (#Blob, empty): 20 bytes with 2-byte heap indexes, 28 with 4-byte.
The token is computed from the definition's `Assembly.PublicKey` with `llvm::SHA1::hash`
(`llvm/include/llvm/Support/SHA1.h:53`): last 8 bytes of the SHA-1, reversed (the resolver's `Asm.TokenOf`, which
reproduces all known tokens, including `b77a5c561934e089` from the 16-byte ECMA key of `mscorlib`).

TypeRef (II.22.38): `ResolutionScope` (coded index, 2 tag bits: Module 0, ModuleRef 1, AssemblyRef 2, TypeRef 3; a
nested type is scoped to its enclosing TypeRef), `TypeName`, `TypeNamespace`. MemberRef (II.22.25): `Class`
(MemberRefParent, 3 tag bits: TypeDef 0, TypeRef 1, ModuleRef 2, MethodDef 3, TypeSpec 4), `Name`, `Signature` (the
definition's blob re-encoded with this module's TypeRef/TypeSpec tokens). TypeSpec/MethodSpec for generic instances.
Attribute blobs: `System.Type` arguments as assembly-qualified names of the bound definition (4.4). Rows are
deduplicated by identity. Nothing is emitted for references that bound nothing.

### 5.7 Versions and tokens

The emitted version is the defining reference assembly's own version (8.0.0.0 for every 8.0.x pack; the pack's patch
level never appears), so the output's minimum runtime is exactly what the references say: CoreCLR loads it on that
version or later (v02, v03), Framework only for versions it has (3.3). Emit the real token always: CoreCLR ignores
it, but Framework enforces it for everything except `mscorlib` (v13, v14). Never take versions from facades'
AssemblyRefs (0.0.0.0 in the net8 `mscorlib` facade, 2.3).

### 5.8 Missing members: required versus optional

Design 7.1 says capability comes from the references: an API is usable only if some reference defines it. For object
files the codegen decision is made before the references are known, so two kinds of request:

- **Required** (every member the default codegen uses, and APIs a switch forces on): missing is an error, for
  example `msil-ld: error: undefined .NET member 'float64 System.Math::FusedMultiplyAdd(float64, float64, float64)'
  (requested by foo.o for -mmsil-fma); 'System.Math' is defined in mscorlib 4.0.0.0 (<path>) but has no such member;
  overloads present: ...`.
- **Optional with a fallback**: the object provides both paths, and msil-ld chooses. Two shapes fit the backend:
  (a) *bind-or-drop* for attributes that only optimize (`DisableRuntimeMarshalling`, `SuppressGCTransition`; Phase
  1 runs without them, D10); (b) *bind-or-substitute* for APIs, where codegen calls a private thunk method
  (e.g. `'<msil>bcl.fma'(float64, float64, float64)`) whose body msil-ld supplies: a call to the bound member, or a
  call to a helper symbol resolved like any undefined C symbol from the static helper archive (compiler-rt style,
  `inputs.md`). Value-type APIs (`Int128`, `Half`) need the thunk shape because their call sequences differ from the
  helpers'. With `--verbose` (or a `--warn-bcl-fallback`) msil-ld reports each fallback.

In LTO links codegen runs inside msil-ld after the references are loaded, so the capability can be passed to codegen
directly (a TargetOptions field or module flag computed from the references) and no fallback is needed.

### 5.9 Toolchain-built input assemblies (P2-C)

A library built by this toolchain and linked against (Design 5.4) has its own AssemblyRefs. msil-ld should check
each of them against the references: same name and version <= the reference's: fine; same name, higher version:
error (Roslyn CS1705), because the output would then need that version at run time; name not among the references:
warning (the output depends on an assembly the link did not see). This catches linking a net8-bound library into a
program meant to run on Framework.

### 5.10 No references

| Option | Pros | Cons | Verdict |
|---|---|---|---|
| (a) Error, like csc on .NET (r8: CS0518) | Purely Roslyn-like; no hidden policy | Breaks Design goal 1 (`clang --target=<triple> foo.c -o foo.dll` produces a runnable assembly) and the Phase-1 behaviour; every test and every user command needs a reference path | Keep as the behaviour for anything the table below does not cover |
| (b) **Built-in `mscorlib` 4.0.0.0 table**: the `[mscorlib]`-safe members (2.4) that the default codegen uses, bound to `mscorlib` 4.0.0.0 `b77a5c561934e089`; active only when no reference is given | Default output runs on Framework 4.8 and .NET 6/8/10 (and, *unverified*, Mono/Unity) exactly as Phase 1 does; tiny (tens of entries, generated from the same `.def` as the backend's list, 1.1); matches Framework-hosted csc's default `mscorlib` (r11); `mscorlib` is the most forgiving name on Framework (v06, v07, v10) | A built-in API list to maintain; every entry must be proven `[mscorlib]`-safe by a test (the resolver's `mscorlib_safe.py` shows the method); anything newer needs references | **Recommended** |
| (c) Embedded reference assemblies or a generated API file (TextAPI `.tbd`-like) | General | Size; licensing of Microsoft's reference assemblies is *unverified* (the .NET packs are believed to be MIT-licensed, the Framework reference assemblies to carry a different license); a second source of truth | No |
| (d) Driver discovers installed packs or the Framework reference directory | Zero configuration | A hidden target-framework choice (which pack version?), host-dependent output, not deterministic; contradicts "no target-framework switch" | No |
| (e) `netstandard` 2.0.0.0 fallback | Runs on Framework 4.7.1+ and .NET Core 2.0+ (v04) | Older Framework, and the Framework binder's mscorlib leniency, lost; no gain over (b) for the Phase-1/3 default surface | No |

With (b), `--reference` turns the table off entirely (mixing it with given references could create two core
libraries). A strictness flag is not needed: a build system that wants Roslyn behaviour passes references.

### 5.11 Requests before linking: objects and `-S` text

`-S` output (and an `.s` that an assembler might read back) is pre-link. Proposed: the printer keeps printing
`[mscorlib]` for the members of the default table, so `-S` + ilasm keeps running everywhere (the Phase-1 contract),
and prints switch-gated members with a reserved placeholder scope that only msil-ld resolves. Whether an object
should record "unbound" scopes (recommended) or `[mscorlib]` scopes that msil-ld rebinds is a P2-A detail; binding by
name works either way.

### 5.12 Determinism and cost

Output depends only on the set of references, not their order (ambiguity is an error). Index cost is negligible
(2.5); the net8 pack's 3.2 MB of metadata is read through the name tables only.

## 6. Command-line spelling

### 6.1 msil-ld

Option-table collisions (checked in this tree):

| Spelling | lld/ELF (`lld/ELF/Options.td`) | lld/wasm (`lld/wasm/Options.td`) | lld/MachO | lld/COFF | clang driver (`clang/include/clang/Options/Options.td`) |
|---|---|---|---|---|---|
| `-r` | alias of `--relocatable` (:621) | alias of `--relocatable` (:294) | `-r` (:247) | — | `def r` (:6870), a LinkerInput forwarded to the linker |
| `-R` | alias of `--rpath` (:622) | — | — | — | `R_Joined` `-R<remark>` (:4345) |
| `--reference` | none | none | none | none (`/lib`, `/libpath` only) | none (clang-cl has an unrelated, ignored `/reference`, :9957) |

Proposed: `defm reference: EEq<"reference", "Bind .NET types and members against this reference assembly, or
every assembly in this directory">` in msil-ld's `Options.td`, that is `--reference <path>` and `--reference=<path>`,
repeatable. Double dash only: wasm-ld's `Eq` also accepts a single dash, and its own comment prefers two dashes "for
lld specific or newer long options ... to avoid collision with short options" (`lld/wasm/Options.td:3-6`). No short
alias. A directory argument matches how reference packs ship (163 files for net8) and how users pass the
Framework's `v4.8` directory. Positional `.dll` inputs without the export attribute are references too (Design 8).

### 6.2 clang driver

No new driver option in P2-B:
- managed `.dll` inputs on the clang command line already reach the linker job: an unknown extension becomes
  `types::TY_Object` (`clang/lib/Driver/Driver.cpp:3253-3265`), and P2-B's `MSILToolChain` will pass linker inputs to
  msil-ld like any linker job (today `ToolChain::getLink`, `clang/lib/Driver/ToolChain.cpp:757-770`, rejects every MSIL link);
- `-Wl,--reference=<dir>` / `-Xlinker --reference=<dir>` work with no driver code;
- references matter only at link time (`-c` and `-S` never need them), as the user decided.

A dedicated driver option (for example `--msil-reference=<path>`) can be added later if build-system integrators ask;
it would touch `clang/include/clang/Options/Options.td`, whose generated table many clang TUs include through
`clang/Options/Options.h` (a broad rebuild that plan section 7 asks to batch into a phase's first milestone).

## 7. A CLI metadata reader in LLVM

### 7.1 Need

`llvm/lib/Object` has no CLI metadata reader (`grep` for `COR20`/`CLR_RUNTIME_HEADER`: only the data-directory enum
`COFF::CLR_RUNTIME_HEADER`, `llvm/include/llvm/BinaryFormat/COFF.h:645`, and its YAML mapping). It is needed for:
(1) reference assemblies (this task; the user decided msil-ld searches them); (2) toolchain-built assemblies in P2-C
(export attribute, AssemblyRefs for 5.9); (3) object files if P2-A chooses an assembly-shaped object; (4) tests and
tools (`llvm-readobj`/`llvm-objdump` support, instead of ildasm). The PE layer exists: `COFFObjectFile` parses IL-only
PE files (checked on a PE32 reference assembly: the VS-bundled llvm-readobj 22 prints `Machine:
IMAGE_FILE_MACHINE_I386`, `CLRRuntimeHeaderRVA: 0x2008`, `CLRRuntimeHeaderSize: 0x48` for the net8
`System.Runtime.InteropServices.dll`; the PE32+ outputs of Phase 1 were not checked), and `getDataDirectory` /
`getRvaAndSizeAsBytes`
(`llvm/include/llvm/Object/COFF.h:1155`, `:1207`) give the 72-byte COR20 header and the metadata.

### 7.2 Scope for binding

- Metadata root (`BSJB` signature, version string, stream headers) and the `#~` stream; the `#-` (uncompressed, with
  Ptr tables) form can be an error at first (ilasm, Roslyn and every pack use `#~`; *unverified* for arbitrary
  third-party assemblies).
- Heaps: `#Strings`, `#Blob`; `#GUID` and `#US` are not read.
- Tables read: Module (0x00, nothing but row size), TypeRef (0x01), TypeDef (0x02), Field (0x04), MethodDef (0x06),
  MemberRef (0x0A, attribute constructors), CustomAttribute (0x0C, `[ReferenceAssembly]`, P2-C's attribute), TypeSpec
  (0x1B), Assembly (0x20), AssemblyRef (0x23), ExportedType (0x27), NestedClass (0x29), GenericParam (0x2A,
  optional). The **column schema of all 45 tables** (0x00-0x2C) is still needed: row sizes depend on row counts of
  other tables (coded indexes) and on the heap-size flags, and a reference assembly can have any table.
- Signatures (II.23.2): compressed integers, calling-convention byte, generic counts, element types
  (`0x01`-`0x1E`), `CMOD_REQD`/`CMOD_OPT` (`0x1F`/`0x20`), `SENTINEL` (`0x41`), `PINNED` (`0x45`),
  TypeDefOrRefOrSpecEncoded, arrays, function pointers;
  custom-attribute value blobs (prolog `0x0001`, SerString) for P2-C.
- Sizes to handle (2.1): the net8 pack has 163 files, 3.2 MB metadata, 47,146 MethodDefs, but a link touches the name
  tables plus a handful of types.

### 7.3 Size and placement

Estimate 1,800-2,500 lines of reader (schema `.def` ~150, root/streams/heaps ~400, table layout and row access ~350,
signature decoding ~350, name index with nested types and forwarders ~250, attribute blobs ~200) plus 600-900 lines
of binder in msil-ld; for scale, `llvm/lib/Object/WasmObjectFile.cpp` is 2,376 lines and `COFFObjectFile.cpp` 2,491.
Placement, by analogy with WebAssembly: format constants and the table schema in
`llvm/include/llvm/BinaryFormat/CLI.h` + `CLITables.def` (header-only like `BinaryFormat/Wasm.h`), the reader in
`llvm/include/llvm/Object/CLIMetadata.h` + `llvm/lib/Object/CLIMetadata.cpp` (an `ObjectFile` subclass only if
P2-A's objects are assemblies), the binder in `lld/MSIL/`. New headers and new files only, so no broad rebuild;
adding a new `ObjectFile` kind to `Binary.h`/`ObjectFile.h` would be a widely-included edit (front-load it if
chosen).

### 7.4 Sharing with the metadata writer

Yes, and it should. The writer (P2-A object writer and/or msil-ld's PE writer, later the Portable PDB writer, whose
tables 0x30-0x37 use the same `#~` format) and the reader need the same: table ids, the column list of every table,
the coded-index definitions (ResolutionScope, TypeDefOrRef, MemberRefParent, HasCustomAttribute (22 targets, 5 tag
bits), Implementation, ...), heap-index width rules, element types and signature calling conventions, flag enums
(TypeAttributes incl. the forwarder bit, MethodAttributes, FieldAttributes, PInvokeAttributes, AssemblyFlags), the
COR20 header and metadata-root structs, and compressed-integer encode/decode. One function computing the layout
(row size and column offsets per table from the 64 row counts and the three heap sizes) serves both sides; an X-macro
`.def` can generate the writer's row structs and the reader's column accessors. System.Reflection.Metadata itself
is organized this way (`TableIndex`, `HeapIndex`, coded-index constants and `MetadataSizes` shared by
`MetadataReader` and `MetadataBuilder`).

### 7.5 Testing

Reference packs cannot be checked in (size; licensing *unverified*). Tests should use small synthetic reference
sets: ilasm assembles forwarders (`.class extern forwarder System.Object { .assembly extern FakeCore }`, see
`fakepack/fakefacade.il`) and the resolver resolves through them (`FORWARDED-TO-MISSING` when the destination is
absent). One ilasm quirk: a class without `extends` gets `[mscorlib]System.Object` as its base unless the assembly is
named `mscorlib`, so a fake core library named anything else (`System.Runtime`) gets an AssemblyRef and stops being a
core library by Roslyn's rule (observed with the Framework ilasm and ilasm 10.0.12). Fake core libraries other than
`mscorlib` need csc (`-nostdlib`, a source-defined `System.Object`) or the project's own writer; small binaries can be
checked in with their generator script. Gate legs that need real packs (net48, net8, net10) run on this host only.

## 8. FeatureIdeas: what a binding choice could preclude

- Ideas 3-6 (typed pointers, managed memory and refs, `stackalloc`/`Span<T>`, the enclosed model with
  `GC.AllocateArray<T>` or `GCHandle`) and phase 4 SIMD need generic types, generic methods, value-type locals and
  nested types bound like any other request. A binder limited to a fixed list of non-generic static methods would
  preclude them; the request model of 5.1 does not.
- Idea 7 (several triples in one assembly, a facade choosing a variant with `RuntimeInformation`): binding is
  per output assembly, so all variants share one reference set; fine. Note that `RuntimeInformation` moved between
  assemblies (2.2), which only data-driven binding handles.
- Idea 1 (class placement): independent of binding.
- Light-up (3.3, v17) is possible on the CLR but needs two reference sets; the proposal leaves it out without
  precluding it later (requests could carry a "light-up" strength).

## 9. Risks and open questions

- PEVerify cannot verify outputs bound to .NET 8+ references (3.5); D21's PEVerify and neutralized-pass steps need a
  mscorlib-bound twin of each program in such legs.
- The default table (5.10 b) must stay inside the `[mscorlib]`-safe intersection; one entry that only modern runtimes
  forward (e.g. `MathF`) silently breaks Framework. A test should re-run `mscorlib_safe.py`'s check (or its C++
  successor) against every entry.
- Two references with one identity: Roslyn's silent choice (r14) shows that order can matter; msil-ld must warn.
- Overload sets differ by runtime (NativeMemory `Copy`/`Fill`/`Clear` are .NET 7+, `Interlocked` byte/short are .NET
  9+, `X86Base.Pause` .NET 8+): switch defaults must not assume a member that the minimum supported reference pack
  lacks.
- Facades' AssemblyRef versions of 0.0.0.0 (2.3): any code path that copies an AssemblyRef from a facade emits a
  reference that no longer states the version requirement (how each runtime binds 0.0.0.0 was not tested).
- Whether P2-A objects carry unbound scopes or `[mscorlib]` scopes (5.11), and how `-S` prints switch-gated members.

## 10. Work-package ideas (disjoint files)

1. **Structured BCL operands in the backend** (P2-A, target-local, no broad rebuild): a `.def` list of every BCL
   member codegen uses (namespace, type, member, signature, default-table flag); `MEMMOVE` and `MEMORY_BARRIER`
   (`MSILInstrMemory.td`) get a BCL-member operand; `SynthesizedBody` (`MSILAsmPrinter.cpp`) emits structured
   instructions; `MSILMemberRefExpr::BCLMember` carries a member id instead of text (`MCTargetDesc/MSILMCExpr.{h,cpp}`,
   `MSILInstPrinter.cpp`). `-S` text stays byte-identical (`[mscorlib]`), pinned by the existing tests.
2. **Shared CLI format header**: `llvm/include/llvm/BinaryFormat/CLI.h`, `CLITables.def`, `llvm/lib/BinaryFormat/CLI.cpp`
   (compressed integers, table layout computation), unit tests.
3. **CLI metadata reader**: `llvm/include/llvm/Object/CLIMetadata.h`, `llvm/lib/Object/CLIMetadata.cpp`,
   `llvm/unittests/Object/CLIMetadataTest.cpp` with small checked-in assemblies and their generator script.
4. **Binder in msil-ld**: `lld/MSIL/ReferenceSet.{h,cpp}` (loading, identities, core library, name index),
   `lld/MSIL/BCLBinder.{h,cpp}` (5.4-5.9), the built-in default table, `--reference` in `lld/MSIL/Options.td`.
5. **Binder tests**: `lld/test/MSIL/bcl-*.test` over synthetic packs built with ilasm (forwarders, ambiguity,
   duplicate identities, two core libraries, missing members, modreq copying, nested and generic types), plus
   host-only legs over the real net48/net8/net10 packs whose expected bindings come from `bclresolve matrix`.
6. **Gate legs**: link with no references (runs on Framework 4.8, .NET 8 and 10) and with the net8 pack (runs on
   .NET 8 and 10; ILVerify; PEVerify on the mscorlib-bound twin), and a check that every default-table entry is
   `[mscorlib]`-safe on each runtime (the method of `mscorlib_safe.py`).

## 11. Reproduction

```text
cd C:\llvm-msil-scratch\p2\bcl-binding\tool           (copy of bcl-binding-files/tool)
dotnet build -c Release -o bin
dotnet bin/bclresolve.dll matrix --queries queries.txt "net48=<v4.8>;<v4.8>/Facades" "ns2.0=<ns20 ref>" ^
   "ns2.1=<ns21 ref>" "net6=<pack>/6.0.36/ref/net6.0" "net8=<pack>/8.0.31/ref/net8.0" "net10=<pack>/10.0.12/ref/net10.0"
python mscorlib_safe.py
cd ..\exp && python gen.py runs && python run.py runs   (runtime/gen.py, runtime/run.py)
bash roslyn/run-roslyn.sh
```

Costs: 1.9 MB of scratch on C:, nothing large on D: (D: had 20 GB free before and after), no LLVM build.
