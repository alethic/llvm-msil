/* p4b.c */
__declspec(selectany) int sel = 7;
int ga(void);
int main(void) { int a = ga(); return a + sel; }   /* one copy: 7 + 8 = 15 */
