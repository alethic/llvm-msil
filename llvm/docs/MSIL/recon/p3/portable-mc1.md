# P-0 recon `mc1`: the design of MC-1 (minimum managed calls from C and C++)

Read-only recon at HEAD `c3460446ac09` (M25). Scratch: `<scratch>/p0/mc1/` (earlier attempt:
`survey.*`, `table*.txt`, `safe*/`, `neg*.txt`, `proto/`; this attempt: `r2/`). Nothing in the repository was written
except this file. Binding constraints: the user decisions listed in the task (option A order, MC-1 after M27, no
interim OS layer before MC-1, Framework 4.8 and modern .NET alike, no runtime support assembly, no exception type
shared between assemblies, fail loudly before prerequisites).

## 0. Summary

1. **The link side of MC-1 already exists.** msil-ld's M21 binder binds *any* `[msil.bcl]` (or other non-placeholder)
   TypeRef, MemberRef, TypeSpec and field reference found in an object, by name and full signature, public members
   only, with overload diagnostics (`lld/MSIL/Synthetic.cpp` `OutputModule::mapBCLToken` / `mapTypeToken` ->
   `BCLBinder::bind`). The critic's "binds only BCLMembers.def rows" is true only for links **without** references
   (the built-in `[mscorlib]` table, `bcl-builtin.test`). Measured (section 3.3): a hand-written CIL object with an
   MC-1 stub's shapes (arbitrary members with `class`/`valuetype` signatures, `castclass`, a `valuetype GCHandle` local,
   enum arguments as `int32`, a filter clause with `isinst`) links with `--reference` to the Framework 4.8 directory
   (AssemblyRefs `mscorlib` + `System`) or the .NET 8 pack (`System.Runtime` + `System.Console`), verifies with
   ILVerify, and runs on Framework 4.8.1, .NET 8, .NET 10, WSL .NET 8 and WSL .NET 10. A wrong return type gives
   `undefined .NET member 'int32 System.Diagnostics.Stopwatch::GetTimestamp()' ... overloads present: int64
   GetTimestamp()`. Without references every non-table member is `... is not in msil-ld's built-in mscorlib table;
   pass --reference=<reference assemblies>`.
2. **What is missing is the compiler side**: a clang attribute, Sema checks, IR attributes, and backend stubs whose IL
   the AsmPrinter writes (new token kinds for arbitrary members and types; the MC layer today knows BCL members only by
   `cli::BCLMemberId`, `MSILMemberRefExpr::Kind::BCLMember`, and its object-writer type parser has no `class` /
   `valuetype`).
3. **Exceptions: MC-1 needs no new clause kind.** A per-stub *filter* clause whose filter code tests `isinst` per listed
   exception (prototype, section 3.2) maps exceptions to error codes; unlisted exceptions keep propagating from the
   first pass and crash loudly. It needs only M27.a's clause plumbing (`MSILTargetStreamer::emitExceptionClause`,
   object-writer clause storage); msil-ld already copies object clauses (measured). So the critic's "new clause kinds
   in M27-owned files" is not needed, and MC-1 stays after M27 (the user's order) because P-2's `fopen` needs the
   mapping and the clause plumbing is M27.a's.
4. **Strings** go through `Encoding.UTF8.GetString(uint8*, int32)` (public on Framework 4.8 and .NET 10, measured), never
   `Marshal.PtrToStringAnsi` (ANSI code page on Windows) or `PtrToStringUTF8` (absent on Framework 4.8: reflection
   `False`, and the `ldtoken` probe fails on netfx only).
5. **Two defects/hazards found:** (a) `msil/utils/check-mscorlib-safe.py` proves rows with `ldtoken`, which does no
   accessibility check: `Environment::get_TickCount64()` is *internal* in Framework 4.8.1's mscorlib, passes the probe on
   netfx, and a real `call` throws `MethodAccessException` (section 3.5). No current table row is affected (all public,
   `Attribute::.ctor()` is the known family exception), but any MC-1 or P-2 mscorlib-safe row could be. (b) The
   Framework 4.8 JIT silently accepts an `int32` where a struct (`DateTime`) parameter is expected (returns `True`),
   while .NET 8 throws `InvalidProgramException` (section 3.6): enum-as-integer arguments must be checked at link time,
   not left to the runtime.
6. **Effort**: about one large milestone (roughly 22-32 h plus a ~5 h post-commit run), 9-11 WPs, about 2,500-3,000
   lines of code plus 1,500-2,000 of tests. Rebuild: a new `Attr.td` record is the broad class (about 520 objects,
   3,500-4,000 CPU-s, about 8 minutes at 8 jobs; plan 5.6.10 and decision brief 3.12), `DiagnosticSemaKinds.td` about
   138 dependents, `BCLMembers.def` 21 objects. It needs a 5.13.5 exemption row.
7. **Host triples too: yes** (the feature is triple-generic; only the wide-string width differs).

## 1. What exists today (findings with citations)

### 1.1 The M13 import attributes (the precedent for a clang attribute on MSIL)

- `clang/include/clang/Basic/Attr.td`: `TargetWebAssemblyOrMSIL` (a `TargetSpec` with `T.isWasm() ||
  T.isOSBinFormatMSIL()`), and `WebAssemblyImportModule` / `WebAssemblyImportName` retargeted to it (`Subjects
  Function, GlobalVar`). M13 measured that retargeting changed only 2 of 22 generated `.inc` files (4 objects); a *new
  record* rebuilds about 519-526 objects (plan 5.6.10 table row "for contrast: a new attribute record", and decision
  brief 3.12 item 1, CR J1: 526 objects, about 4,000 CPU-s).
- Sema: `clang/lib/Sema/SemaDeclAttr.cpp` `ProcessDeclAttribute` case `AT_WebAssemblyImportModule` ->
  `SemaWasm::handleWebAssemblyImportModuleAttr`; redeclaration merge in `clang/lib/Sema/SemaDecl.cpp`
  `mergeDeclAttribute` -> `SemaWasm::mergeImportModuleAttr`.
- CodeGen: `clang/lib/CodeGen/CodeGenModule.cpp` `addMSILImportAttributes` (string function attributes
  `"msil-import-module"` / `"msil-import-name"` from the most recent declaration), `updateMSILImportAttributes`
  (late redeclaration), `dropMSILImportAttributes` (a definition is never an import; warns
  `warn_import_on_definition`).
- Backend: `llvm/lib/Target/MSIL/MSILMCInstLower.cpp` `hasImportAttribute` and `MSILMCInstLower::createMethodRef`
  (an import-attributed declaration gets a per-call-signature import stub, rule R7, a MethodDef of the object);
  `MCTargetDesc/MSILObjectWriter.h` `StubRecord`; `MSILAsmPrinter.cpp` prints the stubs in text.
- lld: `lld/MSIL/SymbolTable.cpp` uses `getImportModule` to keep explicit imports native and unchecked
  (`bindManagedSymbol`, `rejectObjectDefinition`); `lld/MSIL/LTO.cpp` reads the IR attributes for the import
  annotation of the merged module.

### 1.2 How the backend calls BCL methods today

- `llvm/include/llvm/CLI/BCLMembers.def` (hot list, append-only, B1-B7): 20 `MscorlibSafe` entries in the built-in
  table today (`msil-ld --print-bcl-table`, saved as `r2/pub/bcl-table.txt`).
- `llvm/include/llvm/CLI/BCLMembers.h` `printBCLMemberRef`: `[mscorlib]` scope in whole-program text, `[msil.bcl]` in
  object form (`MSILLink::BCLAssembly`).
