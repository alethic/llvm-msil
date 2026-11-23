// Jumps past the declaration of a .NET reference (ImplementationPlan.md 5.21.4
// RS28 and 5.21.14 RV7, milestone R-a). A reference starts as NULL because
// CodeGen stores NULL at its declaration (RS2, RG3); a jump into its scope
// would skip that store, and the optimizer would then fold the unset value
// away. So a 'goto', a 'switch' case label, a computed 'goto' and an
// 'asm goto' that enter the scope of a reference are clang's jump errors, with
// the note T-R23 for each reference bypassed (beside clang's own notes, such
// as a VLA's or a computed goto's 'possible target'). Jumps out of a scope,
// backward jumps (the declaration stores NULL again), jumps over a scope that
// is not entered, a switch whose reference is declared before it or inside a
// braced case, and a computed goto whose targets all lie in the reference's
// scope are accepted. So are a jump out of a reference's initializer to a
// label after the declaration, in its scope (a 'goto' or a computed 'goto' in
// a GNU statement expression, also inside one arm of '?:'), and the
// self-initialization 'String ^s = s;': such a jump skips no NULL store,
// because the declaration stores NULL before its initializer runs (RS2 and
// RG3 as amended by section 12's row of 2026-11-21; CodeGen/MSIL/ref-ir.c
// checks the store), so the variable reads NULL there. Every text is pinned
// whole ('{{^}}' text '{{$}}').

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
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c23 -fsyntax-only -verify %s

#define CLI_TYPE(N) __attribute__((cli_type(N)))
#define NULL ((void *)0)

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE("System.Object") Object Object;

//===----------------------------------------------------------------------===//
// Refused: jumps into the scope of a reference.
//===----------------------------------------------------------------------===//

static int goto_initialized(String ^a, int c) {
  if (c)
    goto L;                 // expected-error-re {{{{^}}cannot jump from this goto statement to its label{{$}}}}
  String ^r = a;            // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'r'{{$}}}}
L:
  return 0;
}

static int goto_uninitialized(int c) {
  if (c)
    goto L;                 // expected-error-re {{{{^}}cannot jump from this goto statement to its label{{$}}}}
  Object ^o;                // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'o'{{$}}}}
L:
  return 0;
}

static int goto_two(String ^a, int c) {
  if (c)
    goto L;                 // expected-error-re {{{{^}}cannot jump from this goto statement to its label{{$}}}}
  String ^r = a;            // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'r'{{$}}}}
  Object ^o;                // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'o'{{$}}}}
L:
  return 0;
}

static int goto_with_vla(String ^a, int c, int n) {
  if (c)
    goto L;                 // expected-error-re {{{{^}}cannot jump from this goto statement to its label{{$}}}}
  int v[n];                 // expected-note-re {{{{^}}jump bypasses initialization of variable length array{{$}}}}
  String ^r = a;            // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'r'{{$}}}}
L:
  return 0;
}

static int goto_into_block(String ^a) {
  goto In;                  // expected-error-re {{{{^}}cannot jump from this goto statement to its label{{$}}}}
  {
    String ^q = a;          // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'q'{{$}}}}
  In:
    return q == NULL;
  }
}

static int switch_case(String ^a, int c) {
  switch (c) {
  case 0:;
    String ^r = a;          // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'r'{{$}}}}
    break;
  case 1:                   // expected-error-re {{{{^}}cannot jump from switch statement to this case label{{$}}}}
    return 1;
  }
  return 0;
}

static int switch_default(int c) {
  switch (c) {
  case 0:;
    Object ^o;              // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'o'{{$}}}}
    (void)o;
  default:                  // expected-error-re {{{{^}}cannot jump from switch statement to this case label{{$}}}}
    return 1;
  }
}

static int computed_goto(String ^a, int c) {
  void *t = c ? &&M : &&N;
  goto *t;                  // expected-error-re {{{{^}}cannot jump from this indirect goto statement to one of its possible targets{{$}}}}
N:
  return 1;
  String ^r = a;            // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'r'{{$}}}}
M:                          // expected-note-re {{{{^}}possible target of indirect goto statement{{$}}}}
  return 0;
}

static int asm_goto(String ^a) {
  __asm__ goto("" :::: Target); // expected-error-re {{{{^}}cannot jump from this asm goto statement to one of its possible targets{{$}}}}
  String ^r = a;            // expected-note-re {{{{^}}jump bypasses the initialization of the .NET reference 'r'{{$}}}}
Target:                     // expected-note-re {{{{^}}possible target of asm goto statement{{$}}}}
  return 0;
}

//===----------------------------------------------------------------------===//
// Accepted (no diagnostics).
//===----------------------------------------------------------------------===//

static int jump_out(String ^a, int c) {
  String ^r = a;
  {
    String ^q = r;
    if (c)
      goto Out;
    r = q;
  }
Out:
  return r == NULL;
}

static int jump_back(String ^a, int c) {
Again:;
  String ^r = a;
  if (c-- > 0)
    goto Again;
  return r == NULL;
}

static int jump_over_scope(String ^a, int c) {
  if (c)
    goto L;
  {
    String ^q = a;
    (void)q;
  }
L:
  return 0;
}

static int switch_before(String ^a, int c) {
  String ^r = a;
  switch (c) {
  case 0: {
    String ^q = r;
    (void)q;
    break;
  }
  case 1:
    return r == NULL;
  default:
    break;
  }
  return 0;
}

static int computed_goto_in_scope(String ^a, int c) {
  String ^r = a;
  void *t = c ? &&M : &&N;
  goto *t;
N:
  return 1;
M:
  return r == NULL;
}

static int asm_goto_in_scope(String ^a) {
  String ^r = a;
  __asm__ goto("" :::: Done);
  return 1;
Done:
  return r == NULL;
}

// Jumps out of the initializer to a label after the declaration, and the
// self-initialization: the declaration has stored NULL before the
// initializer runs.
static int goto_out_of_initializer(String ^a, int c) {
  String ^s = ({
    if (c)
      goto Out;
    a;
  });
Out:
  return s == NULL;
}

static int computed_goto_out_of_initializer(String ^a, int c) {
  void *t = &&Out;
  String ^s = ({
    if (c)
      goto *t;
    a;
  });
Out:
  return s == NULL;
}

static int goto_out_of_conditional_arm(String ^a, int c) {
  String ^s = c ? ({
    goto Out;
    a;
  })
                : a;
Out:
  return s == NULL;
}

static int self_initialization(String ^a, int c) {
  String ^s = s;
  if (c)
    s = a;
  return s == NULL;
}
