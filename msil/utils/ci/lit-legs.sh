#!/usr/bin/env bash
# lit-legs.sh - the test legs of the MSIL CI (Linux job): the test-leg step of msil-ci.yml.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   bash msil/utils/ci/lit-legs.sh [--full] [--leg NAME[,NAME...]] [--list] [BUILD_DIR]
#
# The leg set of the Phase 2 exit (plan 5.12.11, 5.12.12; M23) for the Linux job, which runs the
# triple x86_64-unknown-linux-gnu-msil (the triple of Design.md section 12) natively, with
# msil_linux_sysroot=/ (lit.cfg.py's default on Linux) and the apt clang's system linker as the
# native oracle. CIL objects are the default pipeline (Q31); -S plus ilasm is the asm oracle.
# lit-legs.ps1 has the Windows legs and the WSL legs of the development host, and the
# Windows-only obj-text, encoder-parity and modern-ilasm legs (the NuGet ilasm is this job's
# only ilasm, so asm-oracle covers it); this job has no WSL legs. M23 gate step 12 checks
# 'lit-legs.sh --list' inside WSL, and run-m1-gate.ps1's ci-sh row (M23 gate step 16) compares it
# with the list below.
# Legs, in order:
#   net8         llc/clang FileCheck tests (lld/test/MSIL, the clang driver tests, the
#                llvm-cil-dump and llvm-cil-copy tests, llvm/test/MC/MSIL) and msil/test (M1, M2
#                and the harness; msil/test/Programs is filtered out: the corpus legs run it)
#                under obj on .NET 8 (tiered), -O0 and -O2, native oracle required, msil_equiv=1
#                (O3; O6 needs PEVerify, which Linux lacks, so msil-verify.py skips it with a
#                note) and msil_also_run=10,8+tc0,10+tc0, which replaces the net10, net8-tc0 and
#                net10-tc0 legs; no msil_scale_tests (lit-legs.ps1's net8 alone sets it)
#   more-levels  msil/test/M1 under obj on .NET 8 at O1,O3,Os,Oz (O0 and O2 run in net8)
#   asm-oracle   msil/test/M1 and msil/test/harness/pipeline under asm (-S or llc text and the
#                NuGet ilasm) on .NET 8, native oracle required, msil_roundtrip=1
#   lto          msil/test/M1/11-programs and msil/test/M2 under lto on .NET 8, native oracle
#                required, msil_equiv=1 (O4) and msil_also_run=10; .ll tests are filtered out
#   det          msil/test/M2/06-det and msil/test/Programs/det: objects, archives and msil-ld's
#                image built twice and compared (%{msil_det}; corpus leg)
#   refs-net48   msil/test/M1/11-programs, msil/test/M2/00-link and msil/test/M2/05-refs under
#                obj with msil_references=net48: every link binds the BCL against the .NET
#                Framework 4.8 reference assemblies, here the NuGet package
#                microsoft.netframework.referenceassemblies.net48 that
#                msil/utils/tools/msil-tools.proj pins ($MSIL_REF_NET48, which msil-ci.yml
#                exports), with msil_equiv=1 (O3 with the scopes entry of equiv-allowed.txt; no
#                O6 without PEVerify), on .NET 8; lit-legs.ps1's refs-net48 also runs the .NET
#                Framework 4.8, which Linux lacks
#   refs-net8    the same paths, msil/test/M2/04-shared and the P2-C rows p2c-bsymbolic,
#                p2c-cycle and p2c-weak-def with msil_references=net8 (the newest
#                Microsoft.NETCore.App.Ref 8.x pack) on .NET 8 and, msil_also_run=10, .NET 10
#   refs-net10   the refs-net48 paths with msil_references=net10 (the newest 10.x pack) on .NET
#                10
#   roundtrip    msil/test/M1/11-programs, msil/test/M2/07-equiv and
#                msil/test/M2/04-shared/roundtrip-library.test under obj, msil_roundtrip=1: O2
#                on msil-ld's outputs through ildasm -> ilasm, then run (Design.md 5.5)
#   programs     the nine exit-corpus programs (msil/test/Programs/<name>) under obj, -O0 and
#                -O2, on .NET 8 and, msil_also_run=10, .NET 10, native oracle required (corpus
#                leg)
#   programs-lto  the same under lto, with msil_equiv=1 (O4) (corpus leg)
#   ctest        msil/test/Programs/c-testsuite (the 150 libc-free tests) under obj on .NET 8,
#                native oracle required (corpus leg); while $MSIL_CORPUS_SKIP names c-testsuite
#                it prints 'ctest: skipped (c-testsuite is in MSIL_CORPUS_SKIP)', runs no lit and
#                is reported as SKIP, not PASS
#   hosted       optional, runs only when named with --leg hosted (plan Q16, 5.12.11):
#                msil/test/M2/09-hosted, the hosted compile-only tests, with msil_glibc_sysroot
#                given ($MSIL_GLIBC_SYSROOT, else /, the runner's own glibc headers and
#                libraries), so a sysroot without usr/include/stdio.h is a configuration error;
#                lit-legs.ps1's hosted leg is the development host's
#   expected     update-expected.py --check msil/test/M1 msil/test/M2: the Linux job's native
#                Linux oracle (a native build of each test with the tree's clang and the system
#                linker, of M2's %{msil_prog} tests by msil-prog.py --mode native with lld, run
#                on the runner) against <stem>.linux.expected where the Linux transcript
#                differs, else <stem>.expected (plan 5.12.11); on the development host,
#                lit-legs.ps1's expected-linux leg makes the same check through the E7 oracle
#   utc          run-utc-tests.py with the six update_llc_test_checks tests of lit-legs.ps1's
#                utc leg: the same list, so both jobs check the same files, and a test whose
#                target the build lacks fails instead of being UNSUPPORTED
#   all-levels   --full: msil/test at O0,O1,O2,O3,Os,Oz
#   lto-m1       --full: all of msil/test/M1 under lto, native oracle required
#   upstream     --full: ninja check-llvm check-clang (regressions our hooks cause upstream;
#                CI only: they build every tool, which the development host never does)
# Removed in M23, and --leg <name> fails naming the leg that covers it: net10, net8-tc0 and
# net10-tc0 (net8's msil_also_run entries), obj (net8; objects are the default pipeline).
#
# The corpus legs (programs, programs-lto, ctest, det) need the exit corpus that
# msil/utils/corpus/fetch.py downloads into a cache outside the checkout (msil-ci.yml's corpus
# steps): they pass msil_corpus_dir=$MSIL_CORPUS_CACHE (when it is set; else lit's default,
# ~/.cache/msil-corpus), msil_corpus_skip=$MSIL_CORPUS_SKIP and msil_require_corpus=1, so a
# missing or stale cache is a configuration error and an entry left out on purpose (msil-ci.yml's
# MSIL_CORPUS_SKIP: coremark,c-testsuite, plan section 10 item 11) is UNSUPPORTED with a note.
#
# ilasm and ildasm are the NuGet runtime.linux-x64.Microsoft.NETCore.ILAsm/ILDAsm packages
# ($MSIL_ILASM and $MSIL_ILDASM, which lit.cfg.py reads, or found in the NuGet global-packages
# folder); the roundtrip and asm-oracle legs take both from that folder at the version that
# msil/utils/tools/msil-tools.proj pins (MsilILAsmVersion, read by lit.cfg.py), like
# lit-legs.ps1. ILVerify is the dotnet tool; there is no PEVerify on Linux.
#
# Every lit leg passes --param msil_require_verify=1 (plan D38), --param msil_require_exec=1 and
# --param msil_require_link=1 (plan 5.12.2, M14), writes <build>/lit-results/<leg>.xml (xunit)
# and <leg>.log, and passes when lit exits 0 and at least one test passed. The other legs write
# <build>/lit-results/<leg>.log and pass when their command exits 0. Every selected leg runs
# even after a failure; the exit status is 1 if any leg failed (a SKIP row is no failure).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/ci-common.sh"