- `MCTargetDesc/MSILMCExpr.h` `MSILMemberRefExpr::Kind::BCLMember` carries only a `cli::BCLMemberId`.
- `MCTargetDesc/MSILObjectWriter.cpp` `parseBCLType` accepts primitives, `*` and `[]` only (no `class`/`valuetype`),
  and the `Resolve` lambda's `BCLMember` case encodes `Info.Return`/`Info.Params`; `MethodRecord::Locals` is a
  `std::vector<cli::TypeSig>` (so typed locals are representable, but nothing produces a TypeRef-typed local today).

### 1.3 How msil-ld binds (M21) — already open-ended

- `lld/MSIL/TokenMap.h` `TokenInfo::Kind::BCL`: "a MemberRef on another TypeRef, a TypeRef, a TypeSpec or a MethodSpec:
  what the BCL binder makes of it"; `scope` "the binder binds any name but the placeholders' by the type's name".
- `lld/MSIL/Synthetic.cpp` `OutputModule::mapTypeToken`: every TypeRef not scoped to `msil.extern`/`msil.link` is a
  `BCLRequest::Type` bound by name; `OutputModule::mapBCLToken`: every MemberRef on such a TypeRef becomes a
  `BCLRequest::Method` or `Field` with the object's signature mapped to output tokens (`mapTypeRefs`); MethodSpecs are
  "not supported yet" (no generics in MC-1 either). `mapStandAloneSig` re-encodes local and calli signatures with
  output TypeRefs.
- `lld/MSIL/BCLBinder.cpp` header comment and `lld/test/MSIL/bcl-members.test`: a member binds by name, calling
  convention, generic arity, parameter count, return and parameter types; only public members count; a base type's
  method is found and emitted on the requested type; the error lists `overloads present`. `bcl-builtin.test`: without
  references, the built-in table only; anything else is `'<x>' is not in msil-ld's built-in mscorlib table; pass
  --reference=<reference assemblies>`.
