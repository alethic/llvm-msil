// Operations on .NET references in C (ImplementationPlan.md 5.21.4 RS1-RS9,
// RS14, RS27 and 5.21.14 RV4, milestone R-a). A reference is neither scalar
// nor sized (RC3); the rules below let in exactly the first slice's
// operations, and everything else is a hard error.
// - Allowed, with no diagnostic at all (the ok_* functions): automatic
//   variables, 'register' ones and those of nested scopes and 'for'
//   statements (RS1); NULL at start, so a 'const' reference without an
//   initializer gets no const-initialization warning while 'const int' does
//   (RS2); conversions (RS4): the same class in any spelling, every null
//   pointer constant (NULL, 0, (void *)0, '\0', 1 - 1, 0L, (void *)(1 - 1),
//   and C23's nullptr in the C23 runs) in initialization, assignment, an
//   argument and a return, and any reference to the Object reference;
//   '==' and '!=' (RS5) on the same class, on Object and a class in both
//   orders, and on each null form in both orders; truth tests (RS6) in 'if',
//   'while', 'for', 'do', '!', '&&', '||' and a '?:' condition; '?:' operands
//   (RS7): the same class (also two spellings), a reference and a null
//   constant in both orders, a class and Object (Object), GNU 'r ?: s'; casts
//   (RS8): to the same class, to another spelling of it, to Object, to
//   another class (checked at run time), a null constant to a reference, a
//   reference to void; RS9's uses: typedef, __typeof__ (and C23's typeof and
//   typeof_unqual), _Generic, __builtin_types_compatible_p, __auto_type
//   (and C23's auto), the comma operator, statement expressions, 'const'
//   after the caret; __builtin_classify_type gives -1 (no C type class).
// - Refused (each pinned whole): every other conversion (RS4, RS27:
//   err_typecheck_convert_incompatible in initialization, assignment,
//   arguments, with clang's note, and returns), '_Bool b = r;' (RS6), other
//   comparisons and every relational operator (RS5: 'invalid operands'), '?:'
//   of two classes or with a non-null operand (RS7: 'incompatible operand
//   types'), casts to a reference from anything but a reference or a null
//   constant (T-R22, RS8) and from a reference to a scalar
//   (err_typecheck_expect_scalar_operand) or to a struct, the arithmetic,
//   bitwise, shift and compound-assignment operators, unary '-', '+', '~',
//   '++', '--', '*r', 'r[i]', 'r->x', 'r.x', 'switch (r)' (clang's texts),
//   and RS14's expression forms: sizeof, _Alignof, __alignof__, compound
//   literals, braced initializers (C23's '{}' too) and __builtin_bit_cast.
// - BODY_ASSERT: a '_Static_assert' with a message inside a function body of
//   a unit that formed references compiles with no diagnostic.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c23 -fsyntax-only -DC23 -verify=expected,c23 %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c23 -fsyntax-only -DC23 -verify=expected,c23 %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c23 -fsyntax-only -DC23 -verify=expected,c23 %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c23 -Wdefault-const-init -fsyntax-only -DC23 -verify=expected,c23 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DBODY_ASSERT -verify=body %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c23 -fsyntax-only -DBODY_ASSERT -verify=body %s

#define CLI_TYPE(N) __attribute__((cli_type(N)))
#define NULL ((void *)0)

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE(" System.String ") Text Text; // another spelling of String
typedef struct CLI_TYPE("System.Object") Object Object;
typedef struct CLI_TYPE("System.IO.Stream") Stream Stream;
typedef struct CLI_TYPE("string[]") Strings Strings;
typedef String ^SRef;
struct Complete { int x; };

#if defined(BODY_ASSERT)

// body-no-diagnostics
static int b_assert(String ^s, Object ^o) {
  _Static_assert(1, "no reference in the condition");
  _Static_assert(__builtin_types_compatible_p(String ^, Text ^), "one class");
  _Static_assert(!__builtin_types_compatible_p(String ^, Object ^), "two classes");
  return s == o;
}
int b_use(void) { return b_assert(NULL, NULL); }

#else

