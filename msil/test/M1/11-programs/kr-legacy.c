// A legacy K&R-style program (Milestone 10 program corpus, Design.md
// section 12): every function but one printing helper is declared without
// a prototype ('int f();', as pre-ANSI headers had them) and defined with
// an identifier list and old-style parameter declarations, so every call
// passes the default argument promotions (char and short as int, float as
// double) and the IR fix-up passes (AddMissingPrototypes,
// FixFunctionBitcasts) and the call lowering must make each call match the
// callee's method signature. The
// program tokenizes a global text into words and numbers through a char **
// cursor, counts words in a table of structs, sorts the table with a Shell
// sort that calls its comparison through a prototype-less function pointer
// parameter ('int (*cmp)();'), converts numbers with its own atoi/itoa,
// computes powers by squaring, dispatches through a global table of
// prototype-less function pointers, and calls K&R functions with float,
// short, char, unsigned, long long and struct parameters, a float result,
// mutual recursion, and calls that precede the definition. 'register' and
// 'int main()' are kept for flavour. Results are printed; MSIL_EXPECT
// returns 1 + (line % 255) at the first failed check; when every check
// passes, the exit status is a checksum of the printed values folded into
// 8 bits (D37).
// RUN: %{msil_c}
#include "msil_test.h"

#pragma clang diagnostic ignored "-Wdeprecated-non-prototype"

#define NOINLINE __attribute__((noinline))

int nextword();
int lookup();
int ipow();
int katoi();
char *kitoa();
void shellsort();
int bycount();
int byname();
int kstrcmp();
double average();
int clampc();
float halfof();
long long widen();
int is_even();
int is_odd();
int kadd();
int ksub();
int kmul();
int rect_area();

struct entry {
  char *word;
  int count;
};

struct rect {
  int x0, y0, x1, y1;
};

volatile int zero = 0;
static unsigned check;

static char text[] = "the old style of c had no prototypes so every call "
                     "passed promoted arguments and every function trusted "
                     "its caller ; in 1978 the book showed the style , in "
                     "1989 the standard kept it , and in 2023 it was "
                     "finally removed : 3 times 14 is 42 and 42 is the "
                     "answer";

#define MAXWORDS 64
static struct entry table[MAXWORDS];
static int nentries;
static char storage[512];
static int stored;

static int (*ops[])() = {kadd, ksub, kmul};

/* The one prototyped helper: callers pass int and long long values. */
static void report(const char *name, long long v) {
  put_kv(name, v);
  check = check * 31u + (unsigned)v;
}

