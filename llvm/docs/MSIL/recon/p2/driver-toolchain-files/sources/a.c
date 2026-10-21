int puts(const char *);
int add(int, int);
extern int counter;
int main(void) { counter += add(2, 3); puts("two-file program"); return counter; }