static String ^take_string(String ^s) { return s; }   // expected-note 2 {{passing argument to parameter 's' here}}
static Object ^take_object(Object ^o) { return o; }
static int take_int(int i) { return i; }               // expected-note {{passing argument to parameter 'i' here}}
static void *take_ptr(void *p) { return p; }           // expected-note {{passing argument to parameter 'p' here}}

//===----------------------------------------------------------------------===//
// Allowed (no diagnostics).
//===----------------------------------------------------------------------===//

// RS1, RS2: automatic variables at every level, NULL at start, and no
// const-initialization warning for a reference.
static int ok_locals(String ^s) {
  String ^a;
  register String ^b;
  String ^const c;
  register String ^const d;
  {
    String ^e;
    { String ^f = s; a = f; }
    b = e;
  }
  for (String ^g = s; g; g = NULL)
    a = g;
  return (a == b) + (c == d);
}
static int contrast(void) {
  const int ci; // expected-warning-re {{{{^}}default initialization of an object of type 'const int' leaves the object uninitialized{{$}}}}
  return ci;
}

// RS4: conversions in initialization, assignment, arguments and returns.
static String ^ok_null_return1(void) { return NULL; }
static String ^ok_null_return2(void) { return 0; }
static String ^ok_null_return3(void) { return (void *)0; }
static Object ^ok_object_return(String ^s) { return s; }
static String ^ok_spelling_return(Text ^t) { return t; }
static void ok_conversions(String ^s, Text ^t, Object ^o, Stream ^st) {
  String ^n1 = NULL, ^n2 = 0, ^n3 = (void *)0, ^n4 = '\0', ^n5 = 1 - 1, ^n6 = 0L, ^n7 = (void *)(1 - 1);
  n1 = NULL;
  n2 = 0;
  n3 = (void *)0;
  n4 = '\0';
  n5 = 1 - 1;
  take_string(NULL);
  take_string(0);
  take_string((void *)0);
  take_string('\0');
  String ^same = t;
  same = s;
  t = s;
  take_string(t);
  Object ^up1 = s;
  Object ^up2 = st;
  Object ^up3 = (Strings ^)NULL;
  up1 = t;
  up2 = o;
  take_object(s);
  take_object(st);
  take_object(t);
  take_object(NULL);
  Object ^const up4 = s;
}

// RS5: '==' and '!='.
static int ok_compare(String ^s, Text ^t, Object ^o, Stream ^st) {
  int r = 0;
  r += s == t;
  r += t != s;
  r += o == s;
  r += s == o;
  r += st != o;
  r += o != st;
  r += s == NULL;
  r += NULL == s;
  r += s != NULL;
  r += s == 0;
  r += 0 != s;
  r += s == (void *)0;
  r += (void *)0 == s;
  r += s == '\0';
  r += o != 1 - 1;
  r += (s == t) == (o == NULL);
  return r;
}

// RS6: truth tests.
static int ok_truth(String ^s, Object ^o, int c) {
  int r = 0;
  if (s)
    r++;
  if (!s)
    r++;
  if (s && c)
    r++;
  while (o)
    break;
  for (; s;)
    break;
  do
    r++;
  while (!o && c);
  r += !s;
  r += !!o;
  r += s && o;
  r += s || o;
  r += !s || !o;
  r += s ? 1 : 2;
  r += (s && o) ? 3 : 4;
  return r;
}

// RS7: '?:' operands.
static Object ^ok_conditional(String ^s, Text ^t, Object ^o, Stream ^st, int c) {
  String ^a = c ? s : NULL;
  String ^b = c ? NULL : s;
  String ^d = c ? s : 0;
  String ^e = c ? s : t;
  Text ^f = c ? t : s;
  Object ^g = c ? s : o;
  Object ^h = c ? o : st;
  String ^i = s ?: NULL;
  String ^j = s ?: t;
  Object ^k = s ?: o;
  Object ^l = c ? (c ? s : NULL) : o;
  // The type of a class and Object is Object; of two spellings, the class.
  (void)sizeof(char[_Generic(c ? s : o, Object ^: 1, default: -1)]);
  (void)sizeof(char[_Generic(c ? o : st, Object ^: 1, default: -1)]);
  (void)sizeof(char[_Generic(c ? s : t, String ^: 1, default: -1)]);
  (void)sizeof(char[_Generic(c ? s : NULL, String ^: 1, default: -1)]);
  (void)a; (void)b; (void)d; (void)e; (void)f; (void)i; (void)j; (void)k;
  return c > 1 ? g : (c ? h : l);
}

