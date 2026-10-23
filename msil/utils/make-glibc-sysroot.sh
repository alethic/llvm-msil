#!/usr/bin/env bash
# make-glibc-sysroot.sh - build a glibc sysroot (headers and libraries) for the
# optional hosted tests of msil/test/M2/09-hosted, without root.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   bash msil/utils/make-glibc-sysroot.sh DIR
#
# Run inside WSL (Ubuntu) or on any Debian-based Linux; the development host
# makes C:\llvm-msil-scratch\glibc-sysroot with
#   wsl -d Ubuntu --cd <repository> --exec bash msil/utils/make-glibc-sysroot.sh \
#     /mnt/c/llvm-msil-scratch/glibc-sysroot
# (plan 5.12.8 and step G0 of every gate from M15; lit's msil_glibc_sysroot,
# Q16). The recipe is that of recon p2/driver-toolchain section 5.1
# (scripts/mk-sysroot.sh):
#
#   1. 'apt-get download' the distribution's candidate versions of libc6-dev,
#      linux-libc-dev, libc6 and libgcc-s1 into a cache directory
#      ($MSIL_GLIBC_DEBS, default $HOME/.cache/msil/glibc-debs); a .deb that
#      is already there is not downloaded again;
#   2. unpack them with 'dpkg-deb -x' into a temporary directory, drop
#      usr/share, and turn every absolute symbolic link into a relative one;
#   3. add the links of a merged-usr system that the packages do not carry
#      (lib -> usr/lib, lib64 -> usr/lib64), because the libc.so linker
#      script names /lib64/ld-linux-x86-64.so.2;
#   4. copy usr, lib, lib64 and etc into DIR with 'cp -rL', so every link
#      becomes a file (a Windows drive keeps no Linux links), and write
#      DIR/msil-glibc-sysroot.stamp, the list of the .deb files.
#
# No root and no system setting: nothing is installed into the distribution.
# Idempotent: when DIR's stamp already lists the same .deb files, nothing is
# copied. DIR is replaced only when it is empty or holds the stamp, so no other
# directory is ever emptied. Linux headers whose names differ only in case
# (netfilter's xt_CONNMARK.h and xt_connmark.h, ...) collide on a
# case-insensitive Windows drive and keep one copy each; C programs do not
# use them. The result has usr/include/stdio.h, which lit checks.
set -euo pipefail

prog=make-glibc-sysroot
die() {
  echo "$prog: error: $*" >&2
  exit 1
}

[[ $# -eq 1 && $1 != -* ]] || {
  echo "usage: bash msil/utils/make-glibc-sysroot.sh DIR" >&2
  exit 2
}
out=$(realpath -m -- "$1")
[[ $out != / ]] || die "DIR is the file-system root"
for tool in apt-get dpkg-deb realpath cp; do
  command -v "$tool" >/dev/null || die "$tool not found (a Debian-based distribution is needed)"
done

pkgs=(libc6-dev linux-libc-dev libc6 libgcc-s1)
debs=${MSIL_GLIBC_DEBS:-$HOME/.cache/msil/glibc-debs}
mkdir -p "$debs"
stamp_name=msil-glibc-sysroot.stamp

tmp=$(mktemp -d "${TMPDIR:-/tmp}/msil-glibc-sysroot.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# 1. The candidate .deb files, downloaded only when missing. --print-uris runs
# in an empty directory: apt-get prints nothing for a file already there.
mapfile -t uris < <(cd "$tmp" && apt-get download --print-uris "${pkgs[@]}")
((${#uris[@]} == ${#pkgs[@]})) ||
  die "apt-get download --print-uris printed ${#uris[@]} lines for ${#pkgs[@]} packages"
files=()
for line in "${uris[@]}"; do
  # 'URI' FILE SIZE HASH
  read -r _ file _ <<<"$line"
  [[ $file == *.deb ]] || die "unexpected apt-get output: $line"
  files+=("$file")
done
missing=()
for i in "${!pkgs[@]}"; do
  [[ -s $debs/${files[$i]} ]] || missing+=("${pkgs[$i]}")
done
if ((${#missing[@]})); then
  echo "$prog: downloading ${missing[*]} into $debs"
  (cd "$debs" && apt-get download "${missing[@]}")
fi
for f in "${files[@]}"; do
  [[ -s $debs/$f ]] || die "$debs/$f is missing after the download"
done
wanted=$(printf '%s\n' "${files[@]}" | sort)

# Already built from these packages?
if [[ -f $out/$stamp_name && -f $out/usr/include/stdio.h ]] &&
  [[ $(cat "$out/$stamp_name") == "$wanted" ]]; then
  echo "$prog: $out is up to date ($(echo "$wanted" | tr '\n' ' '))"
  exit 0
fi
if [[ -e $out ]]; then
  [[ -d $out ]] || die "$out is not a directory"
  if [[ -n $(ls -A "$out") && ! -f $out/$stamp_name ]]; then
    die "$out is not empty and holds no $stamp_name; refusing to replace it"
  fi
fi

# 2. Unpack into a temporary directory.
root=$tmp/root
mkdir -p "$root"
for f in "${files[@]}"; do
  dpkg-deb -x "$debs/$f" "$root"
done
rm -rf "$root/usr/share"
# Absolute links point into the host; make them relative to the link.
while IFS= read -r -d '' link; do
  target=$(readlink "$link")
  if [[ $target == /* ]]; then
    ln -sfn "$(realpath -m --relative-to="$(dirname "$link")" "$root$target")" "$link"
  fi
done < <(find "$root" -type l -print0)

# 3. The merged-usr links.
for top in lib lib64; do
  if [[ ! -e $root/$top && -d $root/usr/$top ]]; then
    ln -s "usr/$top" "$root/$top"
  fi
done

# 4. Copy with every link resolved.
if [[ -d $out ]]; then
  rm -rf "$out"
fi
mkdir -p "$out"
errors=$tmp/cp-errors
for top in usr lib lib64 etc; do
  [[ -e $root/$top ]] || continue
  # Case-only name pairs collide on a case-insensitive drive ('File exists').
  cp -rL "$root/$top" "$out/" 2>"$errors" || true
  if grep -v 'File exists' "$errors" | grep -q .; then
    cat "$errors" >&2
    die "copying $top into $out failed"
  fi
done
[[ -f $out/usr/include/stdio.h ]] || die "$out/usr/include/stdio.h is missing"
[[ -f $out/lib64/ld-linux-x86-64.so.2 ]] || die "$out/lib64/ld-linux-x86-64.so.2 is missing"
echo "$wanted" >"$out/$stamp_name"
count=$(find "$out" -type f | wc -l)
echo "$prog: wrote $out from $(echo "$wanted" | tr '\n' ' ')($count files)"
