// A single-source test for ../../update-expected-m23.test (M23): its
// %{msil_copts} value has an unterminated quote, which update-expected.py
// reports instead of building natively without the flag (that build would
// return 3, not 7).
// RUN: %{msil_c}
// REDEFINE: %{msil_copts} = -DVAL="7
#ifndef VAL
#define VAL 3
#endif
int main(void) { return VAL; }
