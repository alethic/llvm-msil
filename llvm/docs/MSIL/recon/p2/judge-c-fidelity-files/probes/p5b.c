/* p5b.c */
extern _Thread_local int tcount;
int bump(void) { return ++tcount; }
