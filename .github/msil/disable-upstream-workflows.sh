#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##
#
# Disable every GitHub Actions workflow inherited from llvm/llvm-project in this
# fork, leaving only .github/workflows/msil-*.yml active.
#
# This changes repository settings, not files, so it never conflicts with
# upstream merges (the upstream workflow files stay untouched). Re-run it after
# each upstream sync: newly added upstream workflows start out enabled.
#
# Run by the repository owner only, never by an agent or a CI job (plan
# section 10, item 5). After running it, the push, pull_request and schedule
# triggers in .github/workflows/msil-ci.yml can be uncommented.
#
# Usage:
#   .github/msil/disable-upstream-workflows.sh [--dry-run] [owner/repo]
#
#   --dry-run   only list what would be disabled
#   owner/repo  the fork (default alethic/llvm-msil); never llvm/llvm-project
#
# Needs the GitHub CLI (gh), authenticated with a token that may change the
# repository's Actions settings ('repo' scope, or fine-grained 'Actions: write').
# Workflows that are not files under .github/workflows (dynamic ones such as
# Dependabot updates or CodeQL default setup) are listed but left alone; they
# are switched off in the repository settings.
set -euo pipefail

usage() {
  sed -n '/^# Usage:/,/^# repository/p' "$0" | sed 's/^# \{0,1\}//'
}

repo=alethic/llvm-msil
dry_run=0
while [ $# -gt 0 ]; do
  case "$1" in
    -n | --dry-run) dry_run=1 ;;
    -h | --help) usage; exit 0 ;;
    -*) echo "error: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    */*) repo=$1 ;;
    *) echo "error: expected owner/repo, got '$1'" >&2; exit 2 ;;
  esac
  shift
done

case "$repo" in
  llvm/* | */*/*)
    echo "error: refusing to change '$repo'; pass the fork as owner/repo" >&2
    exit 2 ;;
esac

if ! command -v gh > /dev/null 2>&1; then
  echo "error: the GitHub CLI (gh) is not installed" >&2
  exit 2
fi

# One line per workflow: id, state and path, tab-separated. gh lists 50
# workflows by default and upstream has more than that (58 files at
# 2829ef573b08), so ask for up to 200 and stop if that might have truncated.
limit=200
listing=$(gh workflow list --repo "$repo" --all --limit "$limit" \
  --json id,state,path --jq '.[] | "\(.id)\t\(.state)\t\(.path)"')
if [ -z "$listing" ]; then
  echo "error: gh listed no workflows for '$repo'" >&2
  exit 1
fi
if [ "$(printf '%s\n' "$listing" | wc -l)" -ge "$limit" ]; then
  echo "error: $limit or more workflows listed; raise the limit in $0" >&2
  exit 1
fi

kept=0 disabled=0 already=0 other=0 failed=0
while IFS=$'\t' read -r id state path; do
  case "$path" in
    .github/workflows/msil-*)
      echo "keep           $path ($state)"
      kept=$((kept + 1))
      continue ;;
    .github/workflows/*) ;;
    *)
      echo "not a file     $path ($state): manage it in the repository settings"
      other=$((other + 1))
      continue ;;
  esac
  if [ "$state" != active ]; then
    echo "already off    $path ($state)"
    already=$((already + 1))
  elif [ "$dry_run" = 1 ]; then
    echo "would disable  $path"
    disabled=$((disabled + 1))
  elif gh workflow disable "$id" --repo "$repo" < /dev/null; then
    echo "disabled       $path"
    disabled=$((disabled + 1))
  else
    echo "FAILED         $path (workflow id $id)" >&2
    failed=$((failed + 1))
  fi
done <<< "$listing"

verb=disabled
[ "$dry_run" = 1 ] && verb="to disable"
echo "$repo: $disabled $verb, $already already off, $kept msil-* kept," \
  "$other not files, $failed failed"
[ "$failed" -eq 0 ]
