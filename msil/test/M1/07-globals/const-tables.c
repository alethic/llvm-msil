// Constant lookup tables (Milestone 8, D12): const arrays in the data image
// read with indexes the compiler cannot see (they come from a volatile
// global), so every lookup is a load from the data segment at -O2 too. A
// nibble-wise CRC-32 table (the CRC-32 of "123456789" is 0xcbf43926), a
// population-count table, a 2-D table of short values, a table of doubles,
// a table of long long powers of three, a struct table with char, short, int
// and long long members (padding inside each element), a const char array
// initialized from a string literal and a static const table inside a
// function. Lookups are printed; the exit status is a checksum of them,
// modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

static const unsigned crc_nibble[16] = {
    0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
    0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
    0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
    0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
};

const unsigned char popcount4[16] = {0, 1, 1, 2, 1, 2, 2, 3,
                                     1, 2, 2, 3, 2, 3, 3, 4};

const short table2d[3][4] = {
    {-1, -2, -3, -4}, {100, 200, 300, 400}, {-32768, 32767, 0, 1}};

static const double scales[] = {0.001, 0.5, 2.0, 1024.0, -3.25};

const long long pow3[] = {1,
                          3,
                          9,
                          27,
                          81,
                          243,
                          729,
                          2187,
                          6561,
                          19683,
                          59049,
                          177147,
                          531441,
                          1594323,
                          4782969,
                          14348907,
                          43046721,
                          129140163,
                          387420489,
                          1162261467,
                          3486784401ll,
                          10460353203ll,
                          31381059609ll,
                          94143178827ll,
                          282429536481ll,
                          847288609443ll,
                          2541865828329ll,
                          7625597484987ll,
                          22876792454961ll,
                          68630377364883ll,
                          205891132094649ll,
                          617673396283947ll,
                          1853020188851841ll,
                          5559060566555523ll,
                          16677181699666569ll,
                          50031545098999707ll,
                          150094635296999121ll,
                          450283905890997363ll,
                          1350851717672992089ll,
                          4052555153018976267ll};

struct opinfo {
  char symbol;
  short precedence;
  int arity;
  long long code;
};

static const struct opinfo ops[] = {
    {'+', 10, 2, 0x100000001ll}, {'-', 10, 2, 0x200000002ll},
    {'*', 20, 2, 0x300000003ll}, {'/', 20, 2, 0x400000004ll},
    {'~', 30, 1, -5ll},          {'!', 30, 1, -6ll},
};

const char vowels[] = "aeiouAEIOU";

volatile int zero = 0;
static unsigned checksum;

static void report(const char *name, long long v) {
  put_kv(name, v);
  checksum = checksum * 31u + (unsigned)v;
}

static unsigned crc32(const char *s) {
  unsigned crc = 0xffffffffu;
  for (; *s; s++) {
    crc ^= (unsigned char)*s;
    crc = (crc >> 4) ^ crc_nibble[(crc & 15) + zero];
    crc = (crc >> 4) ^ crc_nibble[(crc & 15) + zero];
  }
  return ~crc;
}

static int popcount32(unsigned v) {
  int n = 0;
  for (int i = 0; i < 8; i++)
    n += popcount4[((v >> (4 * i)) & 15) + zero];
  return n;
}

static int is_vowel(char c) {
  for (int i = 0; vowels[i + zero] != '\0'; i++)
    if (vowels[i + zero] == c)
      return 1;
  return 0;
}

static int roman_value(char c) {
  static const char letters[] = "IVXLCDM";
  static const short values[] = {1, 5, 10, 50, 100, 500, 1000};
  for (int i = 0; i < 7; i++)
    if (letters[i + zero] == c)
      return values[i + zero];
  return 0;
}

static int roman(const char *s) {
  int total = 0;
  for (; *s; s++) {
    int v = roman_value(s[0]), next = roman_value(s[1]);
    total += v < next ? -v : v;
  }
  return total;
}

int main(void) {
  put_str("crc32");
  put_hex(crc32("123456789"));
  report("crc_low", crc32("const tables") & 0xffff);
  put_str("popcount");
  report("pop(0)", popcount32(0u + (unsigned)zero));
  report("pop(0xffffffff)", popcount32(0xffffffffu + (unsigned)zero));
  report("pop(0x12345678)", popcount32(0x12345678u + (unsigned)zero));
  put_str("table2d");
  for (int r = 0; r < 3; r++) {
    unsigned long long row = 0;
    long long sum = 0;
    for (int c = 0; c < 4; c++) {
      row = row << 16 | (unsigned short)table2d[r + zero][c];
      sum += table2d[r + zero][c];
    }
    put_hex(row);
    report("row sum", sum);
  }
  put_str("scales");
  for (int i = 0; i < COUNT(scales); i++)
    report("scale*1000", (long long)(scales[i + zero] * 1000.0));
  put_str("pow3");
  for (int i = 0; i < COUNT(pow3); i += 7)
    report("pow3", pow3[i + zero]);
  report("pow3[last]", pow3[COUNT(pow3) - 1 + zero]);
  int ok = 1;
  for (int i = 1; i < COUNT(pow3); i++)
    ok &= pow3[i + zero] == pow3[i - 1 + zero] * 3;
  report("pow3 consistent", ok);
  put_str("ops");
  for (int i = 0; i < COUNT(ops); i++) {
    const struct opinfo *op = &ops[i + zero];
    report("op", op->symbol * 1000000ll + op->precedence * 1000 + op->arity);
    report("code", op->code);
  }
  put_str("strings");
  int nv = 0;
  const char *text = "Constant Tables Are Read At Run Time";
  for (int i = 0; text[i]; i++)
    nv += is_vowel(text[i]);
  report("vowels", nv);
  report("MCMXCIV", roman("MCMXCIV"));
  report("MMXXVI", roman("MMXXVI"));
  return (int)(checksum & 0xff);
}
