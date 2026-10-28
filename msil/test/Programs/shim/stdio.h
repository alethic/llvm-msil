/* Shim of <stdio.h> for the exit-corpus programs (both builds; -isystem
   ../shim): output goes through puts and putchar only. */
#ifndef SHIM_STDIO_H
#define SHIM_STDIO_H
int puts(const char *);
int putchar(int);
#endif
