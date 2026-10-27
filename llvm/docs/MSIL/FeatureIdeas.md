# llvm-msil feature ideas (evaluate at the very end)

Recorded 2026-10-14 from the user. These are not scheduled. Evaluate them after Phase 1 (M10) and the phases in plan section 7. Each idea says where it touches the current design and what to work out first. The common direction: **generate closer-to-idiomatic .NET where it is provably safe, and always fall back to the raw model.** Many of these could start as opt-in feature flags. Idea 6 (the fully enclosed model) is the most radical: it fails the build instead of falling back.

## 1. Choosing the class the code goes into

**Today:** every function is a global method of `<Module>`, and every global is an `initonly native int` address field there (D12). The user thinks that is a good default.

**Wanted:**
- (a) An option that names the single class everything goes into, for example `Contoso.Zlib.Native`.
- (b) A mapping file that places different exports in different classes.
- (c) An in-language way to say it: a clang attribute on a function or variable (for example `__attribute__((msil_class("Contoso.Zlib.Inflate")))`), or a pragma that covers a region of a file.

**Touchpoints:**
- Design.md already plans `-shared` libraries with one public export class (`-Wl,--export-class=Contoso.Zlib.Native`, plan P2-C). That covers the link-time half of (a).
- Placing every method, not only exports, means changing naming and printing in MSILAsmPrinter.
- A mapping file fits best in `msil-ld` (P2-B/P2-C), which sees the whole program.
- An attribute needs clang `Attr.td`, a broad rebuild that plan section 7 says to front-load into a phase's first milestone. It also needs IR plumbing: a function attribute or metadata.

**Questions:**
- Precedence: attribute, then mapping file, then the default class, then `<Module>`?
- Namespaces and nested classes.
- Where internal (`static`) functions and exported data go.
- Name collisions between classes.
- How this interacts with P2-C's symbol-table custom attribute.

## 2. Feature flags in general

A switch set, for example clang `-mmsil-*` or `-fmsil-*` options carried as module flags or TargetOptions, like the planned `-mdotnet=` profile. Candidates are ideas 1 and 3–5 here.
- Decide the flag surface and the defaults.
- Keep the raw model as the always-available fallback.

## 3. Typed and delegate-accepting secondary signatures for exports

**Scheduled (user, 2026-10-26):** with the managed-calls goal (C calling .NET methods), after Phase 3, not at the end of the project; both need clang to carry C prototypes to the backend.

**Today:** every pointer in a signature is `native int`; C# sees `nint` for data pointers and function pointers alike, so nothing stops a wrong argument and call sites need casts.

**Wanted (refined by the user):** keep the raw signatures, which stay the ABI that C uses, and add secondary overloads on a library's export class (M22) for C# callers:
- **typed pointers:** `T*` for data pointers and `delegate* unmanaged[Cdecl]<...>` for function pointers;
- **delegate-accepting overloads:** a parameter of a generated, named delegate type (one per function-pointer shape, marked cdecl; `Marshal.GetFunctionPointerForDelegate` refuses generic delegates such as `Func<int, int>`), converted to a native pointer and forwarded to the raw method; works on .NET Framework 4.8 too;
- later, with struct types: `ref`/`in` parameters and value types for C structs (explicit layout), possibly `Span<T>` overloads.

