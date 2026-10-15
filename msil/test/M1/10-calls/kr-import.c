// Imports declared without a prototype (Milestone 9): 'int putchar();' and
// 'int puts();' are undefined in the module, so clang emits them as
// prototype-less declarations (the "no-prototype" attribute, M1.d) and
// MSILAddMissingPrototypes gives each one the signature of its call sites
// before the P/Invoke declaration is written (D14). On every MSIL triple,
// the linux one included, clang gives a call through a prototype-less
// declaration a fixed-arity type of exactly the promoted arguments (it skips
// X86_64TargetCodeGenInfo::isNoProtoCallVariadic, the native SysV x86-64 %al
// convention, for MSIL), so the P/Invoke is not variadic on any triple.
// Covered: putchar with int and promoted char arguments, puts with a string
// literal and a local buffer, both results used, and output interleaved in
// program order. This file does not include msil_test.h, whose puts has a
// prototype. The exit status is the number of failed checks.
// RUN: %{msil_c}

#pragma clang diagnostic ignored "-Wdeprecated-non-prototype"

int putchar();
int puts();

volatile int zero = 0;

int main(void) {
  int failed = 0;
  char word[6];
  word[0] = 'k';
  word[1] = (char)('&' + zero);
  word[2] = 'r';
  word[3] = 0;
  failed += puts("prototype-less puts") < 0;
  failed += putchar('<') != '<';
  char c = (char)('A' + zero);
  for (int i = 0; i < 5; i++)
    failed += putchar(c + i) != 'A' + i;
  failed += putchar('>') != '>';
  failed += putchar('\n') != '\n';
  failed += puts(word) < 0;
  short s = (short)(zero + '!');
  failed += putchar(s) != '!';
  failed += putchar(10) != 10;
  return failed;
}
