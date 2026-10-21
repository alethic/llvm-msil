#!/bin/bash
ls -la /usr/include/stdio.h /usr/lib/x86_64-linux-gnu/crt1.o /usr/lib/x86_64-linux-gnu/libc.so /usr/lib/x86_64-linux-gnu/libc.so.6 /usr/lib/x86_64-linux-gnu/libm.so.6 /usr/lib/x86_64-linux-gnu/libc_nonshared.a 2>&1
dpkg -l | grep -E 'libc6-dev|linux-libc-dev|binutils' | awk '{print $2, $3}'
