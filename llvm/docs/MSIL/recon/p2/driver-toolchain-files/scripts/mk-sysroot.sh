#!/bin/bash
# Build a glibc sysroot (headers + libs) from Ubuntu .debs without root.
# Linux kernel headers have case-only name pairs (netfilter xt_CONNMARK.h/xt_connmark.h)
# that collide on NTFS; those copy errors are filtered.
DEBS=/mnt/c/llvm-msil-scratch/p2/driver-toolchain/debs
OUT=/mnt/c/llvm-msil-scratch/p2/driver-toolchain/sysroot-glibc
TMP=/tmp/msil-sysroot
rm -rf "$TMP" && mkdir -p "$TMP"
for d in "$DEBS"/*.deb; do dpkg-deb -x "$d" "$TMP"; done
cd "$TMP" || exit 1
rm -rf usr/share
find . -type l | while read -r l; do
  t=$(readlink "$l")
  case "$t" in /*) ln -sfn "$TMP$t" "$l";; esac
done
rm -rf "$OUT"; mkdir -p "$OUT"
for top in usr lib lib64 etc; do
  if [ -e "$top" ] && [ ! -L "$top" ]; then
    cp -rL "$top" "$OUT/" 2>&1 | grep -v "File exists" || true
  fi
done
du -sh "$OUT"
ls "$OUT" "$OUT/usr" "$OUT/usr/lib/x86_64-linux-gnu" | head -80