- `lld/MSIL/Synthetic.cpp` already has a canonical-listing parser with `class`/`valuetype`
  (`parseBCLType`/`parseBCLMethodSig`, used for msil-ld's own requests).

So: an object that names arbitrary members binds today when `--reference` is given. The resolution scope is whatever
assembly defines the type in the reference set: `mscorlib`/`System` with the Framework 4.8 directory, `System.Runtime`,
`System.Console`, ... with the .NET 8 pack (measured, 3.3). That is Design.md 7.1's rule, not an MC-1 cost.

### 1.4 What M27 provides that MC-1 reuses

Plan M27.a (`M27/eh`, root) Provides `MSILTargetStreamer::emitExceptionClause(Kind, TryBegin, TryEnd, HandlerBegin,
HandlerEnd, FilterBegin)` in all three streamers, object-writer clause storage (`cli::MethodBodyInfo::Clauses`),
AsmPrinter-written bodies (the throw helper "IR body `unreachable`; none of its machine instructions is emitted",
AsmPrinter-written instructions taking the object token-order path R6), and `MSILStackCheck` raising `.maxstack`
for such bodies. `MSIL::WholeBodyKind` / `getWholeBodyKind` is the sjlj wrapper kind; MC-1 does not need a new
`WholeBodyKind`: its stub has its own small clause. msil-ld already copies object clauses (M20; measured in 3.3).

## 2. Proposed design

### 2.1 Attribute grammar (C and C++)

Two clang attributes, spelled `__attribute__((...))` and `[[clang::...]]`, valid only on MSIL object-format targets
(a new `TargetSpec` `TargetMSIL`: `T.isOSBinFormatMSIL()`; this covers the host MSIL triples and the portable triples,
which default to the MSIL object format after P-1a):

```c
__attribute__((msil_method(TYPE, MEMBER, SIGNATURE [, ASSEMBLY])))
__attribute__((msil_catch(EXCEPTION_TYPE, CODE)))      // repeatable, ordered
```

- `TYPE`: `"Ns.Name"`, a nested type as `"Ns.Outer/Inner"`, or an SZARRAY type such as `"uint8[]"` (only with
  `MEMBER` `".ctor"`, which lowers to `newarr`).
- `MEMBER`: a method name; `".ctor"` (`newobj`, or `newarr` for an array type); a property is its accessor
  (`"get_Length"`); a field name when `SIGNATURE` starts with `field` (read-only access: `ldsfld`/`ldfld`; needed for
  `Stopwatch.Frequency` and `BitConverter.IsLittleEndian`, which are public static fields, measured).
- `SIGNATURE`: the member's own CIL signature in canonical listing v1 text (plan 5.7.10), exactly as the definition
  declares it: `"[instance ]RET(P1, P2, ...)"` or `"[instance ]field T"`. Types: the listing primitives (`bool`, `char`,
  `int8`..`uint64`, `float32`, `float64`, `native int`, `native uint`, `string`, `object`), `class Ns.T`,
  `valuetype Ns.T`, `T*`, `T[]`. Not in MC-1: generics, byrefs `&`, custom modifiers, `vararg`, function pointers.
- `ASSEMBLY` (optional): only the scope printed in standalone `-S` text (default `mscorlib`, like BCL members in
  whole-program text). Objects always use `[msil.bcl]`, and msil-ld binds by name against the references.
- `msil_catch(EXCEPTION_TYPE, CODE)`: `"Ns.Name"` of an exception type and a positive integer constant; in the order
  written, the first type the exception is an instance of (`isinst`) wins.

Example (P-2's open):

```c
typedef struct __dn_object *dn_handle;            /* an opaque object handle (2.5) */
__attribute__((msil_method("System.IO.FileStream", ".ctor",
  "instance void(string, valuetype System.IO.FileMode, valuetype System.IO.FileAccess, "
  "valuetype System.IO.FileShare, int32)"),
  msil_catch("System.IO.FileNotFoundException", ENOENT),
  msil_catch("System.IO.DirectoryNotFoundException", ENOENT),
  msil_catch("System.UnauthorizedAccessException", EACCES),
  msil_catch("System.IO.PathTooLongException", ENAMETOOLONG),
  msil_catch("System.IO.IOException", EIO)))
dn_handle __dn_fs_open(const char *path, int mode, int access, int share, int bufsize, int *err);
```

Rejected alternatives: (a) reusing `import_module`/`import_name` with a reserved prefix (no `Attr.td` rebuild, but it
overloads the P/Invoke attributes whose bindings are "unchecked" by 5.10.7, cannot express the catch list, and is the
kind of workaround the standing rules forbid); (b) one attribute with a catch-list string (harder Sema diagnostics,
no per-entry source locations); (c) a linker map file (exports-multiasm 8.2's other idea: no C-side type checking).

### 2.2 Sema checks (new file, one hook in `SemaDeclAttr.cpp`)

On the declaration, in a new `clang/lib/Sema/SemaMSILManagedCall.cpp` with a private header called from the
`ProcessDeclAttribute` cases (no `Sema.h` change):

1. Subject: a function *declaration* without a body; not variadic, not K&R (needs a prototype); in C++ a
   namespace-scope function only (no member functions, templates, `constexpr`/`consteval`); not together with
   `import_module`/`import_name`; `msil_catch` only with `msil_method`. A definition carrying it is an error.
2. Redeclarations must repeat the same arguments or inherit them (`mergeDeclAttribute` hook in `SemaDecl.cpp`, as the
   wasm import attributes do).
3. The signature text parses (the header-only parser of 2.4, so clang needs no new LLVM library dependency).
4. Parameter correspondence: C parameters = [`this` handle if `instance`, except for `.ctor`] + the CIL parameters +
   [one trailing `int *` when any `msil_catch` is present]. Return correspondence likewise.
5. Per pair (C type, CIL type), exactly one conversion must apply (table below); Sema records it in the IR (2.3).

| CIL type (param/return) | C type accepted | Conversion (stub IL) |
|---|---|---|
| `bool` | `_Bool`/`bool`, any integer <= 32 bits | in: `ldc.i4.0; cgt.un`; out: as is |
| `int8`..`int32`, `uint8`..`uint32`, `char` | integer of the same width (`char16_t` for `char`) | as is |
| `int64`, `uint64` | 64-bit integer | as is |
| `native int`, `native uint` | pointer, `intptr_t`, `uintptr_t`, `size_t`, `ptrdiff_t` | as is |
| `float32`, `float64` | `float`, `double` | as is |
| `T*` | any object pointer | as is (unmanaged pointer) |
| `valuetype E` where E is an enum | integer of E's underlying width | as is; **msil-ld checks E is an enum of that width** (2.6) |
| `string` | `const char *` / `char8_t *` (in) | UTF-8: strlen loop, `Encoding::get_UTF8()`, `callvirt GetString(uint8*, int32)`; null -> null |
| `string` | `const char16_t *` (and `wchar_t *` where 16-bit) (in) | `newobj String::.ctor(char*)`; null -> null |
| `string` | `const char32_t *` (and `wchar_t *` where 32-bit, i.e. both portable triples) (in) | count to 0, `Encoding::get_UTF32()`, `GetString(uint8*, int32)` with 4*n bytes |
| `string`, `object`, `class T`, `T[]` | handle type (2.5) | in: handle -> object, `castclass T`; out: new handle (0 for null) |
| `valuetype V` (not an enum, not GCHandle) | handle type | in: handle -> object, `unbox.any V`; out: `box V`, new handle |
| `valuetype System.Runtime.InteropServices.GCHandle` | handle type | the handle *is* the GCHandle's IntPtr (`FromIntPtr`/`ToIntPtr`, never boxed), so `GCHandle::Free` frees a handle |
| `this` of `class T` / interface | handle type | handle -> object, `castclass T`, `callvirt` |
| `this` of `valuetype V` | handle type | handle -> object, `unbox V`, `call` (a managed pointer to the boxed copy) |
| `void` return | `void` | - |

Handle types: `void *`, `intptr_t`/`uintptr_t`, or a pointer to an incomplete struct (the recommended opaque typedef).
Any other combination is an error naming both types. String results are always handles (C converts them with
`Encoding::GetByteCount(string)`, `Encoding::GetBytes(string)` -> `uint8[]` handle and `Marshal::Copy`); an out-buffer
convenience form is left for later.

### 2.3 IR representation: function attributes on the declaration

String attributes, compiler-internal (5.15.1 rule 7), on the IR *declaration*, added by a `CodeGenModule.cpp` hook
beside `addMSILImportAttributes`/`updateMSILImportAttributes`:

```
"msil-managed-type"="System.IO.FileStream"
"msil-managed-member"=".ctor"
"msil-managed-sig"="instance void(string, valuetype System.IO.FileMode, ...)"
"msil-managed-asm"="mscorlib"                (only when given)
"msil-managed-conv"="utf8,val,val,val,val,err;handle"   (per C parameter; return after ';')
"msil-managed-catch"="System.IO.FileNotFoundException=2;...;System.IO.IOException=5"
```

Why attributes and not metadata: they survive bitcode and LTO on declarations exactly as the import attributes do,
and IR loses the C distinctions (`const char *` vs `void *`, `char16_t *`, `_Bool`), so Sema's conversion decision has
to be written down; the backend executes it and decides nothing. Calls stay ordinary calls to the declaration; the
optimizer sees an external declaration (no `unreachable` body is visible to IPO).

### 2.4 Lowering (L2: one new file, hooks only)

New files:

- `llvm/include/llvm/CLI/ManagedSignature.h`: a header-only parser of the signature grammar into a small value type
  (`HasThis`, `IsField`, return, params; each type a kind, an element type and a type name), shared by clang Sema, the
  target and lld (header-only like `BCLMembers.h`, so clang links nothing new). Unit tests in
  `llvm/unittests/CLI/ManagedSignatureTest.cpp`.
- `llvm/lib/Target/MSIL/MSILManagedCalls.{h,cpp}`:
  1. `bool MSIL::createManagedCallStubs(Module &M)` (P0 hook in `MSILPrepareIRLegacy::doInitialization`, after the
     existing P0 items; codegen only, so after the optimizer): for each declaration with `"msil-managed-type"`, one
     internal `noinline` function `@__msil_mc.<n>` with the declaration's IR type, body `unreachable`, attribute
     `"msil-managed-stub"`; every direct call is redirected to it; any other use (address taken, a cast callee) is the
     error `the address of a managed-call function cannot be taken for MSIL yet` (valid IR left behind). No pass slot,
     no `pipeline.ll` change, no record change. (Alternative: a P1a slot pass `MSILManagedCallsLegacy`, which needs a
     5.13.6 amendment and `pipeline.ll` re-pins; not recommended.)
  2. The body writer, called from the AsmPrinter's method-body hook (the hook point 5.13.2 already lists for M27's
     clauses): argument conversions, the call (`call` static, `callvirt` instance of a reference type, `call` after
     `unbox` for a value type, `newobj`, `newarr`, `ldsfld`/`ldfld`), the result conversion, `ret`; with
     `msil-managed-catch`, a `.try` around the call plus a filter (`isinst` per entry in order, `endfilter` 1 with the
     code in a local, else 0) and a handler that stores the code through the trailing `int *` and returns 0/null,
     emitted through `emitExceptionClause`. `.maxstack` and the stub's primitive locals come from the same file
     (MSILStackCheck/MFI hooks, as M27 does for the throw helper).
  3. Handle dereference without typed locals: `GCHandle::FromIntPtr(h)`, `box GCHandle`, `unbox GCHandle`,
     `call instance object GCHandle::get_Target()` (measured: verifies with ILVerify and runs on Framework and .NET 8,
     section 3.4). This keeps MC-1 off the object writer's typed-local path; the cost is one small allocation per
     dereference. A later step may switch to a `valuetype GCHandle` local (measured to link and run in 3.3).
- New MC token kinds in `MCTargetDesc/MSILMCExpr.{h,cpp}`: `ManagedMember` (type name, member, parsed signature,
  `-S` scope) and `ManagedType` (a TypeRef or SZARRAY TypeSpec for `castclass`, `isinst`, `box`, `unbox`,
  `unbox.any`, `newarr`); printed as `[msil.bcl]`/`[<asm>]` text, encoded by `MSILObjectWriter.cpp` with
  `class`/`valuetype` support in its type parser (or the shared header parser).
- `MSILMCInstLower.cpp`: nothing beyond today's handling, because calls target the internal stub, a MethodDef of the
  module (R1).
- New `BCLMembers.def` block (one owner, B2 heading; mscorlib-safe, measured): `GCHandle` type, `Alloc(object)`,
  `ToIntPtr(valuetype GCHandle)`, `FromIntPtr(native int)`, `get_Target()`, `Free()`; `Encoding` type,
  `get_UTF8()`, `get_UTF32()`, `GetString(uint8*, int32)`; `String::.ctor(char*)`. That block needs a heading rule for a
  non-`M<n>` milestone (critic note) and changes `--print-bcl-table` (L7).

### 2.5 Object handles and their lifetime contract

- A handle is the `IntPtr` of a `GCHandleType.Normal` GCHandle (`GCHandle.ToIntPtr(GCHandle.Alloc(o))`); 0 is null.
- Every reference-typed or boxed result is a **new strong handle owned by the caller**, freed exactly once by a
  declaration of `GCHandle::Free` (`msil_method("System.Runtime.InteropServices.GCHandle", "Free", "instance void()")`
  on `void f(dn_handle)`), the GCHandle rule of 2.2 making the C value the handle itself. Arguments borrow: the stub
  never frees or keeps them.
- Handles are process-wide (any thread, any assembly in the process, `-static` copies included, consistent with "code
  private, state shared"); on Framework they do not cross AppDomains, matching the per-AppDomain C state there.
- Misuse: a handle used after `Free` gives a null target on Framework and .NET 8 (measured, 3.4), so the stub's
  `castclass`/`callvirt` usually fails with `NullReferenceException`; a handle of another type fails `castclass` with
  `InvalidCastException` (measured on all runtimes in the earlier prototype, `proto/run-windows.txt`,
  `proto/run-wsl.txt`); a recycled slot holding an object of the same type is not detectable. Documented as undefined.
- Leaks at exit are harmless (the process ends).

### 2.6 msil-ld: resolution and signature checks

- Members bind through the existing binder (no change): with `--reference`, any public member; without, only the
  built-in table, else the existing error. The signature check is the binder's full-signature match (measured
  diagnostics in 3.3).
- New check (new file `lld/MSIL/ManagedCalls.{h,cpp}`, one call from `Writer.cpp`): for each object method named
  `<msil>`-reserved `__msil_mc.<n>` (5.14.2 name class), pair its own MethodDef parameters with its member's
  parameters by the fixed stub shape and require every (integer, `valuetype E`) pair to have E an enum whose
  underlying type matches the width (the binder's TypeDef gives `value__`). Reason: Framework's JIT silently accepts a
  wrong scalar for a struct parameter (3.6). Optional in the same file: each `msil_catch` type derives from
  `System.Exception`.
- LTO: the merged module's managed-call declarations are not undefined symbols: `BitcodeFile` symbols whose IR
  declaration has `"msil-managed-type"` are skipped by resolution (`lld/MSIL/InputFiles.cpp`, `SymbolTable.cpp`, and
  `LTO.cpp`'s import annotation leaves them alone), and the LTO code generation creates the stubs at P0 as llc does.
- Standalone `-S` text: scope `[<asm>]`, unchecked (as 5.10.13's frozen import table is); documented.

### 2.7 Exceptions: compare and recommend

| | (A) typed mapping in MC-1 | (B) every exception escapes |
|---|---|---|
| Mechanism | per-stub filter clause, `isinst` per `msil_catch` entry, code into the trailing `int *` | no clause; unhandled exception, process crash with stack trace (5.15.1 rule 1) |
| Needs from M27 | `emitExceptionClause` and object clause storage (M27.a); no new clause kind, no change to `WholeBodyKind` | nothing |
| Proven | prototype on Framework, .NET 8/10, WSL .NET 8/10 (3.2); object-path link probe (3.3) | trivially |
| P-2 consequence | `fopen` of a missing file returns NULL with `ENOENT`; unlisted exceptions still loud | `fopen` of a missing file crashes the process, or P-2 pre-checks `File.Exists` (racy, incomplete: a workaround the rules forbid) |
| Cost | about 150-250 lines in `MSILManagedCalls.cpp`, Sema rules for `msil_catch`, tests | none now; the same work later plus a P-2 rework |

**Recommendation: (A).** MC-1 then runs after M27 (the user's order). Running MC-1 before M27 would mean (B), or
MC-1 building M27.a's clause plumbing itself; P-2 comes after M27 either way, so there is nothing to gain, and I do
not flag the order.

### 2.8 Host triples

Yes. Nothing in the design depends on the portable triple: on Windows host triples `wchar_t *` is the UTF-16 form, on
Linux host triples and both portable triples the UTF-32 form. A host-triple program can call the BCL directly, which
also gives the feature tests that do not need `c.dll`.

## 3. Measurements

Commands run from Git Bash on Windows 11 (Framework 4.8.1, release 533509), `dotnet` 8.0.33 and 10.0.14 on Windows,
WSL Ubuntu with `$HOME/.dotnet-msil` (8.0.31, 10.0.13) used read-only. ilasm:
`C:/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe`.

### 3.1 Where the types live, and the built-in-table probe (earlier attempt, re-checked)

- `<scratch>/p0/mc1/survey.ps1` -> `survey.txt` (System.Reflection.Metadata over reference assemblies):
  every type MC-1 and P-2 need is defined in Framework 4.8's `mscorlib.dll` except `System.Diagnostics.Stopwatch`
  (`System.dll`) and `NativeMemory` (absent); in the .NET 8 pack they are defined in `System.Runtime.dll` (Console in
  `System.Console.dll`, Marshal and NativeMemory in `System.Runtime.InteropServices.dll`, Thread in
  `System.Threading.Thread.dll`, Monitor in `System.Threading.dll`) and forwarded by the `mscorlib` facade, except
  `NativeMemory`, which no `mscorlib` facade forwards.
- `check-mscorlib-safe.py` on `table.txt` (83 entries: GCHandle, Encoding, String ctors, Marshal, Console, Stream,
  FileStream, SafeHandle, Environment, File, Directory, Path, DateTime, DateTimeOffset, exception types): `safe.log`:
  83 of 83 resolve through `[mscorlib]` on netfx, net8, net10 and wsl-net10.
- Negative probes `safe-neg1..6`: `Marshal::PtrToStringUTF8(native int)` fails on netfx only; `NativeMemory` and
  `Stopwatch` (type and `GetTimestamp`) fail through `[mscorlib]` everywhere (on Framework Stopwatch is in `System`);
  `PtrToStringAnsi` resolves everywhere (but is ANSI on Windows); `Environment::get_TickCount64()` "resolves" on netfx
  (see 3.5).

### 3.2 IL prototype of the stub shapes (earlier attempt; re-run on Windows this attempt)

`proto/mc1proto.il` (stubs `dn_*`: UTF-8 in via strlen + `Encoding.UTF8.GetString(uint8*, int32)`, GCHandle
new/get/free, `FileStream` open with a filter mapping five exception types, `Console.OpenStandardOutput`, `newarr`,
`Marshal.Copy`, `Stream.Write`/`Dispose`, `File.Exists`/`Delete`, `[System]Stopwatch` field and method,
`GetEnvironmentVariable` -> handle -> `Encoding.UTF8.GetBytes` -> length). Re-run: `r2/run-windows-r2.txt`:

```
cd <scratch>/p0/mc1/r2 && ilasm -nologo -quiet mc1proto.il -exe -out=mc1proto.exe
./mc1proto.exe ; dotnet exec --runtimeconfig net8.runtimeconfig.json mc1proto.exe [unlisted|cast]
```

Default run: exit 42 on netfx and net8 with `open missing: handle 0, err 2`; WSL net8/net10 the same
(`proto/run-wsl*.txt`). `unlisted` (empty path, `ArgumentException` not listed): unhandled exception with the
stack through `dn_fs_open`, loud on every runtime. `cast` (string handle used as a Stream): `InvalidCastException` in
`dn_write`, loud on every runtime. PEVerify/ILVerify: only the 4 allow-listed classes (`StackByRef`,
`UnmanagedPointer`) of `msil/utils/ilverify-allowed.txt`; the `int32` arguments for `valuetype FileMode` etc. and
`native int` for `uint8*` raise nothing.

### 3.3 The object path through msil-ld (this attempt)

`r2/link/named.il`: object-form text (the `named-obj.il` record of `lld/test/MSIL/Inputs/fakepack`) whose function
`named` uses `[msil.bcl]` Console, GCHandle (Alloc/ToIntPtr/FromIntPtr/get_Target/Free) with a `valuetype GCHandle`
local, Encoding, `castclass Stream`, `Stream::Write`, a `FileStream` ctor with enum arguments as `int32` inside a
filter clause testing `isinst FileNotFoundException`, `String::Concat`, `Stopwatch::GetTimestamp`.

```
llc -filetype=obj main-named.ll -o main-named.o
ilasm -nologo -quiet -dll -pe64 -x64 named.il -out=named.o
msil-ld main-named.o named.o -o noref.exe                      # errors: '<type>' is not in msil-ld's built-in mscorlib table; pass --reference=...
msil-ld main-named.o named.o --reference=<.NETFramework/v4.8> -o fx.exe   # AssemblyRefs mscorlib 4.0.0.0, System 4.0.0.0
msil-ld main-named.o named.o --reference=<Microsoft.NETCore.App.Ref/8.0.33/ref/net8.0> -o n8.dll  # System.Console 8.0.0.0, System.Runtime 8.0.0.0
```

Runs (`r2/link/runs.txt`, `r2/link/wsl-runs.txt`): fx.exe on netfx, net8, net10, WSL net8, WSL net10 and n8.dll on
net8, net10, WSL net8, WSL net10 all print `mc1 link probe: stream write through a handle` and `filter mapped
FileNotFoundException to errno 2`. `ilverify fx.exe -r <Framework 4.8 refs>`: `All Classes and Methods ... Verified.`
Negative (`bad.il`: `int32 GetTimestamp()` and `get_TickCount64`): with Framework refs, `undefined .NET member 'int32
System.Diagnostics.Stopwatch::GetTimestamp()' ... defined in System 4.0.0.0 ... overloads present: int64
GetTimestamp()` and `'int64 System.Environment::get_TickCount64()' ... overloads present: none`; with the .NET 8 pack,
the GetTimestamp error names `System.Runtime 8.0.0.0`.

### 3.4 Handle dereference without typed locals (this attempt)

`r2/acc/bx.il`: `FromIntPtr`, `box GCHandle`, `unbox GCHandle`, `call instance get_Target()`/`Free()`; a boxed
`DateTime` handle as `this` of `DateTime::get_Ticks()` via `unbox DateTime`. netfx and net8: `target through
box/unbox: ok`, `True`; use after `Free` prints an empty line (null target, no exception) on both. ILVerify: all
verified (no allow-listed class needed).

### 3.5 check-mscorlib-safe.py's probe misses accessibility (defect)

- Windows PowerShell 5.1 (CLR 4.0.30319.42000, Framework release 533509): `[Environment].GetProperty('TickCount64')`
  is null; with `NonPublic` it is `Int64 get_TickCount64()` with attributes `Assembly` (internal).
- `r2/acc/acc.il`: `ldtoken method int64 [mscorlib]System.Environment::get_TickCount64()` succeeds on netfx; the next
  `call` throws `System.MethodAccessException: Attempt by method '<Module>.main()' to access method
  'System.Environment.get_TickCount64()' failed.` On net8 both succeed.
- So `check-mscorlib-safe.py` (docstring: "loads the entry's token ('ldtoken ...')") would pass an internal member.
  `r2/pub/pubcheck.ps1` (reflection, exact signature, `IsPublic`) over the current `--print-bcl-table`: every row is
  public except `System.Attribute::.ctor()` (protected; msil-ld's own request, accepted by the 2026-10-26 row). Fix:
  the probe should also `call`/`newobj` a method (or check `IsPublic` by reflection on netfx), or bind against the
  Framework reference assemblies, which contain public API only.

### 3.6 Framework accepts a scalar for a struct parameter (hazard)

`r2/acc/vt.il`: a stub passing `int32` twice to `DateTime::op_Equality(valuetype DateTime, valuetype DateTime)`:
netfx prints `True` and exits 42; net8 throws `InvalidProgramException: Common Language Runtime detected an invalid
program.` Hence the link-time enum check of 2.6.

### 3.7 P-2 member presence (this attempt)

`r2/pub/pubcheck.ps1` over `all.txt` (the 83-entry table plus negatives) and `extra.txt` (30 more), on Framework
(Windows PowerShell) and .NET 10 (pwsh 7, `.NET 10.0.14`): `pub-netfx.txt` 66 public, `PtrToStringUTF8` MISSING,
`get_TickCount64` internal; `pub-core.txt` 68 public. `extra-*.txt`: all present and public on both, except
`BitConverter::get_IsLittleEndian` (it is a public static readonly *field*, not a property) and my mistyped
`op_Explicit`. `fx3.ps1`: `File.Move(string, string, bool)` absent on Framework, present on .NET 10;
`File.Replace(string, string, string)` on both; `Stopwatch.Frequency` is a public static field on both. .NET 8 is
covered by the `ldtoken` probe (resolution) plus .NET 10 reflection (public); a public BCL member does not become
internal between 8 and 10.

## 4. The P-2 OS layer over MC-1

All members below are public on Framework 4.8 and .NET 8/10 (3.1, 3.7). "Scope on 4.8" is the defining assembly with
the Framework references; with the .NET 8 pack it is `System.Runtime` unless noted.

| libc need | Members (canonical text) | Scope on 4.8 | Notes |
|---|---|---|---|
| standard streams | `class System.IO.Stream System.Console::OpenStandardInput()`, `OpenStandardOutput()`, `OpenStandardError()` | mscorlib | `System.Console` on .NET 8 |
| read/write/flush/seek/close | `System.IO.Stream::Read(uint8[], int32, int32)`, `Write(uint8[], int32, int32)`, `Flush()`, `Seek(int64, valuetype System.IO.SeekOrigin)`, `get_Position()`, `get_Length()`, `SetLength(int64)`, `get_CanSeek()`, `Dispose()`, `ReadByte()` | mscorlib | instance, `callvirt` |
| byte buffers | `uint8[]` `.ctor` (`newarr`), `Marshal::Copy(native int, uint8[], int32, int32)`, `Marshal::Copy(uint8[], int32, native int, int32)`, `System.Array::get_Length()` | mscorlib | `System.Runtime.InteropServices` on .NET 8 |
| open | `System.IO.FileStream::.ctor(string, valuetype FileMode, valuetype FileAccess, valuetype FileShare, int32)` (and the `FileOptions` overload), `FileStream::Flush(bool)`, `get_SafeFileHandle()`, `SafeHandle::DangerousGetHandle()` | mscorlib | errno via `msil_catch`: FileNotFound, DirectoryNotFound -> ENOENT; UnauthorizedAccess -> EACCES; PathTooLong -> ENAMETOOLONG; IOException -> EIO; Argument/NotSupported -> EINVAL |
| remove/rename | `System.IO.File::Delete(string)`, `File::Move(string, string)`, `File::Replace(string, string, string)`, `File::Exists(string)`, `Directory::Exists(string)`, `Directory::Delete(string)`, `Directory::CreateDirectory(string)`, `File::GetAttributes(string)` | mscorlib | POSIX `rename` over an existing file: `File.Move(s, s, bool)` is .NET Core 3.0+ only, so 4.8 needs `File.Replace` or Delete+Move (not atomic) |
| tmpfile/tmpnam | `System.IO.Path::GetTempFileName()`, `GetTempPath()`, `GetFullPath(string)` | mscorlib | |
| exit/abort | `System.Environment::Exit(int32)`, `FailFast(string)`, `set_ExitCode(int32)` | mscorlib | |
| getenv/environ | `Environment::GetEnvironmentVariable(string)`, `SetEnvironmentVariable(string, string)`, `GetEnvironmentVariables()` with `IDictionary::GetEnumerator()`, `IEnumerator::MoveNext()`, `IDictionaryEnumerator::get_Key()`/`get_Value()` | mscorlib | strings back to C: `Encoding::GetByteCount(string)`, `GetBytes(string)`, `Marshal::Copy` |
| malloc/free/realloc | `Marshal::AllocHGlobal(native int)`, `FreeHGlobal(native int)`, `ReAllocHGlobal(native int, native int)` | mscorlib | `NativeMemory` is absent on 4.8 and not reachable through `[mscorlib]` on .NET 8 (3.1) |
| CLOCK_REALTIME, time() | `valuetype DateTime DateTime::get_UtcNow()` (boxed handle), `instance int64 DateTime::get_Ticks()`, `ToFileTimeUtc()` | mscorlib | value types by boxed handle (2.2) |
| CLOCK_MONOTONIC, clock() | `int64 System.Diagnostics.Stopwatch::GetTimestamp()`, field `int64 Stopwatch::Frequency` | **System** | not in `[mscorlib]` on 4.8; `Environment::get_TickCount()` (int32 ms) is the mscorlib-only fallback; `get_TickCount64` is internal on 4.8 (3.5) |
| local time | `TimeZoneInfo::get_Local()`, `instance valuetype TimeSpan TimeZoneInfo::GetUtcOffset(valuetype DateTime)`, `TimeSpan::get_Ticks()`, `DateTime::get_Now()` | mscorlib | |
| sleep | `System.Threading.Thread::Sleep(int32)` | mscorlib | `System.Threading.Thread` on .NET 8 |
| getpid | `class Process System.Diagnostics.Process::GetCurrentProcess()`, `instance int32 Process::get_Id()` | **System** | `Environment.ProcessId` absent on 4.8 |
| cwd | `Environment::get_CurrentDirectory()`, `set_CurrentDirectory(string)` | mscorlib | |
| stub-internal (MC-1 rows) | `GCHandle::Alloc(object)`, `ToIntPtr`, `FromIntPtr`, `get_Target`, `Free`; `Encoding::get_UTF8()`, `get_UTF32()`, `GetString(uint8*, int32)`; `String::.ctor(char*)` | mscorlib | mscorlib-safe (3.1, 3.7) |

Binding consequence for `c.dll`: to run on Framework 4.8 and modern .NET alike, `c.dll` must bind against Framework
4.8 reference assemblies (`mscorlib` + `System`, measured to run everywhere in 3.3), because the built-in table covers
only `BCLMembers.def` rows and is `[mscorlib]`-only, and binding against the .NET 8 pack gives `System.Runtime`
references that load only on .NET 8+. Framework reference assemblies exist on Windows build hosts; a Linux build host
has no NuGet-free source for them. This is question U3.

## 5. Ownership (5.13 tables and the milestones' Owned files)

MC-1 is its own milestone between M27 and M28, so nothing runs concurrently; the cost is re-pinning later WP texts and
the merge surface. Files MC-1 would touch that M27-M35 roles own in their milestones:

| File | Owners in M27-M35 | MC-1's change |
|---|---|---|
| `llvm/lib/Target/MSIL/MSILAsmPrinter.{h,cpp}` (hot) | M27/eh, M28/dispatch, M29/exit-target, M31/tls-target, M32/shadow-stack, M33/requests, M34/target | one body-writer call at the method-body hook, `-S` scope (hook, L2) |
| `llvm/lib/Target/MSIL/MSILPrepareIR.cpp` (hot) | M29/exit-target, M31/tls-target, M32/fallbacks, M33/i128 | one P0 call in `MSILPrepareIRLegacy::doInitialization` |
| `llvm/include/llvm/CLI/BCLMembers.def` (hot list) | M27/eh, M28/lists, M29/lists, M30/lists, M31/lists, M32/lists, M33/switches | the MC-1 block (about 11 rows); later `--print-bcl-table` texts re-pinned |
| `MCTargetDesc/MSILObjectWriter.{h,cpp}` | M27/eh, M31/tls-target | `ManagedMember`/`ManagedType` encoding, `class`/`valuetype` parsing |
| `MCTargetDesc/MSILMCExpr.{h,cpp}` | M31/tls-target | the two new kinds |
| `MCTargetDesc/MSILTargetStreamer.*`, `MSILTargetObjectStreamer.cpp` | M27/eh | only if printing the new kinds needs it |
| `MSILStackCheck.cpp`, `MSILMachineFunctionInfo.*` | M27/eh, M32/shadow-stack | `.maxstack`/locals of stubs (hooks) |
| `MSILMCInstLower.*` | M27/eh, M28/dispatch, M30/atomics-target, M31/tls-target, M33/bcl | probably none (calls target a MethodDef) |
| `llvm/lib/Target/MSIL/CMakeLists.txt` | M27/sjlj, M28/dispatch, M30, M31, M32/lists, M33/switches, M34/target | the new file |
| `MSILLowerSjLj.cpp` | M27/sjlj, M32/shadow-stack | optional: do not wrap calls of `"msil-managed-stub"` (they cannot longjmp) |
| `lld/MSIL/Writer.cpp` | M28/lld-thunks, M29/lld-exit, M31/lld-tls | one call into `lld/MSIL/ManagedCalls.cpp` |
| `lld/MSIL/InputFiles.*`, `SymbolTable.*` | M29/lld-exit, M31/lld-tls | LTO skip of managed-call declarations |
| `lld/MSIL/LTO.cpp` | (M26) | import annotation leaves them alone |
| `lld/MSIL/CMakeLists.txt` | M34/lld | the new file |
| `msil/utils/check-mscorlib-safe.py` | (M21's tool; M27 and later gates run it) | accessibility fix (or a defect row before M27) |
| `msil/utils/upstream-hooks.txt` | each milestone with upstream edits | MC-1's blocks (L6) |

Not touched: `MSILTrailerTextSink.*`, `Trailer.{h,cpp}`, `TrailerDefs.def`, `MSILLink.h`/`LinkInfo.*` (no record
minor), `MSILTargetMachine.cpp`/`MSIL.h` (no pass), `pipeline.ll`, `MSILEHClauses.*` (no new `WholeBodyKind`).

Upstream files (need a **5.13.5 exemption row**: "No Phase 3 milestone edits `Attr.td` ..."):
`clang/include/clang/Basic/Attr.td` (two records and `TargetMSIL`), `AttrDocs.td`, `DiagnosticSemaKinds.td` (about
10 errors; no new group), `clang/lib/Sema/SemaDeclAttr.cpp` (two cases), `clang/lib/Sema/SemaDecl.cpp`
(`mergeDeclAttribute`), `clang/lib/Sema/CMakeLists.txt` (the new Sema file), `clang/lib/CodeGen/CodeGenModule.cpp`
(beside `addMSILImportAttributes`), with manifest checks such as `lit clang/test/Sema/attr-wasm.c` (the shared
TargetSpec neighbourhood) and new fork-only tests `clang/test/Sema/attr-msil-method.c`, `clang/test/SemaCXX/
attr-msil-method.cpp`, `clang/test/CodeGen/MSIL/msil-method.c`. 5.13.5 already says `msil_fixed_arity` "moves to the
managed-calls work"; MC-1 does not need it (portable fixed-arity names belong in M26's per-triple list via P-1r).

## 6. Effort and work packages

About one large milestone: 22-32 h of WPs plus the ~5 h post-commit run (the plan's milestones run 10-24 h; this one
has an upstream clang part, a target part and an lld part). Proposed WPs (5.13.4 style):

1. `MC-1/lists` (root): `ManagedSignature.h`, the `BCLMembers.def` block, the IR attribute names in a header (~400).
2. `MC-1/clang`: Attr.td, AttrDocs.td, DiagnosticSemaKinds.td, SemaDeclAttr.cpp, SemaDecl.cpp, new
   `SemaMSILManagedCall.cpp`, CodeGenModule.cpp (~700).
3. `MC-1/target`: `MSILManagedCalls.{h,cpp}` and the hooks above (~1,100).
4. `MC-1/lld`: `lld/MSIL/ManagedCalls.{h,cpp}`, LTO skip, Writer hook (~350).
5. `MC-1/harness`: `check-mscorlib-safe.py` accessibility fix with self-test; lit substitutions for reference sets.
6-9. Tests: clang Sema/CodeGen (C and C++), CLI unit test of the parser, llc (`-S` and objects through
   `llvm-cil-dump`), lld (binding, enum check, LTO), execution on Framework, .NET 8/10 and WSL (host triples; a
   portable msil64 smoke test that needs no `c.dll`).
10. Docs: Status.md, GettingStarted.md; Design.md text with the user's approval.

Rebuild: `Attr.td` new records about 520 objects (about 8 minutes at 8 jobs), `DiagnosticSemaKinds.td` about 138
clang dependents (M26 measured 36 job-minutes for its share), `BCLMembers.def` 21, the target and lld files; one
private build per WP as usual. After MC-1: the re-pins of M28-M35 texts (BCL table lines, hot-file hook context),
which the recommendation budgets at 2-4 h.

## 7. Risks

1. `Attr.td` and `SemaDeclAttr.cpp` become permanent merge surfaces; the new record costs the broad clang rebuild.
2. The grammar and handle contract freeze early, because the libc is built on them (the recommendation's risk).
3. Framework's JIT accepts wrong scalars for struct parameters (3.6): every enum pairing must be link-checked, and
   `-S` text (no msil-ld) stays unchecked.
4. `check-mscorlib-safe.py` passes internal members (3.5); a future mscorlib-safe row could fail at run time on
   Framework only.
5. Handle discipline in the libc: leaks are silent; use after free is usually but not always loud (3.4).
6. Allocation per handle dereference with the box/unbox shape; acceptable for I/O-bound calls, worth revisiting for
   hot paths.
7. `c.dll`'s reference set (U3): without Framework reference assemblies on the build host, `c.dll` either needs a
   built-in-table extension or runs only on .NET 8+.
8. Calls to stubs inside setjmp-reachable code get wrapped by M27's pass unless exempted (overhead only).
9. GCHandles do not cross AppDomains on Framework (consistent with per-AppDomain C state, but must be documented).
10. P-2 details surfaced here: POSIX `rename` semantics on 4.8; `Stopwatch`/`Process` live in `System` on 4.8.
11. Side finding for P-1a/P-1r: `BitConverter.IsLittleEndian` (named for P-1a's guard) is a field, so a
    `BCLMembers.def` row needs the reserved `CLI_BCL_FIELD` kind (B5, "no Phase 3 milestone is expected to need one"),
    or the guard uses another check.

## 8. Questions for the user

- **U1 Attribute spelling.** `msil_method(type, member, signature[, assembly])` plus repeatable
  `msil_catch(exception, code)` (recommended); or one attribute with a catch-list string; or a reserved prefix on
  `import_module`/`import_name` (no Attr.td rebuild, but overloads P/Invoke semantics).
- **U2 Exceptions in MC-1.** (A) listed exceptions mapped to codes through a per-stub filter clause, unlisted ones
  escape loudly, MC-1 after M27 (recommended); (B) every exception escapes, mapping later.
- **U3 How `c.dll` binds so that it runs on Framework 4.8 and .NET alike.** (a) its build always uses Framework 4.8
  reference assemblies (Windows build hosts only, no NuGet-free source on Linux); (b) a reviewed built-in list of the
  OS layer's members (mscorlib-safe, proved by a fixed `check-mscorlib-safe.py`, extended to a `[System]` scope for
  `Stopwatch` and `Process`), so `c.dll` links without references on any host (recommended); (c) bind against the
  .NET 8 pack and drop Framework for `c.dll` (conflicts with the standing rule).
- **U4 Value types beyond enums.** By boxed handle in MC-1 (recommended; needed for `DateTime`/`TimeSpan` in P-2, no
  overlap with M33/M34's struct work), or none until the fuller managed calls.
- **U5 `check-mscorlib-safe.py`.** Record the accessibility gap as a defect row now and fix it in M26's or M27's
  harness WP, before M27 appends mscorlib-safe rows (recommended), or fold it into MC-1.

## Verifier corrections

Adversarial check of this report (2026-11-01). Scratch: `<scratch>/p0/mc1/verify/`. Nothing else in
the repository was written. The tree is at `e38f9a199bc9`, a plan-only commit on top of M25 `c3460446ac09`, and it holds
someone's uncommitted M26 work (`DiagnosticSemaKinds.td`, `SemaChecking.cpp`, `MSILTargetMachine.cpp`, `MSIL.h`,
`pipeline.ll`, `MSILLink.h`, `CallAdapter.*`, `LinkInfo.*` and others). None of the code citations below is in a file
that work touches.

### Confirmed (re-measured or re-read)

- msil-ld binds arbitrary members today. `lld/MSIL/Synthetic.cpp` `OutputModule::mapTypeToken`/`mapBCLToken` make every
  TypeRef or MemberRef that is not a placeholder into a `BCLRequest`. The `lld/MSIL/BCLBinder.cpp` header confirms
  public-only, full-signature binding. Re-run: `msil-ld main-named.o named.o` with no references gives the built-in-table
  errors. With `--reference=<.NETFramework/v4.8>` the output runs on netfx with exit 0 and the expected two lines.
- The object-writer gap is real. `MCTargetDesc/MSILObjectWriter.cpp` `parseBCLType` accepts only the primitives, `*`
  and `[]`. `MSILMemberRefExpr::Kind::BCLMember` carries only a `cli::BCLMemberId`.
- The 3.5 defect is real. `acc.exe` re-run: on netfx, `ldtoken ok; now calling` is followed by `MethodAccessException`;
  on net8 the call succeeds. The docstring and the probe writer of `check-mscorlib-safe.py` use `ldtoken` only.
- The 3.6 hazard is real. `vt.exe` re-run: netfx prints `True` with exit 42; net8 throws `InvalidProgramException`.
- The M13 citations exist: `TargetWebAssemblyOrMSIL` in `Attr.td`, `SemaDecl.cpp` `mergeImportModuleAttr`,
  `CodeGenModule.cpp` `addMSILImportAttributes`/`updateMSILImportAttributes`, `MSILMCInstLower.cpp`
  `hasImportAttribute`, and `MSILPrepareIRLegacy::doInitialization`. So do the M27.a Provides
  (`emitExceptionClause` with `FilterBegin`, object-writer clause storage, the throw-helper pattern), 5.13.5's "no
  `Attr.td`" sentence, B5's reserved `CLI_BCL_FIELD`, and the 519-523-object rebuild class (5.6.10).
  `BCLMembers.def` today has exactly 20 mscorlib-safe entries.
- **New: section 4's members resolve through `[mscorlib]` on .NET 8.** Section 4 supported this for the 30 "extra"
  members only by reflection on netfx and .NET 10. I ran `check-mscorlib-safe.py --table sec4.txt --runtimes netfx,net8`
  over 72 entries: the extras without the two bad lines, plus every section-4 member not in `table.txt` (Stream,
  Console, File/Directory/Path, Environment, DateTime, Marshal.Copy/FreeHGlobal/ReAllocHGlobal, SafeHandle, Encoding,
  GCHandle, `String::.ctor(char*)`, and `field bool BitConverter::IsLittleEndian`). Result: 72 of 72 on both runtimes
  (`verify/sec4/`).
- **New: the `[System]` scope works everywhere.** `verify/sys.il` calls `[System]Stopwatch::GetTimestamp`, reads
  `ldsfld [System]Stopwatch::Frequency`, calls `Process::GetCurrentProcess().get_Id()`, and reads
  `ldsfld [mscorlib]BitConverter::IsLittleEndian`. It exits 42 on netfx, net8, net10, WSL net8 and WSL net10
  (`verify/wsl-sysp.txt`), so U3(b)'s `[System]` scope is feasible on the runtime side.

### Errors

1. **The ownership table (section 5) and the summary's "Ownership surface" are wrong for several lld and MC files**
   (checked against each M26-M35 WP's "Owned files" line):
   - `lld/MSIL/LTO.cpp` belongs to M27.c, M28.d, M29.d and M31.d. Section 5 says "(M26)" and the summary says M34; both
     are wrong.
   - `lld/MSIL/InputFiles.*` belongs to M33.c only. It is not owned by M29 or M31.
   - `lld/MSIL/SymbolTable.*` belongs to M27.c, M29.d, M31.d and M33.c. M27 and M33 are missing from the report.
   - `lld/MSIL/Writer.cpp` belongs to M28.d, M29.d, M31.d, M33.d and M34.d. M33 and M34 are missing.
   - `MCTargetDesc/MSILObjectWriter.*` is also owned by M33.c.
   - `MCTargetDesc/MSILTargetStreamer.*` is also owned by M28.c, M31.c and M33.c.

   The re-pin estimate (2-4 h) should count these files.
2. **The report misses a direct contract conflict: plan 5.14.1.** 5.14.1 says "no IR declaration names a BCL member:
   code that needs a .NET API calls a K2 definition". 5.15.1 rule 4 says private helpers use only 5.14's four delivery
   kinds K1-K4. MC-1's `msil-managed-*` declarations and its internal, AsmPrinter-written `@__msil_mc.<n>` stubs fit
   none of K1-K4. MC-1 therefore needs a 5.14.1 amendment and a new delivery kind (or a K4 variant), not only the
   5.13.5 exemption row. Design.md section 1's non-goal line ("after phase 3 at the earliest: C source calling managed
   (.NET) methods directly") also needs the user-approved amendment that option A implies. Section 6's docs WP should
   name it.
3. **U3(b), the recommended answer, disables the link-time enum check in 2.6, the check the report relies on for the
   3.6 hazard.** The check reads the enum's `value__` from "the binder's TypeDef". In a link without references (U3(b):
   `c.dll` links against the built-in table) there is no TypeDef. `BCLMembers.def` has no underlying-type column, and
   B3 forbids changing columns, so adding one needs a new X-macro kind under B5. Under U3(b), Framework 4.8 would
   silently accept a struct-for-enum mistake in `c.dll`, which is exactly the case 3.6 measured. U3's option text and
   risk 3 must say this, or U3(b) must include an enum-width record.
4. **U3(b) is under-costed.** The built-in table binds to `mscorlib` 4.0.0.0 only (plan 5.11.7, decision D10, the
   `BCLBinder.cpp` header). A `[System]` scope is therefore a binder and contract change, not just a list extension.
   Every OS-layer member and type (about 70-90 rows, by section 4) would also become a `BCLMembers.def` row in the hot
   list, with B1-B7, a B4 proof and `--print-bcl-table` re-pins (L7). Neither MC-1's estimate nor P-2's carries this
   work.
5. **U3(a) is framed too narrowly.** "Framework 4.8 reference assemblies (available on Windows hosts)" needs the
   4.8 Developer/Targeting Pack, which is not on every Windows host. Measured alternative: `--reference=` to the
   *implementation* `mscorlib.dll` and `System.dll` in `C:/Windows/Microsoft.NET/Framework64/v4.0.30319` links the same
   probe, and the output runs on netfx and net8 (`verify/impl.exe`). That works on any Windows host with Framework 4.8;
   it binds public members only, but a member public in 4.8.1 and absent from 4.8 could slip in. On Linux, "no
   NuGet-free source" is unverified: this Ubuntu archive offers `mono-devel` 6.14.1, and Mono has historically shipped
   `4.8-api` reference assemblies. I did not check whether Debian's packaging ships them.
6. **"M26 measured 36 job-minutes" is a misattribution.** M26 has not run. The 138 objects and about 36 job-minutes
   are M26's planned Rebuild cost, measured read-only from build logs on 2026-10-29.
7. **The 3.5 fix is a contract change too.** Plan 5.11.7 itself defines `check-mscorlib-safe.py`'s method ("fails on
   any TypeLoadException or MissingMethodException"). U5's defect row must amend 5.11.7, not only the script.
8. **A name-class slip in 2.6.** `__msil_mc.<n>` is in N3 ("Toolchain names `__msil_*`", 5.14.2), not the
   `'<msil>'`-reserved class (K3 services). This is harmless because the stub is internal, but the contract text
   should cite N3.

### Gaps (unsupported or unproven claims)

- **Silent string loss.** `Encoding.UTF8` replaces invalid UTF-8 with U+FFFD without any error. POSIX paths on Linux
  are byte strings, so a non-UTF-8 file name silently becomes a different name, which goes against fail-loudly. The
  report should weigh `new UTF8Encoding(false, true)` (throws on invalid bytes, public on 4.8) or document the loss.
- **AsmPrinter-written branches are unproven.** The UTF-8 and UTF-32 strlen loops and the filter need branches and
  labels in AsmPrinter-written code on the object path. M27's throw body, the cited precedent, has no branches. Only
  ilasm-built objects were measured, never llc/`MSILObjectWriter` output.
- **The LTO skip design is unverified.** irsymtab carries no function attributes. Today, explicit imports in bitcode
  are recognised after the merge (`lld/MSIL/LTO.cpp` reads `msil-import-module` from the merged module), not by
  `InputFiles`/`SymbolTable`. The proposed skip "in `InputFiles.cpp`/`SymbolTable.cpp`" may be unnecessary or
  differently shaped; an LTO probe should settle it.
- **The address-of restriction is not justified on portable.** Function pointers there are managed (`ldftn`), so the
  address of an internal stub would just work. The 2.4 error is loud and legal, but the report should say why it also
  applies on portable, or lift it there.
- **"GCHandles do not cross AppDomains on Framework" is unmeasured.** It is plausible but has no probe.
- **Width mismatches for real enums are untested.** 3.6 measured a non-enum struct (DateTime). An `int32` passed for an
  enum with an `int64` or `uint8` underlying type was not tested on netfx.
- **FeatureIdeas item 3 is not addressed.** The typed and delegate-accepting export overloads are "scheduled with the
  managed-calls goal". The report should state that MC-1 leaves them to the fuller managed calls.

### Effect on the summary's questions

- U1, U2, U4 and U5 stand. U5 should add "and amend plan 5.11.7".
- U3 needs rewording before it goes to the user:
  - (a) should name the implementation-assembly variant, which works on any Windows host, and leave the Linux claim
    open.
  - (b) must state that it loses the enum check unless an enum-width record is added, and that it needs a second
    built-in scope plus about 70-90 hot-list rows.
- The plan text MC-1 needs: the 5.13.5 exemption, a 5.14.1/5.15.1-rule-4 amendment (a new helper kind and BCL members
  named by IR declarations), the Design.md section 1 amendment, the corrected ownership table, and the 5.11.7
  amendment.
