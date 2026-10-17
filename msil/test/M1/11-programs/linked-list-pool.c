// Singly linked lists in a static node pool (Milestone 10 program corpus,
// Design.md section 12): a global array of nodes threaded onto a free list,
// allocation that reports exhaustion and reuse of freed nodes; push front,
// append through a pointer to the last next field, sorted insertion and
// removal of every node with a value through pointers to pointers, in-place
// reversal both iterative and recursive, merging of two sorted lists, a
// merge sort that splits with slow and fast pointers, removal of duplicates,
// Floyd's cycle detection on a list that is temporarily made circular, and a
// constant list linked by a global initializer (relocations to other globals
// in the data segment). Values come from a 32-bit linear congruential
// generator. Lists are printed; MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; when every check passes, the exit status is 64 plus a
// checksum of the printed lists folded into 7 bits, so 64..191 (D37), which
// keeps it apart from the checks, all on lines 192..317 (re-check that it is
// not 1 + (line % 255) for an MSIL_EXPECT line after edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

struct node {
  int value;
  struct node *next;
};

#define POOL 48
static struct node pool[POOL];
static struct node *free_list;
static int in_use, peak_use;

volatile int zero = 0;
static unsigned check;
static unsigned rng = 12345u;

// A list linked by the data-segment initializer.
static struct node c4 = {40, 0};
static struct node c3 = {30, &c4};
static struct node c2 = {20, &c3};
static struct node c1 = {10, &c2};

static int next_random(int range) {
  rng = rng * 1103515245u + 12345u;
  return (int)((rng >> 16) % (unsigned)range);
}

static NOINLINE void pool_init(void) {
  free_list = 0;
  for (int i = POOL - 1; i >= 0; i--) {
    pool[i].next = free_list;
    free_list = &pool[i];
  }
  in_use = 0;
}

static NOINLINE struct node *node_new(int v, struct node *next) {
  struct node *n = free_list;
  if (!n)
    return 0;
  free_list = n->next;
  n->value = v;
  n->next = next;
  if (++in_use > peak_use)
    peak_use = in_use;
  return n;
}

static void node_free(struct node *n) {
  n->next = free_list;
  free_list = n;
  in_use--;
}

static void list_free(struct node *l) {
  while (l) {
    struct node *next = l->next;
    node_free(l);
    l = next;
  }
}

static NOINLINE void append(struct node **head, int v) {
  while (*head)
    head = &(*head)->next;
  *head = node_new(v, 0);
}

static NOINLINE void insert_sorted(struct node **head, int v) {
  while (*head && (*head)->value < v)
    head = &(*head)->next;
  *head = node_new(v, *head);
}

static NOINLINE int remove_all(struct node **head, int v) {
  int removed = 0;
  while (*head) {
    if ((*head)->value == v) {
      struct node *dead = *head;
      *head = dead->next;
      node_free(dead);
      removed++;
    } else {
      head = &(*head)->next;
    }
  }
  return removed;
}

static NOINLINE struct node *reverse(struct node *l) {
  struct node *r = 0;
  while (l) {
    struct node *next = l->next;
    l->next = r;
    r = l;
    l = next;
  }
  return r;
}

static NOINLINE struct node *reverse_rec(struct node *l) {
  if (!l || !l->next)
    return l;
  struct node *rest = reverse_rec(l->next);
  l->next->next = l;
  l->next = 0;
  return rest;
}

static NOINLINE struct node *merge(struct node *a, struct node *b) {
  struct node head;
  struct node *tail = &head;
  while (a && b) {
    struct node **take = a->value <= b->value ? &a : &b;
    tail->next = *take;
    tail = *take;
    *take = (*take)->next;
  }
  tail->next = a ? a : b;
  return head.next;
}

static NOINLINE struct node *merge_sort(struct node *l) {
  if (!l || !l->next)
    return l;
  struct node *slow = l, *fast = l->next;
  while (fast && fast->next) {
    slow = slow->next;
    fast = fast->next->next;
  }
  struct node *second = slow->next;
  slow->next = 0;
  return merge(merge_sort(l), merge_sort(second));
}

