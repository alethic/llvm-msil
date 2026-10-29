#!/bin/bash
cd "$(dirname "$0")"
echo "=== order.linux (glibc crt1) ret, ORDER_EXIT_IN_CTOR=1"; ORDER_EXIT_IN_CTOR=1 ./order.linux ret 2>&1 | cat; echo "status=${PIPESTATUS[0]}"