// RS8: casts.
static void ok_casts(String ^s, Text ^t, Object ^o, Stream ^st) {
  String ^a = (String ^)o;
  String ^b = (String ^)s;
  String ^d = (String ^)t;
  Text ^e = (Text ^)s;
  Object ^f = (Object ^)s;
  Object ^g = (Object ^)o;
  Stream ^h = (Stream ^)o;
  String ^i = (String ^)st;
  Strings ^j = (Strings ^)o;
  String ^k = (String ^)NULL;
  String ^l = (String ^)0;
  String ^m = (String ^)'\0';
  String ^n = (String ^)(void *)0;
  SRef p = (SRef)o;
  (void)s;
  (void)(String ^)o;
  (void)a; (void)b; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k; (void)l; (void)m; (void)n; (void)p;
}

// RS9: typedef, typeof, _Generic, __builtin_types_compatible_p, __auto_type,
// the comma operator, statement expressions and 'const' after the caret.
static int ok_rs9(String ^s, Text ^t, Object ^o, int c) {
  SRef a = s;
  __typeof__(s) b = t;
  __typeof__(String ^const) d = s;
  __auto_type e = s;
  __auto_type f = c ? s : t;
  int g = _Generic(s, Text ^: 1, Object ^: 2, default: 3);
  int h = _Generic(o, String ^: 1, Object ^: 2, default: 3);
  int i = _Generic((Object ^)s, String ^: 1, Object ^: 2, default: 3);
  int j = __builtin_types_compatible_p(__typeof__(s), Text ^);
  int k = __builtin_types_compatible_p(__typeof__(s), Object ^);
  String ^l = (take_int(c), s);
  String ^m = ({ String ^q = t; q; });
  String ^const n = s;
#if defined(C23)
  typeof(s) p = t;
  typeof_unqual(n) q = s;
  auto r2 = s;
  q = r2;
  (void)p;
#endif
  return (a == b) + (d == e) + (f == l) + (m == n) + g + h + i + j + k;
}

// __builtin_classify_type (RC8): a reference is none of C's type classes
// (-1, GCC's no_type_class), unlike a pointer (5).
_Static_assert(__builtin_classify_type((String ^)0) == -1, "no C type class");
_Static_assert(__builtin_classify_type((Object ^)NULL) == -1, "no C type class");
_Static_assert(__builtin_classify_type((void *)0) == 5, "a pointer");
static int ok_classify(String ^s) { return __builtin_classify_type(s); }

#if defined(C23)
// C23's nullptr is a null pointer constant like the others.
static String ^ok_nullptr(String ^s) {
  String ^a = nullptr;
  a = nullptr;
  take_string(nullptr);
  if (s == nullptr || nullptr != a)
    return nullptr;
  return (String ^)nullptr;
}
#endif

//===----------------------------------------------------------------------===//
// RS4, RS27: other conversions.
//===----------------------------------------------------------------------===//

