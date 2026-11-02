static volatile long long a = 1LL << 40;
static volatile long long b = 3;
int main(void) {
  long long v = a * b;                /* 3 * 2^40 */
  return (int)(v >> 36) & 0xff;       /* 48 when long long has 64 bits */
}
