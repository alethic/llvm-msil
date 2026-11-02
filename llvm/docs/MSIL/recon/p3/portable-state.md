# P-0 recon `state`: the managed libc's shared state layout and the portable C ABI freeze list

- Date: 2026-11-01. Checkout: branch `msil` at `c3460446ac09` (M25). Read-only; scratch `<scratch>/p0/state/`.
- Scope: where every piece of llvm-libc state lives on `msil64-unknown-dotnet` (and later `msil32-unknown-dotnet`) so
  that the shared `c.dll` and every `-static` copy keep one process-wide C state (user decision 2026-11-01 10:32),
  the versioned header and discovery names of that state, which pieces stay per copy by design, and which portable
  C ABI items freeze when the shared state ships.
- Inputs: the orchestrator's research on .NET homes for the C library's state (synthesis in
  `<scratch>/p0/state/synth-answer.md`), `llvm/docs/MSIL/recon/p3/r5-shared-state.md`, the review of the portable
  program order (its notes "the state layout must be designed now, not at P-3" and on the portable C ABI), the
  research on the C library's modes, the lock-path freeze rule, the user's decisions recorded in the plan's
  section 12 rows, Design.md section 6, plan 5.14.4 (T9-T11) and 5.15.4 (the
  `'MSJ1'` `jmp_buf` prefix), and the llvm-libc sources under `libc/`.
- Binding decisions are not reopened. Where this report needs a choice the user has not made, it is in section 11.

---

## 1. Short answer

1. **Four homes, five frozen names.** Every copy of the managed libc finds one process root (a never-freed unmanaged
   block published as a boxed `System.IntPtr` under the AppDomain-data key `<msil>v1:libc.root`, created under
   `Monitor` on the interned string `<msil>v1:libc.root.lock`), one per-thread C block (named thread data slot
   `<msil>v1:libc.thread`), 64 wait buckets (`Monitor` on interned `<msil>v1:libc.wait:<i>`), and r5's atomic lock
   names `<msil>v1:atomic.lock:<i>` (already decided). Everything else is reached from the root's directory or the
   C block, or is a BCL object (`System.Environment`, `TimeZoneInfo`, `Console.OpenStandard*`, `SafeFileHandle`,
   `Thread`). Per AppDomain on .NET Framework, per process (all ALCs) on .NET 5+.
2. **The root is a header plus a directory of sub-blocks**, one per area (stdio, descriptors, exit, environment,
   locale, misc, signals, tss, threads, time, start-up). Each sub-block has its own `{magic, major, minor, size}`
   header and a layout check. Rule: a sub-block never grows after the freeze; new fields go into a new directory
   entry. The `v1` in a name is a namespace and never changes.
3. **The per-thread C block** holds errno at a fixed offset (12), the `uselocale` locale, the tss value table, the
   thread-exit list, the thread descriptor, the software FP flags and an extension pointer. llvm-libc is built with
   `LIBC_ERRNO_MODE_EXTERNAL`; `__llvm_libc_errno` returns the cached block address plus 12.
4. **The heap has no shared object, only a frozen scheme:** a 2-pointer header `{base, size}` over
   `Marshal.AllocHGlobal`/`FreeHGlobal` on every runtime. `NativeMemory` is absent on Framework (measured), and a
   copy that picked it would corrupt blocks freed by a copy that did not.
5. **Shared lists hold managed function pointers** (`ldftn`, called with `calli`), as decided for the portable
   triple. Each entry records its owner's `__dso_handle`, so a collectible copy removes its entries at `Unloading`.
6. **Per copy by design:** per-call result buffers (`strerror`, `strsignal`, `asctime`, `ctime`, `gmtime`,
   `localtime`, `l64a`, `inet_ntoa`, `tmpnam(NULL)`, `setlocale`'s returned string, `localeconv`'s struct),
   `__dso_handle`, `__stack_chk_guard`, constants, the getopt variables (recommended), the `tzname`/`timezone`/
   `daylight` variables (computed from the shared `TimeZoneInfo`), and the toolchain's own per-assembly state.
7. **Phasing.** P-1b cannot reach the root (no managed calls before MC-1), so its few stateful freestanding
   entrypoints (rand, strtok, the hidden mbstates, setlocale) keep llvm-libc's C globals in the single `c.dll` copy,
   and errno uses EXTERNAL mode over one cell (question Q5). P-2 implements the root and the C block for real, with
   major 0 (provisional). P-3's commit, the first at which two copies can share, freezes the layouts as major 1
   (a new plan contract, "5.19 Portable C library shared state", plus a frozen-items rule like T10/T11).
