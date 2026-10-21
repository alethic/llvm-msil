#!/bin/bash
cd "$(dirname "$0")"
echo "== WSL .NET $(dotnet --list-runtimes | grep NETCore | tail -1 | awk '{print $2}')"
cat > /tmp/exit.rc.json <<'J'
{ "runtimeOptions": { "tfm": "net8.0", "rollForward": "Major", "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" } } }
J
dotnet exec --runtimeconfig /tmp/exit.rc.json bin/Release/net8.0/exit.dll; echo "exit=$?"
