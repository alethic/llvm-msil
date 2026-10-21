/* p9b.c: ... and a common (tentative, -fcommon) definition of the same name */
int cw;
int get(void);
int main(void) { return get() * 10 + 1; }   /* lld/ELF: the common wins, 1; the weak value would give 51 */