8. **C ABI items that P-1a/P-1b must fix now:** errno numbering (a new dotnet family; the upstream generic family
   is incomplete and has `EOVERFLOW` 35, which is Linux's `EDEADLK`), wchar_t/wint_t, `long double` = binary64,
   time_t 64-bit, `struct timespec`, `struct tm`, `mbstate_t`, `locale_t` and `LC_*`, the `jmp_buf` type (200
   bytes, the `'MSJ1'` prefix), FILE as an opaque pointer and how `stdin`/`stdout`/`stderr` are spelled. P-2 fixes
   off_t/fpos_t, `BUFSIZ`/`L_tmpnam`/`TMP_MAX`, `O_*`, `SEEK_*`, signal numbers and `sigset_t`. P-3 fixes the thread
   types (`once_flag`, `mtx_t`, `cnd_t`, `tss_t`, `pthread_*`). P-4 fixes msil32's `clock_t`.

---

## 2. Measurements

All commands were run from Git Bash on the Windows host. Transcripts are in the scratch directory named.

### 2.1 Every BCL member this design names exists on Framework 4.8 and modern .NET

`apiprobe.cs` (from the earlier, interrupted attempt of this run; re-checked: the transcripts below are complete and
the binary matches the source) checks each member by reflection and runs the discovery protocol of section 3.
`apiprobe2.cs` (new) checks the further members this report names and the `[Obsolete]` state of the discovery
members.

```
C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe -nologo -out:apiprobe2.exe apiprobe2.cs
./apiprobe2.exe                                    # Framework 4.8.9363, x64
dotnet exec --runtimeconfig net8.json  apiprobe2.exe   # 8.0.33 Windows
dotnet exec --runtimeconfig net10.json apiprobe2.exe   # 10.0.14 Windows
wsl -d Ubuntu -- bash .../api2/wslrun.sh           # 8.0.31 and 10.0.13 from ~/.dotnet-msil (read only)
```

Results (`<scratch>/p0/state/api/win.txt`, `api/wsl.txt`, `api2/win.txt`, `api2/wsl.txt`):

| Member group | Framework 4.8 (x64 and x86) | .NET 8 / 10, Windows and WSL |
|---|---|---|
| `AppDomain.CurrentDomain`, `GetData(String)`, `SetData(String,Object)`, `ProcessExit`, `DomainUnload`, `Id`, `IsFinalizingForUnload` | present | present |
| `String.Intern`; `Monitor.Enter(Object)`, `Enter(Object,Boolean&)`, `TryEnter(Object,Int32)`, `Exit`, `Wait(Object)`, `Wait(Object,Int32)`, `PulseAll`, `IsEntered` | present | present |
| `Thread.GetNamedDataSlot`, `FreeNamedDataSlot`, `GetData`, `SetData`, `CurrentThread`, `ManagedThreadId`, `Join(Int32)`, `IsBackground`, `Interrupt`, `Start`, `Sleep(Int32)`, `Yield`, `IsThreadPoolThread`; `ThreadInterruptedException` | present | present |
| `Interlocked.CompareExchange` (Int32/Int64/IntPtr), `Exchange(IntPtr)`, `Increment(Int64)`, `Read(Int64)`, `MemoryBarrier` | present | present |
| `Marshal.AllocHGlobal`, `ReAllocHGlobal`, `FreeHGlobal`, `Copy` (both ways), `ReadInt32`, `WriteInt32`, `ReadIntPtr`, `WriteIntPtr` | present | present |
| `GCHandle.Alloc/ToIntPtr/FromIntPtr/Target/Free`, `GC.KeepAlive`, `GC.SuppressFinalize` | present | present |
| `Environment.GetEnvironmentVariable(s)`, `SetEnvironmentVariable`, `CurrentDirectory`, `GetCommandLineArgs`, `Exit`, `FailFast`, `HasShutdownStarted`, `Is64BitProcess`, `Version`, `ProcessorCount`; `Process.GetCurrentProcess().Id`; `Stopwatch.GetTimestamp/Frequency`; `DateTime.UtcNow/Ticks` | present | present |
| `TimeZoneInfo.Local`, `ClearCachedData`, `GetUtcOffset`, `IsDaylightSavingTime`, `StandardName`, `DaylightName`, `BaseUtcOffset`, `Id`; `CultureInfo.GetCultureInfo`, `InvariantCulture`, `NumberFormat`; `NumberFormatInfo.NumberDecimalSeparator`; `CultureNotFoundException` | present | present |
| `Console.OpenStandardInput/Output/Error`, `Is*Redirected`, `CancelKeyPress`, `SetOut`; `Stream.Read/Write/Seek/Flush/Dispose`; `FileStream(String,FileMode,FileAccess,FileShare,Int32,FileOptions)`, `FileStream(SafeFileHandle,FileAccess,Int32)`; `SafeFileHandle(IntPtr,Boolean)`, `SafeHandle.SetHandleAsInvalid`, `DangerousGetHandle`; `File.Delete/Move`; `Path.GetTempPath/GetTempFileName` | present | present |
| `Encoding.UTF8`, `Encoding.GetString(Byte[],Int32,Int32)`, `GetBytes(String)`, `UTF8Encoding(Boolean,Boolean)` | present | present |
| `ThreadLocal<T>.Value`, `ExternalException(String,Int32)` | present | present |
| **`NativeMemory.Alloc`, `NativeMemory.AlignedAlloc`** | **absent (type)** | present |
| **`GC.AllocateArray`** (pinned-object heap) | **absent** | present |
| **`Environment.TickCount64`, `Environment.ProcessId`** | **absent** | present |
| **`Marshal.PtrToStringUTF8`** | **absent** | present |
| **`AssemblyLoadContext.Unloading`, `GetLoadContext`; `PosixSignalRegistration.Create`** | **absent (type)** | present |
| `[Obsolete]` on `Thread.GetNamedDataSlot/GetData/SetData`, `AppDomain.GetData/SetData` | not obsolete | not obsolete (8.0.33, 10.0.14, 8.0.31, 10.0.13) |

Behaviour of the discovery protocol, all five runtimes plus Framework x86: `root stable: True magic=0x52434C4D`,
`thread block stable: True, per thread: True`, `interned identity: True` (a freshly built string interns to the
same object as the literal).

Consequences for the design: the default libc (which runs on Framework 4.8 and modern .NET alike) uses only the
"present everywhere" rows. `NativeMemory`, `GC.AllocateArray`, `TickCount64`, `ProcessId` and `PtrToStringUTF8`
are never needed: `AllocHGlobal`, `Process.GetCurrentProcess().Id`, `Stopwatch` and `Encoding.UTF8` cover them.
`AssemblyLoadContext.Unloading` and `PosixSignalRegistration` exist only on .NET 5+/6+, so the code that hooks them
must be reached only there (section 7.3 and risk R4).

### 2.2 The state that llvm-libc's freestanding objects actually carry

```
for f in <scratch>/reorder-eval/libc-probe/bulk/*.o; do
  <build>/bin/llvm-nm "$f" | awk '$2 ~ /^[BbDdCc]$/'; done
```

229 data symbols (`nm-state-recheck.txt`; identical to the earlier attempt's `nm-state.txt`). After removing the
math constants (`quick_get_round::x`, `fenv_is_round_up::x`, which are read-only in practice), the mutable state of
the freestanding set (ctype, inttypes, locale, math, search, stdbit, stdlib, string, strings, time, wchar; no stdio,
as the critic noted) is:

| Symbol (namespace `__llvm_libc_msil`) | Source | Kind |
|---|---|---|
| `locale`, `c_locale` | `libc/src/locale/locale.cpp` | process state |
| `__setlocale_impl__::locale_str` | `libc/src/locale/setlocale.cpp` | per-call result buffer |
| `C_LCONV`, `DOT_STRING`, `EMPTY_STRING` | `libc/src/locale/localeconv.cpp` | constants / result |
| `atexit_callbacks`, `handler_list_mtx` | `libc/src/stdlib/atexit.cpp` | process state |
| `at_quick_exit_callbacks` | `libc/src/stdlib/at_quick_exit.cpp` | process state |
| `rand_next` | `libc/src/stdlib/rand_util.cpp` | process state |
| `strtok_str` | `libc/src/string/strtok.cpp` | process state |
| `internal_mbstate` of `mbrlen`, `mbrtowc`, `mbsnrtowcs`, `mbsrtowcs`, `wcrtomb`, `wcsnrtombs`, `wcsrtombs`, `mbstowcs`, `wcstombs` (three) | `libc/src/wchar/*.cpp`, `libc/src/stdlib/*.cpp` | process state |
| `BASE64_BUFFER` | `libc/src/stdlib/l64a.cpp` | per-call result buffer |
| `asctime`/`ctime` `buffer`, `gmtime`/`localtime` `tm_out`, `time_utils::localtime::result` | `libc/src/time/*.cpp` | per-call result buffers |

### 2.3 Source survey of the rest (stdio, threads, environment, time, signals)

Read from `libc/src` at this checkout; each row is used in section 5.

| State | Where llvm-libc keeps it | Note for the portable port |
|---|---|---|
| errno | `src/errno/libc_errno.cpp` (`thread_errno` for THREAD_LOCAL, `shared_errno` for SHARED, nothing for EXTERNAL: the embedder defines `__llvm_libc_errno`); mode default THREAD_LOCAL in `src/__support/libc_errno.h` | EXTERNAL is the only mode that lets the OS layer own the storage |
| `LIBC_THREAD_LOCAL` | `src/__support/macros/attributes.h`: empty when `LIBC_THREAD_MODE == LIBC_THREAD_MODE_SINGLE` | `thread_local` is an MSIL error until M31 (Status.md, "thread_local / _Thread_local"), so P-1b and P-2 build single-threaded |
| FILE object | `src/__support/File/file.h` (`class File`): `platform_write/read/seek/close` function pointers, a `Mutex mutex` created non-recursive, `ungetc_buf`, `buf`/`bufsize`/`bufmode`/`own_buf`, `mode`, `pos`, `prev_op`, `read_limit`, `eof`, `err`, `orientation`, `mbstate`, `prev`/`next`; static `list_all` and `list_lock` | the platform half exists only for Linux (`src/__support/File/linux/`); P-2 writes a dotnet half |
| std streams | `src/stdio/linux/stdout.cpp`: `stdout_buffer[]`, `static LinuxFile StdOut`, `LLVM_LIBC_VARIABLE(FILE *, stdout)`; likewise stdin, stderr; baremetal uses cookie stubs (`src/stdio/baremetal/stdout.cpp`) | a per-copy static object and buffer: must move to the root |
| exit lists | `src/stdlib/exit_handler.h` (`AtExitUnit`, `ExitCallbackList`, `call_exit_callbacks`: unlock, call, relock), `atexit.cpp`, `at_quick_exit.cpp`, `quick_exit.cpp`, `exit.cpp` (`__cxa_finalize(nullptr)`, then `flush_all_streams` under `LIBC_COPT_EXIT_FLUSH_STREAMS`) | units hold a raw C function pointer and a payload |
| environment | `src/stdlib/environ_internal.h` (`EnvironmentManager`: `storage`, ownership flags, `get_instance()`), `src/unistd/environ.cpp` (`LLVM_LIBC_VARIABLE(char **, environ)`) | replaced, not ported (the user's decision, 2026-11-01) |
| heap | `src/stdlib/baremetal/malloc.cpp` over `src/__support/freelist_heap.h` (`FreeListHeap` between the linker symbols `_end` and `__llvm_libc_heap_limit`) | replaced: no linker-defined heap exists on .NET |
| rand | `src/stdlib/rand_util.cpp`: `cpp::Atomic<unsigned long> rand_next = 1` | `unsigned long` is 4 bytes on msil32: the shared cell must be fixed at 8 |
| tss keys and values | `src/__support/threads/thread.cpp`: `TSSKeyMgr tss_key_mgr` (1024 units), `static LIBC_THREAD_LOCAL cpp::array<TSSValueUnit, 1024> tss_values` (about 24 KiB per thread), `static LIBC_THREAD_LOCAL ThreadAtExitCallbackMgr atexit_callback_mgr` | the key table is process state; values and the exit list are per thread |
| call_once | `src/__support/threads/callonce.h` (`#error` unless Linux), `linux/callonce.h`: futex word NOT_CALLED 0, START 0x11, WAITING 0x22, FINISH 0x33 | the flag lives in user memory; only its wait needs the buckets |
| thread list | none: llvm-libc keeps no global list of threads (no `LIBC_THREAD_LOCAL Thread self` in this checkout either) | a descriptor per thread, reached from the C block |
| per-call buffers | `src/__support/StringUtil/error_to_string.cpp` (`LIBC_THREAD_LOCAL error_buffer`), `signal_to_string.cpp` (`signal_buffer`), `src/stdlib/l64a.cpp`, `src/arpa/inet/inet_ntoa.cpp`, `src/netinet/ether/ether_ntoa.cpp` | per copy by design |
| tmpnam | `src/stdio/linux/tmpnam.cpp`: `static char TMPBUF[L_tmpnam]`, `static cpp::Atomic<size_t> tmpnam_budget = TMP_MAX`; `"/tmp/"` prefix sized into `L_tmpnam` 20 (`include/llvm-libc-macros/stdio-macros.h`) | the budget is process state; the buffer is a result buffer; `"/tmp/"` is wrong on Windows |
| time zone | `src/time/linux/tz_variables.cpp` (`tzname`, `timezone`, `daylight`), `src/time/linux/tzset.cpp` (empty `tzset`) | data source is `TimeZoneInfo.Local` |
| getopt | `src/unistd/getopt.cpp`: `optarg`, `optind`, `optopt`, `opterr` variables, `static unsigned optpos`, `static GetoptContext ctx` | section 6 |
| signals | `src/signal/linux/*` call the kernel; no disposition table exists | the portable port needs its own table |
| public variables | `LLVM_LIBC_VARIABLE` in `program_invocation_name.cpp`, `program_invocation_short_name.cpp`, `_r_debug.cpp`, `in6addr_any.cpp`, `in6addr_loopback.cpp`, `std{in,out,err}.cpp`, `tz_variables.cpp`, `environ.cpp`, `getopt.cpp` | the complete list of C globals used by address |

---

## 3. Discovery: the frozen names and the protocol

### 3.1 Names (the discovery ABI)

| Name | Kind | Value / meaning | Scope on .NET 5+ | Scope on Framework |
|---|---|---|---|---|
| `<msil>v1:libc.root` | `AppDomain.SetData` key | boxed `System.IntPtr`: address of the root (section 4) | process (one store for all ALCs) | per AppDomain |
| `<msil>v1:libc.root.lock` | interned string, used only as a `Monitor` | guards root creation and every directory insertion | process | process (harmless: it only serializes) |
| `<msil>v1:libc.thread` | `Thread.GetNamedDataSlot` name | `object[]`: `[0]` boxed `IntPtr` (the C block), `[1]` optional holder (section 5.2) | process-wide slot table, per thread | per (thread, AppDomain) |
| `<msil>v1:libc.wait:<i>`, `i` = 0..63 in decimal | interned strings, `Monitor` wait queues | `i = (uint32)(((uint64)(uintptr)addr * 0x9E3779B97F4A7C15) >> 58)` | process | process (cross-domain wakeups are only spurious; waiters re-check) |
| `<msil>v1:atomic.lock:<i>`, `i` = 0..1023 | interned strings, `Monitor` | r5's O4 lock path of atomics (`atomic.c`'s `lock_for_pointer`); frozen by T11 when it ships | process | process |

Why these and not others: the research collapsed four spellings (`libc.block`, `libc.root`, `process:...`,
`cthread`) into one key, one creation lock, one slot and one bucket set (synthesis section 4, "Fewer names"). Every
libc lock is then an ordinary llvm-libc `Mutex`-like CAS word in shared memory, and only its blocking path uses a
bucket. Host-triple outputs never look up any name (T9), so the key needs no "dotnet" tag; an alternative spelling
`<msil>v1:dotnet.libc.root` would make the C runtime identity explicit (question Q9).

### 3.2 Protocol (what every copy's OS layer does)

```
root():                                     // cached in a static of the copy after the first success
  v = AppDomain.CurrentDomain.GetData("<msil>v1:libc.root")
  if v == null:
    lock (String.Intern("<msil>v1:libc.root.lock")):      // Monitor.Enter(Object, ref bool) / Exit
      v = GetData(key)
      if v == null:
        p = AllocHGlobal(root size); zero; write header; SetData(key, (object)p); return p
  if v is not System.IntPtr: Environment.FailFast("<msil>v1:libc.root holds a <type>")   // runtimeconfig preset, squatting
  p = (IntPtr)v; check magic, major, ptr_size (section 4.4) or FailFast
  return p

thread_block():                             // cached in the copy's own per-thread cache (see 5.2)
  s = Thread.GetNamedDataSlot("<msil>v1:libc.thread"); v = Thread.GetData(s)
  if v == null: create the block on this thread, Thread.SetData(s, new object[]{ (object)p, holder })
  else if v is not object[] a || a.Length < 1 || a[0] is not IntPtr: FailFast
  check magic and major of the block or FailFast

sub_block(id):                              // directory lookup, then lazy creation
  e = root->dir[slot(id)]; b = volatile load e.block; if b != 0: check e.major/e.check; return b
  lock (String.Intern("<msil>v1:libc.root.lock")): re-check; allocate on the shared heap; fill header; publish
  e.id/major/minor/size/check, then e.block last (Interlocked.Exchange)
```

Measured by the state-homes research: without the creation lock two roots appeared in up to 772 of 2,000 races
(Framework) and 400 of 400 (WSL); with it, never. A runtimeconfig `configProperties` preset of the key arrives as a
`System.String`, which the type test catches. Root lookup 73-301 ns once per copy; block lookup 12-33 ns once per
thread per copy; errno set+get 1.5-2.1 ns cached (about 1 ns more than a private `[ThreadStatic]`).

What the glue needs from MC-1: object-typed BCL calls (`AppDomain.GetData` returns `object`), boxing and an
`isinst`/`unbox` of `System.IntPtr`, `new object[]`, `String.Intern`, `Monitor.Enter(object, ref bool)`,
`Thread.GetData(LocalDataStoreSlot)`. See risk R1.

---

## 4. The process root, v1

### 4.1 Header

All offsets are those of a natural-alignment C struct, so msil32 and msil64 get different offsets from one
declaration; the two widths never meet in one process (each refuses the other width), and `ptr_size` checks it.
Proposed home of the declaration: `libc/src/__support/OSUtil/dotnet/shared_state.h` (new; the dotnet OS layer).

```c
struct __msil_libc_dirent {          // 16 bytes on msil32, 24 on msil64
  uint32_t  id;                       // section 4.2; 0 = free slot
  uint16_t  major, minor;             // of the sub-block
  uint32_t  size;                     // bytes as created
  uint32_t  check;                    // layout check: FNV-1a of the sub-block's frozen field list text
  uintptr_t block;                    // published last; 0 = not created yet
};
struct __msil_libc_root {
  uint32_t  magic;                    // 0x52434C4D 'MLCR'
  uint16_t  major;                    // 0 provisional (P-2); 1 from P-3's commit; never changes afterwards
  uint16_t  minor;                    // header fields appended; informational
  uint32_t  size;                     // bytes of this header as created
  uint8_t   ptr_size;                 // 4 or 8
  uint8_t   scope;                    // 0 process (.NET 5+), 1 AppDomain (Framework): diagnostics only
  uint16_t  dir_cap;                  // 64
  uint64_t  creator;                  // libc release id of the creating copy (diagnostic)
  uint32_t  flags;                    // bit 0: a copy reported a mismatch (diagnostic, before FailFast)
  uint32_t  reserved;
  struct __msil_libc_dirent dir[64];  // slot = id - 1
};
```

The root itself is never freed (an `AllocHGlobal` block that outlives every copy, including a collectible creator:
the research measured adoption after the creator unloaded).

### 4.2 Directory ids (frozen enumeration) and what each sub-block holds

Each sub-block starts with `{uint32 magic, uint16 major, uint16 minor, uint32 size}` and lives on the shared heap
(section 5.4), never in a copy's static data.

| id | Name | Contents (v1) | First phase |
|---|---|---|---|
| 1 | STDIO | `FILE *std[3]` (stdin, stdout, stderr; the slots `stdin`/`stdout`/`stderr` expand to, section 9 item F); open-FILE list head; list lock word; the "flush hook registered" flag | P-2 |
| 2 | FD | descriptor table (pointer, capacity, lowest-free rule), lock word; open file descriptions `{refcount, offset, raw handle, kind, flags, serial}` | P-2 (with `open`/`fileno`) |
| 3 | EXIT | atexit/`__cxa_atexit` list and `at_quick_exit` list: 32-byte entries `{fn, payload, owner dso, kind}` (16 bytes on msil32); list lock word; exit-state word (0 live, 1 `exit` running, 2 `_Exit`/`quick_exit`, 3 ProcessExit fallback running); exit status | P-2 |
| 4 | ENV | `char **block` built lazily from `System.Environment`; generation counter; registry of every copy's `&environ` with owner dso; env lock word; the empty-value overlay (.NET 8 and Framework delete empty values) | P-2 |
| 5 | LOCALE | global locale: per-category locale ids (`LC_CTYPE` .. `LC_MESSAGES`, 6) and pointers to immutable shared descriptors; the C locale descriptor | P-2 |
| 6 | MISC | `uint64 rand_next` (xorshift64*, initial 1); `char *strtok_save`; ten hidden `mbstate_t` cells (in a fixed order: `mbrlen`, `mbrtowc`, `mbsnrtowcs`, `mbsrtowcs`, `wcrtomb`, `wcsnrtombs`, `wcsrtombs`, `mbstowcs`, `wcstombs`, `wctomb`); `uint64 tmpnam_budget`; `hsearch` table pointer | P-2 |
| 7 | SIGNAL | 65 dispositions (0 `SIG_DFL`, 1 `SIG_IGN`, else a managed function pointer `void(int)`), each swapped with `Interlocked.Exchange`; owner dso per entry; "OS hook registered" bits | P-2 (`signal`/`raise`); P-3 (`sigaction` flags, masks) |
| 8 | TSS | 1024 units `{active, dtor, owner dso}` (llvm-libc's `TSSKeyMgr` shape, 32-bit keys) and its lock word | P-3 |
| 9 | THREAD | main-thread descriptor pointer; next thread serial; detach/join protocol constants | P-3 |
| 10 | TIME | parsed `TZ` rules (the libc parses `TZ` itself: Windows ignores it and Linux falls back to UTC for POSIX rule strings), generation counter | P-2 |
| 11 | STARTUP | `char *argv0`, `char *argv0_short` (for `program_invocation_name`), `argc`/`argv` copy built once from `Environment.GetCommandLineArgs()`, set-once flag | P-2 |
| 12 | HEAP | no state: `{scheme id = 1 (2-pointer header over AllocHGlobal), header size}`, written once and checked by every copy | P-2 |
| 13-64 | reserved | new features take a new id, never a larger existing sub-block | - |

### 4.3 Versioning rules (proposed text for contract 5.19)

1. **Names never change.** `v1` is a namespace. A "v2" key would silently create a second state (r5 measured two
   objects for v1 and v2 keys without any error), so no later release may introduce one.
2. **Append-only by new ids.** After the freeze a sub-block's layout never changes, not even by appending: a copy
   of an older release may have created it with the old size, and a newer copy could not grow it in place. A new
   field goes into a new directory id; a new per-thread field goes into the C block's extension (5.3).
3. **Never reinterpret a field.** Changing a field's meaning takes a new field in a new sub-block.
4. **Checks.** Each directory entry records `major` and `check`. A copy that finds an entry whose `check` differs
   from its compiled value for the same id, or a root or sub-block major it does not know, calls
   `Environment.FailFast` with both versions and the id (question Q1 offers the private-instance alternative).
5. **Provisional layouts.** Before P-3's commit every header carries major 0, and a section 12 row may change any
   layout (as T11 allows rows before its milestone's commit). Major 1 copies FailFast on a major 0 root, so a
   pre-freeze `c.dll` and a frozen static copy never share silently.
6. **The contract version travels with each output**: the symbol-table attribute that already carries an ABI
   version records "libc shared-state major" for `c.dll` and every `-static` output, so msil-ld `--verbose` can
   report a mix (state-homes synthesis, item 7; EXEs have no `<msil>SymbolTable`, so the EXE case needs a field the
   portable triple's design adds).
7. **Gate:** a two-release test (an assembly built by the previous release and one by the current release share
   stdout order, descriptors, errno and the exit list) from the first release after the freeze.

### 4.4 Checks every copy makes at discovery

`magic`; `major` known; `ptr_size == sizeof(void *)`; the HEAP sub-block's scheme id equals 1. Any failure is a
`FailFast` naming the key (fail loudly, standing rule).

---

## 5. Per-thread state, the heap, and the thread block

### 5.1 The C block, v1

```c
struct __msil_libc_thread {
  uint32_t  magic;           // 0x54434C4D 'MLCT'
  uint16_t  major, minor;
  uint32_t  size;
  int32_t   err;             // errno: offset 12 on both widths; __llvm_libc_errno() returns &blk->err
  uintptr_t desc;            // thread descriptor on the shared heap (P-3), 0 before
  uintptr_t locale;          // locale_t set by uselocale; 0 = the global locale (never LC_GLOBAL_LOCALE raw)
  uintptr_t tss_values;      // lazily allocated 1024 x {value, dtor} on the shared heap (P-3)
  uintptr_t exit_list;       // thread-exit list head: nodes {fn, obj, owner dso, next} (P-3, with M31)
  uint32_t  flags;           // bit 0 libc-started, bit 1 main thread, bit 2 exiting, bit 3 in exit()
  uint32_t  fe_except;       // software FP exception flags (fenv); rounding is always to-nearest
  uint64_t  sigmask;         // blocked signals 1-64 (P-3)
  uintptr_t ext;             // extension block for fields added after the freeze (rule 4.3.2)
};
```

Rules: a thread's block is created and written only by that thread, except `desc`'s join state, which the
descriptor protocol guards. llvm-libc is built with `LIBC_ERRNO_MODE_EXTERNAL`
(`src/__support/libc_errno.h`); the OS layer defines `__llvm_libc_errno` (`src/errno/libc_errno.cpp`'s EXTERNAL
branch then forwards `libc_errno` to it). This settles the critic's NOTE ("EXTERNAL over one cell" vs SHARED):
EXTERNAL, always.

### 5.2 Allocation and lifetime of the block

The decode rule (`object[]`, `[0]` a boxed `IntPtr`, `[1]` optional) lets two kinds of block coexist, as the
research measured:
- **.NET 5+:** a pinned-object-heap `long[]` (`GC.AllocateArray<long>(n, pinned: true)`), whose address is the
  block; `[1]` is the array itself; no finalizer, no type of the copy, freed by the GC after the thread ends.
- **Framework:** an `AllocHGlobal` block plus a finalizable holder that frees it; Framework has no collectible ALC,
  so a holder type defined in the copy pins nothing; the holder must not free while
  `Environment.HasShutdownStarted` (Framework finalizes reachable objects at exit).

The copy picks the kind once by `Environment.Version.Major >= 5` (present everywhere). Reaching
`GC.AllocateArray` needs MC-1 to call a generic method that mscorlib lacks; if MC-1 cannot, the fallback is the
Framework kind on every runtime, which pins a collectible copy for the life of each thread it touched (measured: 20+
GC rounds while referenced). Each copy caches the block in its own per-thread cache. Before M31 that cache cannot
be a C `thread_local`; it is the OS layer's managed-call result cached in nothing, i.e. one slot lookup (12-33 ns)
per errno-touching call, unless MC-1 can declare a `[ThreadStatic]` field (risk R2).

### 5.3 The heap

- **Scheme 1, frozen:** every block is `AllocHGlobal(size + H + align slack)` with a header of `H = 2 *
  sizeof(void *)` bytes (16 on msil64, 8 on msil32) immediately before the returned pointer: `{base, size}`.
  `free(p)` reads `base` and calls `FreeHGlobal(base)`; `malloc_usable_size` reads `size`; `realloc` uses
  `ReAllocHGlobal` only when `base == p - H` (no alignment offset), else allocate-copy-free; `aligned_alloc` and
  `posix_memalign` over-allocate and store the header before the aligned pointer.
- `malloc` alignment: 16 on msil64 (Windows `HeapAlloc` and glibc `malloc` both give 16 on 64-bit), 8 on msil32;
  `max_align_t` must agree (P-1a, section 9).
- Any copy, ALC or AppDomain can free any block, and a block survives its allocating copy's unload (measured,
  64-byte malloc+free 87-125 ns).
- **Never `NativeMemory`** for the C heap: absent on Framework (2.1), and a copy built with a `NativeMemory` switch
  next to a default copy would hand `AlignedAlloc` blocks to `FreeHGlobal` (the research saw heap corruption
  0xC0000374 when schemes mix). This narrows the 23:09 record's wording "the heap on NativeMemory (Marshal.AllocHGlobal
  on Framework)" (question Q2).
- llvm-libc's `FreeListHeap` (`src/__support/freelist_heap.h`) is not built: it needs `_end` and
  `__llvm_libc_heap_limit`.
- Everything the shared state points to (FILE objects and buffers, the environment block, descriptors, tss tables,
  exit entries' nodes) comes from this heap, and llvm-libc's `operator new`/`delete` must call it too (the
  research: "LLVM's `operator delete` calls the copy's own `free`").

---

## 6. Where every piece lives

Legend: **CB** per-thread C block (5.1); **R/n** root sub-block id n (4.2); **BCL** a .NET object; **copy**
deliberately per copy (section 7). "Phase" is when the piece first exists on the portable triple.

| State | llvm-libc today (file: symbol) | Home | Phase | Notes |
|---|---|---|---|---|
| errno | `src/errno/libc_errno.cpp`: `thread_errno`/`shared_errno` | CB `err`; EXTERNAL `__llvm_libc_errno` | P-1b cell (Q5), CB from P-2 | `extern int errno;` in user code gets a link diagnostic (the name is a macro over the function) |
| stdin/stdout/stderr FILE objects and buffers | `src/stdio/linux/std*.cpp`: `StdOut`, `stdout_buffer` | R/1 `std[3]`, FILEs and buffers on the shared heap | P-2 | buffering chosen by `Console.Is*Redirected`; stderr unbuffered; I/O through each copy's own `Console.OpenStandard*` stream (never `Console.Out`, which re-encodes bytes >= 0x80) |
| open-FILE list | `src/__support/File/file.h`: `File::list_all`, `File::list_lock` | R/1 head + lock word; link fields in each FILE | P-2 | `fflush(NULL)` in any copy flushes FILEs opened by every copy (measured) |
| FILE object layout | `class File` with `platform_*` function pointers | shared-heap struct with a `kind` enum (console stdin/stdout/stderr, fd description, cookie, memstream, popen) and a description pointer; function pointers only for `fopencookie` callbacks | P-2 | the creator copy may unload; a `kind` dispatches in whichever copy does the I/O |
| FILE lock | `File::mutex`, created non-recursive | CAS word `{owner ManagedThreadId, recursion count}` in the FILE, blocking through R wait buckets | P-2 | recursive, as POSIX requires (fixes the upstream `flockfile`+`fputs` deadlock, section 12) |
| descriptors and open file descriptions | Linux kernel table | R/2; the table owns the OS handle (`SafeHandle.SetHandleAsInvalid` on the opener); each copy wraps it in `new SafeFileHandle(raw, false)` cached by serial | P-2 | the OS table is no option: Windows has no small-integer descriptors |
| popen | `src/stdio/linux/popen.cpp` | a popen FILE in R/1's list; child through `System.Diagnostics.Process` | P-3 or later | Framework domains cannot share a `Process` handle (per-domain scope makes that moot) |
| malloc heap and its header | `src/stdlib/baremetal/malloc.cpp`, `FreeListHeap` | no shared object; frozen scheme 1 (5.3); R/12 records it | P-2 (malloc is OS-dependent: absent in P-1b) | |
| atexit, `__cxa_atexit`, `exit` | `src/stdlib/atexit.cpp`: `atexit_callbacks`, `handler_list_mtx`; `exit.cpp` | R/3 list LIFO across copies, exit-state word; `exit` in any copy runs the list on its own thread, flushes, then `Environment.Exit`; every copy hooks `AppDomain.ProcessExit` as a fallback, first CAS wins | P-2 | keep `call_exit_callbacks`'s unlock-call-relock rule; `Environment.Exit` inside a ProcessExit handler hangs .NET 8/10, so an `exit` during the fallback unwinds to the runner; withheld from P-1b (no `exit` before P-2); M29's binding of `__cxa_atexit` must be re-pinned for portable (P-1r; plan section 11's M29 question) |
| `at_quick_exit`, `quick_exit`, `_Exit` | `at_quick_exit.cpp`: `at_quick_exit_callbacks` | R/3 second list; state 2 | P-2 | `_Exit` still runs the C# host's own ProcessExit handlers (no BCL immediate exit with a status) |
| environ / getenv cache | `EnvironmentManager`, `environ` | store: `System.Environment` itself (process-wide even across Framework domains); R/4 block rebuilt under the env lock after each change from C; each copy's `environ` variable kept in step through R/4's registry | P-2 | `getenv` returns pointers into the shared block, valid until the next change; a change from C# reaches `environ` at the next change from C |
| current directory | (kernel) | `Environment.CurrentDirectory` (BCL) | P-2 | |
| rand / srand | `rand_util.cpp`: `rand_next` | R/6 `uint64` with llvm-libc's CAS loop | P-1b C global; R/6 from P-2 | fixed 8 bytes on both widths |
| strtok | `strtok.cpp`: `strtok_str` | R/6, per process (LLVM and glibc semantics) | P-1b global; R/6 from P-2 | Q8 |
| hidden mbstates | `internal_mbstate` in 10 functions | R/6, ten cells | P-1b globals; R/6 from P-2 | |
| strerror, strsignal, l64a, inet_ntoa, ether_ntoa buffers | `error_buffer`, `signal_buffer`, `BASE64_BUFFER`, `buffer` | copy (per assembly; per thread once M31 makes `LIBC_THREAD_LOCAL` real) | P-1b | C promises the result only until the next call |
| asctime, ctime, gmtime, localtime results | `time/*.cpp` statics | copy | P-1b | |
| global locale (`setlocale`, `localeconv`) | `locale.cpp`: `locale`, `c_locale` | R/5 per-category ids and immutable descriptors on the shared heap; `CultureInfo.GetCultureInfo` only as the data source | P-1b C global; R/5 from P-2 | never `CultureInfo.DefaultThreadCurrentCulture` (the host's setting leaks into C); invariant-globalization mode makes `GetCultureInfo` throw `CultureNotFoundException`, so `setlocale` returns NULL there |
| per-thread locale (`uselocale`) | (process-wide `locale` today: a POSIX deviation) | CB `locale` | P-2 (with `newlocale`) | |
| `setlocale`'s returned string, `localeconv`'s struct | `locale_str`, `C_LCONV` | copy (content from R/5) | P-1b | |
| tss / pthread key table | `thread.cpp`: `tss_key_mgr` | R/8, LLVM's 32-bit keys, 1024 units | P-3 | absent (link errors) before P-3 |
| tss values | `thread.cpp`: `tss_values` (`LIBC_THREAD_LOCAL`, 24 KiB per thread per copy) | CB `tss_values`, lazily allocated | P-3 | a copy that sets another copy's key needs the shared key table; the destructor is copied into the unit at set time, as llvm-libc does |
| thread-exit list (C++ `thread_local` dtors, tss dtors, cleanup handlers) | `thread.cpp`: `atexit_callback_mgr` | CB `exit_list`; the managed `thrd_create`/`pthread_create` trampoline runs it LIFO across copies | P-3 / M31 | threads the libc did not start: no BCL thread-exit event (Q10 of the research: fail fast, post-mortem or never; plan's host-triple choice is fail fast) |
| thread list / descriptors | none | descriptor on the shared heap from CB `desc`: `{GCHandle to System.Threading.Thread, join/detach state, return value, cleanup stack}`; no global list | P-3 | managed thread ids are reused, so `pthread_t` is the descriptor address |
| once flags | `callonce.h` futex word | user memory (`once_flag`); waiters block on R wait buckets | P-3 | the state values 0/0x11/0x22/0x33 freeze with the type |
| mutexes, condition variables, semaphores, joins (wait side) | futexes | user memory words + R wait buckets (`<msil>v1:libc.wait:<i>`) | P-3 | measured: 0 lost wakeups in 100,000 cross-copy round trips; every blocking path must handle `ThreadInterruptedException` |
| lock path of atomics | `atomic.c`'s table | `<msil>v1:atomic.lock:<i>` (r5 O4) | P-3 (with M30's protocol for portable) | T11 freezes it |
| signal handlers | none (kernel) | R/7 table; `raise` calls the handler on the calling thread; OS delivery through `PosixSignalRegistration` (.NET 6+, deduplicated by `PosixSignalContext.Cancel`) or `Console.CancelKeyPress` (Framework) | P-2 (`signal`, `raise`); P-3 (`sigaction`, masks in CB `sigmask`) | the CLR owns SIGSEGV/SIGFPE/SIGILL/SIGBUS; Windows accepts only SIGHUP, SIGINT, SIGQUIT, SIGTERM |
| time zone (`tzset`, `tzname`, `timezone`, `daylight`) | `tz_variables.cpp`, empty `tzset` | `TimeZoneInfo.Local` (BCL, process-wide cache); `tzset` = `ClearCachedData` + re-parse `TZ` into R/10; the three variables are per copy, recomputed by that copy's `tzset`/`localtime` | P-2 | |
| tmpnam / tmpfile | `tmpnam.cpp`: `TMPBUF`, `tmpnam_budget` | budget R/6 (`Interlocked`); `TMPBUF` copy; names under `Path.GetTempPath()`; `tmpfile` = a descriptor opened with `FileOptions.DeleteOnClose` | P-2 | `L_tmpnam` 20 cannot hold a Windows temp path (section 9) |
| getopt (`optarg`, `optind`, `opterr`, `optopt`, `optpos`) | `getopt.cpp` | copy (recommended, Q4) | P-2 | the program's own copy runs `getopt` over its own variables, so a program is always consistent; only a library's `getopt` on the program's argv would differ |
| `program_invocation_name`, `_short_name` | `errno/program_invocation*.cpp` | variable per copy, filled once from R/11 (same value everywhere) | P-2 | |
| argv for `main` | crt1 / `AppProperties` (`libc/config/linux/app.h`) | the generated `Main` (Design.md section 12); R/11 for library copies that need argv0 | P-2 | |
| FP environment | `FEnvImpl.h` fallback does nothing | CB `fe_except` for flags the libc raises itself; rounding fixed to nearest (ECMA-335 I.12.1.3) | P-1b decides `math_errhandling` = `MATH_ERRNO` | |
| hsearch table | `search/hsearch/global.cpp`: `global_hash_table` | R/6 pointer; table on the shared heap | P-1b global; R/6 from P-2 | |
| `pthread_atfork` list | `fork_callbacks.cpp` | none: `fork` fails (no .NET equivalent) | - | |
| libc++abi `__cxa_eh_globals` (C++ goal) | libc++abi | CB extension field | C++ phase | only if libc++abi is linked statically into several copies |

---

## 7. Per copy by design, and the duties that come with sharing

### 7.1 Per copy

- Per-call result buffers (6, rows marked "copy"): C allows separate buffers, because a result is valid only until
  the next call.
- `__dso_handle`: identifies the module to `__cxa_atexit` and to the unload duties, so it must differ per assembly.
  A shared `c.dll` probably needs a small static companion per output for it (libc-modes gap 4).
- `__stack_chk_guard`: each function checks its own copy's guard.
- Constants (`in6addr_any`, `in6addr_loopback`, `C_LCONV`), `_r_debug` (inert on .NET).
- getopt variables and the `tzname`/`timezone`/`daylight` variables (values from shared sources).
- The toolchain's per-assembly state: TLS block, thunk table, shadow stack, `'MSJ1'` status blocks, compiler-rt
  builtins (stateless).
- Code and therefore function identity: each copy's `&printf` differs, as under `/MT`.

### 7.2 Function pointers in shared lists

The portable triple's function pointers are managed (`ldftn`/`calli`, user decision). Shared lists (exit lists,
signal table, thread-exit lists, tss destructors, `fopencookie` callbacks) therefore store `ldftn` values, and any
copy calls them with `calli` and the frozen signature (`void(void)`, `void(void *)`, `void(int)`). The research
measured cross-copy `calli` for the exit, signal and thread-exit prototypes. On Framework the state is per
AppDomain, so a stored pointer never crosses a domain. Identity caveat: Design.md 4.5's per-function address fields
apply (an `ldftn` in two methods may differ on Framework), which matters only for comparisons such as
`signal()`'s returned previous handler; the libc returns the stored value, never a fresh `ldftn`.

### 7.3 Unload duties (collectible copies, .NET 5+ only)

A copy in a collectible ALC registers on `AssemblyLoadContext.GetLoadContext(own assembly).Unloading` (absent on
Framework, so reached only on .NET 5+) and removes every entry whose owner is its `__dso_handle`: exit entries (run
them, as `__cxa_finalize(dso)` does at `dlclose`), signal dispositions (reset to `SIG_DFL`), tss destructors,
`fopencookie` FILEs, environ registry entries, and its own `ProcessExit` subscription. Without this the research
saw exit crash (0xC0000005 on Windows, SIGSEGV on Linux). A copy's own types must never be stored in shared slots or
AppDomain data (they pin the ALC).

---

## 8. Phasing against the decided program order

| Phase | What exists | State work |
|---|---|---|
| P-1a | `msil64-unknown-dotnet`, no libc | fix the C ABI items marked P-1a (section 9) in clang's target info |
| P-1b | freestanding llvm-libc as `c.dll`, no managed calls | no root, no C block (they need object-typed BCL calls). The single copy keeps llvm-libc's C globals for rand, strtok, the hidden mbstates, the global locale and hsearch; errno through EXTERNAL over one process cell (Q5). Withhold `atexit`/`at_quick_exit` with `exit` (OS-dependent). Programs see only functions (and `stdout`-style macros), so moving these into the root at P-2 changes no program-visible ABI. Fix the C ABI items marked P-1b |
| P-1r | re-pins | M27's portable `jmp_buf` branch (section 9, J); M29's `__cxa_atexit` binding for portable goes into plan section 11's M29 question |
| MC-1 | managed calls | must cover the glue of 3.2 (risk R1) |
| P-2 | single-threaded OS layer, first printf | implement 3.2, the root with sub-blocks 1-7, 10-12, the C block (errno, locale), heap scheme 1, the dotnet `File` half with `kind` dispatch; all headers major 0. Estimate on top of P-2's own: 8-12 h (root, directory, block, FailFast checks, tests on all five runtimes) |
| P-3 | threads, per-thread state, `-static` | sub-blocks 8-9, CB `desc`/`tss_values`/`exit_list`/`sigmask`, wait buckets, `-static` copies; **freeze: major 1 at P-3's commit**, contract 5.19 and the frozen-items rule; the two-release test from the first release after. Estimate: 10-15 h on top of P-3 (cross-copy tests with S/I/C/F/H scopes, the unload duties) |
| P-4 | msil32 | the same declarations at 4-byte pointers; `clock_t` and `struct timespec` confirmation |

Relation to existing rules: T9 forbids named lookups for host-triple helpers and leaves the portable triple's
objects to its design (this report). T10 covers helper names; the new frozen-items rule covers the names of 3.1 and
the layouts of 4-5. T11 covers `<msil>v1:atomic.lock:<i>`; the wait buckets join the same kind of freeze.

---

## 9. Portable C ABI items that freeze when the shared state ships

"Program ABI" means the item is compiled into user code, so it freezes with the first `c.dll` users link against,
independent of the shared state. "Copy ABI" means it is part of what copies share.

| Item | llvm-libc today | Proposal | Fix in | Why then |
|---|---|---|---|---|
| **E. errno numbering** (program ABI) | `libc/hdr/errno_macros.h` picks `include/llvm-libc-macros/generic-error-number-macros.h` when neither `__linux__` nor `__APPLE__`; that family defines only `EPERM`..`ERANGE` (1-34), `EOVERFLOW` **35** and `EILSEQ` 84, so `EOVERFLOW` collides with Linux's `EDEADLK` (35; Linux `EOVERFLOW` is 75) and `ENOSYS`, `ETIMEDOUT`, `EDEADLK`, `ENAMETOOLONG`, `ENOTSUP`, `EWOULDBLOCK` are missing | a dotnet family `include/llvm-libc-macros/dotnet/error-number-macros.h`, selected by the portable triple's macro in `hdr/errno_macros.h` and the public errno header: the complete Linux asm-generic numbering (1-133, `EWOULDBLOCK` = `EAGAIN` 11, `ENOTSUP` = `EOPNOTSUPP` 95, `EOVERFLOW` 75, `ECANCELED` 125 .. `EHWPOISON` 133 as in `include/llvm-libc-macros/linux/error-number-macros.h`). Do not change the upstream generic family (baremetal users depend on it) | P-1b | freestanding entrypoints already return `ERANGE`, `EINVAL`, `EDOM`, `EILSEQ`, `EOVERFLOW` |
| errno access (program ABI) | `errno` is `(*__llvm_libc_errno())` | freeze that spelling; `extern int errno;` gets a link diagnostic | P-1b | |
| **W. wchar_t** (program ABI) | `__WCHAR_TYPE__` | decided: 32-bit signed on both; `wint_t` unsigned 32-bit, `WEOF` 0xFFFFFFFFu; UTF-32 C locale | P-1a | |
| plain `char` signedness (program ABI) | target info | signed (x86 convention; most portable code assumes nothing, but some assumes signed) | P-1a | for the target recon to confirm |
| **L. long double** (program ABI) | `LIBC_TYPES_LONG_DOUBLE_IS_FLOAT64` (`src/__support/macros/properties/types.h`) | IEEE binary64, `sizeof` 8, on both (D3's DataModel rule already says 8); no `long double` forwarders on portable; `%Lf` reads a `double` | P-1a/P-1b | math `l` functions and `strtold` ship in P-1b |
| **T. time_t** (program ABI) | `include/llvm-libc-types/time_t_64.h`: `__INT64_TYPE__`, unconditional | 64-bit on both, msil32 included (Y2038-safe) | P-1b | `time.h` functions are in the freestanding set |
| `struct timespec` (program ABI) | `include/llvm-libc-types/struct_timespec.h`: `{time_t tv_sec; __INT64_TYPE__ tv_nsec;}` | keep: 16 bytes on both widths (on msil32 `tv_nsec` is 64-bit, not POSIX's `long`, as C23 allows and llvm-libc chose) | P-1b (msil32 confirmed at P-4) | `timespec_get` is P-2, but the type ships with `time.h` |
| `struct tm` (program ABI) | llvm-libc layout (`tm_gmtoff` type in `tm_gmtoff_t.h`) | keep llvm-libc's | P-1b | |
| `clock_t` (program ABI) | `include/llvm-libc-types/clock_t.h`: `long` | msil64: `long` (64); msil32: 64-bit too (a 32-bit `clock_t` at `CLOCKS_PER_SEC` 1,000,000 wraps after about 36 minutes) | msil64 P-1b; msil32 P-4 | |
| **O. off_t, fpos_t** (program ABI) | `off_t.h`: `__INT64_TYPE__`; `fpos_t.h`: `{off_t, mbstate_t}` | keep: 64-bit `off_t` on both; `fpos_t` 16 bytes | P-2 | `fseeko`/`fgetpos` arrive with stdio |
| **F. FILE** (program ABI: opaque) | `FILE.h`: `typedef struct FILE FILE;` | opaque forever; the struct is copy ABI only (6, "FILE object layout") | P-1b | |
| `stdin`/`stdout`/`stderr` spelling (program ABI) | `LLVM_LIBC_VARIABLE(FILE *, stdout)`, `extern FILE *stdout;` | lvalue macros over a function returning the R/1 slot: `#define stdout (*__llvm_libc_stdio_slot(1))` (name to be chosen). C requires only "expressions of type pointer to FILE"; assignment still works and reaches every copy; needs no msil-ld feature and no constructor ordering (Q3) | P-1b if `stdio.h` ships with `snprintf`; else P-2 | a variable would need each copy initialized before any C constructor prints, plus msil-ld's process-data kind for `stdout = f` |
| `BUFSIZ`, `EOF`, `FOPEN_MAX`, `FILENAME_MAX`, `L_tmpnam`, `TMP_MAX`, `_IOFBF`/`_IOLBF`/`_IONBF`, `SEEK_*` (program ABI) | `include/llvm-libc-macros/stdio-macros.h`: `BUFSIZ` 1024, `L_tmpnam` 20 (`"/tmp/"` + 14 + NUL), `TMP_MAX` 1,000,000 | keep `BUFSIZ` 1024 and `TMP_MAX`; raise `L_tmpnam` and `FILENAME_MAX` to fit `Path.GetTempPath()` on Windows (for example 260 + 16); `SEEK_SET/CUR/END` 0/1/2 | P-2 | `setbuf(f, buf)` assumes `BUFSIZ`; `tmpnam(buf)` assumes `L_tmpnam` |
| `O_*`, `AT_*`, `F_*`, `S_I*`, `mode_t`, `struct stat` (program ABI) | `include/llvm-libc-macros/fcntl-macros.h` includes `linux/fcntl-macros.h` only under `__linux__` | a dotnet family with Linux generic values (`O_RDONLY` 0, `O_WRONLY` 1, `O_RDWR` 2, `O_CREAT` 0100, `O_EXCL` 0200, `O_TRUNC` 01000, `O_APPEND` 02000, `O_NONBLOCK` 04000, `O_CLOEXEC` 02000000); flags the BCL cannot honour fail with `EINVAL` | P-2 (`open`), P-3 (`stat`, directories) | |
| signal numbers, `NSIG`, `sigset_t`, `SIG_DFL`/`SIG_IGN`/`SIG_ERR` (program ABI and copy ABI) | `signal-macros.h` has no branch for the portable triple; `sigset_t.h` needs `__NSIGSET_WORDS` | dotnet family: Linux numbers 1-64 (`SIGINT` 2, `SIGTERM` 15, ...), `NSIG` 65, `sigset_t` = one `uint64_t`; `SIG_DFL` 0, `SIG_IGN` 1, `SIG_ERR` -1 (matches R/7's encoding) | P-2 (`signal`, `raise`) | |
| **J. jmp_buf / sigjmp_buf** (program ABI and the toolchain's M27 contract) | `include/llvm-libc-types/__jmp_buf.h` `#error`s for any architecture it does not list | dotnet branch: `typedef struct { __UINT64_TYPE__ __opaque[25]; }` (200 bytes, 8-aligned) on both widths; the `'MSJ1'` prefix of plan 5.15.4 at 0-15; the savemask word at 64 and the saved `sigset_t` (8 bytes) at 72, as on Linux triples | type in P-1b (setjmp.h ships); M27's portable branch pinned in P-1r | `setjmp` is lowered per module (no libc entrypoint), so only the size and offsets freeze; nothing is keyed by the buffer address |
| `mbstate_t` (program ABI and copy ABI: R/6 cells) | `mbstate_t.h`: `{uint32, uint8, uint8}`, 8 bytes | keep | P-1b | |
| `locale_t`, `LC_*`, `LC_GLOBAL_LOCALE` (program ABI and copy ABI) | `locale_t.h`: pointer to `{__locale_data *data[6]}`; `uselocale` stores `LC_GLOBAL_LOCALE` (-1) raw | keep the struct; descriptors immutable on the shared heap; CB stores 0 for the global locale | P-1b (types), P-2 (R/5) | |
| `max_align_t`, malloc alignment (program ABI) | clang target info | 16 on msil64, 8 on msil32 (5.3) | P-1a | |
| `RAND_MAX`, `MB_LEN_MAX`, `MB_CUR_MAX` (program ABI) | llvm-libc macros | keep llvm-libc's values; UTF-8 `MB_CUR_MAX` 4 in the C.UTF-8 locale | P-1b | |
| thread types: `once_flag`, `mtx_t`, `cnd_t`, `tss_t`, `thrd_t`, `pthread_*_t` sizes, `PTHREAD_*_INITIALIZER` (program ABI) | `once_flag.h` `#error`s off Linux; `tss_t`/`pthread_key_t` `unsigned int` | user-memory words of llvm-libc's shapes, waiting through the wait buckets; 32-bit keys (Q8); a portable branch for each `#error` | P-3 | absent before P-3 |
| `size_t`, `long`, pointer widths | decided (ILP32 / LP64) | - | P-1a | |

Upstream conflict surface: the dotnet branches touch `hdr/errno_macros.h`, `include/llvm-libc-macros/*-macros.h`
dispatchers, `__jmp_buf.h`, `once_flag.h` and `sigset_t`'s macro. Each is a small `#elif`, but each is a merge
surface; an overlay include directory under `msil/` is the alternative (risk R6).

---

## 10. Risks

- **R1 MC-1 scope.** The glue (3.2) needs object-typed BCL calls: `object` returns, boxing and type tests of
  `System.IntPtr`, `new object[]`, `String.Intern`, `Monitor.Enter(object, ref bool)`, `Thread.GetData`. The
  research flagged this ("the managed-calls goal does blittable statics first"). If MC-1 is blittable-only, P-2
  cannot build the root. The MC-1 recon must list these members as required.
- **R2 Per-thread caching before M31.** Without a C `thread_local` or a `[ThreadStatic]` declared through managed
  calls, every errno access costs a slot lookup (12-33 ns) instead of about 1.5 ns. Correct, but slow for
  errno-heavy code (`strtol` loops). MC-1 could expose a `[ThreadStatic]` IntPtr field, or M31's TLS gives the cache.
- **R3 Freezing too early or too late.** Major 0 until P-3's commit keeps P-2 free to change layouts; the risk is a
  public toolchain release between P-2 and P-3, whose `c.dll` would then be a pre-freeze layout. Rule 4.3.5 makes
  that mix fail loudly instead of splitting silently.
- **R4 APIs absent on Framework.** `AssemblyLoadContext.Unloading`, `PosixSignalRegistration`, `GC.AllocateArray`
  exist only on .NET 5+/6+ (2.1). The default libc must reach them only behind a runtime check and without a static
  reference that Framework's JIT resolves eagerly (the 7.1 reference-set rules; the call sites must be in methods
  Framework never JIT-compiles).
- **R5 Foreign interference.** Any code can call `Thread.FreeNamedDataSlot("<msil>v1:libc.thread")` or overwrite
  the AppDomain key. The type tests and FailFast catch overwrites; a freed slot makes later lookups create a second
  block for a thread (errno split). Documented as unsupported.
- **R6 Upstream conflicts.** Dotnet branches in llvm-libc headers (section 9) and a new `__support/File/dotnet` and
  `OSUtil/dotnet` add merge surface; FILE needing a `kind` instead of function pointers diverges from `class File`.
- **R7 Thread.Interrupt and aborts.** `Monitor.Enter`/`Wait` throw `ThreadInterruptedException`; every blocking
  path (FILE lock, exit-list lock, wait buckets, atomics lock path) must restore its state before letting it escape
  loudly. Framework's `AppDomain.Unload` aborting a thread mid-lock orphans an interned lock process-wide (r5).
- **R8 Isolation lost between ALCs.** Two C programs in two ALCs share exit lists, signals, locale and the std FILEs;
  a re-run inherits state. The user accepted this price; a per-ALC option stays a later design question.
- **R9 Framework scope.** Per AppDomain for all libc state while the interned locks and wait buckets are
  process-wide; a C call chain that crosses domains through a thunk sees two errnos. Recommended and measured, but
  it is a documented limit.
- **R10 NativeAOT and trimming** were not tested (named slots and AppDomain data are believed to work there).

---

## 11. Questions for the user

1. **Version mismatch.** When a copy meets a root or sub-block with an unknown major or a different layout check:
   (a) `Environment.FailFast` naming both versions (recommended: fail loudly, and a split silently breaks
   `FILE *`/descriptor exchange and output order); (b) fall back to a private instance with a diagnostic (the
   native two-CRT precedent the research recommended).
2. **Heap allocator.** (a) A 2-pointer header over `Marshal.AllocHGlobal` on every runtime, frozen (recommended:
   `NativeMemory` is absent on Framework and two schemes in one process corrupt the heap); (b) `NativeMemory` on .NET
   6+ and `AllocHGlobal` on Framework, as the 23:09 record words it, chosen identically by every copy at run time.
3. **`stdin`/`stdout`/`stderr`.** (a) Lvalue macros over a function returning the shared slot (recommended); (b)
   variables initialized by each copy's constructor, with msil-ld's process-data binding later for assignment.
4. **getopt state.** (a) Per copy (recommended; no msil-ld feature, a program is always consistent with itself);
   (b) shared through msil-ld's process-data binding.
5. **errno in P-1b**, before managed calls exist. (a) EXTERNAL mode over one process cell in `c.dll` until P-2
   moves it into the C block, documented (no C thread can exist before P-3; recommended); (b) withhold every
   entrypoint that sets errno until P-2 (most of `strto*` and math).
6. **errno numbering.** (a) A dotnet header family with the complete Linux asm-generic numbering (recommended);
   (b) extend upstream's generic family (changes `EOVERFLOW` for baremetal users); (c) WASI's numbering.
7. **Freeze point.** (a) Provisional major 0 from P-2, frozen major 1 at P-3's commit (recommended); (b) frozen at
   P-2's commit.
8. **Semantic choices that freeze.** strtok per process (recommended, LLVM and glibc) or per thread (UCRT); tss and
   pthread keys as LLVM's 32-bit keys with the table in the root (recommended) or 64-bit GCHandle keys.
9. **Key spelling.** `<msil>v1:libc.root` and friends (recommended) or `<msil>v1:dotnet.libc.root` to name the C
   runtime identity.

---

## 12. Upstream llvm-libc defects seen in passing (from source, not run)

- `class File`'s mutex is created non-recursive (`src/__support/File/file.h`, the `File` constructor), while
  POSIX `flockfile` requires a recursive lock; `flockfile` followed by `fputs` would deadlock.
- `uselocale` stores `LC_GLOBAL_LOCALE` (-1) raw (state-homes research).
- `generic-error-number-macros.h` gives `EOVERFLOW` the value 35, which is `EDEADLK` on Linux, and lacks most POSIX
  codes.
- `exit` flushes streams only under `LIBC_COPT_EXIT_FLUSH_STREAMS` and closes none; tmpfiles are not removed
  (`src/stdlib/exit.cpp`).

## 13. Files

- Report: `llvm/docs/MSIL/recon/p3/portable-state.md` (this file).
- Probes: `<scratch>/p0/state/api/apiprobe.cs`, `win.txt`, `wsl.txt`;
  `<scratch>/p0/state/api2/apiprobe2.cs`, `win.txt`, `wsl.txt`.
- Symbol survey: `<scratch>/p0/state/nm-state.txt`, `nm-state-recheck.txt` (identical).
- Synthesis copy: `<scratch>/p0/state/synth-answer.md`.

---

## Verifier corrections

Adversarial check of this report, 2026-11-01. I re-ran `llvm-nm` over the bulk probe, looking in the non-math
objects for `__atomic_*`/`__sync_*` (output in `<scratch>/p0/state/atomic-users.txt`). I re-read the
probe transcripts and re-checked the cited llvm-libc and plan sources at this checkout. Nothing was written outside
this section and the scratch directory.

### Errors

1. **rand/srand cannot ship in P-1b as written (sections 1.7, 6, 8).**
   - Re-measured: `stdlib_rand.cpp.o` needs `__atomic_compare_exchange_8` and `__atomic_load_8`.
     `stdlib_srand.cpp.o` needs `__atomic_store_8`. They are the only non-math objects in the bulk probe that need
     libatomic.
   - `rand_next` is a `cpp::Atomic<unsigned long>` (`libc/src/stdlib/rand_util.cpp`). Every atomic on MSIL is a
     call to libatomic's interface (Status.md, row "Atomics"), and that interface is undefined on the portable
     triple until P-3 (critic NOTE "atomics before P-3").
   - So "the single copy keeps llvm-libc's C globals for rand" gives link errors in P-1b. P-2's "R/6 with
     llvm-libc's CAS loop" has the same gap, and so does `tmpnam_budget` (also a `cpp::Atomic`).
   - Fix: either withhold rand/srand (and tmpnam) until portable atomics exist, or have P-1r pin who supplies
     `__atomic_*_8` on portable before P-3. Say which.
2. **hsearch is not a P-1b C global (sections 6, 8).**
   - `search_hsearch.cpp` and `search_hcreate*.cpp` failed to compile in the probe, so they are not in the
     229-symbol survey. hsearch fails with `thread-local variable ...randomness::state/counter is not supported for
     MSIL yet`.
   - `libc/src/__support/HashTable/randomness.h` uses a raw `thread_local`, not `LIBC_THREAD_LOCAL`, so single-thread
     mode does not remove it. It also calls Linux `getrandom`, and hcreate needs malloc.
   - So hsearch is OS-dependent (P-2 at the earliest) and needs `thread_local` (M31).
   - The per-thread hash seed `randomness::state`/`counter` is a missed state piece. It needs a home: the C block,
     or per copy by design.
3. **The dotnet errno family cannot be copied "as in `include/llvm-libc-macros/linux/error-number-macros.h`".**
   - That file defines only 7 macros: ECANCELED 125, EOWNERDEAD 130, ENOTRECOVERABLE 131, ERFKILL 132, EHWPOISON 133,
     EOPNOTSUPP 95 and ENOTSUP.
   - Codes 1-124 come from the kernel's `<linux/errno.h>`, which both `hdr/errno_macros.h` and
     `include/errno.h.def` include on `__linux__`. The portable triple has no kernel headers, so the new header must
     spell out the whole asm-generic table itself.
   - The proposal stands; the citation and the source of the table are wrong. The facts about the generic family
     are confirmed (37 defines, EOVERFLOW 35, EILSEQ 84).
4. **The directory entry is 20 bytes on msil32, not 16 (section 4.1).** In `{u32 id; u16 major, minor; u32 size;
   u32 check; uintptr_t block}`, `block` sits at offset 16 on msil32, which gives 20 bytes. The 24 bytes stated for
   msil64 are correct.
5. **The `max_align_t` of 16 on msil64 has no mechanism and does not follow from the other choices (5.3, 9).**
   - clang's `__stddef_max_align_t.h` aligns `max_align_t` to `max(alignof(long long), alignof(long double))`. With
     `long double` = binary64, which this report proposes, that is 8 on msil64.
   - A 16 needs an explicit target or header override, plus a matching `__STDCPP_DEFAULT_NEW_ALIGNMENT__` for the
     C++ goal. Alternatively, keep `max_align_t` at 8 while malloc returns 16-aligned blocks, which C allows.
   - "max_align_t must agree" is unsupported. Hand it to the target recon or to the user as a choice.
6. **"unsigned long is 4 bytes on msil32, so the shared cell must be fixed at 8 / xorshift64*" (2.3, 4.2 R/6, 6).**
   - `libc/src/stdlib/rand.cpp` picks xorshift32 over a 4-byte cell when `sizeof(void *) != 8`. Fixing 8 bytes and
     xorshift64* on msil32 means patching upstream rand, which adds to R6's merge surface.
   - The report's own rule (4.1) says the two widths never share a process, so a width-dependent cell is safe. The
     stated motivation is moot.
7. **The P-1b errno rationale is false as stated (Q5, findings item 6).**
   - "No C thread can exist before P-3" holds only for threads that C starts. Any C# host can call `c.dll`'s
     `strtol` from several .NET threads (for example `Parallel.For`), and then one process cell races. Q5(a) may
     still be right, but it must document this race instead of ruling it out.
   - For P-1b, SHARED mode (`libc/src/errno/libc_errno.cpp`, `shared_errno`) gives the identical program ABI with
     no new code: `__llvm_libc_errno` is exported and `errno` is `(*__llvm_libc_errno())`, confirmed in
     `libc/include/errno.h.def`. The probe itself was built with `LIBC_ERRNO_MODE_SHARED`
     (`reorder-eval/libc-probe/one.sh`).
   - "EXTERNAL, always" is justified only from P-2 on. Either mode answers the critic's note, as long as the plan
     names one.
8. **The probe size is mislabelled.** The bulk probe has 726 objects. 1541 counts the objects plus their 815 logs.
   The 229-symbol result itself re-checks.
9. **The mbstate table (2.2) does not match the nm output.**
   - nm shows `internal_mbstate` in 9 functions. The ".0/.1/.2" of `wcstombs` is one `mbstate` that the optimizer
     split into three symbols, not three cells. `wctomb`'s static was optimized away.
   - The source has 10 static sites (`wctomb` included), so "ten cells" in R/6 is right by source but not by the nm
     table it cites.
10. **jmp_buf, "as on Linux triples" (section 9, J).**
    - Plan 5.15.4 gives the saved mask 128 bytes at offset 72 on Linux triples. The portable 8-byte `sigset_t` there
      is a new choice, not the Linux one.
    - A `sigsetjmp(buf, 1)` site references `sigprocmask` (5.15.4 step 2). That is OS-dependent and absent on
      portable until P-3, so P-1r must say what such a site does there before then. A link error is fine if it is
      stated.
11. **The shape of `sigset_t`.**
    - llvm-libc's `sigset_t` is `struct { unsigned long __signals[__NSIGSET_WORDS]; }`
      (`libc/include/llvm-libc-types/sigset_t.h`), not a `uint64_t`.
    - A dotnet branch should set `__NSIGSET_WORDS` to `64 / (8 * sizeof(unsigned long))`: 2 on msil32, 1 on msil64.
      That keeps 8 bytes, but with 4-byte alignment on msil32. Otherwise the report must say it changes the type,
      which adds merge surface.

### Gaps

- **R1 under-states the MC-1 scope.** Besides object-typed calls, the design needs:
  - delegates created from C functions: `EventHandler` for `ProcessExit`, `Action<AssemblyLoadContext>` for
    `Unloading`, `ConsoleCancelEventHandler` and `Action<PosixSignalContext>`;
  - a generic method instantiation (`GC.AllocateArray<long>`);
  - a finalizable holder type defined in the copy (the Framework block; without it every block leaks);
  - try/finally around `Monitor.Enter`/`Exit` (C has no `finally`; R7 depends on it).
  The MC-1 recon should list these as required, or name a fallback for each.
- **Shared descriptor offset vs FileStream position.** Each copy wraps the shared handle in its own
  `SafeFileHandle(raw,false)`/`FileStream`. Since .NET 6, FileStream keeps its own file position and uses
  positional I/O, and Framework's FileStream caches `_pos`. Two copies' streams therefore do not see one offset. The
  shared description's `offset` must drive every operation, through `RandomAccess` (.NET 6+ only) or seek-then-read
  with buffering off on Framework. This was neither measured nor designed.
- **Missed state sites.** The per-call buffers `line_buffer` in `libc/src/pwd/pwd_utils.cpp` and
  `libc/src/grp/grp_utils.cpp`. The `__cxa_thread_atexit_impl` entrypoint (`libc/src/stdlib/`) is not named in the
  thread-exit row.
- **Framework leak.** Each AppDomain creates its own never-freed root and sub-blocks, so domain recycling (ASP.NET
  classic) leaks one set per domain. This is not in the risks.
- **Atomics before P-3.** Who supplies atomics on portable between P-1b and P-3 is a missing phasing item (error 1).
  The critic flagged it and the report did not carry it.
- **Directory ids run out.** The "never grow, take a new id" rule leaves only 52 directory ids forever (13-64).
  This is not stated as a risk.

### Confirmed

- **API probe transcripts** (`api/win.txt`, `api/wsl.txt`, `api2/*.txt`):
  - runtimes covered: Framework 4.8.9363 x64 and x86 (ptr=4), .NET 8.0.33 and 10.0.14 on Windows, and 8.0.31 and
    10.0.13 under WSL;
  - absent on Framework only, as stated: NativeMemory, GC.AllocateArray, TickCount64, ProcessId, PtrToStringUTF8,
    AssemblyLoadContext and PosixSignalRegistration;
  - nothing `[Obsolete]`;
  - the root and the per-thread block were stable, and interned identity was True, on every runtime.
- **nm survey:** 229 data symbols, and the two files are identical.
- **errno headers:** `hdr/errno_macros.h` falls back to the generic family off Linux and Apple, and that family has
  EOVERFLOW 35.
- **errno modes:** the modes and default in `libc/src/__support/libc_errno.h` and the EXTERNAL branch are as
  stated. `LIBC_THREAD_LOCAL` is empty in single-thread mode, and the baremetal config already uses EXTERNAL and
  SINGLE.
- **ABI headers:** `__jmp_buf.h`, `once_flag.h`, `fcntl-macros.h` and `signal-macros.h` have no portable branch.
  The `time_t`, `timespec` `{time_t, int64}`, 8-byte `mbstate_t`, `locale_t` (6 pointers), `clock_t` (`long`) and
  `fpos_t` `{off_t, mbstate_t}` facts are right.
- **stdio:**
  - `class File` has a non-recursive mutex and static `list_all`/`list_lock`, and `write` takes `FileLock`.
    `flockfile` exists, so the upstream deadlock claim is plausible from source.
  - `tmpnam.cpp` has `TMPBUF`, `tmpnam_budget` and the "/tmp/" prefix. `L_tmpnam` is 20, `TMP_MAX` 1000000 and
    `BUFSIZ` 1024.
- **Threads:** `callonce.h` `#error`s off Linux. `TSSKeyMgr` has 1024 keys, plus the `LIBC_THREAD_LOCAL`
  `tss_values` and `atexit_callback_mgr`. There is no `Thread self`.
- **getopt and public variables:** the getopt statics `optpos` and `ctx` exist, and the `LLVM_LIBC_VARIABLE` list
  is complete.
- **Plan citations:** the 5.15.4 `'MSJ1'` table (offsets 0-15, 64, 72), the wording of T9-T11, r5's
  `<msil>v1:atomic.lock:<i>` with 1024 slots, Status.md's thread_local row and the user's NativeMemory decision (2026-11-01).
- **Research claims:** heap-scheme mixing giving 0xC0000374, and `Environment.Exit` hanging inside ProcessExit,
  match `synth-answer.md` and `state-homes-research.md`.
- **User decisions:** no conflict found with the binding decisions. Framework's per-AppDomain scope is an inherent
  limit of the decided AppDomain-data root, and the report documents it as R9.