full=0
list=0
selected=()
build=
while (($#)); do
  case $1 in
    --full) full=1 ;;
    --list) list=1 ;;
    --leg)
      shift
      IFS=',' read -r -a more <<< "${1:?--leg needs a name}"
      selected+=("${more[@]}")
      ;;
    --leg=*)
      IFS=',' read -r -a more <<< "${1#--leg=}"
      selected+=("${more[@]}")
      ;;
    -*)
      echo "lit-legs.sh: unknown option $1" >&2
      exit 2
      ;;
    *) build=$1 ;;
  esac
  shift
done
build=${build:-$MSIL_REPO/build}

# check-msil's paths (msil/test/CMakeLists.txt); lit-legs.ps1's $checkMsil is the same list.
check_msil=("$MSIL_REPO/llvm/test/CodeGen/MSIL" "$MSIL_REPO/clang/test/CodeGen/MSIL"
  "$MSIL_REPO/clang/test/Preprocessor/init-msil.c" "$MSIL_REPO/clang/test/Driver/msil-toolchain.c"
  "$MSIL_REPO/clang/test/Driver/msil-link.c" "$MSIL_REPO/clang/test/Driver/msil-lto.c"
  "$MSIL_REPO/clang/test/Driver/msil-header-search.c" "$MSIL_REPO/clang/test/Driver/msil-object.c"
  "$MSIL_REPO/clang/test/Driver/msil-shared.c" "$MSIL_REPO/lld/test/MSIL" "$MSIL_REPO/llvm/test/tools/llvm-cil-dump"
  "$MSIL_REPO/llvm/test/tools/llvm-cil-copy" "$MSIL_REPO/llvm/test/MC/MSIL" "$MSIL_REPO/msil/test")
