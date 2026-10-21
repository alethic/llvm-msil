// The clang driver accepts basic block sections, late function splitting,
// the basic block address map and static data partitioning only for ELF
// (and the address map also for x86-64 COFF); the object format of every
// MSIL triple is MSIL, so each of them is an 'unsupported option' error for
// the MSIL triples, as for any other non-ELF target.
// -fno-split-machine-functions and -fbasic-block-sections=none are accepted
// and not passed on. -fbasic-block-sections=labels is the exception: the
// driver turns it into -fbasic-block-address-map with a deprecation warning
// and no target check, and the MSIL backend then ignores the map with a
// warning (llvm/test/CodeGen/MSIL/stack-protector-warning.ll). What reaches
// the backend through -Xclang or -mllvm is ignored there (decision D8;
// llvm/test/CodeGen/MSIL/basic-block-sections.ll, function-splitting.ll and
// clang/test/CodeGen/MSIL/split-machine-functions.c).

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fsplit-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SPLIT -DT=x86_64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=x86_64-w64-windows-gnu-msil -S -fsplit-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SPLIT -DT=x86_64-w64-windows-gnu-msil %s
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fsplit-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SPLIT -DT=aarch64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -S \
// RUN:   -fsplit-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SPLIT -DT=x86_64-pc-windows-msvc19.40.0-msil %s
// SPLIT: error: unsupported option '-fsplit-machine-functions' for target '[[T]]'

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fsplit-machine-functions \
// RUN:   -fno-split-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSPLIT --implicit-check-not=error: --implicit-check-not=split-machine %s
// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fsplit-machine-functions \
// RUN:   -fno-split-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSPLIT --implicit-check-not=error: --implicit-check-not=split-machine %s
// NOSPLIT: "-cc1"

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-sections=all %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-ALL -DT=x86_64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fbasic-block-sections=all %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-ALL -DT=aarch64-unknown-linux-gnu-msil %s
// BBS-ALL: error: unsupported option '-fbasic-block-sections=all' for target '[[T]]'

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-sections=list=%s %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-LIST -DT=x86_64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fbasic-block-sections=list=%s %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-LIST -DT=aarch64-unknown-linux-gnu-msil %s
// BBS-LIST: error: unsupported option '-fbasic-block-sections=list={{.*}}msil-section-options.c' for target '[[T]]'

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-sections=none %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-NONE --implicit-check-not=error: --implicit-check-not=basic-block %s
// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fbasic-block-sections=none %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-NONE --implicit-check-not=error: --implicit-check-not=basic-block %s
// BBS-NONE: "-cc1"

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-sections=labels %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-LABELS --implicit-check-not=error: %s
// BBS-LABELS: warning: argument '-fbasic-block-sections=labels' is deprecated, use '-fbasic-block-address-map' instead
// BBS-LABELS: "-fbasic-block-address-map"

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-address-map %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MAP -DT=x86_64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=x86_64-w64-windows-gnu-msil -S -fbasic-block-address-map %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MAP -DT=x86_64-w64-windows-gnu-msil %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -S \
// RUN:   -fbasic-block-address-map %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MAP -DT=x86_64-pc-windows-msvc19.40.0-msil %s
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fbasic-block-address-map %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MAP -DT=aarch64-unknown-linux-gnu-msil %s
// MAP: error: unsupported option '-fbasic-block-address-map' for target '[[T]]'

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fpartition-static-data-sections %s 2>&1 \
// RUN:   | FileCheck --check-prefix=PSDS -DT=x86_64-unknown-linux-gnu-msil %s
// PSDS: error: unsupported option '-fpartition-static-data-sections' for target '[[T]]'
