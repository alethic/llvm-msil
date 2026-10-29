cd /mnt/c/llvm-msil-scratch/p3/backlog-p3/e5-sbv
LD_LIBRARY_PATH=. "$HOME/.dotnet-msil/dotnet" sret24.exe; echo "WSL .NET 8 sret24 exit=$? (expect 42)"
