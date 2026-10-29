#!/bin/bash
# wslexec.sh <dir> <prog> [args]: runs a native Linux program in WSL and prints its status
cd "$1"; p=$2; shift 2; "./$p" "$@"; echo "[native rc=$?]"
