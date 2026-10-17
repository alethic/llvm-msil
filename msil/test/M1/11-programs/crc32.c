// Checksums from table lookups and bit operations (Milestone 1 program
// corpus): the reflected CRC-32 (polynomial 0xEDB88320) computed bit by bit
// and through a 256-entry table built at run time in a global array, CRC-32C
// (Castagnoli, 0x82F63B78) through its own table, the non-reflected
// CRC-16/CCITT-FALSE (0x1021) in unsigned short arithmetic, and Adler-32,
// over the standard check strings and over a 4 KiB xorshift-filled global
// buffer, whole and in uneven chunks that continue a running CRC. The known
// check values (CRC-32 of "123456789" is 0xcbf43926, CRC-32C 0xe3069283,
// CRC-16/CCITT-FALSE 0x29b1, Adler-32 of "Wikipedia" 0x11e60398) and the
// CRC-32 residue of a message followed by its own CRC (0x2144df1c) are
// asserted. Prints every checksum in hex. The buffer seed is read from a
// volatile global. MSIL_EXPECT returns 1 + (line % 255) at the first failed
// check; on success the exit status is the low byte of the buffer's CRC-32
// (re-check that it is not 1 + (line % 255) for an MSIL_EXPECT line after
// edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define SIZE 4096

volatile unsigned seed = 0x2545f491u;
static unsigned crc32_table[256], crc32c_table[256];
static unsigned char buffer[SIZE + 4];

static void make_table(unsigned *table, unsigned poly) {
  for (unsigned i = 0; i < 256; i++) {
    unsigned c = i;
    for (int k = 0; k < 8; k++)
      c = (c >> 1) ^ (poly & (0u - (c & 1)));
    table[i] = c;
  }
}

// Continues a running CRC: pass 0 first, then the previous result.
static unsigned crc_update(const unsigned *table, unsigned crc,
                           const unsigned char *p, unsigned n) {
  crc = ~crc;
  while (n--)
    crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
  return ~crc;
}

static unsigned crc32_bitwise(const unsigned char *p, unsigned n) {
  unsigned crc = 0xffffffffu;
  for (unsigned i = 0; i < n; i++) {
    crc ^= p[i];
    for (int k = 0; k < 8; k++)
      crc = crc & 1 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
  }
  return ~crc;
}

static unsigned short crc16_ccitt(const unsigned char *p, unsigned n) {
  unsigned short crc = 0xffff;
  for (unsigned i = 0; i < n; i++) {
    crc ^= (unsigned short)(p[i] << 8);
    for (int k = 0; k < 8; k++)
      crc = (unsigned short)(crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1);
  }
  return crc;
}

static unsigned adler32(const unsigned char *p, unsigned n) {
  unsigned a = 1, b = 0;
  for (unsigned i = 0; i < n; i++) {
    a = (a + p[i]) % 65521;
    b = (b + a) % 65521;
  }
  return b << 16 | a;
}

static unsigned length(const char *s) {
  unsigned n = 0;
  while (s[n])
    n++;
  return n;
}

static char line[128];
static int len;

static void add_hex(unsigned v, int digits) {
  for (int i = digits - 1; i >= 0; i--)
    line[len++] = "0123456789abcdef"[(v >> (4 * i)) & 15];
  line[len++] = ' ';
}

static const char *const inputs[] = {
    "123456789", "", "a", "abc", "Wikipedia",
    "The quick brown fox jumps over the lazy dog"};

int main(void) {
  make_table(crc32_table, 0xedb88320u);
  make_table(crc32c_table, 0x82f63b78u);
  MSIL_EXPECT(crc32_table[1] == 0x77073096u && crc32_table[255] == 0x2d02ef8du);

  put_str("crc32    crc32c   crc16 adler32  input");
  for (int i = 0; i < (int)(sizeof(inputs) / sizeof(inputs[0])); i++) {
    const unsigned char *p = (const unsigned char *)inputs[i];
    unsigned n = length(inputs[i]);
    unsigned crc = crc_update(crc32_table, 0, p, n);
    MSIL_EXPECT(crc == crc32_bitwise(p, n));
    add_hex(crc, 8);
    add_hex(crc_update(crc32c_table, 0, p, n), 8);
    add_hex(crc16_ccitt(p, n), 4);
    line[len++] = ' ';
    add_hex(adler32(p, n), 8);
    line[len++] = '"';
    for (unsigned k = 0; k < n && len < 120; k++)
      line[len++] = (char)p[k];
    line[len++] = '"';
    line[len] = 0;
    puts(line);
    len = 0;
  }
  const unsigned char *check = (const unsigned char *)inputs[0];
  MSIL_EXPECT(crc_update(crc32_table, 0, check, 9) == 0xcbf43926u);
  MSIL_EXPECT(crc_update(crc32c_table, 0, check, 9) == 0xe3069283u);
  MSIL_EXPECT(crc16_ccitt(check, 9) == 0x29b1);
  MSIL_EXPECT(adler32((const unsigned char *)inputs[4], 9) == 0x11e60398u);
  MSIL_EXPECT(crc_update(crc32_table, 0, check, 0) == 0);

  unsigned x = seed;
  for (int i = 0; i < SIZE; i++) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    buffer[i] = (unsigned char)(x >> 24);
  }
  unsigned whole = crc_update(crc32_table, 0, buffer, SIZE);
  MSIL_EXPECT(whole == crc32_bitwise(buffer, SIZE));
  unsigned running = 0, running_c = 0;
  int chunks = 0;
  for (unsigned pos = 0, step = 1; pos < SIZE; step = step * 7 % 61 + 1) {
    unsigned n = SIZE - pos < step ? SIZE - pos : step;
    running = crc_update(crc32_table, running, buffer + pos, n);
    running_c = crc_update(crc32c_table, running_c, buffer + pos, n);
    pos += n;
    chunks++;
  }
  MSIL_EXPECT(running == whole);
  MSIL_EXPECT(running_c == crc_update(crc32c_table, 0, buffer, SIZE));
  put_kv("chunks", chunks);

  put_str("buffer crc32, crc32c, adler32");
  put_hex(whole);
  put_hex(running_c);
  put_hex(adler32(buffer, SIZE));

  // The message followed by its CRC in little-endian order always has the
  // same CRC (the residue); one flipped bit changes the CRC.
  for (int i = 0; i < 4; i++)
    buffer[SIZE + i] = (unsigned char)(whole >> (8 * i));
  unsigned residue = crc_update(crc32_table, 0, buffer, SIZE + 4);
  MSIL_EXPECT(residue == 0x2144df1cu);
  put_hex(residue);
  buffer[100] ^= 0x10;
  unsigned flipped = crc_update(crc32_table, 0, buffer, SIZE);
  MSIL_EXPECT(flipped != whole);
  put_hex(flipped ^ whole);
  return (int)(whole & 0xff);
}
