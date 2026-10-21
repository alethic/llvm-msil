// -mllvm options reach the MSIL backend through clang -cc1 and the driver.
// Since upstream d7316136bc3e, bc8218b282ea and 372a4c359910, clang -cc1 parses
// -mllvm into its CompilerInstance (cl::LibraryOptionsContext): plain cl::opt
// options are still set globally (the MSIL backend's -msil-assembly-name,
// decision D28, and -print-after / -debug-pass), while options of per-context
// libraries (CodeGenOptions: -print-after-isel, -verify-machineinstrs,
// -disable-cgp; InstCombine: -instcombine-code-sinking) are assigned to the
// LLVMContext of each compilation and reach the MSIL TargetPassConfig through
// TargetOptions::MCOptions.LibraryOptions, which clang's
// EmitAssemblyHelper::CreateTargetMachine sets for every target.

// REQUIRES: msil-registered-target

// A global cl::opt of the MSIL backend, through -cc1 and the driver.
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
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -mllvm -msil-assembly-name=foo -o - %s -### 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DRIVER
// DEFAULT: {{^}}.assembly 'mllvm' { .ver 0:0:0:0 }{{$}}
// FOO-NOT: .assembly 'mllvm'
// FOO:     {{^}}.assembly 'foo' { .ver 0:0:0:0 }{{$}}
// DRIVER:  "-cc1" {{.*}}"-triple" "x86_64-pc-windows-msvc{{[0-9.]*}}-msil"
// DRIVER-SAME: "-mllvm" "-msil-assembly-name=foo"

// An unknown option is an error, not ignored.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -mllvm -msil-no-such-option -o - %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UNKNOWN
// UNKNOWN: Unknown command line argument '-msil-no-such-option'

// Global options of the pass managers see the MSIL passes.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -O2 -mllvm -print-after=msil-reg-stackify \
// RUN:   -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=PRINT
// PRINT: # *** IR Dump After MSIL Register Stackify (msil-reg-stackify) ***:
// PRINT: # Machine code for function f:
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -O2 -mllvm -debug-pass=Structure \
// RUN:   -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=STRUCT
// STRUCT:  CodeGen Prepare
// STRUCT:  MSIL Instruction Selection
// STRUCT:  MSIL Register Stackify
// STRUCT:  MSIL Assembly Printer

// Per-context CodeGen options reach the MSIL TargetPassConfig.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -O2 -mllvm -print-after-isel \
// RUN:   -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=ISEL
// ISEL:      # After Instruction Selection:
// ISEL-NEXT: # Machine code for function f: IsSSA, TracksLiveness
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

// A per-context option of an IR library (InstCombine) changes the IL.
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -O1 -o - %s \
// RUN:   | FileCheck %s --check-prefix=SINK
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -O1 -mllvm -instcombine-code-sinking=0 -o - %s \
// RUN:   | FileCheck %s --check-prefix=NOSINK
// SINK-LABEL:   .method public static void 'g'(int32, int32, int32) cil managed
// SINK:         brfalse
// SINK:         div
// SINK:         call void 'use'(int32)
// NOSINK-LABEL: .method public static void 'g'(int32, int32, int32) cil managed
// NOSINK:       div
// NOSINK:       brfalse
// NOSINK:       call void 'use'(int32)

int f(int a, int b) { return a < b ? a : b; }

volatile int sink;
__attribute__((noinline)) void use(int x) { sink = x; }
void g(int c, int a, int b) {
  int d = a / b;
  if (c)
    use(d);
}