static String ^bad_return1(Object ^o) { return o; } // expected-error-re {{{{^}}returning 'Object ^' (aka 'System.Object ^') from a function with incompatible result type 'String ^' (aka 'System.String ^'){{$}}}}
static int bad_return2(String ^s) { return s; }     // expected-error-re {{{{^}}returning 'String ^' (aka 'System.String ^') from a function with incompatible result type 'int'{{$}}}}
static void *bad_return3(String ^s) { return s; }   // expected-error-re {{{{^}}returning 'String ^' (aka 'System.String ^') from a function with incompatible result type 'void *'{{$}}}}
static String ^bad_return4(void) { return 1; }      // expected-error-re {{{{^}}returning 'int' from a function with incompatible result type 'String ^' (aka 'System.String ^'){{$}}}}
static void bad_conversions(String ^s, Object ^o, Stream ^st, void *vp, int c) {
  String ^e1 = o;          // expected-error-re {{{{^}}initializing 'String ^' (aka 'System.String ^') with an expression of incompatible type 'Object ^' (aka 'System.Object ^'){{$}}}}
  String ^e2 = st;         // expected-error-re {{{{^}}initializing 'String ^' (aka 'System.String ^') with an expression of incompatible type 'Stream ^' (aka 'System.IO.Stream ^'){{$}}}}
  String ^e3 = vp;         // expected-error-re {{{{^}}initializing 'String ^' (aka 'System.String ^') with an expression of incompatible type 'void *'{{$}}}}
  String ^e4 = 1;          // expected-error-re {{{{^}}initializing 'String ^' (aka 'System.String ^') with an expression of incompatible type 'int'{{$}}}}
  String ^e5 = c;          // expected-error-re {{{{^}}initializing 'String ^' (aka 'System.String ^') with an expression of incompatible type 'int'{{$}}}}
  String ^e6 = (char *)0;  // expected-error-re {{{{^}}initializing 'String ^' (aka 'System.String ^') with an expression of incompatible type 'char *'{{$}}}}
  String ^e7 = 1.0;        // expected-error-re {{{{^}}initializing 'String ^' (aka 'System.String ^') with an expression of incompatible type 'double'{{$}}}}
  Strings ^e8 = s;         // expected-error-re {{{{^}}initializing 'Strings ^' (aka 'string[] ^') with an expression of incompatible type 'String ^' (aka 'System.String ^'){{$}}}}
  void *e9 = s;            // expected-error-re {{{{^}}initializing 'void *' with an expression of incompatible type 'String ^' (aka 'System.String ^'){{$}}}}
  int e10 = s;             // expected-error-re {{{{^}}initializing 'int' with an expression of incompatible type 'String ^' (aka 'System.String ^'){{$}}}}
  _Bool e11 = s;           // expected-error-re {{{{^}}initializing '{{_Bool|bool}}' with an expression of incompatible type 'String ^' (aka 'System.String ^'){{$}}}}
  long long e12 = o;       // expected-error-re {{{{^}}initializing 'long long' with an expression of incompatible type 'Object ^' (aka 'System.Object ^'){{$}}}}
  s = o;                   // expected-error-re {{{{^}}assigning to 'String ^' (aka 'System.String ^') from incompatible type 'Object ^' (aka 'System.Object ^'){{$}}}}
  s = vp;                  // expected-error-re {{{{^}}assigning to 'String ^' (aka 'System.String ^') from incompatible type 'void *'{{$}}}}
  vp = s;                  // expected-error-re {{{{^}}assigning to 'void *' from incompatible type 'String ^' (aka 'System.String ^'){{$}}}}
  c = s;                   // expected-error-re {{{{^}}assigning to 'int' from incompatible type 'String ^' (aka 'System.String ^'){{$}}}}
  take_string(o);          // expected-error-re {{{{^}}passing 'Object ^' (aka 'System.Object ^') to parameter of incompatible type 'String ^' (aka 'System.String ^'){{$}}}}
  take_string(vp);         // expected-error-re {{{{^}}passing 'void *' to parameter of incompatible type 'String ^' (aka 'System.String ^'){{$}}}}
  take_int(s);             // expected-error-re {{{{^}}passing 'String ^' (aka 'System.String ^') to parameter of incompatible type 'int'{{$}}}}
  take_ptr(s);             // expected-error-re {{{{^}}passing 'String ^' (aka 'System.String ^') to parameter of incompatible type 'void *'{{$}}}}
}

//===----------------------------------------------------------------------===//
// RS5, RS7: other comparisons and '?:' operands.
//===----------------------------------------------------------------------===//

