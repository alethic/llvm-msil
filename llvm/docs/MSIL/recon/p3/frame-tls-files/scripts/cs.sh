#!/bin/bash
# cs.sh Name : compile src/Name.cs against .NET Framework 4.8 refs (mscorlib) -> bin/Name.exe
set -e
here=/c/llvm-msil-scratch/p3/frame-tls
python /d/llvm-msil/msil/utils/jitall/build-jitall.py --csharp "$here/src/$1.cs" --out "$here/bin/$1.exe" --refs net48 --unsafe
