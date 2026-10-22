// Undefined weak references in a whole program (Milestone 12, backlog 2.1,
// plan 5.9.6 and the section 12 row 'D26 amended (whole-program mode, ...)'):
// an extern_weak function that the import policy does not resolve and an
// extern_weak variable are null, as a native link without a definition makes
// them (link.exe, lld-link and ld.lld alike); MSILPrepareIR replaces every
// use of their addresses by null, and a direct call of the function goes to
// the internal trap stub '<msil>undef_maybe', which this program never
// reaches. Before M12 the function was 'undefined function ... is not a
// Phase-1 import' and the variable 'external global variable'. Covered: the
// function tested by 'if (maybe)', the variable by '&weak_data == 0', both in
// static initializers (a function pointer, a data pointer and a struct
// holding both), both loaded through volatile pointers so that -O2 cannot
// fold the test, and a call that only happens when the function exists.
// Results are printed; MSIL_EXPECT returns 1 + (line % 255) at the first
// failed check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

extern int maybe(int) __attribute__((weak));
extern int weak_data __attribute__((weak));

int (*fp_init)(int) = maybe;
int *dp_init = &weak_data;

struct refs {
  int tag;
  int (*fn)(int);
  int *data;
};
struct refs refs_init = {7, maybe, &weak_data};

int main(void) {
  int (*volatile vp)(int) = maybe;
  int *volatile vd = &weak_data;

  int called = 0;
  if (maybe) {
    puts("maybe is defined");
    called = maybe(1);
  }
  put_kv("maybe called", called);
  MSIL_EXPECT(called == 0);

  put_kv("maybe is null", maybe == 0);
  put_kv("&weak_data is null", &weak_data == 0);
  put_kv("fp_init is null", fp_init == 0);
  put_kv("dp_init is null", dp_init == 0);
  put_kv("refs_init.fn is null", refs_init.fn == 0);
  put_kv("refs_init.data is null", refs_init.data == 0);
  put_kv("refs_init.tag", refs_init.tag);
  put_kv("volatile fn is null", vp == 0);
  put_kv("volatile data is null", vd == 0);
  MSIL_EXPECT(!maybe && &weak_data == 0);
  MSIL_EXPECT(fp_init == 0 && dp_init == 0);
  MSIL_EXPECT(refs_init.fn == 0 && refs_init.data == 0 && refs_init.tag == 7);
  MSIL_EXPECT(vp == 0 && vd == 0);

  // A call through the volatile pointer only when it is set.
  int (*f)(int) = vp;
  int r = f ? f(2) : -1;
  put_kv("through volatile", r);
  MSIL_EXPECT(r == -1);
  return 0;
}
