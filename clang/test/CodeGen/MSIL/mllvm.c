// -mllvm options reach the MSIL backend through clang -cc1 and the driver
// (ImplementationPlan.md M11.c, item 10 of the upstream-merge follow-ups).
// Upstream rewrote clang's -mllvm handling in d7316136bc3e, bc8218b282ea and
// 372a4c359910: clang -cc1 parses -mllvm into its CompilerInstance
// (cl::LibraryOptionsContext). Plain cl::opt options are still set globally:
// the MSIL backend's only one, -msil-assembly-name (decision D28, which
// llvm/test/CodeGen/MSIL/assembly-name.ll covers through llc), and the pass
// managers' -print-after and -debug-pass. Options of per-context libraries
// (CodeGenOptions such as -print-after-isel, -disable-cgp and
// -verify-machineinstrs; InstCombine's -instcombine-code-sinking) are assigned
// to the LLVMContext of each compilation and reach the MSIL TargetPassConfig
// through the TargetOptions that clang's EmitAssemblyHelper sets for every
// target. Each kind is checked against the output without the option, and an
// unknown option is an error, not ignored. -stop-after/-stop-before
// (stop-after-isel.ll) and the exception-handling passes (pipeline.ll) are
// tested elsewhere; -stackmap-version is not used because MSIL emits no stack
// maps.

// REQUIRES: msil-registered-target

// A global cl::opt of the MSIL backend, through -cc1 and the driver, on both
// architectures. The default name is the stem of the source file name; with
// two occurrences the last one wins.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -O2 -o - %s \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -O2 -mllvm -msil-assembly-name=foo -o - %s \
// RUN:   | FileCheck %s --check-prefix=FOO
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -O2 -mllvm -msil-assembly-name=foo -o - %s \
// RUN:   | FileCheck %s --check-prefix=FOO
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -S -O2 -mllvm -msil-assembly-name=foo -o - %s \
// RUN:   | FileCheck %s --check-prefix=FOO
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -O2 -mllvm -msil-assembly-name=a \
// RUN:   -mllvm -msil-assembly-name=foo -o - %s | FileCheck %s --check-prefix=FOO
// DEFAULT:     {{^}}.assembly 'mllvm' { .ver 0:0:0:0 }{{$}}
// FOO-NOT:     .assembly '
// FOO:         {{^}}.assembly 'foo' { .ver 0:0:0:0 }{{$}}
// FOO-NOT:     .assembly '

// The driver forwards -mllvm to cc1 after its normalized triple (it adds the
// MSVC version to a windows-msvc triple).
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -mllvm -msil-assembly-name=foo -o - %s -### 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DRIVER
// DRIVER:      "-cc1" {{.*}}"-triple" "x86_64-pc-windows-msvc{{[0-9.]*}}-msil"
// DRIVER-SAME: "-mllvm" "-msil-assembly-name=foo"

// An unknown option is a clean error.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -mllvm -msil-no-such-option -o - %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UNKNOWN --implicit-check-not="PLEASE submit" \
// RUN:     --implicit-check-not="Stack dump"
// UNKNOWN: Unknown command line argument '-msil-no-such-option'

// Global options of the pass managers see the MSIL passes: -print-after
// prints after exactly the named pass, once per function, and -debug-pass
// lists the codegen pipeline. The STRUCT listing does not constrain machine
// verifier lines: the MSIL target keeps the default isMachineVerifierClean(),
// so an EXPENSIVE_CHECKS build inserts the verifier after every machine pass
// even without -verify-machineinstrs. The VERIFY run below shows that
// -mllvm -verify-machineinstrs adds the verifier after MSIL Register Stackify.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -O2 -mllvm -print-after=msil-reg-stackify \
// RUN:   -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=PRINT --implicit-check-not="IR Dump"
// PRINT:      # *** IR Dump After MSIL Register Stackify (msil-reg-stackify) ***:
// PRINT-NEXT: # Machine code for function f:
// PRINT:      # *** IR Dump After MSIL Register Stackify (msil-reg-stackify) ***:
// PRINT-NEXT: # Machine code for function use:
// PRINT:      # *** IR Dump After MSIL Register Stackify (msil-reg-stackify) ***:
// PRINT-NEXT: # Machine code for function g:
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -O2 -mllvm -debug-pass=Structure \
// RUN:   -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=STRUCT
// STRUCT: CodeGen Prepare
// STRUCT: MSIL Instruction Selection
// STRUCT: MSIL Register Stackify
// STRUCT: MSIL Assembly Printer

// Per-context CodeGen options reach the MSIL TargetPassConfig.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -O2 -mllvm -print-after-isel \
// RUN:   -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=ISEL
// ISEL:      # After Instruction Selection:
// ISEL-NEXT: # Machine code for function f: IsSSA
// ISEL:      ARGUMENT_I32
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -O2 -mllvm -disable-cgp -mllvm -debug-pass=Structure \
// RUN:   -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=NOCGP
// NOCGP-NOT: CodeGen Prepare
// NOCGP:     MSIL Instruction Selection
// NOCGP-NOT: CodeGen Prepare
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -O2 -mllvm -verify-machineinstrs \
// RUN:   -mllvm -debug-pass=Structure -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=VERIFY
// VERIFY:      MSIL Register Stackify
// VERIFY-NEXT: Verify generated machine code

// A per-context option of an IR library changes the IL: by default
// InstCombine sinks the division into the block that uses it (after the
// brfalse); -instcombine-code-sinking=0 keeps it in the entry block.
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -O1 -o - %s \
// RUN:   | FileCheck %s --check-prefix=SINK
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -O1 -mllvm -instcombine-code-sinking=0 -o - %s \
// RUN:   | FileCheck %s --check-prefix=NOSINK
// SINK-LABEL:   {{^}}.method public static void 'g'(int32, int32, int32) cil managed{{$}}
// SINK-NOT:     {{^}}  div{{$}}
// SINK:         {{^}}  brfalse @BB
// SINK:         {{^}}  div{{$}}
// SINK:         {{^}}  call void 'use'(int32){{$}}
// NOSINK-LABEL: {{^}}.method public static void 'g'(int32, int32, int32) cil managed{{$}}
// NOSINK-NOT:   {{^}}  brfalse @BB
// NOSINK:       {{^}}  div{{$}}
// NOSINK:       {{^}}  brfalse @BB
// NOSINK:       {{^}}  call void 'use'(int32){{$}}

int f(int a, int b) { return a < b ? a : b; }

// 'use' is defined: an undefined function is an error in Phase 1 (D14). The
// volatile store keeps the call.
volatile int sink;
__attribute__((noinline)) void use(int x) { sink = x; }

void g(int c, int a, int b) {
  int d = a / b;
  if (c)
    use(d);
}
