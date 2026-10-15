// A C constructor calling through function pointers in a module without data
// (Milestone 9, D12): <Module>::.cctor exists here only for the constructor
// and the function address fields, and it must set every address field
// (ldftn, stsfld '<msil>fn_<name>') before it calls the constructor, or the
// constructor's calli goes through a null pointer (an access violation). The
// module has no global variable and no string literal (it does not include
// msil_test.h, as in 07-globals/ctor-results.c, and putchar takes
// characters), so there is no '<msil>data' field and no data-segment setup:
// the address field stores are the only code before the constructor call.
// The constructor calls a static function of the module and putchar (a
// Phase-1 import, D14, whose address field holds an ldftn of its pinvokeimpl
// declaration) through volatile local pointers, and passes a pointer to
// another function; main does the same afterwards. The output 'ABC' from the
// constructor before 'DE' from main shows that the constructor ran first;
// main returns 42, computed through a pointer.
// RUN: %{msil_c}

int putchar(int c);

static int next(int c) { return c + 1; }

static int same(int c) { return c; }

__attribute__((noinline)) static int call_with(int (*f)(int), int c) {
  return f(c);
}

__attribute__((constructor)) static void init(void) {
  int (*volatile step)(int) = next;
  int (*volatile out)(int) = putchar;
  out(step('@'));
  putchar(call_with(step, 'A'));
  out(call_with(same, 'C'));
}

int main(void) {
  int (*volatile out)(int) = putchar;
  int (*volatile step)(int) = next;
  out(step('C'));
  out(call_with(out, 'E') == 'E' ? '\n' : '?');
  return step(41);
}
