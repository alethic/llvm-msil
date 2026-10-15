// Pointers to imported functions (Milestone 9, recon cil-runtime e18): the
// address of a P/Invoke (puts, putchar) is its address field, which
// <Module>::.cctor sets with an ldftn of its pinvokeimpl declaration, and a
// call through it is a managed calli that the runtime routes through the
// P/Invoke stub. Covered: the global
// 'int (*const out[])(const char *) = {puts};' whose element <Module>::.cctor
// writes from that field, called through the table; a writable global pointer
// to putchar; a table mixing puts with a function of the module and a null
// entry; pointers to puts and putchar taken in code (a load of the field in a
// method body) and held in volatile locals; a pointer to an import passed as a
// callback and returned from a function; identity of the import's address taken
// in code and in data; and putchar's result through a pointer. Output of puts
// and putchar through pointers interleaves in program order (one C stdout
// stream). MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

int putchar(int c);

volatile int zero = 0;

static int shout(const char *s) {
  putchar('!');
  putchar(' ');
  return puts(s);
}

int (*const out[])(const char *) = {puts};
int (*out_char)(int) = putchar;
static int (*const writers[])(const char *) = {shout, puts, 0};

NOINLINE int apply_out(int (*f)(const char *), const char *s) { return f(s); }

NOINLINE int (*pick_out(int k))(const char *) { return k ? shout : puts; }

static int put_digits(int (*pc)(int), unsigned v) {
  char buf[12];
  int n = 0;
  do {
    buf[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  while (n > 0)
    pc(buf[--n]);
  return pc('\n');
}

int main(void) {
  MSIL_EXPECT(out[zero]("through the const table") >= 0);

  for (int i = 0; writers[i + zero]; i++)
    writers[i + zero]("through the mixed table");

  int (*volatile p)(const char *) = puts;
  p("through a volatile local");
  int (*volatile pc)(int) = putchar;
  pc('o');
  pc('k');
  pc('\n');
  MSIL_EXPECT(pc('A' + zero) == 'A' && pc('\n') == '\n');
  MSIL_EXPECT(pc(0x100 + 'B' + zero) == 'B');
  pc('\n');

  out_char('d');
  out_char('a');
  out_char('t');
  out_char('a');
  out_char('\n');
  MSIL_EXPECT(put_digits(out_char, 4294967295u) == '\n');
  MSIL_EXPECT(put_digits(pc, 1234567u + zero) == '\n');

  MSIL_EXPECT(apply_out(puts, "puts as a callback") >= 0);
  MSIL_EXPECT(apply_out(out[zero], "puts from the table as a callback") >= 0);
  pick_out(zero)("returned puts");
  pick_out(zero + 1)("returned shout");

  // Interleaving: putchar through one pointer, puts through another.
  out_char('[');
  out[zero]("inside");
  pc(']');
  pc('\n');

  // One import, one address, wherever it was taken.
  MSIL_EXPECT(out[zero] == puts && p == puts && p == out[zero]);
  MSIL_EXPECT(writers[zero + 1] == puts && pick_out(zero) == puts);
  MSIL_EXPECT(out_char == putchar && pc == out_char);
  MSIL_EXPECT(writers[zero] != puts && writers[zero + 2] == 0);
  MSIL_EXPECT(out[zero] != 0 && out_char != 0 && pc != 0);
  return 0;
}
