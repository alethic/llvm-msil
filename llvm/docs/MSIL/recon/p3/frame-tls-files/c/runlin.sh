#!/bin/bash
cd /mnt/c/llvm-msil-scratch/p3/frame-tls/c
for f in tls_threads_lin-O0 tls_threads_lin-O2 vla_cases_lin-O0 vla_cases_lin-O2; do echo "== $f"; ./$f; echo "exit=$?"; done
