// LLVM's inliners and the setjmp fence of .NET references
// (ImplementationPlan.md 5.21.4 RS22, 5.21.7 RB11 rule 8 and section 12's rows
// '5.21.7 RB11 rule 8 amended' of 2026-11-21, and '5.21.7 RB11 rule 8 amended
// (inlining)' and '5.21.7 RB11 rule 8's inlining fence as built' of 2026-11-22,
// milestone R-a). Clang's T-R17 checks each function as written and P1's T-RB3
// checks it after LLVM's inliners, so the target keeps the inliners,
// always_inline included, from crossing the fence
// (MSILTTIImpl::areInlineCompatible): no callee that holds a reference goes
// into a caller that calls setjmp (setjmp, another function that clang gives
// returns_twice by name (vfork, getcontext, savectx), or a declaration that the
// back end lowers as setjmp by name), and no callee that calls setjmp goes into
// a caller that holds a reference. The fence leaves other returns_twice calls,
// of a function of another name or with the attribute on the call site alone,
// to T-RB3 (section 12's row '5.21.7 RB11 rule 8's inlining fence as built');
// this file makes no such call. C that clang accepts then compiles:
// - (i) a static helper that holds a reference (a managed call's result,
//   compared with NULL), called after 'if (setjmp(buf))';
// - (ii) the reverse: a static function that calls a declared '_setjmp3'
//   (clang gives it no returns_twice; the back end knows the name), called
//   from a function that holds a reference;
// - (iii) shape (i) with an always_inline helper: the AlwaysInliner, which
//   runs at -O0 too, asks the target before it honors always_inline;
// - (iv) the controls, so the fence is narrow: from -O1 on, a helper without
//   references is still inlined into a setjmp caller, a helper that holds a
//   reference into a caller that does not call setjmp, and a helper that
//   calls '_setjmp3' into a caller without references;
// - (v) shape (ii) with an always_inline function that calls '_setjmp3': the
//   AlwaysInliner's other direction;
// - (vi) flatten, in both directions: a flatten setjmp caller of a static
//   helper that holds a reference, and a flatten function that holds a
//   reference and calls a static function that calls '_setjmp3'. Clang marks
//   each call in a flatten function always_inline, so the AlwaysInliner
//   inlines them at -O0 too; the control, a flatten setjmp caller of a
//   helper without references, shows that flatten ran: that call is inlined
//   at every level;
// - (vii) the KR runs: '_setjmp3' declared without a prototype (C17), so
//   that each call of it has a function type of its own, which is not the
//   declaration's, and names the function only as its called operand (the
//   KR lines check both types). The target reads that operand: shapes (ii),
//   (v) and (vi)'s second keep their calls.
// Without the target's rule LLVM inlines the helpers of (i)-(iii), (v) and
// (vi) from -O1 on, and those of (iii), (v) and (vi) at -O0 too, and P1
// refuses their callers with T-RB3.
// At -O0, -O1, -O2, -O3, -Os and -Oz on the x86_64 Windows and Linux triples
// and msil64-unknown-dotnet, and at -O0 and -O2 on the other six host triples
// of triples.c, with no diagnostic (-verify): in the IR each caller still
// calls its helper (CHECK), (iv)'s controls call theirs only at -O0 (O0; from
// -O1 on no 'ctl_' helper is left) and (vi)'s control at no level, and cc1's
// -emit-obj succeeds, so P1 accepts the code; the FREE runs compile the
// objects with -ffreestanding too (on the other six triples at -O2). The KR
// runs do the same at -O0 and -O2 on the first three triples. Under LTO,
// -flto=full and -flto=thin at -O2 on the first three triples, and
// -flto=full on aarch64-unknown-linux-gnu-msil, write the bitcode, opt runs
// the 'lto<O2>' and 'thinlto<O2>' pipelines on it with the module's MSIL
// triple (the checks of -O1 and above on the result), and cc1 generates code
// from that alone (-disable-llvm-passes, at LTO's code generation level -O2)
// with no output. Each LTO run has one module, so ThinLTO imports and
// promotes nothing.

// REQUIRES: msil-registered-target

