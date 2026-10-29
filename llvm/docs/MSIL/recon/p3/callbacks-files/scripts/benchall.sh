#!/bin/bash
cd /c/llvm-msil-scratch/p3/callbacks
O=/c/llvm-msil-scratch/p3/callbacks/out
N=20000000
for rt in fx n8 n10 w8 w10; do
  echo "=== bench $rt default"; ./run.sh $O bench.exe $rt $N 2>&1 | tr -d '\r'
done
for rt in n8 n10; do
  echo "=== bench $rt TC=0"; DOTNET_TieredCompilation=0 ./run.sh $O bench.exe $rt $N 2>&1 | tr -d '\r'
  echo "=== benchuco $rt default"; ./run.sh $O benchuco.exe $rt $N 2>&1 | tr -d '\r'
  echo "=== benchuco $rt TC=0"; DOTNET_TieredCompilation=0 ./run.sh $O benchuco.exe $rt $N 2>&1 | tr -d '\r'
done
for rt in w8 w10; do
  echo "=== bench $rt TC=0"; WENV=DOTNET_TieredCompilation=0 ./run.sh $O bench.exe $rt $N 2>&1 | tr -d '\r'
  echo "=== benchuco $rt default"; ./run.sh $O benchuco.exe $rt $N 2>&1 | tr -d '\r'
  echo "=== benchuco $rt TC=0"; WENV=DOTNET_TieredCompilation=0 ./run.sh $O benchuco.exe $rt $N 2>&1 | tr -d '\r'
done
echo ALLDONE
