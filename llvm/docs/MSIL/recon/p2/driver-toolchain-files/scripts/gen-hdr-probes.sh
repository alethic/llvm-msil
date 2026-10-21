#!/bin/bash
# Generates the one-header probe files that header-survey.sh compiles.
H=/c/llvm-msil-scratch/p2/driver-toolchain/hdr; mkdir -p $H; cd $H
for h in assert complex ctype errno fenv float inttypes iso646 limits locale math setjmp signal stdalign stdarg stdatomic stdbool stddef stdint stdio stdlib stdnoreturn string tgmath threads time uchar wchar wctype; do
  printf '#include <%s.h>\nint main(void){return 0;}\n' $h > h_$h.c; done
for h in unistd pthread fcntl sys/stat sys/types sys/time dirent dlfcn sys/mman; do
  n=$(echo $h | tr / _); printf '#include <%s.h>\nint main(void){return 0;}\n' $h > p_$n.c; done
for h in windows io process direct conio malloc; do
  printf '#include <%s.h>\nint main(void){return 0;}\n' $h > w_$h.c; done
