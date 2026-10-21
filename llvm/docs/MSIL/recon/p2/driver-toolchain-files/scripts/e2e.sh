#!/bin/bash
# Two C files through the prototype driver: clang -flto compiles, msil::Linker plans
# the link, the stand-in msil-ld (prototype msil-ld + ilasm) writes the assembly.
P=/c/llvm-msil-scratch/p2/driver-toolchain/proto/bin/clang.exe
cd /c/llvm-msil-scratch/p2/driver-toolchain/e2e
export MSIL_LD_STANDIN_LOG=$PWD/standin.log; rm -f standin.log prog.exe lnx.exe
echo "\$ clang --target=x86_64-pc-windows-msvc-msil -ffreestanding -O2 -flto a.c b.c -o prog.exe"
$P --target=x86_64-pc-windows-msvc-msil -ffreestanding -O2 -flto a.c b.c -o prog.exe; echo "exit=$?"
echo '{ "runtimeOptions": { "tfm": "net8.0", "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" } } }' > prog.runtimeconfig.json
echo "\$ dotnet prog.exe   (.NET 8)"; dotnet prog.exe; echo "exit=$?"
echo "\$ ./prog.exe        (.NET Framework 4.8)"; ./prog.exe; echo "exit=$?"
cat > c.c <<'C'
int add(int, int);
extern int counter;
int main(void) { return add(counter, 5); }
C
echo "\$ clang --target=x86_64-unknown-linux-gnu-msil -ffreestanding -O2 -flto b.c c.c -o lnx.exe"
$P --target=x86_64-unknown-linux-gnu-msil -ffreestanding -O2 -flto b.c c.c -o lnx.exe; echo "exit=$?"
echo '{ "runtimeOptions": { "tfm": "net10.0", "framework": { "name": "Microsoft.NETCore.App", "version": "10.0.0" } } }' > lnx.runtimeconfig.json
echo "\$ wsl dotnet lnx.exe (.NET 10)"; MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- dotnet /mnt/c/llvm-msil-scratch/p2/driver-toolchain/e2e/lnx.exe; echo "exit=$?"
echo "--- link jobs the driver planned (stand-in log)"; cat standin.log