static int bad_compare(String ^s, Text ^t, Object ^o, Stream ^st, void *vp, int c) {
  int r = 0;
  r += s == st;            // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'Stream ^' (aka 'System.IO.Stream ^')){{$}}}}
  r += s == vp;            // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'void *'){{$}}}}
  r += s != c;             // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r += s == 1;             // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r += s == (char *)0;     // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'char *'){{$}}}}
  r += s < t;              // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'Text ^' (aka 'System.String ^')){{$}}}}
  r += s > NULL;           // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'void *'){{$}}}}
  r += o <= s;             // expected-error-re {{{{^}}invalid operands to binary expression ('Object ^' (aka 'System.Object ^') and 'String ^' (aka 'System.String ^')){{$}}}}
  r += st >= o;            // expected-error-re {{{{^}}invalid operands to binary expression ('Stream ^' (aka 'System.IO.Stream ^') and 'Object ^' (aka 'System.Object ^')){{$}}}}
  String ^a = c ? s : st;  // expected-error-re {{{{^}}incompatible operand types ('String ^' (aka 'System.String ^') and 'Stream ^' (aka 'System.IO.Stream ^')){{$}}}}
  String ^b = c ? s : 1;   // expected-error-re {{{{^}}incompatible operand types ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  String ^d = c ? vp : s;  // expected-error-re {{{{^}}incompatible operand types ('void *' and 'String ^' (aka 'System.String ^')){{$}}}}
  String ^e = c ? s : (char *)0; // expected-error-re {{{{^}}incompatible operand types ('String ^' (aka 'System.String ^') and 'char *'){{$}}}}
  String ^f = c ? s : o;   // expected-error-re {{{{^}}initializing 'String ^' (aka 'System.String ^') with an expression of incompatible type 'Object ^' (aka 'System.Object ^'){{$}}}}
  return r;
}

//===----------------------------------------------------------------------===//
// RS8: other casts.
//===----------------------------------------------------------------------===//

static void bad_casts(String ^s, void *vp, int c) {
  String ^a = (String ^)vp;        // expected-error-re {{{{^}}converting 'void *' to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL; only a .NET reference or a null pointer constant converts to it{{$}}}}
  String ^b = (String ^)c;         // expected-error-re {{{{^}}converting 'int' to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL; only a .NET reference or a null pointer constant converts to it{{$}}}}
  String ^d = (String ^)1;         // expected-error-re {{{{^}}converting 'int' to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL; only a .NET reference or a null pointer constant converts to it{{$}}}}
  String ^e = (String ^)1.0;       // expected-error-re {{{{^}}converting 'double' to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL; only a .NET reference or a null pointer constant converts to it{{$}}}}
  String ^f = (String ^)(char *)0; // expected-error-re {{{{^}}converting 'char *' to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL; only a .NET reference or a null pointer constant converts to it{{$}}}}
  String ^g = (String ^)take_int;  // expected-error-re {{{{^}}converting 'int (*)(int)' to the .NET reference type 'String ^' (aka 'System.String ^') is not supported for MSIL; only a .NET reference or a null pointer constant converts to it{{$}}}}
  SRef h = (SRef)vp;               // expected-error-re {{{{^}}converting 'void *' to the .NET reference type 'SRef' (aka 'System.String ^') is not supported for MSIL; only a .NET reference or a null pointer constant converts to it{{$}}}}
  int i = (int)s;                  // expected-error-re {{{{^}}operand of type 'String ^' (aka 'System.String ^') where arithmetic or pointer type is required{{$}}}}
  void *j = (void *)s;             // expected-error-re {{{{^}}operand of type 'String ^' (aka 'System.String ^') where arithmetic or pointer type is required{{$}}}}
  _Bool k = (_Bool)s;              // expected-error-re {{{{^}}operand of type 'String ^' (aka 'System.String ^') where arithmetic or pointer type is required{{$}}}}
  long long l = (long long)s;      // expected-error-re {{{{^}}operand of type 'String ^' (aka 'System.String ^') where arithmetic or pointer type is required{{$}}}}
  double m = (double)s;            // expected-error-re {{{{^}}operand of type 'String ^' (aka 'System.String ^') where arithmetic or pointer type is required{{$}}}}
  struct Complete n = (struct Complete)s; // expected-error-re {{{{^}}used type 'struct Complete' where arithmetic or pointer type is required{{$}}}}
}

//===----------------------------------------------------------------------===//
// Refused by clang as it is (5.21.4, after RS30).
//===----------------------------------------------------------------------===//

