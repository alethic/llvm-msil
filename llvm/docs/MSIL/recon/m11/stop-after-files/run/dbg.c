int sel(_Bool c, int a, int b) { return c ? a : b; }
unsigned long long rotr(unsigned long long a, unsigned long long b) { return (a >> (b & 63)) | (a << ((-b) & 63)); }
