#!/usr/bin/env bash
# Disable every GitHub Actions workflow inherited from llvm/llvm-project in this
# fork, leaving only .github/workflows/msil-*.yml active. This changes repository
# settings, not files, so it never conflicts with upstream merges. Re-run it after
# each upstream sync: newly added upstream workflows start out enabled.
#
#   .github/msil/disable-upstream-workflows.sh [owner/repo]     (needs gh auth, actions:write)
set -euo pipefail
repo="${1:-alethic/llvm-msil}"
gh workflow list --repo "$repo" --all --limit 500 --json id,path,state \
  --jq '.[] | select(.state == "active") | select(.path | test("^\.github/workflows/msil-") | not) | "\(.id) \(.path)"' |
while read -r id path; do
  echo "disabling $path"
  gh workflow disable "$id" --repo "$repo"
done
