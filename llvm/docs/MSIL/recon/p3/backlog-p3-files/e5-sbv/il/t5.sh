cd /mnt/c/llvm-msil-scratch/p3/backlog-p3/e5-sbv/il
LD_LIBRARY_PATH=. "$HOME/.dotnet-msil/dotnet" pinv16.exe; echo "WSL .NET 8 pinv16 exit=$? (expect 42)"
sed 's/"version":"8.0.0"/"version":"10.0.0"/; s/net8.0/net10.0/' pinv16.runtimeconfig.json > p10.json
LD_LIBRARY_PATH=. "$HOME/.dotnet-msil/dotnet" exec --runtimeconfig p10.json pinv16.exe; echo "WSL .NET 10 pinv16 exit=$? (expect 42)"
