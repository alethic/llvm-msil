#!/bin/bash
# usage: wslnet.sh <dll> [args]  -- runs a framework-dependent dll with its own runtimeconfig on the private runtimes
D=$HOME/.dotnet-msil/dotnet
export DOTNET_ROLL_FORWARD=LatestPatch
timeout 120 $D "$@"
echo "exit=$?"