m1=("$MSIL_REPO/msil/test/M1")
m2=("$MSIL_REPO/msil/test/M2")
programs=("$MSIL_REPO/msil/test/M1/11-programs")
# The paths of the roundtrip leg: O2 on msil-ld's outputs (plan 5.12.6, 5.12.11); lit-legs.ps1's
# $roundtrip is the same list.
roundtrip=("$MSIL_REPO/msil/test/M1/11-programs" "$MSIL_REPO/msil/test/M2/07-equiv"
  "$MSIL_REPO/msil/test/M2/04-shared/roundtrip-library.test")
# The paths of the reference legs (plan 5.12.11, M21); lit-legs.ps1's $refs is the same list.
refs=("$MSIL_REPO/msil/test/M1/11-programs" "$MSIL_REPO/msil/test/M2/00-link"
  "$MSIL_REPO/msil/test/M2/05-refs")
# refs-net8 also runs the libraries bound to the net8 pack; lit-legs.ps1's $refsNet8.
refs_net8=("${refs[@]}" "$MSIL_REPO/msil/test/M2/04-shared"
  "$MSIL_REPO/msil/test/M2/08-semantics/p2c-bsymbolic.test"
  "$MSIL_REPO/msil/test/M2/08-semantics/p2c-cycle.test"
  "$MSIL_REPO/msil/test/M2/08-semantics/p2c-weak-def.test")
# The exit corpus (plan 5.12.9); lit-legs.ps1 has the same lists.
corpus_programs=()
for p in zlib tinf monocypher coremark lz4 blake2 tweetnacl kissfft jsmn; do
  corpus_programs+=("$MSIL_REPO/msil/test/Programs/$p")
done
det=("$MSIL_REPO/msil/test/M2/06-det" "$MSIL_REPO/msil/test/Programs/det")
ctest=("$MSIL_REPO/msil/test/Programs/c-testsuite")

# name:kind (kind: lit, expected, utc, upstream; a 'full' suffix marks --full legs, an
# 'optional' suffix legs that run only when --leg names them)
legs=(net8:lit more-levels:lit asm-oracle:lit lto:lit det:lit refs-net48:lit refs-net8:lit
  refs-net10:lit roundtrip:lit programs:lit programs-lto:lit ctest:lit hosted:lit:optional
  expected:expected utc:utc all-levels:lit:full lto-m1:lit:full upstream:upstream:full)

# The legs that M23 removed, and the leg that runs their tests and runtimes now.
removed_leg() {
  case $1 in
    net10) echo 'net8 (msil_also_run=10)' ;;
    net8-tc0) echo 'net8 (msil_also_run=8+tc0)' ;;
    net10-tc0) echo 'net8 (msil_also_run=10+tc0)' ;;
    obj) echo 'net8 (objects are the default pipeline; net8 passes msil_equiv=1)' ;;
  esac
}

# The tests of the utc leg; lit-legs.ps1 has the same list (keep the two the same).
# wasm-function-name is left out: it needs WebAssembly, which msil/cmake/caches/MSIL.cmake does
# not build.
utc_tests=(msil-basic x86-basic x86-mir-only x86-asm-mir-mixed x86-constant-pool asm-show-inst)

