cd /mnt/c/llvm-msil-scratch/p3/backlog-p3/e9-i128
D="$HOME/.dotnet-msil/dotnet"
for v in 8 10; do
  echo "== WSL .NET $v"
  LD_LIBRARY_PATH=. "$D" exec --runtimeconfig rc$v.json i128pad.exe; echo "exit=$? (expect 42)"
done
