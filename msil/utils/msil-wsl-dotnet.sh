#!/usr/bin/env bash
# msil-wsl-dotnet.sh - the private .NET runtimes of the WSL test legs (plan 5.12.8, Q39).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   bash msil/utils/msil-wsl-dotnet.sh               # install what is missing (idempotent)
#   bash msil/utils/msil-wsl-dotnet.sh --print-path  # print the dotnet host; exit 1 if missing
#
# Run inside WSL; from Windows:
#   wsl -d Ubuntu --cd <repository> --exec bash msil/utils/msil-wsl-dotnet.sh
# It installs the Microsoft.NETCore.App runtimes below into $HOME/.dotnet-msil with Microsoft's
# dotnet-install.sh (fetched from https://dot.net/v1/dotnet-install.sh into that directory, and
# run only when the fetched file starts with '#!', since a server can answer an unknown path
# with an HTML page) as 'dotnet-install.sh --runtime dotnet --version <v> --install-dir
# "$HOME/.dotnet-msil" --no-path', and skips a version that is already there. The WSL legs run
# their programs on $HOME/.dotnet-msil/dotnet (lit parameter msil_wsl_dotnet=private), so an
# unattended upgrade of the distribution's dotnet packages (backlog recon 4.3) never changes the
# runtime under test.
# Nothing here needs root: no sudo, no apt, no apt-mark, no change to a system setting, PATH or
# a shell profile. A runtime version change is a row in section 12 of the implementation plan.
#
# --print-path prints $HOME/.dotnet-msil/dotnet and exits 0 when every runtime below is
# installed; otherwise it names what is missing on stderr and exits 1. Other exit statuses:
# 0 when everything is installed, 1 when a download or an install failed (whatever status
# curl, wget, dotnet-install.sh or the installed dotnet host returned; the reason is on
# stderr), 2 on a usage error.
set -euo pipefail

DOTNET8_VERSION=8.0.31
DOTNET10_VERSION=10.0.13
INSTALL_DIR="$HOME/.dotnet-msil"
INSTALL_SCRIPT_URL=https://dot.net/v1/dotnet-install.sh

versions=("$DOTNET8_VERSION" "$DOTNET10_VERSION")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1

print_path=0
while (($#)); do
  case $1 in
    --print-path) print_path=1 ;;
    -h | --help)
      sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      echo "msil-wsl-dotnet.sh: unknown argument '$1' (use --print-path or nothing)" >&2
      exit 2
      ;;
  esac
  shift
done

# A runtime counts as installed when the host and the runtime's CoreCLR are both there.
installed() {
  [[ -x $INSTALL_DIR/dotnet && -f $INSTALL_DIR/shared/Microsoft.NETCore.App/$1/libcoreclr.so ]]
}

missing=()
for v in "${versions[@]}"; do
  installed "$v" || missing+=("$v")
done

if ((print_path)); then
  if ((${#missing[@]})); then
    echo "msil-wsl-dotnet.sh: Microsoft.NETCore.App ${missing[*]} not installed in" \
      "$INSTALL_DIR; run msil/utils/msil-wsl-dotnet.sh" >&2
    exit 1
  fi
  echo "$INSTALL_DIR/dotnet"
  exit 0
fi

if ((${#missing[@]})); then
  mkdir -p "$INSTALL_DIR"
  script=$INSTALL_DIR/dotnet-install.sh
  echo "msil-wsl-dotnet.sh: fetching $INSTALL_SCRIPT_URL into $INSTALL_DIR"
  # Every failure below exits 1 (not curl's, wget's or dotnet-install.sh's own status, which
  # set -e would pass on) after a message on stderr.
  if command -v curl > /dev/null; then
    fetch=curl
    fetch_cmd=(curl -fsSL --retry 3 -o "$script.tmp" "$INSTALL_SCRIPT_URL")
  elif command -v wget > /dev/null; then
    fetch=wget
    fetch_cmd=(wget -q --tries=3 -O "$script.tmp" "$INSTALL_SCRIPT_URL")
  else
    echo "msil-wsl-dotnet.sh: neither curl nor wget is installed" >&2
    exit 1
  fi
  if ! "${fetch_cmd[@]}"; then
    rm -f "$script.tmp"
    echo "msil-wsl-dotnet.sh: download of $INSTALL_SCRIPT_URL failed ($fetch)" >&2
    exit 1
  fi
  # A server can answer 200 with an HTML page: run only a script.
  if [[ $(head -c 2 -- "$script.tmp" 2> /dev/null) != '#!' ]]; then
    rm -f "$script.tmp"
    echo "msil-wsl-dotnet.sh: download of $INSTALL_SCRIPT_URL failed: not a script" \
      "(it does not start with '#!')" >&2
    exit 1
  fi
  mv -f "$script.tmp" "$script"
  chmod +x "$script"
  for v in "${missing[@]}"; do
    echo "msil-wsl-dotnet.sh: installing Microsoft.NETCore.App $v into $INSTALL_DIR"
    if ! bash "$script" --runtime dotnet --version "$v" --install-dir "$INSTALL_DIR" \
      --no-path; then
      echo "msil-wsl-dotnet.sh: dotnet-install.sh failed for Microsoft.NETCore.App $v" >&2
      exit 1
    fi
    if ! installed "$v"; then
      echo "msil-wsl-dotnet.sh: dotnet-install.sh did not install Microsoft.NETCore.App $v" >&2
      exit 1
    fi
  done
fi

for v in "${versions[@]}"; do
  ((${#missing[@]})) && [[ " ${missing[*]} " == *" $v "* ]] && what=installed || what=present
  echo "msil-wsl-dotnet.sh: Microsoft.NETCore.App $v $what in $INSTALL_DIR"
done
if ! "$INSTALL_DIR/dotnet" --list-runtimes; then
  echo "msil-wsl-dotnet.sh: $INSTALL_DIR/dotnet --list-runtimes failed" >&2
  exit 1
fi
