| Variant | WSL .NET 8 | WSL .NET 10 |
|---|---|---|
| v01-mscorlib | 42  | 42  |
| v02-net8refs | 42  | 42  |
/mnt/c/llvm-msil-scratch/p2/bcl-binding/exp/runs/wsl-run.sh: line 5:   524 Aborted                    dotnet exec --runtimeconfig rc8.json "$f" > /dev/null 2>&1
| v03-net10refs | 134 (System.IO.FileNotFoundException: Could not load file or assembly 'System.Runtime.InteropServices, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5) | 42  |
| v04-netstandard20 | 42  | 42  |
/mnt/c/llvm-msil-scratch/p2/bcl-binding/exp/runs/wsl-run.sh: line 5:   614 Aborted                    dotnet exec --runtimeconfig rc8.json "$f" > /dev/null 2>&1
/mnt/c/llvm-msil-scratch/p2/bcl-binding/exp/runs/wsl-run.sh: line 5:   630 Aborted                    dotnet exec --runtimeconfig rc10.json "$f" > /dev/null 2>&1
| v05-marshal-via-sysruntime | 134 (System.TypeLoadException: Could not load type 'System.Runtime.InteropServices.Marshal' from assembly 'System.Runtime, Version=8.0.0.0, Culture=neu) | 134 (System.TypeLoadException: Could not load type 'System.Runtime.InteropServices.Marshal' from assembly 'System.Runtime, Version=10.0.0.0, Culture=ne) |
| v06-mscorlib-badpkt | 42  | 42  |
| v07-mscorlib-nopkt | 42  | 42  |
/mnt/c/llvm-msil-scratch/p2/bcl-binding/exp/runs/wsl-run.sh: line 5:   749 Aborted                    dotnet exec --runtimeconfig rc8.json "$f" > /dev/null 2>&1
/mnt/c/llvm-msil-scratch/p2/bcl-binding/exp/runs/wsl-run.sh: line 5:   765 Aborted                    dotnet exec --runtimeconfig rc10.json "$f" > /dev/null 2>&1
| v08-mscorlib-nativememory | 134 (System.TypeLoadException: Could not load type 'System.Runtime.InteropServices.NativeMemory' from assembly 'mscorlib, Version=4.0.0.0, Culture=neut) | 134 (System.TypeLoadException: Could not load type 'System.Runtime.InteropServices.NativeMemory' from assembly 'mscorlib, Version=4.0.0.0, Culture=neut) |
| v09-facades-4.0 | 42  | 42  |
/mnt/c/llvm-msil-scratch/p2/bcl-binding/exp/runs/wsl-run.sh: line 5:   839 Aborted                    dotnet exec --runtimeconfig rc8.json "$f" > /dev/null 2>&1
/mnt/c/llvm-msil-scratch/p2/bcl-binding/exp/runs/wsl-run.sh: line 5:   855 Aborted                    dotnet exec --runtimeconfig rc10.json "$f" > /dev/null 2>&1
| v10-mscorlib-ver5 | 134 (System.IO.FileNotFoundException: Could not load file or assembly 'mscorlib, Version=5.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089'. The syste) | 134 (System.IO.FileNotFoundException: Could not load file or assembly 'mscorlib, Version=5.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089'. The syste) |
| v11-net8-nativememory | 42  | 42  |
| v12-mixed-mscorlib+nm8 | 42  | 42  |
| v13-sri8-badpkt | 42  | 42  |
| v14-facades-4.0-badpkt | 42  | 42  |
| v15-netstandard21 | 42  | 42  |
| v16-facades-net48ref | 42  | 42  |