**Notes:**
- No run-time cost: `native int`, unmanaged pointers and function pointers are the same value to the JIT; the gains are C# call sites, compile-time checking and decompiler output.
- **Pointee types must come from clang**, because IR pointers are opaque (`ptr`): a per-function C prototype annotation (metadata or an attribute) or debug-info types (`DISubprogram` with `-g`). The managed-calls goal needs the same plumbing in the other direction.
- **Lifetime and validity facts are already in the IR** and need no clang change: `captures(none)` (formerly `nocapture`; inferred by LLVM's function-attribute pass at -O1 and above, or declared in C with `__attribute__((noescape))`), `nonnull`, `dereferenceable(N)`, `align`, `readonly`/`writeonly`, `noalias` (`restrict`). Mapping: `captures(none)` + `nonnull` + `dereferenceable(sizeof T)` gives `ref T` (`in T` if `readonly`), since a C# ref is exactly a non-null, dereferenceable pointer that cannot escape the call; `captures(none)` on a function pointer gives a delegate overload that keeps the delegate alive only for the call. Without that evidence only the typed-pointer overload is generated: a callback that C retains (registered handlers, `atexit`, `signal`) needs explicit lifetime handling, for example a returned handle that the caller must keep, so no convenient overload can crash later.
- Rust compiled with this toolchain benefits automatically: rustc emits `noalias`, `readonly`, `nonnull`, `noundef`, `align` and `dereferenceable` for references (its lifetimes are erased before LLVM; capture is left to LLVM's inference). Recognizing a Rust slice `(ptr, len)` as one `ReadOnlySpan<T>` would need rustc's type information (debug info or rustdoc JSON).
- Struct pointee types need one CIL value type per C struct. Check the verification allow-lists (unmanaged pointers are already unverifiable).

**Questions:**
- Exports only (the default), or also internal signatures, locals and fields for readability?
- The handle API for retained callbacks: a returned `IDisposable`, a `GCHandle`-based registration, or a C-side annotation that names the owner?
- Naming of the generated delegate types and of overloads that would collide.

## 4. Managed memory and refs instead of unmanaged memory

**Today:** all C-visible memory is unmanaged and never moves. Design.md says "The C program never touches the GC heap": malloc goes to the native heap, and globals live in one AllocHGlobal segment.

**Wanted (exploratory):** allocate on the GC heap where possible and pass `ref`s or `Span<T>` instead of pointers. Where that is not possible, for example memory handed to native code, fall back to raw pointers ("try this, else fall back").

**Notes:**
- C allows pointer arithmetic, pointer-to-integer casts, storing pointers in memory and passing them to native code.
- Managed refs cannot be stored in heap memory (only in ref structs), and the GC moves objects.
- So this needs whole-program provenance and escape analysis (LTO in `msil-ld`), a proof per allocation site that a block is managed-safe, and pinning at native boundaries.
- It is most likely an opt-in mode with a fallback per allocation site. Weigh the value carefully against the cost.

## 5. Escape analysis for stack memory: real locals, stackalloc and Span, refs

**Today:** one `localloc` per frame holds every address-taken local (D13).

**Wanted:** when a local's address does not escape, generate idiomatic .NET instead, falling back to `localloc` otherwise:
- a real CIL local (a value type) with `ldloca`, which gives a managed `&`;
- `stackalloc` with `Span<T>`;
- `ref` parameters for pointer parameters that do not escape.

**Touchpoints:**
- Plan section 7 P4-A already lists "revisit the frame model (value-type frame local vs localloc, measured 73 vs 4 bytes of prologue)".
- Plan section 10 has the value-type frame decision pending.
- LLVM's SROA and mem2reg already remove most non-escaping scalars. What remains is mostly arrays and structs whose address is taken but does not escape, which CaptureTracking can find.
- `ref` parameters change the signature for every caller, so they need whole-program knowledge (internal functions only).

## 6. Fully enclosed model: the program as a .NET class instance

Added 2026-10-14.

**Wanted:** instead of static methods over process-wide unmanaged memory, put all of the code in a class and give each instance of that class its own memory, using real .NET allocation:
- the C globals become storage owned by the instance (arrays or fields);
- the C heap is .NET-allocated memory that the instance owns;
- the functions become members of the instance.

A .NET application would then just `new` the library and call it. This only works for **fully enclosed C**: if the code needs something outside the enclosure, the build fails with a clear diagnostic instead of falling back.

**What it would give:**
- Several independent instances in one process, because every C global exists once per instance. That makes non-reentrant C libraries usable from multi-threaded .NET code.
- Memory that is freed when the instance is garbage-collected, with no native heap left behind.
- Easy embedding, and possibly fully safe, verifiable IL (variant b below).

**Two variants, in increasing order of change:**
- **(a) Instance-owned pinned memory.** The instance allocates its memory as one pinned managed array (`GC.AllocateArray<byte>(n, pinned: true)` on .NET 5+, or a pinned `GCHandle` on net48), or as unmanaged memory freed by a finalizer. Pointers stay `native int`, so loads, stores and calls barely change. Globals become offsets from the instance's memory base instead of static fields (a per-instance version of D12). Functions become instance methods, or static methods taking a context argument. `malloc`/`free` are implemented inside the enclosure over that memory.
- **(b) WebAssembly-style sandbox.** Pointers become 32- or 64-bit offsets into a managed `byte[]`. Loads and stores become array accesses (bounds-checked, or through `Span`/`Unsafe` helpers). Function pointers become indices into a table, like WebAssembly's `call_indirect`. This gives safe, verifiable code that is isolated from the process, at the cost of rewriting all memory lowering and paying for bounds checks. This backend is already shaped like WebAssembly, and the same model is how WebAssembly-to-.NET translators work, so the mapping is natural.

**What "enclosed" requires:**
- A libc that lives inside the enclosure: an in-enclosure allocator such as WebAssembly's emmalloc or dlmalloc, and stdio routed to .NET callbacks or streams the host supplies.
- No P/Invoke that passes pointers into the enclosure's memory. In (a) pinned memory could be handed to native code, but then the instance must outlive the call. In (b) data has to be copied across the boundary.
- A closed-world check, which fits the `msil-ld` linker (P2-B) because it sees the whole program: every external symbol must resolve inside the enclosure or to an allowed host import.

**Touchpoints:** D12 (globals in one segment), D13 (frames; the frame could also live in instance memory), D14 (the P/Invoke import table), the Phase 3 runtime assembly and libc choice (P3-A), and P3-B (setjmp, atomics, threads: which of these make sense inside one instance?).

**Questions:**
- What is the public surface: instance methods named after the exported C functions, a generated interface, or both?
- Thread safety: one instance per thread, or a lock?
- How the host passes data in and out (strings, buffers).
- Does variant (a) cover most of the value, with (b) as a later option?

## 7. Several triples in one assembly

Added 2026-10-14.

**Wanted:** compile the same C sources for several triples, for example `x86_64-pc-windows-msvc-msil`, `x86_64-unknown-linux-gnu-msil` and an `arm64-apple-darwin` MSIL triple once it exists, and put all of the results into **one** assembly. Each triple's code goes in its own class, for example `Contoso.Zlib.Windows_x64`, `Contoso.Zlib.Linux_x64` and `Contoso.Zlib.Darwin_arm64`.

**Why it is needed:** C fixes the ABI at compile time: `sizeof(long)`, `wchar_t`, struct layouts, the libc's own structs, and macros such as `_WIN32` and `__linux__`. Design.md says choosing a libc means choosing its ABI. So one compiled body of C cannot serve every OS, but one body per triple, packaged together, can.

**How it could work:**
- **clang:** one compilation per triple, as today. The driver could run several of them from one command, the way Apple's `clang -arch x86_64 -arch arm64` builds universal binaries, or the way the offloading toolchain (CUDA, HIP, OpenMP) packs several target images into one file.
- **Object files:** plan P2-A's CIL object format. Each `.o` already records its triple (in P2-C's symbol-table custom attribute).
- **Linker:** `msil-ld` (P2-B) accepts objects for several triples and keeps them apart. Each triple gets:
  - its own class or namespace;
  - its own data segment (D12);
  - its own P/Invoke table: the libraries differ (ucrtbase vs libc.so.6 vs libSystem), and so can the signatures;
  - its own initializer, run on first use of that variant, not one shared `<Module>::.cctor` that would initialize every variant.
- **Choosing a variant:** a generated facade class picks the variant at run time (`RuntimeInformation.IsOSPlatform`, `OSArchitecture`, or the runtime identifier) and forwards calls to it. The facade only works where every variant's signatures agree. `long` is 32-bit on Windows and 64-bit on Linux, so a function that takes a `long` has different signatures. The option is per-variant classes only, or a facade that widens or narrows types.
- **AnyCPU bonus:** 32-bit and 64-bit triples (i686 and x86_64, planned in P5-A) in one assembly would give a true AnyCPU C library, since `native int` changes size between them.

**Costs:** IL and data size grow with the number of triples, and the build has to compile each translation unit N times.

**Touchpoints:**
- Design.md (around line 210) currently says "one assembly per target", and that "a single multi-target ('fat') assembly is a non-goal". It left the packaging of several targets to NuGet's `runtimes/<rid>/` layout. The user has since ruled NuGet packaging out of this project (2026-10-14). So without this idea, combining targets is entirely the user's own build system's job. The user's position (2026-10-14): a fat assembly **might be a goal, but only at the very end**. The Design.md line is being reworded from "non-goal" to "not planned for phases 1-5".
- The symbol table and export class (P2-C).
- Idea 1 (class placement): the triple becomes one more naming level.
- Idea 6 (enclosed model), especially variant (b): code that never touches the host libc may not depend on the OS at all. Then one variant could serve every platform and remove the need for several triples. Evaluate 6 and 7 together.

**Questions:**
- Which triples is a user likely to combine?
- Facade or explicit per-variant classes?
- How are exported data and callbacks (function pointers into managed code) routed to the right variant?
- Does the linker need to deduplicate code that is identical across triples (for example pure computation)?

## How to evaluate

For each idea:
- Prototype it behind a flag.
- Measure IL size, JIT and run-time performance, and how readable the decompiled code is.
- Check verification (ILVerify and PEVerify) and interop with native code.
- Then decide defaults and which phase owns it.