// DEFINE: %{O0} = FileCheck %s --check-prefixes=CHECK,O0
// DEFINE: %{OPT} = FileCheck %s --implicit-check-not=@ctl_
// DEFINE: %{KR} = -std=c17 -Wno-deprecated-non-prototype -DKR

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O0 -emit-llvm -o - %s -verify | %{O0}
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O0 -emit-obj -o %t.win.O0.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O1 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O1 -emit-obj -o %t.win.O1.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -emit-obj -o %t.win.O2.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O3 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O3 -emit-obj -o %t.win.O3.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Os -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Os -emit-obj -o %t.win.Os.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Oz -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Oz -emit-obj -o %t.win.Oz.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O0 -emit-llvm -o - %s -verify | %{O0}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O0 -emit-obj -o %t.lnx.O0.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O1 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O1 -emit-obj -o %t.lnx.O1.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -emit-obj -o %t.lnx.O2.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O3 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O3 -emit-obj -o %t.lnx.O3.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -Os -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -Os -emit-obj -o %t.lnx.Os.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -Oz -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -Oz -emit-obj -o %t.lnx.Oz.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O0 -emit-llvm -o - %s -verify | %{O0}
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O0 -emit-obj -o %t.p64.O0.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O1 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O1 -emit-obj -o %t.p64.O1.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O2 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O2 -emit-obj -o %t.p64.O2.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O3 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O3 -emit-obj -o %t.p64.O3.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -Os -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -Os -emit-obj -o %t.p64.Os.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -Oz -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -Oz -emit-obj -o %t.p64.Oz.o %s -verify

// The other six host triples of triples.c, at -O0 and -O2.
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -O0 -emit-llvm -o - %s -verify | %{O0}
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -O0 -emit-obj -o %t.gnu.O0.o %s -verify
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -O2 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -O2 -emit-obj -o %t.gnu.O2.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -O0 -emit-llvm -o - %s -verify | %{O0}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -O0 -emit-obj -o %t.mus.O0.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -O2 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -O2 -emit-obj -o %t.mus.O2.o %s -verify
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -O0 -emit-llvm -o - %s -verify | %{O0}
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -O0 -emit-obj -o %t.a64win.O0.o %s -verify
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -O2 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -O2 -emit-obj -o %t.a64win.O2.o %s -verify
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -O0 -emit-llvm -o - %s -verify | %{O0}
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -O0 -emit-obj -o %t.a64gnu.O0.o %s -verify
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -O2 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -O2 -emit-obj -o %t.a64gnu.O2.o %s -verify
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -O0 -emit-llvm -o - %s -verify | %{O0}
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -O0 -emit-obj -o %t.a64lnx.O0.o %s -verify
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -O2 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -O2 -emit-obj -o %t.a64lnx.O2.o %s -verify
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -O0 -emit-llvm -o - %s -verify | %{O0}
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -O0 -emit-obj -o %t.a64mus.O0.o %s -verify
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -O2 -emit-llvm -o - %s -verify | %{OPT}
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -O2 -emit-obj -o %t.a64mus.O2.o %s -verify

// The KR runs (vii): '_setjmp3' without a prototype.
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil %{KR} -O0 -emit-llvm -o %t.win.kr.O0.ll %s -verify
// RUN: %{O0} --input-file=%t.win.kr.O0.ll
// RUN: FileCheck %s --check-prefix=KR --input-file=%t.win.kr.O0.ll
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil %{KR} -O0 -emit-obj -o %t.win.kr.O0.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil %{KR} -O2 -emit-llvm -o %t.win.kr.O2.ll %s -verify
// RUN: %{OPT} --input-file=%t.win.kr.O2.ll
// RUN: FileCheck %s --check-prefix=KR --input-file=%t.win.kr.O2.ll
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil %{KR} -O2 -emit-obj -o %t.win.kr.O2.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil %{KR} -O0 -emit-llvm -o %t.lnx.kr.O0.ll %s -verify
// RUN: %{O0} --input-file=%t.lnx.kr.O0.ll
// RUN: FileCheck %s --check-prefix=KR --input-file=%t.lnx.kr.O0.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil %{KR} -O0 -emit-obj -o %t.lnx.kr.O0.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil %{KR} -O2 -emit-llvm -o %t.lnx.kr.O2.ll %s -verify
// RUN: %{OPT} --input-file=%t.lnx.kr.O2.ll
// RUN: FileCheck %s --check-prefix=KR --input-file=%t.lnx.kr.O2.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil %{KR} -O2 -emit-obj -o %t.lnx.kr.O2.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet %{KR} -O0 -emit-llvm -o %t.p64.kr.O0.ll %s -verify
// RUN: %{O0} --input-file=%t.p64.kr.O0.ll
// RUN: FileCheck %s --check-prefix=KR --input-file=%t.p64.kr.O0.ll
// RUN: %clang_cc1 -triple msil64-unknown-dotnet %{KR} -O0 -emit-obj -o %t.p64.kr.O0.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet %{KR} -O2 -emit-llvm -o %t.p64.kr.O2.ll %s -verify
// RUN: %{OPT} --input-file=%t.p64.kr.O2.ll
// RUN: FileCheck %s --check-prefix=KR --input-file=%t.p64.kr.O2.ll
// RUN: %clang_cc1 -triple msil64-unknown-dotnet %{KR} -O2 -emit-obj -o %t.p64.kr.O2.o %s -verify

