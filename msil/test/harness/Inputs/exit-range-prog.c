// The source exit-range.test hands to update-expected.py, shaped like an
// execution test. The fake toolchain there (Inputs/fake-native-*.il) ignores
// it, so the native result is whatever that fake program returns.
// RUN: %{msil_c}
int main(void) { return 300; }
