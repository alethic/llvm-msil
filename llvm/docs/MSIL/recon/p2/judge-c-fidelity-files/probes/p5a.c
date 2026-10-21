/* p5a.c: thread_local defined in one object, used from another */
_Thread_local int tcount = 5;
int bump(void);
int main(void) { int a = bump(); return a + tcount; }   /* 6 + 6 = 12 */