// The FREE runs: the objects with -ffreestanding.
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ffreestanding -O0 -emit-obj -o %t.win.free.O0.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ffreestanding -O1 -emit-obj -o %t.win.free.O1.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ffreestanding -O2 -emit-obj -o %t.win.free.O2.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ffreestanding -O3 -emit-obj -o %t.win.free.O3.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ffreestanding -Os -emit-obj -o %t.win.free.Os.o %s -verify
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ffreestanding -Oz -emit-obj -o %t.win.free.Oz.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ffreestanding -O0 -emit-obj -o %t.lnx.free.O0.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ffreestanding -O1 -emit-obj -o %t.lnx.free.O1.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ffreestanding -O2 -emit-obj -o %t.lnx.free.O2.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ffreestanding -O3 -emit-obj -o %t.lnx.free.O3.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ffreestanding -Os -emit-obj -o %t.lnx.free.Os.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ffreestanding -Oz -emit-obj -o %t.lnx.free.Oz.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -ffreestanding -O0 -emit-obj -o %t.p64.free.O0.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -ffreestanding -O1 -emit-obj -o %t.p64.free.O1.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -ffreestanding -O2 -emit-obj -o %t.p64.free.O2.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -ffreestanding -O3 -emit-obj -o %t.p64.free.O3.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -ffreestanding -Os -emit-obj -o %t.p64.free.Os.o %s -verify
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -ffreestanding -Oz -emit-obj -o %t.p64.free.Oz.o %s -verify
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -ffreestanding -O2 -emit-obj -o %t.gnu.free.O2.o %s -verify
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -ffreestanding -O2 -emit-obj -o %t.mus.free.O2.o %s -verify
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -ffreestanding -O2 -emit-obj -o %t.a64win.free.O2.o %s -verify
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -ffreestanding -O2 -emit-obj -o %t.a64gnu.free.O2.o %s -verify
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -ffreestanding -O2 -emit-obj -o %t.a64lnx.free.O2.o %s -verify
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -ffreestanding -O2 -emit-obj -o %t.a64mus.free.O2.o %s -verify

// The LTO runs: one module each, the LTO pipeline, then code generation alone.
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -flto=full -emit-llvm-bc -o %t.win.full.bc %s -verify
// RUN: opt -passes='lto<O2>' -S -o %t.win.full.ll %t.win.full.bc
// RUN: %{OPT} --input-file=%t.win.full.ll
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -disable-llvm-passes -emit-obj -x ir -o %t.win.full.o %t.win.full.ll 2>&1 | count 0
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -flto=thin -emit-llvm-bc -o %t.win.thin.bc %s -verify
// RUN: opt -passes='thinlto<O2>' -S -o %t.win.thin.ll %t.win.thin.bc
// RUN: %{OPT} --input-file=%t.win.thin.ll
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -disable-llvm-passes -emit-obj -x ir -o %t.win.thin.o %t.win.thin.ll 2>&1 | count 0
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -flto=full -emit-llvm-bc -o %t.lnx.full.bc %s -verify
// RUN: opt -passes='lto<O2>' -S -o %t.lnx.full.ll %t.lnx.full.bc
// RUN: %{OPT} --input-file=%t.lnx.full.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -disable-llvm-passes -emit-obj -x ir -o %t.lnx.full.o %t.lnx.full.ll 2>&1 | count 0
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -flto=thin -emit-llvm-bc -o %t.lnx.thin.bc %s -verify
// RUN: opt -passes='thinlto<O2>' -S -o %t.lnx.thin.ll %t.lnx.thin.bc
// RUN: %{OPT} --input-file=%t.lnx.thin.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -disable-llvm-passes -emit-obj -x ir -o %t.lnx.thin.o %t.lnx.thin.ll 2>&1 | count 0
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O2 -flto=full -emit-llvm-bc -o %t.p64.full.bc %s -verify
// RUN: opt -passes='lto<O2>' -S -o %t.p64.full.ll %t.p64.full.bc
// RUN: %{OPT} --input-file=%t.p64.full.ll
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O2 -disable-llvm-passes -emit-obj -x ir -o %t.p64.full.o %t.p64.full.ll 2>&1 | count 0
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O2 -flto=thin -emit-llvm-bc -o %t.p64.thin.bc %s -verify
// RUN: opt -passes='thinlto<O2>' -S -o %t.p64.thin.ll %t.p64.thin.bc
// RUN: %{OPT} --input-file=%t.p64.thin.ll
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O2 -disable-llvm-passes -emit-obj -x ir -o %t.p64.thin.o %t.p64.thin.ll 2>&1 | count 0
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -O2 -flto=full -emit-llvm-bc -o %t.a64lnx.full.bc %s -verify
// RUN: opt -passes='lto<O2>' -S -o %t.a64lnx.full.ll %t.a64lnx.full.bc
// RUN: %{OPT} --input-file=%t.a64lnx.full.ll
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -O2 -disable-llvm-passes -emit-obj -x ir -o %t.a64lnx.full.o %t.a64lnx.full.ll 2>&1 | count 0

