#!/bin/bash
for c in cc gcc clang zig tcc; do printf "%s: " $c; command -v $c || echo none; done
dpkg -l | grep -E "dotnet-(runtime|host|sdk)" | awk '{print $2, $3}'
apt-mark showhold
ls /usr/lib/dotnet/shared/Microsoft.NETCore.App/
ls ~/.dotnet 2>/dev/null | head -3
