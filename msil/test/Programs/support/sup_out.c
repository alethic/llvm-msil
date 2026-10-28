/* Output of the support library: one 'label value' line per call through
   puts (hex bytes, unsigned and signed decimal), and the FNV-1a hash the
   drivers print instead of long buffers. */
#include "sup.h"
static char line[4096];
static size_t put_label(const char *label) {
  size_t n = 0;
  while (label[n] && n < 64) { line[n] = label[n]; ++n; }
  line[n++] = ' ';
  return n;
}
void sup_put_hex(const char *label, const void *p, size_t len) {
  static const char d[] = "0123456789abcdef";
  const unsigned char *b = p;
  size_t n = put_label(label);
  for (size_t i = 0; i < len && n + 3 < sizeof line; ++i) {
    line[n++] = d[b[i] >> 4];
    line[n++] = d[b[i] & 15];
  }
  line[n] = 0;
  puts(line);
}
void sup_put_u(const char *label, unsigned long long v) {
  char t[24]; int k = 0;
  size_t n = put_label(label);
  do { t[k++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
  while (k) line[n++] = t[--k];
  line[n] = 0;
  puts(line);
}
void sup_put_i(const char *label, long long v) {
  if (v >= 0) { sup_put_u(label, (unsigned long long)v); return; }
  char t[24]; int k = 0;
  unsigned long long m = 0ull - (unsigned long long)v;
  size_t n = put_label(label);
  line[n++] = '-';
  do { t[k++] = (char)('0' + (int)(m % 10)); m /= 10; } while (m);
  while (k) line[n++] = t[--k];
  line[n] = 0;
  puts(line);
}
unsigned sup_fnv1a(const void *p, size_t n) {
  const unsigned char *b = p; unsigned h = 2166136261u;
  for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
  return h;
}