// expected-no-diagnostics

#include <cli/cli.h>

#define NULL ((void *)0)

typedef struct CLI_TYPE("System.Object") Object Object;
typedef struct CLI_TYPE("System.Text.UTF8Encoding") UTF8Encoding UTF8Encoding;

// A managed call whose result the optimizer cannot see through.
UTF8Encoding ^new_utf8(_Bool, _Bool) __attribute__((msil_method("System.Text.UTF8Encoding", ".ctor", "instance void(bool, bool)")));

// No C library header: setjmp as clang knows it (returns_twice in the IR,
// also under -ffreestanding), and the MSVC name that only the back end knows,
// in the KR runs without a prototype: each call then has the function type
// of its arguments, and the declaration has another.
typedef long long jmp_buf[16];
int setjmp(jmp_buf);
#if defined(KR)
int _setjmp3();
#else
int _setjmp3(void *, int);
#endif
// KR-DAG: call i32 @_setjmp3(ptr
// KR-DAG: declare i32 @_setjmp3(...)

// The external functions are emitted in their order here, each followed by
// the static function it calls.

// (i): the callee holds the reference, its caller calls setjmp.
static jmp_buf buf_i;
static int helper_i(int x) {
  Object ^o = new_utf8(0, 0);
  return (o != NULL) + x;
}
int f_i(int x) {
  if (setjmp(buf_i))
    return -1;
  return helper_i(x);
}
// CHECK-LABEL: define {{.*}}i32 @f_i(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @setjmp(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @helper_i(
// CHECK-LABEL: define internal {{.*}}i32 @helper_i(
// CHECK-NOT:   {{^}}}
// CHECK:       call target("msil.ref") @__msil_mc.new_utf8(

// (ii): the callee calls '_setjmp3', its caller holds the reference.
static long long buf_ii[32];
static int g_ii(void) { return _setjmp3(buf_ii, 0) ? 1 : 0; }
int f_ii(void) {
  Object ^o = new_utf8(0, 0);
  return g_ii() + (o != NULL);
}
// CHECK-LABEL: define {{.*}}i32 @f_ii(
// CHECK-NOT:   {{^}}}
// CHECK:       call target("msil.ref") @__msil_mc.new_utf8(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @g_ii(
// CHECK-LABEL: define internal {{.*}}i32 @g_ii(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @_setjmp3(

// (iii): shape (i) with an always_inline callee.
static jmp_buf buf_iii;
static inline __attribute__((always_inline)) int helper_iii(int x) {
  Object ^o = new_utf8(1, 0);
  return (o != NULL) - x;
}
int f_iii(int x) {
  if (setjmp(buf_iii))
    return -1;
  return helper_iii(x);
}
// CHECK-LABEL: define {{.*}}i32 @f_iii(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @setjmp(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @helper_iii(
// CHECK-LABEL: define internal {{.*}}i32 @helper_iii(
// CHECK-NOT:   {{^}}}
// CHECK:       call target("msil.ref") @__msil_mc.new_utf8(