# The entries of $MSIL_CORPUS_SKIP, one per line (comma-separated; blanks dropped).
corpus_skip() {
  local e
  IFS=',' read -r -a entries <<< "${MSIL_CORPUS_SKIP:-}"
  for e in "${entries[@]}"; do
    e=${e//[[:space:]]/}
    [[ -z $e ]] || echo "$e"
  done
}

# The lit parameters of a corpus leg (plan 5.12.11, 5.12.12).
corpus_params() {
  echo msil_require_corpus=1
  [[ -z ${MSIL_CORPUS_CACHE:-} ]] || echo "msil_corpus_dir=$MSIL_CORPUS_CACHE"
  local skip
  skip=$(corpus_skip | paste -sd ',' -)
  [[ -z $skip ]] || echo "msil_corpus_skip=$skip"
}

leg_paths() {
  case $1 in
    net8) printf '%s\n' "${check_msil[@]}" ;;
    more-levels | lto-m1) printf '%s\n' "${m1[@]}" ;;
    asm-oracle) printf '%s\n' "${m1[@]}" "$MSIL_REPO/msil/test/harness/pipeline" ;;
    lto) printf '%s\n' "${programs[@]}" "${m2[@]}" ;;
    det) printf '%s\n' "${det[@]}" ;;
    refs-net48 | refs-net10) printf '%s\n' "${refs[@]}" ;;
    refs-net8) printf '%s\n' "${refs_net8[@]}" ;;
    roundtrip) printf '%s\n' "${roundtrip[@]}" ;;
    programs | programs-lto) printf '%s\n' "${corpus_programs[@]}" ;;
    ctest) printf '%s\n' "${ctest[@]}" ;;
    hosted) echo "$MSIL_REPO/msil/test/M2/09-hosted" ;;
    all-levels) echo "$MSIL_REPO/msil/test" ;;
  esac
}

leg_params() {
  case $1 in
    net8)
      printf '%s\n' msil_pipeline=obj msil_dotnet_runtime=8 msil_native_oracle=required \
        msil_equiv=1 msil_also_run=10,8+tc0,10+tc0
      ;;
    more-levels) printf '%s\n' msil_pipeline=obj msil_dotnet_runtime=8 msil_opt_levels=O1,O3,Os,Oz ;;
    asm-oracle)
      printf '%s\n' msil_pipeline=asm msil_dotnet_runtime=8 msil_native_oracle=required \
        msil_roundtrip=1
      ;;
    lto)
      printf '%s\n' msil_pipeline=lto msil_dotnet_runtime=8 msil_native_oracle=required \
        msil_equiv=1 msil_also_run=10
      ;;
    det) echo msil_dotnet_runtime=8; corpus_params ;;
    refs-net48) printf '%s\n' msil_pipeline=obj msil_references=net48 msil_equiv=1 ;;
    refs-net8)
      printf '%s\n' msil_pipeline=obj msil_references=net8 msil_dotnet_runtime=8 msil_also_run=10
      ;;
    refs-net10) printf '%s\n' msil_pipeline=obj msil_references=net10 msil_dotnet_runtime=10 ;;
    roundtrip) printf '%s\n' msil_pipeline=obj msil_roundtrip=1 ;;
    programs)
      printf '%s\n' msil_pipeline=obj msil_dotnet_runtime=8 msil_native_oracle=required \
        msil_also_run=10
      corpus_params
      ;;
    programs-lto)
      printf '%s\n' msil_pipeline=lto msil_dotnet_runtime=8 msil_native_oracle=required \
        msil_equiv=1 msil_also_run=10
      corpus_params
      ;;
    ctest)
      printf '%s\n' msil_pipeline=obj msil_dotnet_runtime=8 msil_native_oracle=required
      corpus_params
      ;;
    hosted) echo "msil_glibc_sysroot=${MSIL_GLIBC_SYSROOT:-/}" ;;
    all-levels) echo msil_opt_levels=O0,O1,O2,O3,Os,Oz ;;
    lto-m1) printf '%s\n' msil_pipeline=lto msil_dotnet_runtime=8 msil_native_oracle=required ;;
  esac
}

# A leg's extra lit arguments: net8 leaves msil/test/Programs to the corpus legs, and the lto
# legs run .ll tests on llc text, as net8 does, so they leave them out.
leg_lit_args() {
  case $1 in
    net8) echo '--filter-out=MSIL :: Programs/' ;;
    lto | lto-m1) echo '--filter-out=\.ll$' ;;
  esac
}

known=()
for l in "${legs[@]}"; do known+=("${l%%:*}"); done
for s in "${selected[@]}"; do
  instead=$(removed_leg "$s")
  if [[ -n $instead ]]; then
    echo "lit-legs.sh: leg '$s' was removed in M23 (plan 5.12.11): $instead runs its tests and runtimes now" >&2
    exit 2
  fi
  if [[ " ${known[*]} " != *" $s "* ]]; then
    echo "lit-legs.sh: unknown leg '$s' (legs: ${known[*]})" >&2
    exit 2
  fi
done
if ((list)); then
  for l in "${legs[@]}"; do
    [[ $l == *:full ]] && echo "${l%%:*} (--full)" || echo "${l%%:*}"
  done
  exit 0
fi

results=$build/lit-results
mkdir -p "$results"
lit=$build/bin/llvm-lit

