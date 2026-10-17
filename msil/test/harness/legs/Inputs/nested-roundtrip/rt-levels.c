// The C input of ../../roundtrip-composites.test, run only by a nested lit
// with -a: a program that %{msil_c} builds, verifies and runs at every level,
// and with --param msil_roundtrip=1 also round-trips through ildasm and ilasm
// and runs again. The stand-in compiler copies rt-levels.il.in.
// RUN: %{msil_c}

static int add3(int x) { return x + 3; }

int main(void) { return add3(8); }
