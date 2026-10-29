#!/bin/bash
cd "$(dirname "$0")"
for m in ret exit; do echo "=== main.linux $m"; LD_LIBRARY_PATH=. ./main.linux $m 2>&1 | cat; echo "status=${PIPESTATUS[0]}"; done