/* Copies the next word or number at *pp into buf; returns 'w', 'n' or 0. */
NOINLINE int nextword(pp, buf, lim)
char **pp;
char *buf;
int lim;
{
  register char *p = *pp;
  register int n = 0;
  int kind;

  while (*p && !(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9'))
    p++;
  if (!*p) {
    *pp = p;
    return 0;
  }
  kind = *p >= '0' && *p <= '9' ? 'n' : 'w';
  while (n < lim - 1 &&
         (kind == 'n' ? *p >= '0' && *p <= '9' : *p >= 'a' && *p <= 'z'))
    buf[n++] = *p++;
  buf[n] = 0;
  *pp = p;
  return kind;
}

NOINLINE int kstrcmp(s, t)
char *s, *t;
{
  for (; *s == *t; s++, t++)
    if (*s == 0)
      return 0;
  return (unsigned char)*s - (unsigned char)*t;
}

/* Returns the table index of word, adding it on first sight. */
NOINLINE int lookup(word)
char *word;
{
  int i, n;

  for (i = 0; i < nentries; i++)
    if (kstrcmp(table[i].word, word) == 0)
      return i;
  if (nentries == MAXWORDS)
    return -1;
  table[nentries].word = storage + stored;
  for (n = 0; (storage[stored++] = word[n]) != 0; n++)
    ;
  table[nentries].count = 0;
  return nentries++;
}

/* Shell sort with gaps n/2, n/4, ..., comparing through cmp. */
NOINLINE void shellsort(v, n, cmp)
struct entry *v;
int n;
int (*cmp)();
{
  int gap, i, j;
  struct entry tmp;

  for (gap = n / 2; gap > 0; gap /= 2)
    for (i = gap; i < n; i++)
      for (j = i - gap; j >= 0 && (*cmp)(&v[j], &v[j + gap]) > 0; j -= gap) {
        tmp = v[j];
        v[j] = v[j + gap];
        v[j + gap] = tmp;
      }
}

/* More frequent first, then alphabetical. */
NOINLINE int bycount(a, b)
struct entry *a, *b;
{
  if (a->count != b->count)
    return b->count - a->count;
  return kstrcmp(a->word, b->word);
}

NOINLINE int byname(a, b)
struct entry *a, *b;
{
  return kstrcmp(a->word, b->word);
}

NOINLINE int katoi(s)
char *s;
{
  int n = 0;

  while (*s >= '0' && *s <= '9')
    n = 10 * n + (*s++ - '0');
  return n;
}

NOINLINE char *kitoa(n, s)
int n;
char *s;
{
  char tmp[12];
  int i = 0, j = 0;

  do
    tmp[i++] = (char)(n % 10 + '0');
  while ((n /= 10) > 0);
  while (i > 0)
    s[j++] = tmp[--i];
  s[j] = 0;
  return s;
}

/* base ** n by repeated squaring; base is squared only while bits remain,
   so ipow(2, 30) never computes 65536 * 65536. */
NOINLINE int ipow(base, n)
int base;
unsigned n;
{
  int r = 1;

  for (; n; n >>= 1) {
    if (n & 1)
      r *= base;
    if (n > 1)
      base *= base;
  }
  return r;
}

int main()
{
  char word[32], back[16];
  char *cursor = text;
  int kind, words = 0, numbers = 0, sum = 0, i, idx;

  while ((kind = nextword(&cursor, word, (int)sizeof word)) != 0) {
    if (kind == 'n') {
      int v = katoi(word);
      numbers++;
      sum += v;
      MSIL_EXPECT(kstrcmp(kitoa(v, back), word) == 0);
    } else {
      words++;
      idx = lookup(word);
      MSIL_EXPECT(idx >= 0);
      table[idx].count++;
    }
  }
  report("words", words);
  report("distinct", nentries);
  report("numbers", numbers);
  report("sum of numbers", sum);
  MSIL_EXPECT(numbers == 7 && sum == 1978 + 1989 + 2023 + 3 + 14 + 42 + 42);
  MSIL_EXPECT(words == 43 && nentries == 31);

  shellsort(table, nentries, bycount);
  for (i = 0; i < 6; i++)
    report(table[i].word, table[i].count);
  for (i = 1; i < nentries; i++)
    MSIL_EXPECT(bycount(&table[i - 1], &table[i]) < 0);
  shellsort(table, nentries, byname);
  for (i = 0; i < 4; i++)
    put_str(table[i].word);
  put_str(table[nentries - 1].word);
  for (i = 1; i < nentries; i++)
    MSIL_EXPECT(kstrcmp(table[i - 1].word, table[i].word) < 0);

  for (i = 0; i <= 12; i += 4)
    report("3**i", ipow(3, (unsigned)i));
  MSIL_EXPECT(ipow(2, 30u) == 1 << 30 && ipow(-3, 3u) == -27);
  MSIL_EXPECT(ipow(7 + zero, 0u) == 1);

  /* Promoted arguments: float -> double, short and char -> int. */
  report("average*8", (long long)(average(3, 1.5f, (short)-2, 'A') * 8));
  MSIL_EXPECT(average(2, 0.25f, (short)1, 'A' + zero) == 0.625);
  report("clampc", clampc('z', 'a', 'm') * 1000 + clampc('A' + zero, 'a', 'm'));
  report("halfof*4", (long long)(halfof(5.5f) * 4));
  MSIL_EXPECT(halfof(3.0f + zero) == 1.5f);
  report("widen", widen(-2, 7u));
  MSIL_EXPECT(widen(1, 0u) == 4294967296LL);
  MSIL_EXPECT(is_even(40u + zero) && is_odd(17u) && !is_even(9u));

  /* A table of prototype-less function pointers. */
  for (i = 0; i < 3; i++)
    report("op", (*ops[i + zero])(12 + zero, 5));

  {
    struct rect r;
    r.x0 = 2;
    r.y0 = 3;
    r.x1 = 12 + zero;
    r.y1 = 8;
    report("rect area", rect_area(r));
    MSIL_EXPECT(r.x0 == 2 && r.x1 == 12);
  }
  return (int)((check ^ check >> 8 ^ check >> 16 ^ check >> 24) & 0xff);
}

/* Definitions after their first calls. */

NOINLINE double average(n, a, b, c)
int n;
float a;
short b;
char c;
{
  return (a + b + (c - 'A')) / n;
}

NOINLINE int clampc(c, lo, hi)
char c, lo, hi;
{
  return c < lo ? lo : c > hi ? hi : c;
}

NOINLINE float halfof(x)
float x;
{
  return x / 2;
}

NOINLINE long long widen(hi, lo)
int hi;
unsigned lo;
{
  return (long long)hi * 4294967296LL + lo;
}

NOINLINE int is_even(n)
unsigned n;
{
  return n == 0 ? 1 : is_odd(n - 1);
}

NOINLINE int is_odd(n)
unsigned n;
{
  return n == 0 ? 0 : is_even(n - 1);
}

NOINLINE int kadd(a, b)
int a, b;
{
  return a + b;
}

NOINLINE int ksub(a, b)
int a, b;
{
  return a - b;
}

NOINLINE int kmul(a, b)
int a, b;
{
  return a * b;
}

/* A struct parameter: the callee changes its own copy only. */
NOINLINE int rect_area(r)
struct rect r;
{
  int area = (r.x1 - r.x0) * (r.y1 - r.y0);
  r.x0 = r.x1 = 0;
  return area + r.x0;
}