static int bad_operators(String ^s, Text ^t) {
  int r = 0;
  r = s + 1;       // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r = s - s;       // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'String ^'){{$}}}}
  r = s * 2;       // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r = s / 1;       // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r = s % 2;       // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r = s & 1;       // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r = s | 1;       // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r = s ^ 1;       // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r = s << 1;      // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r = s >> 1;      // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  s += 1;          // expected-error-re {{{{^}}invalid operands to binary expression ('String ^' (aka 'System.String ^') and 'int'){{$}}}}
  r = -s;          // expected-error-re {{{{^}}invalid argument type 'String ^' (aka 'System.String ^') to unary expression{{$}}}}
  r = +s;          // expected-error-re {{{{^}}invalid argument type 'String ^' (aka 'System.String ^') to unary expression{{$}}}}
  r = ~s;          // expected-error-re {{{{^}}invalid argument type 'String ^' (aka 'System.String ^') to unary expression{{$}}}}
  s++;             // expected-error-re {{{{^}}cannot increment value of type 'String ^' (aka 'System.String ^'){{$}}}}
  ++t;             // expected-error-re {{{{^}}cannot increment value of type 'Text ^' (aka 'System.String ^'){{$}}}}
  s--;             // expected-error-re {{{{^}}cannot decrement value of type 'String ^' (aka 'System.String ^'){{$}}}}
  --s;             // expected-error-re {{{{^}}cannot decrement value of type 'String ^' (aka 'System.String ^'){{$}}}}
  r = *s;          // expected-error-re {{{{^}}indirection requires pointer operand ('String ^' (aka 'System.String ^') invalid){{$}}}}
  r = s[0];        // expected-error-re {{{{^}}subscripted value is not an array, pointer, or vector{{$}}}}
  r = s->x;        // expected-error-re {{{{^}}member reference type 'String ^' (aka 'System.String ^') is not a pointer{{$}}}}
  r = s.x;         // expected-error-re {{{{^}}member reference base type 'String ^' (aka 'System.String ^') is not a structure or union{{$}}}}
  switch (s) {     // expected-error-re {{{{^}}statement requires expression of integer type ('String ^' (aka 'System.String ^') invalid){{$}}}}
  default:
    break;
  }
  return r;
}

//===----------------------------------------------------------------------===//
// RS14: the expression forms of the sizeless rules.
//===----------------------------------------------------------------------===//

static int bad_sizeless(String ^s, __INTPTR_TYPE__ ip) {
  int r = 0;
  r += sizeof(s);                    // expected-error-re {{{{^}}invalid application of 'sizeof' to sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  r += sizeof(String ^);             // expected-error-re {{{{^}}invalid application of 'sizeof' to sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  r += _Alignof(String ^);           // expected-error-re {{{{^}}invalid application of 'alignof' to sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  r += __alignof__(s);               // expected-error-re {{{{^}}invalid application of '__alignof' to sizeless type 'String ^' (aka 'System.String ^'){{$}}}}
  String ^a = (String ^){0};         // expected-error-re {{{{^}}illegal initializer type 'String ^' (aka 'System.String ^'){{$}}}}
  String ^b = {0};                   // expected-error-re {{{{^}}illegal initializer type 'String ^' (aka 'System.String ^'){{$}}}}
  String ^d = {NULL};                // expected-error-re {{{{^}}illegal initializer type 'String ^' (aka 'System.String ^'){{$}}}}
#if defined(C23)
  String ^e = {};                    // c23-error-re {{{{^}}illegal initializer type 'String ^' (aka 'System.String ^'){{$}}}}
#endif
  // An integer of a pointer's size, so that no size mismatch comes first.
  __INTPTR_TYPE__ f = __builtin_bit_cast(__INTPTR_TYPE__, s); // expected-error-re {{{{^}}'__builtin_bit_cast' source type must be trivially copyable{{$}}}}
  String ^g = __builtin_bit_cast(String ^, ip);   // expected-error-re {{{{^}}'__builtin_bit_cast' destination type must be trivially copyable{{$}}}}
  return r;
}

#endif