run_lit_leg() {
  local name=$1 log=$results/$1.log
  local -a paths params extra args
  mapfile -t paths < <(leg_paths "$name")
  mapfile -t params < <(leg_params "$name")
  mapfile -t extra < <(leg_lit_args "$name")
  params+=(msil_require_verify=1 msil_require_exec=1 msil_require_link=1)
  args=(-sv "--xunit-xml-output=$results/$name.xml" "${extra[@]}")
  for p in "${params[@]}"; do args+=(--param "$p"); done
  echo "[msil-ci] leg $name: llvm-lit ${args[*]} ${paths[*]}"
  local rc=0
  python3 "$lit" "${args[@]}" "${paths[@]}" 2>&1 | tee "$log" || rc=$?
  local counts passed
  counts=$(sed -n 's/^  \([A-Za-z][A-Za-z ]*[A-Za-z]\) *: *\([0-9]*\) (.*/\1 \2/p' "$log" |
    tr '[:upper:]' '[:lower:]' | paste -sd ',' - | sed 's/,/, /g')
  passed=$(sed -n 's/^  Passed *: *\([0-9]*\) (.*/\1/p' "$log" | tail -n 1)
  LEG_DETAIL="${counts:-no summary}; lit exit $rc"
  if ((rc == 0)) && ((${passed:-0} >= 1)); then
    return 0
  fi
  if ((rc == 0)); then
    LEG_DETAIL+="; no test passed"
  else
    # A configuration error (lit_config.fatal, e.g. a missing tool under msil_require_verify=1).
    local fatal
    fatal=$(sed -n 's/.*: fatal: //p' "$log" | head -n 1)
    [[ -z $fatal ]] || LEG_DETAIL+="; fatal: $fatal"
  fi
  return 1
}

run_expected_leg() {
  local log=$results/expected.log rc=0
  python3 "$MSIL_REPO/msil/utils/update-expected.py" --clang "$build/bin/clang" --check \
    "${m1[@]}" "${m2[@]}" 2>&1 | tee "$log" || rc=$?
  LEG_DETAIL="update-expected.py --check msil/test/M1 msil/test/M2 (exit $rc)"
  return $((rc != 0))
}

run_utc_leg() {
  # run-utc-tests.py, not lit, as on Windows (where lit reports these tests UNSUPPORTED): the
  # fixed list runs in both jobs, and a missing target fails the test.
  local log=$results/utc.log rc=0 summary
  python3 "$MSIL_REPO/msil/utils/run-utc-tests.py" --llc "$build/bin/llc" "${utc_tests[@]}" 2>&1 |
    tee "$log" || rc=$?
  summary=$(grep -E '^[0-9]+ of [0-9]+ tests passed$' "$log" | tail -n 1 || true)
  LEG_DETAIL="run-utc-tests.py: ${summary:-no summary} (exit $rc)"
  return $((rc != 0))
}

run_upstream_leg() {
  local log=$results/upstream.log rc=0
  ninja -C "$build" check-llvm check-clang 2>&1 | tee "$log" || rc=$?
  LEG_DETAIL="ninja check-llvm check-clang (exit $rc)"
  return $((rc != 0))
}

for l in "${legs[@]}"; do
  name=${l%%:*}
  kind=${l#*:}
  kind=${kind%%:*}
  if ((${#selected[@]})); then
    [[ " ${selected[*]} " == *" $name "* ]] || continue
  elif [[ $l == *:full ]] && ((!full)); then
    continue
  elif [[ $l == *:optional ]]; then
    continue
  fi
  # The CI leaves c-testsuite out while MSIL_CORPUS_SKIP names it (plan section 10 item 11): the
  # ctest leg is then a SKIP row, not a pass.
  if [[ $name == ctest && " $(corpus_skip | tr '\n' ' ') " == *" c-testsuite "* ]]; then
    echo "ctest: skipped (c-testsuite is in MSIL_CORPUS_SKIP)"
    ci_result lit-legs "$name" SKIP 0 "c-testsuite is in MSIL_CORPUS_SKIP (plan section 10 item 11); no lit run"
    continue
  fi
  start=$SECONDS
  LEG_DETAIL=
  status=PASS
  case $kind in
    lit) run_lit_leg "$name" || status=FAIL ;;
    expected) run_expected_leg || status=FAIL ;;
    utc) run_utc_leg || status=FAIL ;;
    upstream) run_upstream_leg || status=FAIL ;;
  esac
  ci_result lit-legs "$name" "$status" $((SECONDS - start)) "$LEG_DETAIL"
done

# ci_summary fails on a FAIL row; the ctest leg's SKIP row is no failure.
rc=0
ci_summary 'MSIL CI: test legs' || rc=$?
exit "$rc"
