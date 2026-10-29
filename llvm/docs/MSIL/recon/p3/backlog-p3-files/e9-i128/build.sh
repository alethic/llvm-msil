set -e
B=/d/llvm-msil/build/bin
$B/clang.exe --target=x86_64-unknown-linux-gnu -O2 -fPIC -c nat128.c -o nat128.o
$B/ld.lld.exe -shared -soname libnat128.so nat128.o -o libnat128.so
$B/clang.exe --target=x86_64-unknown-linux-gnu -O2 -S nat128.c -o nat128.s
grep -nE "^(mul3p1|split5|stk7|stk7s):|rsp|rdi|rsi|rdx|rax" nat128.s | head -40
