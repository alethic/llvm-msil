/* p4a.c: __declspec(selectany) data (COMDAT any) defined in two objects */
__declspec(selectany) int sel = 7;
int ga(void) { return sel++; }
