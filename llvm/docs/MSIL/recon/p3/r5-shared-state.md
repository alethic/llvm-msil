# R5 addendum: state shared between assemblies, and the AppDomain key

- Date: 2026-10-28. Checkout: branch `msil` at `cc4c79787d6a`. Key: `r5-shared-state` (R5 synthesizer). This is an
  addendum to [decision-brief.md](decision-brief.md) R5, written because the AppDomain key needed a closer look.
- Built from four investigations (consumers, mechanisms, isolation, alternatives; scratch
  `C:\llvm-msil-scratch\p3\r5\<key>\`) and a re-check of the experiments the conclusions rest on
  (`C:\llvm-msil-scratch\p3\r5\synth\`). "All five" means .NET Framework 4.8, .NET 8.0.33 and 10.0.14 on Windows, and
  .NET 8.0.31 and 10.0.13 under WSL.
- Reading order: sections 1-4 (about 10 minutes). Section 5 is the exact text for the brief, and section 6 lists the
  re-check and the corrections.
- **Short answer.** Phase 3 needs no AppDomain key.
  - Exit inside a library, calls through another assembly's function pointers, and thread-exit destructors need to
    know the program's other assemblies, not a shared object. The program can pass that down its own link graph at
    start-up (`'<msil>attach'`, section 3).
  - Only the lock path of atomics (16-byte, generic-size and misaligned objects) needs one object per process. The
    best mechanism for it is a Monitor on documented interned lock names, which stores no value. A per-assembly
    table plus documentation is the alternative.
  - That choice is the one real decision for you (section 4).

---

## 1. What the question really is

Every assembly this toolchain writes has its own copy of every C global, including the toolchain's private helper
state: `atomic.c`'s lock table, the exit lists, the function-pointer table and the TLS block. Natively, that is what
linking a C runtime statically into each module (`/MT`) gives you. The question is where C needs **one** object per
process or per program, so that per-assembly copies would behave wrongly without any diagnostic.

The brief answers with a name looked up at run time: an `AppDomain.GetData`/`SetData` slot called
`<msil>v1:<concern>`, which every assembly finds. The investigators asked two things of each consumer. Does it really
need a shared object? If so, is a run-time name the right way to find it?

| Consumer | Native behaviour | Per assembly in MSIL (measured) | Need |
|---|---|---|---|
| Atomics of 1-8 bytes | Hardware, by address | `Interlocked` and CAS loops are atomic by address: 4,000,000 of 4,000,000 across two assemblies; 20/20 exact for partword loops (all five) | none |
| Lock path of `atomic.c`: 16-byte, generic-size and misaligned atomics | glibc: one `libatomic.so` per process (cmpxchg16b through an ifunc). clang for windows-msvc cannot link it without `-mcx16`. MSVC puts a spinlock inside a 24-byte object | One table per assembly. Two assemblies on one object lost 45,182-935,563 of 2,000,000 updates, with about 2M torn reads; across ALC copies 223,879-527,684; across Framework AppDomains 562,674-599,119 | **real** but rare: 0 of 912 corpus C/H files; none in zlib, Lua, SQLite or Rust std |
| `exit()` inside a library | glibc: one list. `/MD`: the program's handlers, then the DLL's. `/MT`: only the DLL's, and the program's buffered stdout is lost | The library runs its own list and calls `Environment.Exit`. The program's handlers run late, in its ProcessExit fallback on the finalizer thread, inside Framework's 2.3-2.6 s budget | **avoidable**: it needs to know the root, not a shared object |
| `atexit` order across modules | glibc: one list in registration order. `/MD`: one list per module | Per-assembly lists plus R4's `'<msil>fini'` cascade give the `/MD` model | none (lifecycle 5.7 documents the Linux deviation) |
| Calls through another assembly's function pointer (CB4, S6) | Direct | The miss path costs 17-31 ns more. A `longjmp` through it aborts on WSL .NET 8, and **on every Linux runtime once R8's thunk filter exists** (re-run: the filter fired on WSL .NET 10 too) | **avoidable**: the program can hand each assembly the others' tables |
| Thread-exit destructors that one assembly's code registers on a thread another assembly started (R7; not in the brief's list) | glibc: one list per thread. Windows FLS: per process | Only the starting assembly's list runs (analysis) | **avoidable**: the same hand-down |
| TLS blocks, shadow stack, setjmp state | One TLS block per module (ELF `PT_TLS`, PE TLS index) | Per assembly. A library exports a `thread_local` through its accessor | none |
| errno, environ, stdio, heap, rand, strtok, locale, signals | Host libc, process-wide | Host libc, so `/MD` semantics (M22's `c-state.test`) | none |
| The command line a library sees | Per process | `Environment.GetCommandLineArgs` gives the program's own line under `dotnet exec` (Windows and Linux) | none |

So only one consumer needs an object per process. The others need a **scope of one program**: the program's own
assemblies, in one AppDomain or load context. Explicit link edges already give that scope.

## 2. The mechanisms, measured

Which assemblies share the object. The rows come from the re-check, using one C# source compiled twice and a host per
runtime, except where a row names an investigator.

| Mechanism | Two separately built assemblies | Framework, 2nd AppDomain | .NET, isolated ALC copy | .NET, collectible ALC unloaded | C# host, no toolchain program | Native code on the same object | Uncontended cost |
|---|---|---|---|---|---|---|---|
| Per-assembly spinlock table (`atomic.c` as is) | not shared: loses updates | not shared | not shared | unloads | not shared | not atomic | 5.6-11.1 ns |
| AppDomain-key table (the brief) | shared, 0 lost (all five) | **not shared**: D2 reads null, builds its own table, and loses 659,992-753,738 of 2M | shared process-wide, 0 lost | an IntPtr table unloads; a **delegate value pins** the ALC (20 GC rounds) | shared | not atomic (consumers: lost 176,887 and 549,304 against native cmpxchg16b) | 5.5-10.9 ns cached in a static |
| Monitor on interned names `<msil>v1:atomic.lock:<i>` | shared, 0 lost (all five) | **shared**: D2's `TryEnter` blocks while D1 holds it; 0 lost | shared, 0 lost | unloads; it got the same lock objects as the default copy | shared | not atomic | 17.2-38.1 ns; cheaper than a spinlock under same-object contention on Windows (isolation: 16-29 against 34-51 ns) |
| Link-time handshake `'<msil>attach'` | only along link edges, program to libraries | per AppDomain (statics) | each program copy has its own context | unloads (nothing escapes) | **not covered**: no root | n/a | nothing per use; one call per library at start-up |
| P/Invoke `libatomic.so.1` (consumers, Linux) | shared | - | - | - | shared | **atomic** (0 lost) | about 31 ns |
| Hardware CAS stub written at run time (alternatives) | correct | - | - | - | correct | **atomic** with libatomic and the MSVC STL export | 8.8-14 ns |

**Failure modes of the AppDomain key** (each measured by at least two investigations or the re-check):

1. **Two scopes behind one API.** The key is per AppDomain on Framework and per process on .NET 8/10, where every ALC
   shares it. For locks that guard native memory, the Framework scope is too narrow: updates were lost across domains.
   For a slot that names "the program", the .NET scope is too wide: two copies of one program in two ALCs cross-wire,
   so copy 2's libraries exit through copy 1 (mechanisms E3).
2. **Anyone can write it.** A runtimeconfig `configProperties` entry presets the key on .NET 8/10, where it is the
   same store as AppContext and the runtime properties. `GetData` then returned `System.String` on all four .NET
   runs. E5's `unbox.any` shape throws `InvalidCastException` inside a module initializer, so the accessor must test
   the type and FailFast.
3. **It needs the interned lock anyway.** Creating the table for the first time without the interned-string Monitor
   produced two tables in 2-60 of 2,000 rounds. So the key depends on interned-string identity, which the
   interned-lock design uses on its own.
4. **Values with code pin or dangle.** A delegate or an instance of the assembly's own class keeps a collectible ALC
   alive. A raw entry point outlives the unload and is adopted by the next load. Only BCL data, such as an IntPtr to
   unmanaged memory, is safe.
5. **No ordering advantage.** Library module initializers, and so library constructors, run before the first
   instruction of the program's module initializer (mechanisms E3; Phase 2's `init-order.test` shows them before
   the program's constructors). A program cannot
   write the slot before its libraries' constructors run, any more than it can attach them before then.
6. **A permanent contract.** The name, the layout and the hash are frozen. v1 and v2 keys give different tables,
   silently.

**Rejected outright** (mechanisms, isolation):
- an environment variable: inherited by child processes and invisible to C `getenv`;
- a named mutex: a lock only, and it leaves files under `/tmp/.dotnet`;
- a named memory map: `PlatformNotSupportedException` on Linux, and not in mscorlib;
- `GetEntryAssembly`: null under native hosts and in `CreateDomain` domains, and the host itself under ALC plugin
  hosts;
- `AppContext.SetData`: missing on Framework, and the same store as the key;
- thread data slots: per thread.

## 3. The options for R5 as they now stand

`'<msil>attach'` is the handshake that three investigators prototyped independently in IL, using mscorlib only. The
re-check re-ran one of the prototypes and got identical transcripts on Framework, .NET 8 and .NET 10 (21 cases). It
works like this:
- every `-shared` library's export class gets a public static `'<msil>attach'`;
- the program's module initializer calls it for each linked library, after the ldtokens, in msil-ld's simulated
  initialization order;
- each library forwards it to its own libraries; the first attach wins, and a diamond's repeat is ignored.

`exit` in a library then runs the program's exit on the calling thread, including on a second thread and from a
grandchild library, with the status kept. In a C# host with no toolchain program, the library runs its own sequence,
calls `Environment.Exit`, and the host's ProcessExit handlers run. It is the mirror image of R4's `'<msil>fini'`.

| Option | What it gets right | What it gets wrong |
|---|---|---|
| **O1. The brief:** the AppDomain key for the root-exit slot and the lock table; registry deferred | One mechanism. The lock table covers C# hosts | Failure modes 1-6 above. Deferring the registry now means a cross-assembly `longjmp` through callbacks aborts on every Linux runtime, not only .NET 8 |
| **O2. Per assembly everywhere**, with documentation and an msil-ld warning | No contracts and the least code. Every later route can still be added without breaking anything | The program's handlers run late, on the finalizer thread, and Framework can cut them off. Atomicity is lost silently. Cross-assembly `longjmp` through callbacks aborts on Linux |
| **O3. `'<msil>attach'`** for the root exit, peer function-pointer tables and thread-exit runners; a per-assembly lock table, documented, with a warning (consumers' lean) | Exit, callbacks and thread lists are correct wherever a toolchain program is the root. There is no run-time name, no cost per use, and the code verifies. Each program copy is scoped correctly, and collectible contexts unload | Lock-path atomics stay silently non-atomic across assemblies. The warning needs a marker that libraries do not carry today (their symbol table lists exports only). It warns on every pair that merely uses 16-byte atomics and misses C# hosts |
| **O4. O3 plus Monitor locks on interned names** for the lock path (mechanisms, isolation) | Correct among all toolchain assemblies in a process: across Framework domains, ALC copies and C# hosts. Nothing is stored, so there is nothing to squat, pin or race on. It needs only `String.Intern` and `Monitor` from mscorlib, and it is architecture-independent, so it serves the portable triple too | Still a name convention: the name pattern, the count (1024) and the hash are frozen forever. A later change, including a move to hardware CAS, silently breaks processes that mix old and new assemblies. 10-30 ns more per operation on a rare path. On Framework, `AppDomain.Unload` aborting a thread mid-operation orphans that lock process-wide (isolation; C code is not abort-safe anyway). String agility is long-standing CLR behaviour, but ECMA-335 does not specify it. Not atomic with native code |
| **O5. O3 plus the AppDomain-key table** for locks only | Spinlock speed | Per AppDomain on Framework (lost updates across domains); it can be squatted, so it must FailFast; its creation needs the interned lock anyway; the same permanent contract. O4 beats it on everything except about 10-30 ns |
| **O6. P/Invoke `libatomic.so.1`** on Linux (an add-on) | The only BCL-only route that is atomic with native modules | Linux only. An optional package that the project's sysroots lack. About 31 ns. The helper archive resolves `__atomic_*` first. Every assembly in the process must make the same choice, so it is a contract too. At most a later switch |
| **O7. A hardware CAS stub** (alternatives) | No shared state, atomic with native code, fast | Machine code written at run time into IL-only outputs: a policy question, antivirus heuristics, and environments that deny executable memory (iOS, wasm). Stubs per architecture (aarch64 not measured). No use for the portable triple. Misaligned and generic sizes still need a lock path. Mixing it with locks was measured broken, so it is a permanent contract as well. Interop is a Linux-only benefit, because Windows has no native 16-byte `_Atomic` ABI to match (MSVC's is a 24-byte object with a lock inside) |

`Marshal.GetDelegateForFunctionPointer` is another way round the registry. Given a foreign delegate thunk, it
returns the exporter's own delegate on all five runtimes (re-run). It stays a fallback idea for C# hosts only: the
behaviour is undocumented and does not cover `--uco-thunks`.

## 4. Recommendation

**O4: no AppDomain key; `'<msil>attach'` for what is scoped to a program; interned-name Monitor locks for the lock
path of atomics.** Also put the peer function-pointer tables into the handshake from the start (M29) rather than
deferring them.

Reasons:
- **Each mechanism's scope matches its consumer.** The program's link graph is the right scope for "who is the root"
  and "whose functions are these". Locks that guard native memory must be per process, and interned names were the
  only mechanism measured exact across every boundary where native memory is shared, Framework AppDomains included.
  The AppDomain key has the wrong scope for both consumers, on one runtime family or the other.
- **Nothing found by name holds a value.** The handshake uses only explicit link edges, in keeping with "outputs
  depend only on the BCL, native libraries and assemblies they were explicitly linked against". The lock names hold
  no data that anyone could overwrite, mistype or leave pinned.
- **The brief's premise for deferring the registry no longer holds.** Under R8's filter, a pointer from another
  linked assembly that takes the miss path aborts a `longjmp` on every Linux runtime. Examples are Lua built as a
  `-shared` library raising errors in the program's C functions, and libpng's or libjpeg's error callbacks across a
  library boundary. Peer tables in the attach context make those calls managed `calli`s for little code.
- **The silent case is cheap to close.** Lock-path atomics are rare, but the project otherwise turns silent
  misbehaviour into errors or correct code (judges CF and DV), and closing it costs one small IL helper.

What you should weigh:
1. **Is any name convention acceptable?** If your unease about the key is about assemblies coordinating through a
   hidden name at all, interned locks are the smallest form of it: a lock identity, no value. It is still a contract
   that can never change. If that is not acceptable, take O3: per-assembly locks, documented, with the silent case.
   O4 can be added later without breaking anything that worked.
2. **What is irreversible.** Per-assembly locks freeze nothing. Interned locks freeze names, count and hash, and rule
   out a later move to hardware CAS for processes that mix toolchain versions. No BCL-only option is atomic against
   native code on the same object. That stays a documented limit under every option.
3. **C# hosts.** The handshake does nothing for a C# host that loads toolchain libraries without a toolchain
   program:
   - exit runs the library's own sequence (acceptable, and better than a native `exit` under .NET 8/10);
   - callbacks between those libraries take the miss path;
   - destructors that one library registers on another's thread leak.

   Covering this would need the per-domain key, which is not recommended now. It can be added later for that case
   alone, with BCL data values only.
4. **A related R4 item, not decided here.** R4's ProcessExit subscription pins a collectible ALC (re-run: pinned for
   20 GC rounds; it unloads once the handler is removed). An attached library can drop its subscription, because the
   root's exit cascades into it. An unattached library under a plug-in host would need an
   `AssemblyLoadContext.Unloading` hook, found by name at run time or bound only when the references define it. That
   is a rule question for R4.

## 5. Exact changes to the brief

**Section 2.0, row R5:** "| R5 | State shared by all assemblies of a process | No AppDomain key: a link-time
`'<msil>attach'` handshake (root exit, peer function-pointer tables, thread-exit runners) and process-wide Monitor
locks on documented interned names for lock-path atomics | **Contested** |"

**Section 1:**
- Replace "Assemblies share state through one versioned `AppDomain` key convention, used only where C needs a
  process-wide object (R5)." with "At start-up a program passes its exit, its assemblies' function-pointer tables and
  their thread-exit runners down its link graph, through an `'<msil>attach'` member of each library's export class.
  Nothing is shared by a name looked up at run time, except the documented lock names of lock-based atomics (R5)."
- Replace "16-byte atomics come from compiler-rt's `atomic.c` with one process-wide lock table." with "16-byte and
  generic atomics come from compiler-rt's `atomic.c`, whose lock path takes process-wide Monitor locks on documented
  interned names."

**R5, Options:** add "(c) by scope: the `'<msil>attach'` handshake for program-scoped consumers and interned-name
Monitor locks for lock-path atomics, with no AppDomain key; (c') the same with per-assembly lock tables, documented."
Add the thread-exit lists of R7 to the question's list. **R5, Recommendation:** replace it with:

- (c). Design.md 6, text D-41, becomes: "Assemblies share no mutable state by name. A program passes its exit, its
  assemblies' function-pointer tables and their thread-exit runners to the libraries it links, through
  `'<msil>attach'` on their export classes. The lock path of lock-based atomics uses process-wide `Monitor` locks on
  the interned names `<msil>v1:atomic.lock:<i>` (i from `atomic.c`'s `lock_for_pointer`, 1024 slots), a permanent
  contract."
- `'<msil>attach'` (M29):
  - It takes one versioned, append-only context of BCL-typed values.
  - The program's module initializer calls it for each linked library after the ldtokens and cross-assembly
    relocations and before its own constructors, in simulated initialization order. Libraries forward it; the first
    attach wins; a repeat is ignored.
  - An attached library's `exit` runs the program's exit on the calling thread. An unattached library (a C# host, or
    a library constructor that runs before the attach) runs its own sequence, then `Environment.Exit`.
  - An attached library drops its ProcessExit fallback.
  - The miss path probes the attached peer tables before its `calli unmanaged`.
  - msil-ld warns when a linked library lacks the member (libraries built by M22); such a library keeps per-assembly
    behaviour.
- Lock path (M30): `atomic.c`'s `lock`/`unlock` on MSIL triples call a private IL helper that does
  `Monitor.Enter`/`Exit` on the interned names, cached per assembly.
- Documented limits:
  - no 16-byte atomicity against native code on the same object;
  - two toolchain programs sharing one library instance are unsupported;
  - library constructors run before the attach;
  - on Framework, `AppDomain.Unload` during a lock-path atomic operation can orphan that lock.

**R8, Recommendation:** add "Calls between linked assemblies through function pointers are managed `calli`s through
R5's peer tables, so the filter fires only for native callers and for pointers from outside the program's link
graph. The filter cannot tell whether native frames lie between, so it aborts any `longjmp` that leaves a thunk."

**Section 3.6:** add "The miss path probes the peer tables delivered by R5's attach (M29) before the
`calli unmanaged`." **Section 3.7:** add the attach bullet above. **Section 3.8:** add "The lock path of `atomic.c`
calls the interned-name Monitor helper (R5)."

**M29:**
- Goal: "R4, R5 (the `'<msil>attach'` handshake: root exit and peer function-pointer tables) and R13".
- WPs: add "msil-ld: `'<msil>attach'` on export classes, the program's attach calls and the warning for libraries
  without it; the miss path's peer probe".
- Exit check: add:
  - `exit` inside a library, inside a grandchild library of a diamond and on a second thread runs the program's exit
    on the calling thread with its status;
  - a C# host of two libraries;
  - a `longjmp` through a program-registered callback that a `-shared` library calls, on both triples and every
    runtime (the Lua-as-library shape).
- Size 2.8-3.5k.

**M30:**
- Goal: replace "the process-wide lock table (R5)" with "the interned-name lock helper (R5)".
- Exit check: replace the last item with "a 16-byte atomic shared by a program and its `-shared` library, and by two
  copies of a library in two load contexts (.NET) or two AppDomains (Framework), stays exact".

**M31:**
- WPs: add "thread-start trampolines run the attached assemblies' thread-exit runners".
- Exit check: add "a destructor that a library's code registers on a thread the program started runs at that
  thread's exit".

**Section 8:** D-41 as above. **Section 9:** replace "Leave room for an instance qualifier in the key scheme" with
"The attach context is per program copy, which an enclosed-instance model can narrow further. The atomic lock names
stay process-wide, as native memory is."

**If you choose O3 instead:** drop the lock-path helper from R5 and M30. D-41's last sentence becomes "Lock-based
atomics use a lock table per assembly: an object shared by two assemblies is not atomic (documented)". M30's last
exit item becomes a documented-limit test.

---

## 6. Re-check, and where claims did not hold

Re-run for this addendum (logs in `C:\llvm-msil-scratch\p3\r5\synth\`: `fx.txt`, `core-win.txt`, `core-wsl.txt`,
`preset-win.txt`, `core-win-unload.txt`, `x3\win.txt`, `x4\win.txt`, `x4\wsl.txt`):
- interned-name identity, AppDomain data, statics and 16-byte loss across separately built assemblies, Framework
  AppDomains and .NET ALC copies (all five);
- the runtimeconfig preset (four .NET runs);
- unloading collectible ALCs (.NET 8/10, Windows and WSL);
- the alternatives' handshake prototype (Framework, .NET 8 and .NET 10, transcripts identical to the investigator's);
- the alternatives' thunk-filter and `GetDelegateForFunctionPointer` probes (all five).

Corrections:
- **Brief R5:** the registry does not protect "only .NET 8 Linux". With R8's filter, the miss path aborts on WSL
  .NET 10 too (re-run).
- **Brief R5:** skipping the ProcessExit fallback in Linux executables needs no slot: an executable knows statically
  that it is the root.
- **Brief R5:** "BCL-typed values only" is not enforceable, because configuration or any code can preset the key
  (re-run).
- **Consumers:** treated the AppDomain table and interned Monitors as interchangeable. They are not: the table lost
  659,992-753,738 of 2M across Framework domains, while the interned locks were exact.
- **Consumers:** the msil-ld warning it proposed needs a new library marker, over-warns, and misses C# hosts.
- **Isolation:** called the registry "the one place" for an AppDomain key. That holds only for C# hosts joining
  unrelated libraries; inside a program the handshake's narrower scope is correct.
- **Alternatives:** the hardware stub "needs no shared state" but is still a cross-assembly contract. Its interop
  benefit applies on Linux only.
- **Not measured** by anyone: Mono/Unity, NativeAOT, aarch64, and the Framework abort mitigation (a critical section
  inside a `finally`).
