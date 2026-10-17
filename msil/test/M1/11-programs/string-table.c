// A string table with relocations (Design.md section 12): 'keywords' is a
// writable global 'const char *[]' holding the 32 C89 keywords in scrambled
// order, so each element is a relocation to a string literal that
// <Module>::.cctor applies. It is Shell-sorted with a strcmp written in C
// (only the pointers move), and a binary search over it classifies the
// identifiers that a small lexer cuts from a C fragment held in a global
// char array; two-character punctuators come from a 'const char *const'
// table, and the class names are pointers into one char array (relocations
// with addends). Prints the sorted table, every token with its class and the
// count of each class. MSIL_EXPECT returns 1 + (line % 255) at the first
// failed check; on success the exit status is the sum of the sorted-table
// indexes of the keywords found, modulo 256 (re-check that it is not
// 1 + (line % 255) for an MSIL_EXPECT line after edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

const char *keywords[] = {
    "while",  "auto",   "volatile", "break",  "void",     "case",  "unsigned",
    "char",   "union",  "const",    "typedef", "continue", "switch", "default",
    "struct", "do",     "static",   "double", "sizeof",   "else",  "signed",
    "enum",   "short",  "extern",   "return", "float",    "register", "for",
    "long",   "goto",   "int",      "if"};

static const char class_text[] = "keyword\0identifier\0number\0punctuator";
static const char *const class_names[] = {class_text, class_text + 8,
                                          class_text + 19, class_text + 26};
enum { KEYWORD, IDENTIFIER, NUMBER, PUNCTUATOR, CLASSES };

static const char *const two_char[] = {"->", "==", "!=", "<=", ">=", "&&",
                                       "||", "++", "--", "+=", "-="};

char source[] =
    "typedef struct node { struct node *next; unsigned long key; } node;\n"
    "static int count(const node *n, unsigned long key) {\n"
    "  int found = 0;\n"
    "  for (; n != 0; n = n->next)\n"
    "    if (n->key == key && found <= 100)\n"
    "      found += 2;\n"
    "  return found;\n"
    "}\n";

static int compare(const char *a, const char *b) {
  while (*a != '\0' && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

static void shell_sort(const char **v, int n) {
  static const int gaps[] = {13, 4, 1};
  for (int g = 0; g < COUNT(gaps); g++) {
    int gap = gaps[g];
    for (int i = gap; i < n; i++) {
      const char *x = v[i];
      int j = i;
      while (j >= gap && compare(v[j - gap], x) > 0) {
        v[j] = v[j - gap];
        j -= gap;
      }
      v[j] = x;
    }
  }
}

static int lookup(const char *word) { // index in keywords[], or -1
  int lo = 0, hi = COUNT(keywords) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = compare(word, keywords[mid]);
    if (c == 0)
      return mid;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return -1;
}

static int is_alpha(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_digit(int c) { return c >= '0' && c <= '9'; }

static char line[96];
static int len;

static void add_str(const char *s) {
  while (*s && len < 90)
    line[len++] = *s++;
}

static void flush(void) {
  line[len] = 0;
  puts(line);
  len = 0;
}

int main(void) {
  shell_sort(keywords, COUNT(keywords));
  put_str("sorted keywords");
  for (int i = 0; i < COUNT(keywords); i++) {
    MSIL_EXPECT(i == 0 || compare(keywords[i - 1], keywords[i]) < 0);
    MSIL_EXPECT(lookup(keywords[i]) == i);
    add_str(i % 8 ? " " : "");
    add_str(keywords[i]);
    if (i % 8 == 7)
      flush();
  }
  MSIL_EXPECT(lookup("main") < 0 && lookup("Int") < 0 && lookup("") < 0);
  MSIL_EXPECT(lookup("whilex") < 0 && lookup("a") < 0 && lookup("zz") < 0);

  put_str("tokens");
  int counts[CLASSES] = {0};
  unsigned keyword_sum = 0;
  const char *p = source;
  while (*p) {
    if (*p == ' ' || *p == '\n') {
      p++;
      continue;
    }
    char tok[32];
    int n = 0, cls;
    if (is_alpha(*p)) {
      while ((is_alpha(*p) || is_digit(*p)) && n < 31)
        tok[n++] = *p++;
      tok[n] = 0;
      int k = lookup(tok);
      cls = k >= 0 ? KEYWORD : IDENTIFIER;
      if (k >= 0)
        keyword_sum += (unsigned)k;
    } else if (is_digit(*p)) {
      while (is_digit(*p) && n < 31)
        tok[n++] = *p++;
      tok[n] = 0;
      cls = NUMBER;
    } else {
      cls = PUNCTUATOR;
      tok[n++] = *p++;
      for (int i = 0; i < COUNT(two_char); i++)
        if (two_char[i][0] == tok[0] && two_char[i][1] == *p) {
          tok[n++] = *p++;
          break;
        }
      tok[n] = 0;
    }
    counts[cls]++;
    add_str(class_names[cls]);
    add_str(" ");
    add_str(tok);
    flush();
  }

  put_str("counts");
  for (int c = 0; c < CLASSES; c++)
    put_kv(class_names[c], counts[c]);
  MSIL_EXPECT(counts[KEYWORD] == 14 && counts[NUMBER] == 4);
  put_kv("keyword index sum", keyword_sum);
  return (int)(keyword_sum & 0xff);
}
