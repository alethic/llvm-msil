#!/bin/bash
cd "$(dirname "$0")"
for t in fini-array p6-dtors atexit-order; do echo "=== $t (glibc crt1, real _start)"; ./$t/$t.linux 2>&1 | cat; echo; echo "status=${PIPESTATUS[0]}"; done