static NOINLINE int dedupe_sorted(struct node *l) {
  int removed = 0;
  while (l && l->next) {
    if (l->next->value == l->value) {
      struct node *dead = l->next;
      l->next = dead->next;
      node_free(dead);
      removed++;
    } else {
      l = l->next;
    }
  }
  return removed;
}

static NOINLINE int has_cycle(const struct node *l) {
  const struct node *slow = l, *fast = l;
  while (fast && fast->next) {
    slow = slow->next;
    fast = fast->next->next;
    if (slow == fast)
      return 1;
  }
  return 0;
}

static int length(const struct node *l) {
  int n = 0;
  for (; l; l = l->next)
    n++;
  return n;
}

static int is_sorted(const struct node *l) {
  for (; l && l->next; l = l->next)
    if (l->value > l->next->value)
      return 0;
  return 1;
}

static void show(const char *name, const struct node *l) {
  char line[256];
  int n = 0;
  while (*name)
    line[n++] = *name++;
  line[n++] = ':';
  for (; l && n < 240; l = l->next) {
    char tmp[12];
    int k = 0;
    int v = l->value;
    unsigned m = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do {
      tmp[k++] = (char)('0' + m % 10);
      m /= 10;
    } while (m);
    line[n++] = ' ';
    if (v < 0)
      line[n++] = '-';
    while (k)
      line[n++] = tmp[--k];
    check = check * 31u + (unsigned)v;
  }
  line[n] = 0;
  put_str(line);
}

int main(void) {
  pool_init();
  show("static", &c1);
  MSIL_EXPECT(length(&c1) == 4 && c1.next->next->value == 30);

  struct node *a = 0, *b = 0;
  for (int i = 0; i < 6; i++)
    a = node_new(i * 3 + zero, a);
  show("pushed", a);
  a = reverse(a);
  show("reversed", a);
  a = reverse_rec(a);
  show("reversed again", a);
  a = reverse(a);
  for (int i = 0; i < 4; i++)
    append(&a, 100 - i);
  show("appended", a);

  for (int i = 0; i < 12; i++)
    insert_sorted(&b, next_random(20) - 5);
  show("sorted inserts", b);
  MSIL_EXPECT(is_sorted(b) && length(b) == 12);
  int removed = remove_all(&b, b->next->value);
  put_kv("removed", removed);
  insert_sorted(&b, -100);
  insert_sorted(&b, 1000);
  show("after removal", b);
  MSIL_EXPECT(is_sorted(b) && length(b) == 14 - removed);

  a = merge_sort(a);
  show("merge sorted", a);
  MSIL_EXPECT(is_sorted(a) && length(a) == 10);
  struct node *m = merge(a, b);
  a = b = 0;
  show("merged", m);
  MSIL_EXPECT(is_sorted(m));
  put_kv("duplicates removed", dedupe_sorted(m));
  show("deduplicated", m);

  // Make the list circular, detect it, and restore it.
  struct node *last = m;
  while (last->next)
    last = last->next;
  MSIL_EXPECT(!has_cycle(m));
  last->next = m->next->next;
  MSIL_EXPECT(has_cycle(m));
  last->next = 0;
  MSIL_EXPECT(!has_cycle(m) && is_sorted(m));
  put_kv("in use", in_use);

  // Exhaust the pool, then free everything and allocate again.
  struct node *fill = 0;
  int got = 0, before = in_use;
  for (struct node *n; (n = node_new(got, fill)) != 0; fill = n)
    got++;
  put_kv("allocated until exhausted", got);
  MSIL_EXPECT(got + before == POOL && in_use == POOL);
  list_free(fill);
  list_free(m);
  MSIL_EXPECT(in_use == 0);
  struct node *big = 0;
  for (int i = 0; i < POOL; i++)
    big = node_new(next_random(1000), big);
  MSIL_EXPECT(node_new(0, 0) == 0 && length(big) == POOL);
  big = merge_sort(big);
  MSIL_EXPECT(is_sorted(big));
  show("pool sorted", big);
  put_kv("peak use", peak_use);
  check = check * 31u + (unsigned)peak_use;
  // 64..191: no MSIL_EXPECT on lines 192..317 returns a status in that range.
  return 64 + (int)((check ^ check >> 8 ^ check >> 16 ^ check >> 24) & 0x7f);
}
