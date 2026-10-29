#!/bin/bash
cd "$(dirname "$0")"
for p in fa ao p6; do echo "=== $p.e7"; ./$p.e7 2>&1 | cat; echo; echo "status=${PIPESTATUS[0]}"; done
for m in ret exit nested exitinhandler exitindtor atexitindtor quick; do echo "=== order.e7 $m"; ./order.e7 $m 2>&1 | cat; echo "status=${PIPESTATUS[0]}"; done
