// '#pragma comment' on MSIL triples (ImplementationPlan.md 5.10.6, M13;
// recon p2/judge-c-fidelity.md J12): the pragma handler is installed for every
// MSIL triple, with or without -fms-extensions, and CodeGen keeps the non-ELF
// path of AddDependentLib, so every library request goes to
// '!llvm.linker.options' in the spelling of the native triple's
// TargetCodeGenInfo and never to '!llvm.dependent-libraries' (msil-ld reads
// the linker options from the bitcode symbol table, M15). Before M13 the
// pragma was dropped silently on the Linux MSIL triples unless -fms-extensions
// was given.
//
// - Linux MSIL triples (glibc, musl, x86_64 and aarch64) accept only the 'lib'
//   kind, as ELF does: '#pragma comment(lib, "ws2_32")' gives '-lws2_32', and
//   the 'linker', 'user', 'compiler' and 'exestr' kinds get ELF's
//   '#pragma comment <kind>' ignored' warning, also under -fms-extensions.
// - Windows MSIL triples (MSVC and MinGW, x86_64 and aarch64) accept what the
//   MSVC triples accept, without -fms-extensions too: 'lib' gives
//   '/DEFAULTLIB:<name>.lib', 'linker' passes its text through, and 'user',
//   'compiler' and 'exestr' are accepted and dropped without a diagnostic.
//   The expected spellings were generated once with the native triples and
//   -fms-extensions ('%clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions
//   -emit-llvm', likewise aarch64-pc-windows-msvc, x86_64-w64-windows-gnu and
//   aarch64-w64-windows-gnu, which all print the three lines of WIN).
// - The native ELF triple is the unchanged control: '!llvm.dependent-libraries'
//   with the bare names and the same warnings.
//
// The source strings of the dropped kinds ('msil_sym' on Linux,
// 'msil_user_comment', 'msil_exestr') never reach the IR. Frontend only: no
// backend is needed.

// DEFINE: %{lnx_not} = --implicit-check-not=llvm.dependent-libraries --implicit-check-not=msil_sym \
// DEFINE:   --implicit-check-not=msil_user_comment --implicit-check-not=msil_exestr
// DEFINE: %{win_not} = --implicit-check-not=llvm.dependent-libraries \
// DEFINE:   --implicit-check-not=msil_user_comment --implicit-check-not=msil_exestr

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -verify=lnx -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=LNX %{lnx_not} %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -fms-extensions -verify=lnx -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=LNX %{lnx_not} %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -verify=lnx -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=LNX %{lnx_not} %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -verify=lnx -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=LNX %{lnx_not} %s

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -verify=win -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=WIN %{win_not} %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fms-extensions -verify=win -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=WIN %{win_not} %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -verify=win -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=WIN %{win_not} %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -verify=win -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=WIN %{win_not} %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -verify=win -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=WIN %{win_not} %s

// Native ELF control (unchanged by M13).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -verify=lnx -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=ELF --implicit-check-not=llvm.linker.options \
// RUN:     --implicit-check-not=msil_sym --implicit-check-not=msil_user_comment \
// RUN:     --implicit-check-not=msil_exestr %s

// win-no-diagnostics

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "m")
#pragma comment(linker, "/include:msil_sym") // lnx-warning {{'#pragma comment linker' ignored}}
#pragma comment(user, "msil_user_comment")   // lnx-warning {{'#pragma comment user' ignored}}
#pragma comment(compiler)                    // lnx-warning {{'#pragma comment compiler' ignored}}
#pragma comment(exestr, "msil_exestr")       // lnx-warning {{'#pragma comment exestr' ignored}}

int msil_pragma_anchor;

// LNX:      !llvm.linker.options = !{![[WS2:[0-9]+]], ![[M:[0-9]+]]}
// LNX-DAG:  ![[WS2]] = !{!"-lws2_32"}
// LNX-DAG:  ![[M]] = !{!"-lm"}

// WIN:      !llvm.linker.options = !{![[WS2:[0-9]+]], ![[M:[0-9]+]], ![[INC:[0-9]+]]}
// WIN-DAG:  ![[WS2]] = !{!"/DEFAULTLIB:ws2_32.lib"}
// WIN-DAG:  ![[M]] = !{!"/DEFAULTLIB:m.lib"}
// WIN-DAG:  ![[INC]] = !{!"/include:msil_sym"}

// ELF:      !llvm.dependent-libraries = !{![[WS2:[0-9]+]], ![[M:[0-9]+]]}
// ELF-DAG:  ![[WS2]] = !{!"ws2_32"}
// ELF-DAG:  ![[M]] = !{!"m"}
