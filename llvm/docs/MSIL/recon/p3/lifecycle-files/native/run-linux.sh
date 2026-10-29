#!/bin/bash
# Runs a native Linux probe in every mode; prints stderr+stdout merged (2>&1 through a pipe) and the exit status.
cd "$(dirname "$0")"
prog=$1; shift
for m in "$@"; do
  echo "=== $prog $m"
  out=$(./$prog "$m" 2>&1 | cat; echo "status=${PIPESTATUS[0]}")
  echo "$out"
done
