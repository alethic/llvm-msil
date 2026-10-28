# ci-common.sh - source me: helpers shared by the bash scripts in msil/utils/ci.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Defines MSIL_REPO (the repository root) and, like ci-common.ps1:
#   ci_result STEP NAME PASS|FAIL|SKIP SECONDS DETAIL
#       records one result row, prints it, appends it to $MSIL_CI_RESULTS (tab-separated) if set;
#       SKIP marks a leg left out on purpose (lit-legs.sh's ctest leg while $MSIL_CORPUS_SKIP
#       names c-testsuite), which is no pass and no failure;
#   ci_summary TITLE
#       prints the rows as a table and appends a Markdown table to $GITHUB_STEP_SUMMARY if set;
#       returns 1 if any row failed (a row that is neither PASS nor SKIP).

MSIL_REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
MSIL_CI_ROWS=()

ci_result() {
  local step=$1 name=$2 status=$3 seconds=$4 detail
  detail=$(printf '%s' "$5" | tr '\t\r\n' '   ')
  local time
  time=$(printf '%d:%02d' $((seconds / 60)) $((seconds % 60)))
  MSIL_CI_ROWS+=("$step"$'\t'"$name"$'\t'"$status"$'\t'"$time"$'\t'"$detail")
  if [[ -n ${MSIL_CI_RESULTS:-} ]]; then
    printf '%s\t%s\t%s\t%d\t%s\n' "$step" "$name" "$status" "$seconds" "$detail" >> "$MSIL_CI_RESULTS"
  fi
  echo "[msil-ci] $status $step/$name ($time) $detail"
}

ci_summary() {
  local title=$1 row failed=0 step name status time detail
  echo
  echo "==== $title ===="
  printf '%-11s %-18s %-6s %-6s %s\n' Step Name Result Time Detail
  for row in "${MSIL_CI_ROWS[@]}"; do
    IFS=$'\t' read -r step name status time detail <<< "$row"
    printf '%-11s %-18s %-6s %-6s %s\n' "$step" "$name" "$status" "$time" "$detail"
    [[ $status == PASS || $status == SKIP ]] || failed=1
  done
  if [[ -n ${GITHUB_STEP_SUMMARY:-} ]]; then
    {
      echo "### $title"
      echo
      echo '| Step | Name | Result | Time | Detail |'
      echo '|---|---|---|---|---|'
      for row in "${MSIL_CI_ROWS[@]}"; do
        IFS=$'\t' read -r step name status time detail <<< "$row"
        echo "| $step | $name | $status | $time | ${detail//|/\\|} |"
      done
      echo
    } >> "$GITHUB_STEP_SUMMARY"
  fi
  return $failed
}
