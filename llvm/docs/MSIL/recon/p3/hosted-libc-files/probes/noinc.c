/* hosted-libc recon: printf declared without <stdio.h> on windows-msvc (no inline definition). */
int printf(const char *, ...);
int main(void) { printf("hi %d %s\n", 1, "two"); return 0; }