// (iv): the controls, each still inlined from -O1 on: a callee without
// references into a setjmp caller, a callee that holds a reference into a
// caller that does not call setjmp, and a callee that calls '_setjmp3' into a
// caller without references.
static jmp_buf buf_iv;
static int ctl_plain(int x) { return x * 3 + 1; }
int f_iv(int x) {
  if (setjmp(buf_iv))
    return -1;
  return ctl_plain(x);
}
// CHECK-LABEL: define {{.*}}i32 @f_iv(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @setjmp(
// O0-NOT:      {{^}}}
// O0:          call {{.*}}i32 @ctl_plain(
// O0-LABEL:    define internal {{.*}}i32 @ctl_plain(

static int ctl_ref(int x) {
  Object ^o = new_utf8(0, 1);
  return (o == NULL) + x;
}
int f_iv_ref(int x) { return ctl_ref(x) * 2; }
// CHECK-LABEL: define {{.*}}i32 @f_iv_ref(
// O0-NOT:      {{^}}}
// O0:          call {{.*}}i32 @ctl_ref(
// O0-LABEL:    define internal {{.*}}i32 @ctl_ref(
// CHECK-NOT:   {{^}}}
// CHECK:       call target("msil.ref") @__msil_mc.new_utf8(

static long long buf_iv_setjmp3[32];
static int ctl_setjmp3(void) { return _setjmp3(buf_iv_setjmp3, 0) ? 1 : 0; }
int f_iv_setjmp3(void) { return ctl_setjmp3() + 2; }
// CHECK-LABEL: define {{.*}}i32 @f_iv_setjmp3(
// O0-NOT:      {{^}}}
// O0:          call {{.*}}i32 @ctl_setjmp3(
// O0-LABEL:    define internal {{.*}}i32 @ctl_setjmp3(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @_setjmp3(

// (v): shape (ii) with an always_inline callee, so the AlwaysInliner meets
// it at -O0 too.
static long long buf_v[32];
static inline __attribute__((always_inline)) int g_v(void) {
  return _setjmp3(buf_v, 0) ? 1 : 0;
}
int f_v(void) {
  Object ^o = new_utf8(1, 1);
  return g_v() + (o != NULL);
}
// CHECK-LABEL: define {{.*}}i32 @f_v(
// CHECK-NOT:   {{^}}}
// CHECK:       call target("msil.ref") @__msil_mc.new_utf8(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @g_v(
// CHECK-LABEL: define internal {{.*}}i32 @g_v(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @_setjmp3(

// (vi): flatten, both directions. Neither callee is always_inline: clang
// marks the calls in a flatten function always_inline instead.
static jmp_buf buf_vi;
static int helper_vi(int x) {
  Object ^o = new_utf8(1, 0);
  return (o != NULL) * x;
}
__attribute__((flatten)) int f_vi(int x) {
  if (setjmp(buf_vi))
    return -1;
  return helper_vi(x);
}
// CHECK-LABEL: define {{.*}}i32 @f_vi(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @setjmp(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @helper_vi(
// CHECK-LABEL: define internal {{.*}}i32 @helper_vi(
// CHECK-NOT:   {{^}}}
// CHECK:       call target("msil.ref") @__msil_mc.new_utf8(

static long long buf_vi_rev[32];
static int g_vi(void) { return _setjmp3(buf_vi_rev, 0) ? 1 : 0; }
__attribute__((flatten)) int f_vi_rev(void) {
  Object ^o = new_utf8(0, 1);
  return g_vi() + (o != NULL);
}
// CHECK-LABEL: define {{.*}}i32 @f_vi_rev(
// CHECK-NOT:   {{^}}}
// CHECK:       call target("msil.ref") @__msil_mc.new_utf8(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @g_vi(
// CHECK-LABEL: define internal {{.*}}i32 @g_vi(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @_setjmp3(

// The control: flatten inlines a helper without references into a setjmp
// caller at every level, -O0 included (where the helper's body stays behind,
// unused).
static jmp_buf buf_vi_ctl;
static int ctl_flat(int x) { return x * 5 + 2; }
__attribute__((flatten)) int f_vi_ctl(int x) {
  if (setjmp(buf_vi_ctl))
    return -1;
  return ctl_flat(x);
}
// CHECK-LABEL: define {{.*}}i32 @f_vi_ctl(
// CHECK-NOT:   {{^}}}
// CHECK:       call {{.*}}i32 @setjmp(
// CHECK-NOT:   @ctl_flat
// CHECK:       {{^}}}
