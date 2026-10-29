#include <stdio.h>
int main(void) { char b[64]; snprintf(b, sizeof b, "%d-%s", 7, "x"); puts(b); return 0; }
