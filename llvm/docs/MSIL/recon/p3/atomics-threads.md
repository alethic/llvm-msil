# Phase 3 recon: C11 atomics, the memory model and threads (P3-B)

Key `atomics-threads`. HEAD `cc4c79787d6a` (M23), branch `msil`. Scratch: `C:\llvm-msil-scratch\p3\atomics-threads`.
Supporting files (sources, IL, scripts and trimmed logs): [atomics-threads-files/](atomics-threads-files/).

All numbers below were measured on the shared, loaded development host (Windows 11, 4 cores / 8 threads; other
projects' builds ran at the same time), on .NET Framework 4.8 (4.0.30319.42000), .NET 8.0.33, .NET 9.0.22 and .NET
10.0.14 on Windows, and on the private WSL runtimes .NET 8.0.31 and 10.0.13 (Ubuntu, `msil/utils/msil-wsl-dotnet.sh`).
Timings are noisy (single runs vary by 30-50%); the report gives ranges or minimums of repeated runs and draws no
conclusion from differences inside that noise. ARM64 code was not run (no ARM64 host): it was read from the .NET 8 and
.NET 10 JITs through `crossgen2 --targetos:linux --targetarch:arm64` (the JIT `clrjit_universal_arm64_x64.dll` of the
NuGet package `microsoft.netcore.app.crossgen2.win-x64`, already in the local NuGet cache; nothing was installed).

## 0. Summary

1. **Today** every atomic load, store, `atomicrmw` and `cmpxchg` becomes an `__atomic_*` library call
   (`setMaxAtomicSizeInBitsSupported(0)`, `MSILISelLowering.cpp:394-396`, then `AtomicExpand` in
   `MSILPassConfig::addISelPrepare`, `MSILTargetMachine.cpp:312-317`). `-S`/llc report each helper
   (`undefined function '__atomic_fetch_add_4' is not defined in this module and is not a Phase-1 import ...`), CIL
   objects keep them as undefined symbols without a diagnostic, and msil-ld reports `undefined symbol:
   __atomic_fetch_add_4` - **or, when a `libatomic.so.1` is on the library path (`-latomic`), binds every helper to
   it as a P/Invoke, through objects and through `-flto` alike, and the program runs correctly** (measured on WSL .NET
   8/10; 10.5-12.8 ns per `fetch_add` against 6.4-6.8 ns for an inline `Interlocked.Add`). Fences already work (D36:
   `Interlocked.MemoryBarrier`, a comment for single-thread fences), `volatile` is the `volatile.` prefix (D24).
2. **The BCL covers 32- and 64-bit atomics on every runtime** with mscorlib-safe members: `Interlocked.Add`,
   `Exchange` and `CompareExchange` on `int32&`, `int64&` and `native int&` (Add has no native int overload), and
   `Interlocked.MemoryBarrier`. `Interlocked.And/Or` exist from .NET 5 (measured on 8, 9, 10; missing on 4.8), the
   8- and 16-bit `CompareExchange`/`Exchange` from **.NET 9** (missing on 4.8 and 8: `MissingMethodException` through
   `[mscorlib]`), `Volatile.ReadBarrier/WriteBarrier` from .NET 10. Nothing exists for 128-bit atomics.
3. **A `native int` passed where `int32&` is expected runs on all runtimes** (Framework 4.8, .NET 8, .NET 10) and the
   JIT emits exactly the native instruction (`lock xadd`/`lock inc`, `xchg`, `lock cmpxchg`; ARM64 `casal`, `swpal`),
   but **both verifiers reject it** (`[found Native Int][expected address of Int32] Unexpected type on the stack.`,
   ILVerify code `StackUnexpected`), which today's D21 allow-list does not accept; msil-verify fails such an image in
   both passes (measured). Loads and stores need no byref at all: `volatile. ldind`/`stind` are already allowed.
4. **Memory orders** (measured, x64 run + ARM64 code read): `volatile. ldind` is a plain `mov` on x64 and `ldar` on
   ARM64 (`ldapr`, RCpc, when the CPU has RCPC: .NET 10 with `armv8.3-a`); `volatile. stind` is `mov` / `stlr`;
   `Interlocked.*` RMW are full barriers (`lock`-prefixed / `casal`, `swpal`); `MemoryBarrier` is `lock or [rsp],0`
   / `dmb ish`. The **correct C11 mapping** is: every atomic load (relaxed included) `volatile. ldind`; relaxed and
   release stores `volatile. stind`; **seq_cst stores `volatile. stind` + `Interlocked.MemoryBarrier()`** (LLVM's
   `shouldInsertTrailingSeqCstFenceForAtomicStore`); RMW and cmpxchg `Interlocked`. Store-buffering litmus test, 2M
   rounds: 0 forbidden outcomes with that mapping on all 5 runtime configurations, 6,463-80,852 with a plain release
   store (which C allows for release/acquire; natively 105,177-505,074). Relaxed loads need `volatile.`: **the JIT
   hoists a plain `ldind` out of a spin loop** (hangs on 4.8, .NET 8 optimized, .NET 10 tiered and optimized).
5. **D36's single-thread fence is not a barrier for the JIT.** A pre-C11 spin loop on a plain `int` with
   `asm volatile("" ::: "memory")` terminates natively but hangs under the CLR (Framework 4.8, .NET 8 with
   `TieredCompilation=0`, .NET 10, WSL .NET 10), because the barrier is only a comment in the IL. A call of a private
   `noinlining` empty method fixes it on every runtime for 0.4-1.1 ns per barrier (`Interlocked.MemoryBarrier` costs
   4-5 ns). Recommended as a D36 amendment.
6. **8/16-bit RMW and cmpxchg** as a CAS loop on the containing aligned 32-bit word (LLVM's partword expansion,
   `setMinCmpXchgSizeInBits(32)`): correct for adjacent bytes updated atomically or with plain stores on all 5
   configurations; a deliberately broken variant (plain word write-back) loses updates, so the test discriminates.
   Cost about 2x one `Interlocked` op single-threaded (10-14 ns against 5-7 ns).
7. **16-byte and generic atomics**: no BCL API. Native fidelity is poor anyway (native clang on windows-msvc cannot link
   them; on Linux they need `-latomic`). Recommended: compiler-rt `atomic.c` in the P3 helper archive (it compiles for
   the MSIL triples), with an MSIL lock table that is **process-wide** (found by name through `AppDomain` data under an
   interned-string lock: measured to be shared across assemblies on 4.8, 8 and 10), so that a program and a library
   built by this toolchain agree. **Two clang defects block it today**: on `aarch64-*-msil`, and on x86-64 with
   `-mcx16`/`-march=x86-64-v2`, clang reports 16-byte atomics as always lock-free (`__atomic_always_lock_free(16,0)` = 1,
   `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16` defined), so `atomic.c`'s `__atomic_load_16` compiles to `load atomic i128`,
   which the backend would turn back into a call of `__atomic_load_16` (infinite recursion). Fix: `MSILTargetInfo`
   caps `MaxAtomicInlineWidth` at 64 (layouts unchanged; `MaxAtomicPromoteWidth` stays).
8. **Threads**: C programs that create threads through the host libc (`pthread_create`, `CreateThread`, C11
   `thrd_create` in glibc and in `VCRUNTIME140_THREADS.dll`) work once the start routine is a native-callable thunk.
   With hand-made delegate thunks (Design.md 4.5's default) a counter incremented by 4 threads with
   `atomic_fetch_add` gives exactly 8,000,000 on Framework 4.8, .NET 8, .NET 10 and WSL .NET 8/10 for both x86_64
   triples, at the native speed under contention (min 8.6-13.7 ns per increment against 8.0-9.8 natively).
   `pthread_exit`/`ExitThread` from a thread function, `call_once`, `mtx_*`, `thrd_join` and process exit while a
   created thread still runs all behave as natively.

The recommended lowering is in section 10, the verification question in section 11, the tests in section 12 and the
decisions in section 13.

## 1. Method and files

| Exp. | What | Where (scratch) | Supporting files |
|---|---|---|---|
| E1 | what clang emits for every C atomic construct (C11, GNU `__atomic_*`, `__sync_*`, MSVC `_Interlocked*`, `volatile`, lock-free queries) on 3 MSIL and 3 native triples; what `-S`, `-c` and msil-ld do with it | `e1-today` | `e1/atomics.c`, `e1/msvc.c`, `e1/main1.c`, `logs/e1-ir.txt`, `logs/e1-link.txt` |
| E2 | `Interlocked`/`Volatile` members in the 4.8, 8, 9, 10 references; resolution through `[mscorlib]` at run time | `e2-bcl` | `e2/b8.il`, `logs/e2-bcl.txt` |
| E3 | `native int` as `T&`: runs, ILVerify, PEVerify, msil-verify; inlining and cost of 3 shapes; x64 and ARM64 code | `e3-ref` | `e3/*.il`, `e3/gen.py`, `logs/e3-*.txt` |
| E4 | byte/halfword atomics by CAS on the containing word, adjacent bytes, negative control | `e4-subword` | `e4/subword.c`, `logs/e4-subword.txt` |
| E5 | 16/24-byte atomics: native oracle, compiler-rt `atomic.c` on MSIL triples, process-wide lock table, lock costs | `e5-i128` | `e5/*`, `logs/e5-e7.txt` |
| E6 | memory model: store-buffering litmus, JIT hoisting, compiler barrier (D36), barrier cost | `e6-order` | `e6/*`, `logs/e6-*.txt` |
| E7 | lock-free answers and predefines | (E1, E5) | `logs/e5-e7.txt` |
| E8 | threads: 4-thread counter, thread exit, C11 threads, create cost, exit while running | `e8-threads` | `e8/*`, `logs/e8-threads.txt` |

**The prototype pipeline (E4, E6, E8).** Phase 2 has no atomics and no native-callable thunks, so the multi-threaded
experiments compile C with this toolchain's `clang -S` and post-process the IL (`atomics-threads-files/tools/post.py`):

- `e8/proto-atomic.h` declares the sized helpers that `AtomicExpand` calls (`__atomic_fetch_add_4`, `__atomic_load_8`,
  `__atomic_compare_exchange_4`, ...) with `__attribute__((import_module("msil.atomic")))` and keeps them alive with a
  `used` function, so `clang -S` emits P/Invoke declarations instead of the D14 error. `post.py` replaces each with a
  managed `aggressiveinlining` method whose body is the lowering recommended in section 10 (address as `native int`
  passed to `Interlocked`; `volatile. ldind/stind`; `MemoryBarrier` after seq_cst stores). The JIT inlines them (x64
  disassembly shows the same `lock xadd` as an inline call), so this measures the proposed lowering's semantics.
- Thread APIs are declared with `import_module("kernel32.dll")`, `("libc.so.6")` or `("vcruntime140_threads.dll")`.
  `post.py --thunk <f>` replaces the module initializer's `ldftn <f>; stsfld '<msil>fn_<f>'` by Design.md 4.5's
  default thunk: `ldnull; ldftn <f>; newobj <private delegate>::.ctor`, kept alive in a static field, then
  `Marshal.GetFunctionPointerForDelegate`. Only `[mscorlib]` members are used, so the same image runs on 4.8 and modern
  .NET.
- Images are assembled with Framework ilasm (`-exe -pe64 -x64`); modern .NET runs them with
  `dotnet exec --runtimeconfig rc<N>.json` (`tools/rc8.json`, ...); WSL through `tools/wslrun.sh`.
- Native oracles: Windows `clang --target=x86_64-pc-windows-msvc -fuse-ld=lld`; Linux
  `tools/native-linux.sh` (the E7 recipe: `msil/utils/linux-oracle/start.c`, the glibc 2.43 sysroot's headers and
  `libc.so.6`, `ld.lld -dynamic-linker /lib64/ld-linux-x86-64.so.2`), run in WSL.

## 2. What the toolchain does today

### 2.1 What clang emits (E1)

`clang -O2 -ffreestanding -std=c17 -S -emit-llvm` of `e1/atomics.c` (64 functions) gives **the same atomic IR on the
MSIL triple and on its native triple** for `x86_64-pc-windows-msvc`, `x86_64-unknown-linux-gnu` and
`aarch64-unknown-linux-gnu`, with one exception (volatile on windows-msvc, below). Summary
(`logs/e1-ir.txt`):

| C | IR (all three MSIL triples unless noted) |
|---|---|
| `atomic_load[_explicit]` on `_Atomic int` | `load atomic i32 seq_cst/acquire/monotonic, align 4` |
| `atomic_store[_explicit]` | `store atomic i32 seq_cst/release/monotonic` |
| `atomic_exchange`, `fetch_add/sub/and/or/xor`, `x += v` | `atomicrmw xchg/add/sub/and/or/xor i32 seq_cst` |
| `__atomic_fetch_max`, `__atomic_fetch_min` (unsigned), `__atomic_fetch_nand` | `atomicrmw max`, `umin`, `nand` |
| `atomic_compare_exchange_strong` / `_weak_explicit(acq_rel, acquire)` | `cmpxchg i32 seq_cst seq_cst` / `cmpxchg weak ... acq_rel acquire` |
| `_Atomic char`, `_Atomic short` | `atomicrmw add i8`, `cmpxchg i8`, `atomicrmw or/xchg i16` (`align 4` for the windows-msvc global, `align 2` elsewhere; the same natively) |
| `_Atomic long long`, `_Atomic(void *)` | `atomicrmw add i64`, `load atomic i64`, `atomicrmw xchg ptr` |
| `_Atomic float af; af += v` | `atomicrmw fadd float seq_cst` |
| `_Atomic double` load, `__atomic_fetch_add(double *)` | `load atomic double`, `atomicrmw fadd double` |
| `_Atomic struct {char a,b,c;}` | `load atomic i32` (padded to 4 bytes) |
| `_Atomic struct {long long a,b;}`, `_Atomic __int128` | `load/store atomic i128`, `atomicrmw add i128`, `cmpxchg i128` (align 16) - **IR atomics even on x86-64 without cx16**: clang leaves the libcall to the backend |
| `_Atomic struct {long long a,b,c;}` (24 bytes) | `call @__atomic_load(i64 24, ...)` (clang's own generic libcall; warning `-Watomic-alignment`) |
| `atomic_flag_test_and_set` / `_clear` | `atomicrmw xchg i8 seq_cst` / `store atomic i8 seq_cst` |
| `atomic_thread_fence(seq_cst/acquire)`, `__sync_synchronize` | `fence seq_cst` / `fence acquire` |
| `atomic_signal_fence` | `fence syncscope("singlethread") seq_cst` |
| `__sync_fetch_and_add`, `__sync_add_and_fetch`, `__sync_val/bool_compare_and_swap`, `__sync_lock_test_and_set`, `__sync_fetch_and_nand` | `atomicrmw add`, `cmpxchg`, `atomicrmw xchg`, `atomicrmw nand`, all `seq_cst` |
| `__sync_lock_release` | `store atomic i32 release` |
| `__c11_atomic_fetch_add(p, v, relaxed)` | `atomicrmw add i32 monotonic` |
| `volatile int` load/store | `load volatile`/`store volatile` on every MSIL triple; native windows-msvc: `load atomic volatile i32 acquire` / `store atomic volatile i32 release` (`-fms-volatile` is the native default; the MSIL driver leaves it off, `clang/lib/Driver/ToolChains/Clang.cpp:6414-6422`, plan section 12 row 'M1.c amended') |
| `atomic_is_lock_free(&int)` | `ret true` (folded) |
| `atomic_is_lock_free` on 16 bytes | x86-64: `call @__atomic_is_lock_free(16, p)`; **aarch64: `ret true`** |
| `atomic_is_lock_free` on 24 bytes, `__atomic_is_lock_free(n, p)` | `call @__atomic_is_lock_free` |
| `__atomic_always_lock_free(16, 0)` | x86-64: 0; **aarch64: 1** (x86-64 with `-mcx16` or `-march=x86-64-v2`: 1) |
| `ATOMIC_INT_LOCK_FREE`, `ATOMIC_LLONG_LOCK_FREE`, `ATOMIC_POINTER_LOCK_FREE` | 2, 2, 2 |

MSVC intrinsics on `x86_64-pc-windows-msvc-msil` (`e1/msvc.c`, hosted, `<intrin.h>`): `_InterlockedIncrement` ->
`atomicrmw add i32`, `_InterlockedCompareExchange` -> `cmpxchg i32`, `_InterlockedExchangeAdd64` -> `atomicrmw add i64`,
`_InterlockedExchange8` -> `xchg i8`, `_InterlockedOr16` -> `or i16`, `_InterlockedExchangePointer` -> `xchg i64`,
`_InterlockedCompareExchange128` -> `cmpxchg i128`, `_ReadWriteBarrier` -> single-thread fence, `__faststorefence` ->
`fence seq_cst`, `_mm_mfence` -> `llvm.x86.sse2.mfence`, `_mm_pause` -> `llvm.x86.sse2.pause` (both "intrinsic not
supported" errors today; helpers-switches.md 7.2 maps them to `MemoryBarrier` and `Thread.SpinWait(1)`), and
`_interlockedbittestandset` -> `atomicrmw or i8` (natively a `lock bts` asm statement; plan section 12 row M1 'fix-loop
ownership: CGBuiltin.cpp'). `+outline-atomics` in the aarch64 target features (pinned by `cpu-features-silent.ll`)
only steers the AArch64 backend's own instruction selection; the IR is plain atomics, so no `__aarch64_ldadd4_*` call
can reach MSIL.

### 2.2 What the backend and msil-ld do (E1)

- `MSILISelLowering.cpp:394-396`: `setMaxAtomicSizeInBitsSupported(0)` ("No atomics yet: AtomicExpand turns every
  atomic operation into a call of an __atomic_* function, which is not a Phase-1 import (decision D14)").
  `MSILTargetMachine.cpp:312-317` (`MSILPassConfig::addISelPrepare`) adds `createAtomicExpandLegacyPass()`
  (`pipeline.ll:90` pins "Expand Atomic instructions"). With size 0, `AtomicExpandImpl::processAtomicInstr`
  (`llvm/lib/CodeGen/AtomicExpandPass.cpp:380-440`) sends every load, store, RMW and cmpxchg to
  `expandAtomic*ToLibcall`: sized `__atomic_load_4`, `__atomic_fetch_add_8`, `__atomic_compare_exchange_1`, ... when
  naturally aligned, the generic `__atomic_load(size, ptr, ret, order)` otherwise; `atomicrmw max/umin/nand/fadd`
  become a CAS loop around `__atomic_compare_exchange_N`. Order arguments are the C ABI values (seq_cst 5, release 3,
  acquire 2).
- `-S` and llc (standalone module, D14 frozen): each helper is reported once at the end of the module by
  `reportNotImported` (`MSILAsmPrinter.cpp:1289-1300`): `undefined function '__atomic_fetch_add_4' is not defined in
  this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no
  linker`. Library calls built by LowerCall get `runtime helper '<name>' is not available for MSIL yet (phase 3)`
  (`MSILISelLoweringCall.cpp:283-284` linked, `306-307` standalone; `MSILAsmPrinter.cpp:1358-1366` keeps `__` helpers
  an error even when `!msil.imports` names them).
- `clang -c` (CIL objects, M19): no diagnostic; each helper is an `UNDEFINED FUNCTION` symbol called through
  `[msil.extern]'<msil>Symbols'::'__atomic_fetch_add_4'` (`object-calls.ll`, part `atomics.ll`). `e1/atomics.c` gives
  26 undefined helpers per triple (`__atomic_compare_exchange_1/4/8/16`, `__atomic_exchange_1/2/4/8`,
  `__atomic_fetch_add_1/4/8/16`, `_and_4`, `_nand_4`, `_or_2/4`, `_sub_4`, `_xor_4`, `__atomic_is_lock_free`,
  `__atomic_load`, `__atomic_load_4/8/16`, `__atomic_store_1/4/16`).
- msil-ld (`logs/e1-link.txt`): with the default libraries, `undefined symbol: __atomic_fetch_add_4` (Windows and
  Linux). **With a `libatomic.so.1` on the search path** (`-L<dir> -l:libatomic.so.1`; WSL has gcc's libatomic1 16
  installed, the sysroots have none) `--print-imports` shows `__atomic_fetch_add_4 -> libatomic.so.1!__atomic_fetch_add_4
  [function, LIBATOMIC_1.0]`, through objects and through `-flto` alike, and `main1.c` runs correctly on WSL .NET 8 and
  10 (exit 9, "ok"). helpers-switches.md 2.5 proposes that msil-ld never binds a `__` RuntimeLibcalls name to a native
  library; with section 10's lowering the only `__atomic_*` calls left are the 16-byte and generic ones, which the
  helper archive defines first anyway.
- Fences (D36, `MSILInstrMemory.td:157-180`): `atomic_fence` with scope 0 (single thread) selects the generic
  `MEMBARRIER` (printed as a tab and `//MEMBARRIER`), every other fence `MEMORY_BARRIER`, `call void
  [mscorlib]System.Threading.Interlocked::MemoryBarrier()` (`MSILMCInstLower.cpp:634-639`). Empty asm statements
  become the single-thread fence plus `llvm.sideeffect` (`MSILPrepareIR.cpp:956-1010`).
- `volatile` (D24): `MSILSetMemFlags.cpp:105-123` (`computeMemFlags`) sets the `volatile.` bit when a memory operand
  is volatile; `MSILMCInstLower.cpp:455-468` emits the prefix. Atomic memory operands do not set it today (they never
  reach selection).

### 2.3 Tests that pin today's behaviour

`llvm/test/CodeGen/MSIL/atomic-errors.ll` (the six helper errors and their IL), `imports-linked.ll` (lines 452-455,
484-485, 533-541, 555: `__atomic_fetch_add_4`, `__atomic_load_4`), `object-calls.ll` (lines 51-60, 420-460),
`libcall-policy.ll` (comment lines 14-15), `fence.ll` (D36), `pipeline.ll:90`, `clang/test/CodeGen/MSIL/asm-barrier.c`,
`inline-asm.c:520` (`BITTEST: atomicrmw or`), `clang/test/Driver/msil-toolchain.c` (no `-fms-volatile` by default),
`msil/test/M1/05-memory/barrier.c` (fences and barriers at run time), `msil/test/M2/09-hosted/{glibc,ucrt}-headers.test`
(compile `<stdatomic.h>` and `<threads.h>`), Status.md section 3 row 'Atomics (`_Atomic`, `__atomic_*`, `__sync_*`)'.

### 2.4 The Rust experiment

`C:\llvm-msil-scratch\rust-exp\std\RUN.sh` ran `opt -passes=lower-atomic` (atomics to plain memory operations, valid
only single-threaded) as one of its IR stand-ins; `thread::spawn` (`v6_thread`) failed. Its IR
(`logs/rust-atomics.txt`): 294 atomic operations on linux-gnu, 174 on windows-msvc. Mostly `load ptr monotonic` (48),
`atomicrmw sub i64 release` (45, `Arc` drops) followed by `fence acquire` (43), `store ptr monotonic`, relaxed `i64`/`i32`
loads, `cmpxchg i64/i32` (acquire/monotonic, weak and strong), `atomicrmw xchg i32 release`, and on 8-bit
`atomicrmw xchg i8` and `cmpxchg i8` (AtomicBool, `Once`), plus 8 `llvm.x86.sse2.pause` (`spin_loop`). No `i128`
atomics and no seq_cst stores. Every one of these has a lowering in section 10; the 8-bit ones are partword CAS loops.

## 3. The BCL (E2)

Public members (from `llvm-cil-dump --canonical` of the reference assemblies; `logs/e2-bcl.txt`):

| Member | 4.8 `mscorlib` | .NET 8 `System.Threading` | .NET 9 | .NET 10 |
|---|---|---|---|---|
| `Interlocked.Add(int32&/int64&)`, `Increment`, `Decrement` (int32/int64) | yes | yes | yes | yes |
| `Interlocked.Add/Increment/Decrement(uint32&/uint64&)` | - | yes | yes | yes |
| `Interlocked.Exchange` / `CompareExchange` (`int32&`, `int64&`, `native int&`, `float32&`, `float64&`, `object&`, generic `!!0&`) | yes | yes | yes | yes |
| `Interlocked.Exchange` / `CompareExchange` (`uint32&`, `uint64&`, `native uint&`) | - | yes | yes | yes |
| `Interlocked.Exchange` / `CompareExchange` (`int8&`, `uint8&`, `int16&`, `uint16&`) | - | - | **yes** | yes |
| `Interlocked.And` / `Or` (`int32&`, `int64&`, `uint32&`, `uint64&`), returning the original value | - | yes | yes | yes |
| `Interlocked.Read(int64&)` | yes | yes (+`uint64&`) | yes | yes |
| `Interlocked.MemoryBarrier()` | yes | yes | yes | yes |
| `Interlocked.MemoryBarrierProcessWide()` | - | yes | yes | yes |
| `Volatile.Read` / `Write` (bool, int8..uint64, native int/uint, float32/64, generic) | yes | yes | yes | yes |
| `Volatile.ReadBarrier()` / `WriteBarrier()` | - | - | - | **yes** |
| `Thread.MemoryBarrier()`, `Thread.VolatileRead/Write` | yes | yes | yes | yes |
| `Unsafe.AsRef<T>(void*)` | - (NuGet only) | `System.Runtime` | yes | yes |
| any 128-bit atomic (`Int128`, `Vector128`) | - | - | - | - |

There is no `Interlocked.Add(native int&)`, no `Xor`, no `Max`/`Min`, no `ExchangeAdd` (internal on CoreCLR). At run
time through `[mscorlib]` (`e2/b8.il`, the reference-free default): `CompareExchange(uint8&)` and `Exchange(int16&)` are
`MissingMethodException` on 4.8 and .NET 8 and resolve on 9 and 10; `And(int32&)` and `Or(int64&)` are
`MissingMethodException` on 4.8 and resolve on 8, 9, 10. So only the first, fourth (signed and native int forms),
seventh and eighth rows can be default codegen (mscorlib-safe, plan 5.11.7); the others need a switch and a reference
that defines them (Design.md 7.1).

## 4. A managed reference from an unmanaged pointer (E3)

### 4.1 Shapes and run-time behaviour

`e3/e3.il` passes a `native int` argument straight to `T&` parameters: `Interlocked::Add(int32&, int32)`,
`CompareExchange(int32&, ...)`, `Volatile::Read/Write(int32&)`, `CompareExchange(native int&, native int, native int)`,
`Add(int64&, int64)` with `conv.i8` on the value and `conv.i` on the result, the same without the conversions, an
`int32*` parameter, a byref local (`stloc` of the `native int`), and today's `volatile. ldind.i4`/`stind.i4`. The memory
comes from `Marshal.AllocHGlobal` (not the GC heap). **All of them run and compute correctly on .NET Framework 4.8,
.NET 8 and .NET 10** (`e3 bad=0`, `logs/e3-verify.txt`); the GC tolerates byrefs that point outside its heap, as it
does for C# `ref *p`, which compiles to exactly this IL.

### 4.2 Verifier verdicts (ILVerify 10.0.12, PEVerify 4.8 `/il`)

| Shape | ILVerify | PEVerify | D21 today |
|---|---|---|---|
| `ldarg (native int); call ...(int32&, ...)` | `StackUnexpected: [found Native Int][expected address of Int32] Unexpected type on the stack.` | `[found Native Int][expected address of Int32] Unexpected type on the stack.` | **fails** |
| same with `native int&` / `int64&` | `... [expected address of IntPtr]` / `[expected address of Int64]` | `[expected address of Native Int]` / `[expected address of Long]` | fails |
| `int32*` parameter to `int32&` | `UnmanagedPointer` + the same `StackUnexpected` | `Unmanaged pointers are not a verifiable type.` | fails |
| byref local from `native int` | `StackUnexpected` at the `stloc` | same | fails |
| native int value to `int64` parameter (no `conv.i8`) | `[found Native Int][expected Long]` | same | fails (D2 forbids it anyway) |
| `volatile. ldind.i4` / `stind.i4` on `native int` | `StackByRef: [found Native Int] Expected ByRef on the stack.` | same text | **allowed** |

Both verifiers **continue after** `StackUnexpected` (the `add64_mixed` method gets three errors in each, the last one at
its `ret`), so an allowance for this message does not hide later type errors in pass 1. `msil-verify.py` with today's
policy exits 1 on `e3.exe` in both passes: the neutralized copy keeps the `call` with its `native int` argument.

Two helper shapes concentrate the error in private methods (`e3/e3b.il`, `e3/e3c.il`):

- **V2, one helper per operation**, `'<msil>atomic_add_4'(native int, int32) : int32` whose body is the direct call:
  callers are fully clean in both verifiers; the helper body carries the one `StackUnexpected`.
- **V3', one "as ref" helper per type**, `int32& '<msil>asref_i4'(native int)` = `ldarg.0; ret`: callers are fully
  clean (a type error behind it, `int64` passed as the value, is still reported by both); the helper has
  `StackUnexpected` and `ReturnPtrToStack`. With a `void*` parameter (V3) the callers instead get PEVerify's
  already-allowed `[found Native Int][expected unmanaged pointer]`, and the helper `UnmanagedPointer`,
  `StackUnexpected`, `ReturnPtrToStack`.

### 4.3 Inlining and cost (single thread, ns per operation; `logs/e3-bench.txt`)

| Shape (loop of 50M) | 4.8 (7 runs) | .NET 8 TC=0 | .NET 10 TC=0 | WSL 8 / 10 TC=0 | x64 code (.NET 8) |
|---|---|---|---|---|---|
| plain `ldind; add; stind` | 1.1-1.4 | 1.2-1.7 | 1.3-1.8 | 1.5-2.2 | - |
| V1 direct `Interlocked.Add`, result popped | 5.4-8.0 | 5.3-7.7 | 5.8-7.2 | 6.4-6.8 | `lock inc dword ptr [rsi]` |
| V3' `asref` helper + `Add` | **7.2-11.2** | 5.4-6.1 | 5.6-8.3 | 6.9 | `lock inc` (inlined) |
| V2 op helper (aggressiveinlining) | 5.6-7.8 | 5.5-7.8 | 5.7-9.2 | 5.6-7.9 | `lock inc` (inlined) |
| op helper, `noinlining` | 7.3-8.9 | 7.8-9.0 | 8.6-17.6 | 12.6-13.6 | call |
| `volatile. ldind.i4` | 0.8-1.2 | 0.5-1.0 | 0.6-0.9 | 0.7-1.9 | `cmp dword ptr [rsi], esi` (plain load) |
| `Volatile.Read` via `asref` | **1.8-3.5** | 0.5-0.8 | 0.5-0.6 | 0.5-0.8 | plain load |
| `volatile. stind.i4` | 0.5-3.3 | 0.5-0.7 | 0.5-0.6 | 0.7-1.0 | `mov` |
| `Interlocked.Exchange` (seq_cst store via xchg) | 6.9-8.2 | 4.8-5.9 | 5.3-8.2 | 9.3-10.9 | `xchg dword ptr [rsi], ecx` |
| `volatile. stind` + `MemoryBarrier` (trailing fence) | 5.3-7.5 | 5.6-7.2 | 5.6-7.7 | 7.8 | `mov; lock or dword ptr [rsp], 0` |
| CAS loop for an RMW without BCL API (`cas_add`) | 9.1-11.9 | 9.1-12.9 | 12.8-17.2 | 14.1-16.1 | `lock cmpxchg` loop |
| byte add by CAS on the containing word | 10.7-13.7 | 11.1-14.1 | 10.7-12.3 | 13.2-15.0 | |
| `Add(int64&)` with `conv.i8`/`conv.i` | 7.1-9.8 | 5.9-8.9 | 5.1-5.7 | 8.2-8.4 | `lock inc qword ptr [rsi]` |
| libatomic `__atomic_fetch_add_4` P/Invoke (Linux only) | - | - | - | 10.5-12.8 | P/Invoke |

Ranges merge the logged runs (`logs/e3-bench.txt`, `logs/e3-bench-fx-early.txt`) with the first run of the
investigation. The .NET 8 x64 disassembly (`DOTNET_JitDisasm`, `logs/e3-disasm.txt`) shows the same loop for V1, V2
and V3'. The
Framework JIT has no disassembler; its timings put V3' and `Volatile.Read` via `asref` at the cost of a non-inlined
call (inferred: the Framework JIT does not inline a method that returns a byref; *unverified* beyond timing). `fetch_add`
that uses the old value is `lock xadd` plus `add eax, edx; sub eax, edx` (the JIT does not fold
`Interlocked.Add(p, v) - v`; two ALU ops); a cmpxchg whose success flag is used is `lock cmpxchg; sete`.

**Conclusion.** V1 and V2 cost the same on every runtime; V3' costs a call on .NET Framework and is rejected. Loads and
stores need no byref (`volatile.` prefix). The choice between V1 and V2 is a verification-policy choice (section 11).

## 5. The memory model (E6, E3)

### 5.1 What the CLR guarantees and what the JITs emit

ECMA-335 I.12.6.7: a `volatile.` read has acquire semantics, a `volatile.` write release semantics; I.12.6.6: aligned
reads and writes up to the native int size are atomic. `Interlocked` operations and `MemoryBarrier` are full fences
(documented; dotnet/runtime's memory-model document says the same for CoreCLR - *not re-read here, unverified*). What
the JITs emit (`logs/e3-disasm.txt`, `logs/e3-arm64-volatile.txt`):

| IL | x64 (.NET 8 JIT) | linux-arm64 .NET 10 crossgen2 `armv8-a` | `armv8.3-a` (RCPC) | linux-arm64 .NET 8 |
|---|---|---|---|---|
| `volatile. ldind.i4`, value used | `mov` | `ldar w` | **`ldapr w`** | (same as 10, `armv8-a`) |
| `volatile. ldind.u1` / `ldind.i` | `movzx` / `mov` | `ldarb` / `ldar x` | `ldaprb` / `ldapr x` | |
| `volatile. ldind.i4; pop` | `cmp [rsi], esi` | **`ldr wzr`** (no acquire) | `ldr wzr` | `ldr wzr` |
| `volatile. stind.i4` / `stind.i1` | `mov` | `stlr` / `stlrb` | `stlr` / `stlrb` | `stlr` |
| `Interlocked.Exchange(int32&)` | `xchg` | `swpal` | `swpal` | `swpal` |
| `Interlocked.CompareExchange(int32&)` | `lock cmpxchg` | `casal` | `casal` | `casal` |
| `Interlocked.Add(int32&)` | `lock xadd` / `lock inc` | a call in R2R code (*the runtime JIT with LSE uses `ldaddal`; not observed*) | call | call |
| `Interlocked.MemoryBarrier()` | `lock or dword ptr [rsp], 0` | `dmb ish` | `dmb ish` | `dmb ish` |
| `volatile. stind; MemoryBarrier; volatile. ldind` (Dekker) | `mov; lock or; mov` | `stlr; dmb ish; ldar` | `stlr; dmb ish; ldapr` | |

(crossgen2 compiles R2R code; it used LSE `casal`/`swpal` even for `armv8-a`. R2R code may differ from the runtime
JIT's in such details; *unverified on hardware*.)

Consequences:

- **seq_cst store = release store + full fence.** `Volatile.Write` alone is a plain `mov` on x64, so store-load
  reordering stays visible; on ARM64 `stlr` followed by an RCpc `ldapr` (what .NET 10 emits for volatile loads on RCPC
  CPUs) may also reorder. `volatile. stind` + `Interlocked.MemoryBarrier()` gives `mov; lock or` / `stlr; dmb ish`,
  the standard trailing-fence mapping; seq_cst loads then only need acquire (`volatile. ldind`), on x64 and on ARMv8
  (other-multi-copy-atomic, so IRIW cannot be observed with acquire loads). `Interlocked.Exchange` would also be
  correct (`xchg`, `swpal`); the fence form needs no byref, works for every size (no partword expansion for 8/16-bit
  seq_cst stores) and costs the same (section 4.3).
- **A popped volatile load loses its ordering** in both JITs: on ARM64 `ldr wzr` has no acquire, and on x64 the JIT
  does not treat it as a barrier either (5.3). A C acquire load whose value is unused (rare: `(void)atomic_load(p)`)
  needs a trailing fence to keep its meaning.

### 5.2 Store buffering (`e6/sb.c`, 2,000,000 rounds; `logs/e6-sb.txt`)

T0: `x = 1; r0 = y`; T1 (main): `y = 1; r1 = x`, through the prototype pipeline (section 1). Count of `r0 == r1 == 0`:

| Mapping | 4.8 | .NET 8 | .NET 8 TC=0 | .NET 10 | WSL 8 | WSL 10 | native Linux |
|---|---|---|---|---|---|---|---|
| seq_cst: `volatile. stind` + `MemoryBarrier`, `volatile. ldind` | **0** | **0** | **0** | **0** | **0** | **0** | 0 |
| release store, acquire load (C allows the outcome) | 22,675-23,801 | 6,463-37,774 | 32,708 | 36,985-37,566 | 63,649-80,852 | 39,421-46,300 | 105,177-505,074 |

The second row is also what a seq_cst store lowered to `volatile. stind` (or `Volatile.Write`) alone would give.

### 5.3 The JIT hoists plain loads (`e6/hoist.il`, `e6/hoist2.il`; `logs/e6-hoist.txt`)

A managed thread spins on `ldind.i4` of a `native int` address; another thread stores 1 after 300 ms; `Join(3000)`:

| Loop body | 4.8 | .NET 8 tiered | .NET 8 TC=0 | .NET 10 tiered | .NET 10 TC=0 |
|---|---|---|---|---|---|
| plain `ldind.i4` | **hangs** | exits | **hangs** | **hangs** | **hangs** |
| plain `ldind.i4` + a counter | **hangs** | exits | **hangs** | **hangs** | **hangs** |
| `volatile. ldind.i4` | exits | exits | exits | exits | exits |
| plain load + `call` of an empty `noinlining` method | exits | exits | exits | exits | exits |
| plain load + `volatile. ldsfld; pop` | **hangs** | exits | **hangs** | **hangs** | **hangs** |
| plain load + `volatile. ldsfld` whose value is used | exits | exits | exits | exits | exits |

So **relaxed (monotonic) atomic loads must carry `volatile.`**: LLVM keeps them in the loop, but the JIT would hoist
them (the C program `e6/barrier-spin.c WHICH=2`, a relaxed `atomic_load` spin, exits on all runtimes with the
`volatile.` lowering).

### 5.4 D36's compiler barrier does not constrain the JIT (`e6/barrier-spin.c`; `logs/e6-barrier.txt`)

```c
int plain_flag;                       /* set to 1 by another thread after 300 ms */
for (long long i = 0; i < 20000000000LL; i++) {
  if (plain_flag) return 1;
  __asm__ volatile("" ::: "memory");  /* D36: MEMBARRIER, a comment in the IL */
}
```

The IL keeps the reload (`ldsfld native int 'plain_flag'; ldind.i4` in the loop) with `//MEMBARRIER` beside it.
Natively (Linux) the loop exits. Under the CLR it **gives up after the iteration limit (the load was hoisted) on
Framework 4.8, .NET 8 with `TieredCompilation=0`, .NET 10 and WSL .NET 10**, and exits only where Tier-0 code runs
(.NET 8 tiered, WSL .NET 8). Code like this is a data race in C11, but it is the classic pre-C11 idiom (`barrier()`,
`cpu_relax()`), and clang and GCC give the memory clobber the meaning "reload memory". Replacing the comment by
`call void '<msil>compiler_barrier'()`, a private `noinlining` method whose body is `ret`, makes the loop exit on all
runtimes (`bs0fix`). Cost per barrier in a loop of plain increments (`e6/cb.il`): none 1.1-2.4 ns, empty call 1.7-2.3,
`MemoryBarrier` 5.2-6.6 ns per iteration (two runs: `logs/e6-barrier.txt` and the first measurement). The same applies to `atomic_signal_fence` (rarely in loops) and the
operand-carrying barriers of M12 (`black_box`): an empty call costs little and keeps the JIT honest.

## 6. 8- and 16-bit atomics (E4)

LLVM's `AtomicExpand` builds the partword expansion when `TargetLowering::getMinCmpXchgSizeInBits()` is 32 and the
target asks for a CAS loop (`AtomicExpandPass.cpp:941-994` `tryExpandAtomicRMW`, `1247-1300`
`expandPartwordAtomicRMW`, `1302-1343` `widenPartwordAtomicRMW` for and/or/xor, `1345-...` `expandPartwordCmpXchg`;
for `cmpxchg` it happens whenever `shouldExpandAtomicCmpXchgInIR` returns `None`, `2008-2019`). The masked word is
read with an atomic load, the new word computed and `cmpxchg i32` retried until the whole word matched.

`e4/subword.c` writes the same expansion in C (none of the built targets has `MinCmpXchgSizeInBits` 32, so `opt`
could not produce it): 4 threads; two add 1 to byte 0 and byte 1 (1,000,003 times each) and 3 to halfwords at byte 4
and 6; two store `i & 0xff` with plain byte stores to bytes 2 and 3 (2,000,006 times) and check after each store that
their byte still holds it. Result (`logs/e4-subword.txt`), 3 runs each on 4.8, .NET 8, .NET 8 TC=0, .NET 10, WSL 8, WSL
10: **all updates kept, no plain store overwritten**. Negative control (`subword-broken.c`: the new word written back
with a plain store instead of the CAS): `lost byte update`, `lost halfword update`, `plain store overwritten during
the run` on every runtime. A CAS failure caused by a neighbour's plain store just retries, so plain accesses to
adjacent bytes are never lost; reading the 4-byte aligned word never crosses a page.

The .NET 9 `CompareExchange(uint8&/uint16&)` and `Exchange(...)` could replace the loops behind a switch with .NET 9+
references. Mixing both forms on the same byte is safe (hardware atomics on overlapping locations; x86 locked
operations of any size are atomic against each other, ARMv8 LSE single-copy atomicity covers the bytes each
instruction touches - *the latter from the architecture, not measured*). Value: 8/16-bit RMW are rare (Rust `AtomicBool`,
`Once` state) and cost 10-14 ns instead of about 5-7; a later, low-priority switch.

## 7. 16-byte and generic atomics (E5)

- **No BCL API** for 128-bit atomics on any runtime (section 3).
- **Native oracle** (`e5/i128.c`: `_Atomic __int128` add and CAS, a 24-byte struct store/load, lock-free queries):
  `x86_64-pc-windows-msvc` + lld-link cannot link it (`undefined symbol: __atomic_load_16`; with `-mcx16` the 16-byte
  ops are inline `cmpxchg16b` and the 24-byte ones `undefined symbol: __atomic_store`, `__atomic_load`,
  `__atomic_is_lock_free`; VS 2026's `clang_rt.builtins-x86_64.lib` has no `__atomic_*`, compiler-rt builds it with
  `COMPILER_RT_EXCLUDE_ATOMIC_BUILTIN` on by default, `compiler-rt/lib/builtins/CMakeLists.txt:336`).
  `x86_64-unknown-linux-gnu` needs `-latomic`; with WSL's libatomic it prints `1 3 lf16=0 lf24=0 always16=0`
  (libatomic reports 16 bytes as **not** lock-free on x86-64).
- **compiler-rt `atomic.c`** compiles for all three MSIL triples (`-O2 -ffreestanding -fno-builtin
  -fvisibility=hidden`, `-D__ELF__=1` on Linux; helpers-switches.md 2.2-2.6 already lists it for the P3 helper archive,
  family B). Its lock is `_Atomic(uintptr_t)` spun with `__c11_atomic_compare_exchange_weak` (acquire) and released
  with a release store (`atomic.c:104-121`), 1024 locks hashed by address (`lock_for_pointer`, `atomic.c:124-139`),
  so it lowers to section 10's `Interlocked.CompareExchange(native int&)` and `volatile. stind.i`.
- **Infinite recursion on aarch64 (and x86-64 with cx16).** `atomic.c` takes the lock-free path when
  `__atomic_always_lock_free(16, 0)` (`IS_LOCK_FREE_16`, `atomic.c:142-149`). On `aarch64-unknown-linux-gnu-msil` that
  is 1, and `__atomic_load_16`'s body is `load atomic i128` (`logs/e5-e7.txt`); with a 64-bit backend maximum,
  AtomicExpand turns it into a call of `__atomic_load_16` - itself. On x86-64 the body is the lock path. Fix in
  `MSILTargetInfo` (`clang/lib/Basic/Targets/MSIL.h`): override `setMaxAtomicWidth()` to call the native one and then
  cap `MaxAtomicInlineWidth` at 64. `MaxAtomicPromoteWidth` (the `_Atomic` layout) is untouched, so struct layouts and
  the ABI stay native; `__atomic_always_lock_free(16)` becomes 0 and `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16` disappears
  (section 8).
- **Per-assembly locks break atomicity across assemblies.** `atomic.c`'s `static Lock locks[1024]` lives in each
  output assembly's data segment. A program and a `-shared` library built by this toolchain that both operate on one
  16-byte atomic object (inline functions of a lock-free library's header) would use different locks. Natively this is
  why libatomic is a shared library. A process-wide table needs no support assembly: `e5/appdom/liba.il` (the
  proposed `'<msil>atomic_lock_table'()`) takes `string.Intern("msil.atomic.locks.v1")` as a `Monitor` lock, looks the
  name up with `AppDomain.CurrentDomain.GetData`, allocates and zeroes 8 KB with `Marshal.AllocHGlobal` and publishes it
  with `SetData` when absent, and caches the pointer in a static field. Two separately assembled libraries get **the
  same table on 4.8, .NET 8 and .NET 10** (all members bound through `[mscorlib]`; ILVerify reports only the allowed
  `Unverifiable` for `initblk`). `atomic.c`'s spinlocks then work on that table. Uncontended costs (`e5/cs/main.cs`):
  spinlock CAS + release store 6.1-11.5 ns, `Monitor.Enter/Exit` on an interned string 19.6-23.8 ns,
  `Interlocked.Increment` 4.7-6.8 ns; interned strings are shared across assemblies on all three runtimes.
- **Native libatomic** (Linux only) would give native interoperability (cmpxchg16b where libatomic chooses it), but it
  is not on Windows, not always installed, costs a P/Invoke per operation, and the helper archive resolves the
  symbols first anyway (archives win over native libraries, helpers-switches.md 2.5).

## 8. Lock-free answers (E7)

| Query | x86-64 MSIL today (= native) | aarch64 MSIL today (= native) | Proposed for every MSIL triple |
|---|---|---|---|
| `ATOMIC_{CHAR,SHORT,INT,LONG,LLONG,POINTER}_LOCK_FREE` | 2 | 2 | 2 (CAS loops are lock-free) |
| `__atomic_always_lock_free(1/2/4/8, 0)`, `atomic_is_lock_free` on 1-8 bytes | 1 / true | 1 / true | 1 / true |
| `__atomic_always_lock_free(16, 0)` | 0 (**1 with `-mcx16`, `-march=x86-64-v2` and up**) | **1** | 0 |
| `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16` | undefined (**defined with cx16**) | **defined** | undefined |
| `atomic_is_lock_free` on a 16-byte object | call `__atomic_is_lock_free` (libatomic: false) | folded **true** | call -> `atomic.c`: false |
| on 24 bytes, `__atomic_is_lock_free(n, p)` | call -> false | call -> false | call -> `atomic.c`: false for n > 8, aligned true for 1-8 |

Only the 16-byte answers change, and they become what native glibc/libatomic already says on x86-64. Design.md
section 3 says the predefines are the native ones plus `__MSIL__` and minus `__ELF__`; this cap adds one documented
exception (decision Q-AT7).

## 9. Threads (E8)

### 9.1 Threads through the host libc work once start routines are native-callable

| Program | 4.8 | .NET 8 | .NET 10 | WSL .NET 8 | WSL .NET 10 | native |
|---|---|---|---|---|---|---|
| `counter.c`: 4 threads (`CreateThread` / `pthread_create`) x 2,000,000 `atomic_fetch_add_explicit(relaxed)` on `_Atomic long long`, plus a racy plain counter | 8,000,000; plain counter lost updates | same | same | same | same | same |
| `texit.c`: thread ends with `ExitThread(42)` / `pthread_exit((void*)42)` | exit code 42 | 42 | 42 | value 42 | 42 | 42 |
| `c11thr.c`: 4 `thrd_create` threads, `mtx_lock` 200,000 times each, `call_once`, `thrd_join` results | exact, once, ok (`VCRUNTIME140_THREADS.dll`) | same | same | same (`libc.so.6`) | same | same (Windows native) |
| `tcreate.c EXITRUN=1`: main returns while a created thread spins forever | exits with main's status 3 | 3 | 3 | 3 | 3 | 3 |

(`logs/e8-threads.txt`.) Threads that enter managed code through a reverse-P/Invoke thunk are attached to the runtime
on first entry and are background threads, so `return` from `main` ends the process as natively. `ExitThread` and
`pthread_exit` called from managed frames abandon those frames without running `finally` blocks (glibc's forced unwind
stops at the frames it cannot unwind and ends the thread anyway); this matched native behaviour in every run, but
nothing in a frame below such a call may rely on `finally` (Phase 3's setjmp lowering uses filters, not `finally`).

### 9.2 Cost

Contended increments (4 threads on one cache line, 7 runs, ns per increment including thread start): native Windows min
9.8 / median 20.5; MSIL 4.8 13.7 / 20.0; .NET 8 11.7 / 13.0; .NET 10 10.9 / 14.1; native Linux 8.0 / 11.5; WSL .NET 8
8.6 / 10.3; WSL .NET 10 8.7 / 12.8. Thread create + join (WSL, 2000 threads after a warm-up, 3 runs): native 170-542 us,
MSIL .NET 8 230-412 us, .NET 10 210-358 us (two series, the second in `logs/e8-threads.txt`). On this Windows host thread creation itself takes 20-80 ms (native too:
41-75 ms per thread; some host software hooks thread creation), so Windows creation cost was not comparable.

### 9.3 What threads need from the rest of Phase 3

- **Native-callable thunks (P3-B, callbacks recon)**: `pthread_create`, `CreateThread`, `_beginthread(ex)`,
  `thrd_create`, `call_once`, `pthread_once`, `pthread_key_create`, `tss_create` and `pthread_atfork` are on plan
  5.10.14's callback list (`lld/MSIL/NativeLibraries.cpp:80-123`) and are link errors today. The delegate thunk of
  Design.md 4.5 was enough for every thread test here; nothing thread-specific is needed beyond keeping the delegate
  alive for the life of the program (a thread may start long after the call that created it).
- **Threads stay native.** Intercepting `pthread_create` and running the routine on a `System.Threading.Thread` would
  break `pthread_self`, `pthread_t` identity, attributes, cancellation and every native API that takes a `pthread_t`;
  with a host libc the native thread APIs are faithful by construction. The portable triple would implement its libc's
  threads over `System.Threading.Thread` later (through managed calls); nothing here precludes that.
- **TLS and the shadow stack (P3-A, frame-tls recon)**: a thread created natively runs managed code after its first
  reverse P/Invoke; `[ThreadStatic]` fields are per OS thread there, so first-use allocation works. Thread exit does not
  free an unmanaged block hung off a `[ThreadStatic]` field: a small finalizable holder object in the field (its
  finalizer frees the block once the thread is gone) is one option for P3-A. Default stack sizes are native (Linux: the
  `RLIMIT_STACK` default of glibc; Windows: the reserve of the image's PE header, which is `dotnet.exe`'s under the
  host).
- **`'<msil>Data'` spill race** (plan section 12, 'Known limit after M22 (spilled `'<msil>Data'` fields and threads)'):
  P3-B's threads are the planned fix (run phase 1 in the module initializer of a library whose fields spill); C threads
  make the two-thread first use reachable from C for the first time.
- **C11 threads on Windows** come from `VCRUNTIME140_THREADS.dll` (VS 2022 17.8+ redistributable; `vcruntime.lib`
  imports `thrd_create`, `mtx_*`, `call_once`, `tss_create` from it), not from the UCRT. The windows-msvc driver's
  default libraries (`ucrt.lib`, `kernel32.lib`, `oldnames.lib`, Design.md 8) do not name `vcruntime.lib`, so a C11
  threads program on Windows needs `-lvcruntime` and the VC++ redistributable, as `link.exe` would need for the import.
  glibc has them in `libc.so.6` (`thrd_create@@GLIBC_2.34`, `@GLIBC_2.28`).

## 10. Recommended lowering

### 10.1 Per operation and size (64-bit triples)

| LLVM operation | Size | Default CIL (runs on 4.8 and modern .NET) | Switch / note |
|---|---|---|---|
| `load atomic` monotonic, acquire, seq_cst | 1, 2, 4, 8 | `volatile. ldind.{i1,u1,i2,u2,i4,i}` (floating point `ldind.r4/r8`, no integer cast) | dead acquire/seq_cst load: add a trailing `fence acquire` (5.1) |
| `load atomic` unordered | 1-8 | `ldind.*` (no prefix needed: no ordering, no tearing) | `volatile.` is also fine |
| `store atomic` monotonic, release | 1-8 | `volatile. stind.*` | |
| `store atomic` seq_cst | 1-8 | `volatile. stind.*` + `call Interlocked::MemoryBarrier()` | `shouldInsertTrailingSeqCstFenceForAtomicStore` |
| `atomicrmw xchg` | 4 / 8 | `call int32 Interlocked::Exchange(int32&, int32)` / `native int Exchange(native int&, native int)` | float/ptr cast to integer (default hook) |
| `atomicrmw add` | 4 / 8 | `Add(int32&, int32)` / `conv.i8; Add(int64&, int64); conv.i`; old value = result - v | unused result: `pop` (JIT: `lock add/inc`) |
| `atomicrmw sub` | 4 / 8 | `ATOMIC_LOAD_SUB` Expand -> add of `0 - v` (`LegalizeDAG.cpp:3377`) | |
| `atomicrmw and`, `or` | 4 / 8 | CAS loop (AtomicExpand `CmpXChg`) | `Interlocked.And/Or` (returns old) with net5+ references, switch |
| `atomicrmw xor`, `nand`, `max`, `min`, `umax`, `umin`, `fadd`, `fsub`, `fmax`, `fmin`, `fmaximum`, `fminimum`, `uinc_wrap`, `udec_wrap`, `usub_cond`, `usub_sat` | 4 / 8 | CAS loop | none in the BCL |
| `cmpxchg` (weak or strong, any orders) | 4 / 8 | `CompareExchange(int32&, new, cmp)` / `(native int&, native int, native int)`; success = `old == cmp` (`ATOMIC_CMP_SWAP_WITH_SUCCESS` Expand) | strong is a valid weak |
| any RMW, `cmpxchg` | 1, 2 | partword CAS loop on the aligned 32-bit word (`setMinCmpXchgSizeInBits(32)`) | .NET 9 `CompareExchange/Exchange(uint8&/uint16&)`, switch, low priority |
| any | 16, aligned | `__atomic_*_16` from the helper archive (`atomic.c`, lock-based, process-wide table) | |
| any | misaligned, odd sizes, > 16 | generic `__atomic_load/store/exchange/compare_exchange` (clang or AtomicExpand), the same archive | |
| `__atomic_is_lock_free` | | archive (`atomic.c`) | |
| `fence` (system scope, any ordering) | | `call Interlocked::MemoryBarrier()` (D36, unchanged) | .NET 10 `Volatile.ReadBarrier/WriteBarrier` for acquire/release fences, switch, optional |
| `fence syncscope("singlethread")`, empty `asm` with memory clobber | | **`call void '<msil>compiler_barrier'()`** (private, `noinlining`, body `ret`) instead of the `//MEMBARRIER` comment | D36 amendment |
| any atomic with `syncscope("singlethread")` | | as system scope | stronger, correct |
| `load/store volatile` (C `volatile`) | | `volatile.` prefix (D24, unchanged) | with `-fms-volatile`: atomic acquire/release, i.e. the same IL |

`Interlocked` members take the address as the `native int` it already is (V1, section 11 decides the verification
side); the `int64` overload of `Add` keeps D2 by `conv.i8` before and `conv.i` after the call, as `MEMMOVE` does for
`Buffer::MemoryCopy` today. On a future 32-bit triple (phase 5) 8-byte loads must become `Interlocked.Read(int64&)` and
8-byte stores `Interlocked.Exchange(int64&)`, because a `volatile. ldind.i8` is not atomic on a 32-bit CLR (ECMA-335
I.12.6.6); the hooks below are per size, so that is a local change.

### 10.2 Backend changes (llvm/lib/Target/MSIL)

- `MSILISelLowering.cpp` (constructor, replacing lines 394-396): `setMaxAtomicSizeInBitsSupported(64)`;
  `setMinCmpXchgSizeInBits(32)`; `setOperationAction(ISD::ATOMIC_LOAD_SUB, {MVT::i32, MVT::i64}, Expand)`; the
  `ATOMIC_CMP_SWAP_WITH_SUCCESS` expansion is the default; every other `ATOMIC_LOAD_*` stays unselectable because the IR
  hook expands it first. New overrides (`llvm/include/llvm/CodeGen/TargetLowering.h:2514-2570`, `2336-2337`):
  `shouldExpandAtomicRMWInIR` (`None` for `xchg`/`add`/`sub` of 32 and 64 bits, and `and`/`or` when their switch is on;
  `CmpXChg` for everything else, which also triggers the partword expansion below 32 bits);
  `shouldExpandAtomicCmpXchgInIR` (`None`; partword below 32 bits is automatic);
  `shouldInsertTrailingSeqCstFenceForAtomicStore` (true for a seq_cst `StoreInst`; the default `emitTrailingFence`,
  `TargetLoweringBase.cpp:2614-2621`, creates `fence seq_cst`, AArch64's windows-msvc override at
  `AArch64ISelLowering.cpp:33269` is the precedent); `shouldCastAtomicLoadInIR`/`shouldCastAtomicStoreInIR` returning
  `None` for `float`/`double` (so they select `ldind.r4/r8` directly instead of an integer load plus a bitcast through a
  stack slot, D16).
- `MSILInstrMemory.td`: atomic load/store patterns onto the existing `LDIND_*`/`STIND_*` records
  (`atomic_load_nonext_32/64`, `atomic_load_zext_8/16`, `atomic_load_sext_8/16`, `atomic_load_aext_8/16`,
  `atomic_store_8/16/32/64`, `TargetSelectionDAG.td:2136-2280, 2438-2460`); new records for `Interlocked` calls modelled
  on `MEMORY_BARRIER`/`MEMMOVE` (`hasSideEffects = 1, mayLoad = 1, mayStore = 1`, a BCL member operand that
  `MSILMCInstLower` names, like `MSILMCInstLower.cpp:624-639`): `ATOMIC_XCHG_I32/I64`, `ATOMIC_ADD_I32/I64` (the
  pattern computes `old = new - v`), `ATOMIC_CMPXCHG_I32/I64` (operand order `addr, new, cmp`), and behind the switch
  `ATOMIC_AND/OR_I32/I64`; `COMPILER_BARRIER` for the single-thread fence (replacing the generic `MEMBARRIER`
  pattern of D36).
- `MSILSetMemFlags.cpp:113-117` (`computeMemFlags`): also set `volatile.` when `MMO->isAtomic()` and the ordering is
  monotonic or stronger (`MachineMemOperand::getSuccessOrdering()`).
- `MSILPrepareIR`: a dead `load atomic` with acquire or seq_cst ordering gets `fence acquire` after it (5.1).
- `llvm/include/llvm/CLI/BCLMembers.def` (append only, `MscorlibSafe` true): `Interlocked_Add_I4` (`int32`,
  `int32&, int32`), `Interlocked_Add_I8` (`int64`, `int64&, int64`), `Interlocked_Exchange_I4`, `Interlocked_Exchange_I`
  (`native int`, `native int&, native int`), `Interlocked_CompareExchange_I4`, `Interlocked_CompareExchange_I`; for
  switches (`MscorlibSafe` false): `Interlocked_And_I4/I8`, `Interlocked_Or_I4/I8`, and later the .NET 9 8/16-bit
  forms and `Volatile_ReadBarrier/WriteBarrier`. `msil/utils/check-mscorlib-safe.py` proves the safe ones on 4.8, 8, 10
  (E2 already shows them in all three).
- `'<msil>compiler_barrier'` and `'<msil>atomic_lock_table'` are IL helpers (helpers-switches.md family A): msil-ld's
  trailer builder emits them on demand, `-S` text through the same builder.
- `atomic-errors.ll`, `imports-linked.ll`, `object-calls.ll` and `libcall-policy.ll` lose their atomic error cases
  (section 12 lists the replacement tests).

### 10.3 Clang, the helper archive, msil-ld

- `clang/lib/Basic/Targets/MSIL.h` (`MSILTargetInfo`): `void setMaxAtomicWidth() override { Native::setMaxAtomicWidth();
  this->MaxAtomicInlineWidth = std::min<unsigned>(this->MaxAtomicInlineWidth, 64); }`
  (`TargetInfo::setMaxAtomicWidth`, `clang/include/clang/Basic/TargetInfo.h:857`, is the virtual that X86 overrides for
  cx16, `clang/lib/Basic/Targets/X86.h:828-831`; `TargetInfo::CreateTargetInfo` calls it for every target right after
  `handleTargetFeatures`, `clang/lib/Basic/Targets.cpp:976`, so the override also caps AArch64's constructor value of
  128, `AArch64.cpp:163-164`). MSIL.h is included only by the target factory, so this is a small rebuild.
- Driver: optionally restore the native `-fms-volatile` default on `x86_64-pc-windows-msvc-msil`
  (`Clang.cpp:6414-6422`): with atomics it yields the same IL as plain volatile, and stock-clang bitcode (Design.md 3,
  'needs `-fno-ms-volatile` until phase 3') needs no flag any more.
- Helper archive (P3-A, backlog S3): `atomic.c` plus an MSIL `lock_for_pointer` over
  `'<msil>atomic_lock_table'()` (an `extern` the archive member calls, defined by msil-ld's IL helper). compiler-rt's
  `int_lib.h` `__ELF__`/`__MSIL__` change of helpers-switches.md 2.3 applies.
- msil-ld: helpers-switches.md 2.5's rule (a `__` RuntimeLibcalls name never binds to a native library) also stops
  today's silent libatomic binding; `__atomic_*` then always come from the archive.

## 11. The verification policy

D21 (user-approved, Status.md section 4) allows `[found Native Int] Expected ByRef on the stack.` (every `ldind`/`stind`)
and PEVerify's `[found Native Int][expected unmanaged pointer]` (`Buffer::MemoryCopy`), and its pass 2 replaces
`ldind`/`stind`/`cpblk`/`calli`/`ldftn` by checking stubs. An `Interlocked` call with a `native int` address is the
same kind of unverifiability (an unmanaged address used as a managed one) but a different message. Options:

- **V1 (recommended): direct calls, D21 refined.** msil-verify accepts ILVerify `StackUnexpected` and PEVerify
  `Unexpected type on the stack` exactly in the form `[found Native Int][expected address of <T>]` when the ildasm
  listing shows, at that offset, a `call` of a `System.Threading.Interlocked` member whose first parameter is `<T>&`
  (the same offset check as the RVA `ldsflda` allowance), and pass 2 replaces each such call by a stub
  `'<msil-verify>'::'Interlocked.<Name>.<T>'(native int, ...)` with the member's other parameter and return types, so
  every other operand stays type-checked with no allowance. Both verifiers already continue after the message, so
  pass 1 loses nothing. Codegen is one instruction per operation; the same text in `-S` and objects; best speed on
  every runtime; and a later managed-memory feature (FeatureIdeas) can pass real byrefs to the same calls.
- **V2: private per-operation helpers.** `'<msil>atomic_add_4'(native int, int32)` etc., `aggressiveinlining`
  (inlined on all runtimes, measured), defined once per output by msil-ld; callers verify with no allowance; msil-verify
  accepts the one `StackUnexpected` only inside a method of that name whose listing equals the template. Costs a helper
  family (about 12 methods), a dedup story for objects, and the inliner's budget in very large methods.
- **V3': one "as ref" helper per type** - rejected: not inlined by the .NET Framework JIT (+2 ns per atomic, 2x per
  `Volatile.Read`).
- Keep D21 unchanged - impossible without either a byref-producing helper (V2/V3') or giving up `Interlocked`.

## 12. Tests

- `llvm/test/CodeGen/MSIL/atomics.ll` (replaces `atomic-errors.ll`): every row of 10.1 for `i8`, `i16`, `i32`, `i64`,
  `ptr`, `float`, `double`, each ordering, on `x86_64-unknown-linux-gnu-msil`, `x86_64-pc-windows-msvc-msil` and
  `aarch64-unknown-linux-gnu-msil`, `-O0` and `-O2`, `-verify-machineinstrs`: exact IL (prefix, call, `pop` for an
  unused result, the trailing `MemoryBarrier` of seq_cst stores, the CAS loops' `CompareExchange`), `ilasm-verify`.
- `atomics-partword.ll`: `cmpxchg i8/i16` and `atomicrmw add/xchg/and/or/xor i8/i16` become a loop over
  `CompareExchange(int32&)` on `addr & ~3` with shift and mask.
- `atomics-libcalls.ll`: `i128` and misaligned atomics stay `__atomic_*_16`/generic calls (object symbols; `-S`: the D14
  error until `-S` can link the archive), no other `__atomic_*` symbol appears for 1-8 bytes.
- `fence.ll` (updated): single-thread fence -> `call void '<msil>compiler_barrier'()`; system fences unchanged.
- clang: `clang/test/Preprocessor` or `clang/test/CodeGen/MSIL/atomic-lock-free.c`: no
  `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16` and `__atomic_always_lock_free(16, 0) == 0` on aarch64 MSIL triples and on
  x86-64 MSIL with `-mcx16`/`-march=x86-64-v2`; `sizeof`/`_Alignof(_Atomic __int128)` unchanged (16/16).
- `msil/test` execution (transcript against the native oracle): single-threaded semantics of every C11/GNU/`__sync`
  operation and return value (cmpxchg failure writes the expected value back, `fetch_*` return old values, signed and
  unsigned min/max, float `+=`), the MSVC `_Interlocked*` family on the Windows triple, `atomic_is_lock_free` answers;
  16/24-byte atomics need an expected file instead of the oracle (native Windows cannot link them, the E7 oracle has no
  libatomic).
- Multi-threaded (need P3-B thunks; on `obj` and `lto`, both OSes, `TieredCompilation=0` legs per D38): `counter.c`
  (exact count), `sb.c` (seq_cst: zero forbidden outcomes; a fixed number of rounds keeps it under a few seconds),
  `subword.c` (adjacent bytes), a relaxed-load spin (must terminate), the compiler-barrier spin (must terminate, if
  D36 is amended), `texit.c`, `c11thr.c` (glibc; Windows only with `-lvcruntime`), a 16-byte atomic shared between a
  program and a `-shared` library (process-wide lock table).
- Verification: an msil-verify unit test that accepts an `Interlocked` call with a `native int` address and still
  rejects a wrong-typed value argument behind it and a `native int` passed to a non-`Interlocked` byref parameter
  (V1).
- `msil/test/M2/08-semantics/matrix.txt`: the `native-callback` row (thread start routines are callbacks) moves with
  the thunks; no atomics row exists.
- Rust: the std experiment's `v6_thread` without `lower-atomic` once TLS and thunks exist.

## 13. Decisions

**Q-AT1. How are `Interlocked` members given an unmanaged address, and how does the verification policy accept it?**
Options: (a) V1, direct calls with the `native int` address, D21 refined with a listing-pinned allowance and pass-2
stubs; (b) V2, private per-operation helpers that carry the only verifier error; (c) V3', a per-type "as ref" helper;
(d) no `Interlocked`, everything through helpers in C (impossible: C cannot express a CAS without the backend).
Recommendation: (a). Evidence: 4.1-4.3 (same machine code for (a) and (b); (c) not inlined on Framework; verifiers
continue after the message). Needs the user: yes (D21 was approved by the user).

**Q-AT2. seq_cst store mapping.** Options: (a) `volatile. stind` + `Interlocked.MemoryBarrier()` (trailing fence);
(b) `Interlocked.Exchange` with the result popped; (c) `volatile. stind` alone. Recommendation: (a). Evidence: 5.1-5.2
((c) shows the forbidden outcome 6,463-80,852 times per 2M rounds; (a) never; (a) and (b) cost the same; (a) needs no
byref and no partword loop). Needs the user: no.

**Q-AT3. Relaxed loads and stores.** Options: (a) `volatile.` prefix on every atomic load/store of monotonic or
stronger ordering; (b) plain `ldind`/`stind` for monotonic. Recommendation: (a). Evidence: 5.3 (plain loads hoisted
out of spin loops on 4.8, .NET 8 optimized and .NET 10); cost on x64 nil, on ARM64 `ldar`/`ldapr` instead of `ldr`.
Needs the user: no.

**Q-AT4. 8/16-bit RMW and cmpxchg.** Options: (a) partword CAS loop on the containing word, all runtimes; (b) .NET 9
8/16-bit `Interlocked` behind a switch in addition. Recommendation: (a) now, (b) later if ever. Evidence: section 6,
section 3 (`MissingMethodException` on 4.8 and 8). Needs the user: no.

**Q-AT5. `atomicrmw and/or`.** Options: (a) CAS loop in IR (AtomicExpand); (b) `Interlocked.And/Or` behind a codegen
switch, a required request (the link fails without a net5+ reference); (c) plan 5.11.5's optional-substitute request:
codegen calls a private thunk, msil-ld binds `Interlocked.And/Or` when a reference defines it and otherwise makes the
thunk call an archive helper that runs the CAS loop. Recommendation: (a) now; (c) later if P3-A builds the substitute
thunks anyway (no switch needed, capability from the references). Evidence: section 3, 4.3 (CAS loop about 2x a
single `lock` instruction). Needs the user: no.

**Q-AT6. 16-byte and generic atomics.** Options: (a) compiler-rt `atomic.c` in the helper archive with its
per-assembly lock table; (b) the same with a process-wide lock table found by name through `AppDomain` data under an
interned-string `Monitor` lock (an IL helper of msil-ld); (c) P/Invoke native libatomic (Linux only); (d) errors.
Recommendation: (b). Evidence: section 7 (process-wide table measured on 4.8, 8, 10; spinlock 6-12 ns; (a) silently
breaks atomicity between a program and its library; (c) not on Windows). Needs the user: yes - it is a cross-assembly
convention by name (no shared type, no support assembly), next to the user's rule that no exception type is shared.

**Q-AT7. Lock-free answers for 16 bytes.** Options: (a) cap `MaxAtomicInlineWidth` at 64 in `MSILTargetInfo` (16-byte
atomics report not lock-free, `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16` undefined, layouts unchanged); (b) keep the native
answers (aarch64 and cx16 claim lock-free 16-byte atomics that are lock-based, and `atomic.c` recurses forever).
Recommendation: (a). Evidence: sections 7, 8. Needs the user: yes, only because it adds an exception to Design.md
section 3's "predefines are the native ones" (low stakes).

**Q-AT8. D36's single-thread fence.** Options: (a) keep the comment; (b) a call of a private `noinlining` empty method
`'<msil>compiler_barrier'`; (c) `Interlocked.MemoryBarrier()`; (d) .NET 10 `Volatile.ReadBarrier()+WriteBarrier()`
behind a switch. Recommendation: (b) by default, (d) optional later. Evidence: 5.3-5.4 (pre-C11 spin loops hang under
(a) on 4 of 6 configurations; (b) fixes all for under 1.1 ns). Needs the user: no (D36 is a planner decision; a section
12 row amends it).

**Q-AT9. Dead acquire/seq_cst atomic loads.** Options: (a) add a trailing `fence acquire`; (b) leave them (the JIT drops
their ordering). Recommendation: (a). Evidence: 5.1, 5.3. Needs the user: no.

**Q-AT10. Threads.** Options: (a) host-libc thread APIs through P/Invoke with native-callable thunks (no interception);
(b) intercept `pthread_create`/`CreateThread`/`thrd_create` onto `System.Threading.Thread`. Recommendation: (a).
Evidence: 9.1-9.3. Needs the user: no.

**Q-AT11. `-fms-volatile` default on `x86_64-pc-windows-msvc-msil`.** Options: (a) keep the MSIL exception of M1.c;
(b) restore the native default once atomics lower. Recommendation: (b) (identical IL, native fidelity, stock bitcode
works). Needs the user: no.

**Q-AT12. Native binding of `__atomic_*`.** Options: (a) never bind `__` runtime helpers natively
(helpers-switches.md 2.5); (b) keep binding them when the user links `-latomic`. Recommendation: (a); `-latomic` then
links nothing (`--as-needed` semantics), which also matches native clang on x86-64 when every atomic is inline.
Needs the user: no.

## 14. Work packages, risks, later goals

Work package ideas (disjoint files):

1. **Clang lock-free cap and driver** (`clang/lib/Basic/Targets/MSIL.h`, `clang/lib/Driver/ToolChains/Clang.cpp`,
   clang tests): small; small rebuild.
2. **Backend atomics** (`MSILISelLowering.{h,cpp}`, `MSILInstrMemory.td` or a new `MSILInstrAtomics.td`,
   `MSILMCInstLower.cpp`, `MSILSetMemFlags.cpp`, `MSILPrepareIR.cpp` for dead loads, `BCLMembers.def`): medium
   (about 400-600 lines with TableGen); `BCLMembers.def` is widely included (`llvm/CLI`, the target, lld) - front-load
   it into the phase's first milestone with the other header edits.
3. **Compiler barrier (D36 amendment)** (`MSILInstrMemory.td` pattern, MCInstLower, the trailer builder's helper):
   small; touches `fence.ll`, `asm-barrier.c`'s IR expectations only through llc tests.
4. **msil-ld IL helpers** `'<msil>compiler_barrier'`, `'<msil>atomic_lock_table'` (`lld/MSIL/Synthetic.cpp` and the
   trailer builder): small-medium.
5. **Helper archive content** (`atomic.c` + MSIL lock table; with P3-A's archive WP): small.
6. **msil-verify V1 allowance and pass-2 stubs** (`msil/utils/msil-verify.py`, `ilverify-allowed.txt`): small-medium.
7. **Tests** (section 12): medium; multi-threaded ones depend on the thunk WP.

Risks:

- The JIT's treatment of `volatile.` is the only thing that orders relaxed and acquire accesses; a future JIT that
  weakened it would break C atomics silently. The litmus and hoisting tests guard it on every runtime the gates run.
- ARM64 was read, not run: the mapping relies on `ldar`/`ldapr`, `stlr`, `dmb ish`, `casal`, `swpal` as crossgen2's R2R
  code shows them; the runtime JIT's choices (for example `ldaddal` for `Interlocked.Add`, or a `dmb ish; str` volatile
  store for complex addresses) were not observed. The trailing full fence makes the seq_cst mapping independent of
  `ldar` versus `ldapr`.
- Inlining of `Interlocked` calls is intrinsic expansion in every JIT measured; a method so large that the JIT gives up
  optimizing (Design.md 10 risk 1) would still call the BCL method (correct, slower).
- `ExitThread`/`pthread_exit` abandon managed frames: harmless in the tests, but any `finally`-based cleanup that Phase 3
  adds (for example shadow-stack restoration) must not depend on running in such frames.
- Thread creation on the development host is slow for host reasons (20-80 ms per thread natively), so multi-threaded
  tests should create few threads.

Later goals:

- *Portable triple*: the atomics lowering is triple-independent and needs no libc; its libc would implement threads over
  `System.Threading.Thread`; the process-wide lock table and the compiler barrier are IL helpers it reuses. Nothing
  precluded.
- *Managed calls and inline IL*: V1's call shape is what hand-written IL would use; no conflict.
- *FeatureIdeas (managed memory/refs)*: V1 eases atomics on GC references (real byrefs into the same calls); V2 would
  need byref twins of every helper.

## 15. Unverified or not measured

- ARM64 execution (no hardware); only R2R code from crossgen2 was read. The runtime JIT's ARM64 code for
  `Interlocked.Add` was not observed.
- The .NET Framework JIT's inlining decisions (inferred from timings).
- dotnet/runtime's memory-model document was not re-read for this report; the guarantees used are ECMA-335 I.12.6 and
  the measured code.
- `UnmanagedCallersOnly` thunks for thread start routines (the callbacks recon covers thunks; only delegate thunks were
  used here).
- The `MSILTargetInfo` cap of 10.3 was not built (no private clang build); its effect was inferred from the measured
  native answers and from `Targets.cpp:976`.
- musl and Darwin triples, 32-bit triples.
