// Binary trees in static pools (Milestone 10 program corpus, Design.md
// section 12): the benchmarks-game binary-trees program (a stretch tree, a
// long-lived tree, and many short-lived complete trees of depths 4 to 10
// built bottom-up and counted recursively) with malloc replaced by a bump
// allocator over a global array that is reset to a mark after each
// short-lived tree; then a binary search tree whose nodes come from a
// second global pool with a free list: insertion of 300 pseudo-random keys
// with duplicate counts, lookup, deletion with in-order successors (through
// pointers to child pointers), height, an in-order walk, a validity check
// with 64-bit bounds, and deletion of every node until the pool is full
// again. Recursion depth stays below 30. Results are printed; MSIL_EXPECT
// returns 1 + (line % 255) at the first failed check; when every check
// passes, the exit status is a checksum of the printed values folded into 8
// bits (D37).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;
static unsigned check;

static void report(const char *what, long long v) {
  put_kv(what, v);
  check = check * 31u + (unsigned)v;
}

// ---- benchmarks-game binary-trees with a bump allocator ----

struct tree {
  struct tree *left, *right;
};

#define TREE_POOL (1 << 13)
static struct tree arena[TREE_POOL];
static int top, high_water, failed_allocs;

static NOINLINE struct tree *new_tree(struct tree *l, struct tree *r) {
  if (top == TREE_POOL) {
    failed_allocs++;
    return 0;
  }
  struct tree *t = &arena[top++];
  t->left = l;
  t->right = r;
  if (top > high_water)
    high_water = top;
  return t;
}

static NOINLINE struct tree *bottom_up(int depth) {
  if (depth <= 0)
    return new_tree(0, 0);
  struct tree *l = bottom_up(depth - 1);
  struct tree *r = bottom_up(depth - 1);
  return new_tree(l, r);
}

static NOINLINE int item_check(const struct tree *t) {
  return t->left ? 1 + item_check(t->left) + item_check(t->right) : 1;
}

// Returns 0, or 1 + (line % 255) from a failed check.
static int binary_trees(int min_depth, int max_depth) {
  int mark = top;
  report("stretch tree depth", max_depth + 1);
  report("check", item_check(bottom_up(max_depth + 1)));
  top = mark;

  struct tree *long_lived = bottom_up(max_depth);
  for (int d = min_depth; d <= max_depth; d += 2) {
    int iterations = 1 << (max_depth - d + min_depth);
    int sum = 0;
    for (int i = 0; i < iterations; i++) {
      mark = top;
      sum += item_check(bottom_up(d));
      top = mark;
    }
    report("trees", iterations);
    report("depth", d);
    report("check", sum);
    MSIL_EXPECT(sum == iterations * ((1 << (d + 1)) - 1));
  }
  report("long lived tree depth", max_depth);
  report("check", item_check(long_lived));
  return 0;
}

// ---- a binary search tree with a free list ----

struct bst {
  int key, count;
  struct bst *child[2];
};

#define BST_POOL 320
static struct bst bst_pool[BST_POOL];
static struct bst *bst_free;
static int bst_used;

static void bst_pool_init(void) {
  for (int i = 0; i < BST_POOL; i++) {
    bst_pool[i].child[0] = bst_free;
    bst_free = &bst_pool[i];
  }
}

static NOINLINE struct bst *bst_alloc(int key) {
  struct bst *n = bst_free;
  if (!n)
    return 0;
  bst_free = n->child[0];
  n->key = key;
  n->count = 1;
  n->child[0] = n->child[1] = 0;
  bst_used++;
  return n;
}

static void bst_release(struct bst *n) {
  n->child[0] = bst_free;
  bst_free = n;
  bst_used--;
}

// Returns 1 for a new key, 0 for a duplicate.
static NOINLINE int bst_insert(struct bst **root, int key) {
  while (*root) {
    if ((*root)->key == key) {
      (*root)->count++;
      return 0;
    }
    root = &(*root)->child[key > (*root)->key];
  }
  *root = bst_alloc(key);
  return 1;
}

static NOINLINE struct bst *bst_find(struct bst *t, int key) {
  while (t && t->key != key)
    t = t->child[key > t->key];
  return t;
}

static NOINLINE int bst_delete(struct bst **root, int key) {
  while (*root && (*root)->key != key)
    root = &(*root)->child[key > (*root)->key];
  struct bst *n = *root;
  if (!n)
    return 0;
  if (n->child[0] && n->child[1]) {
    // Move the in-order successor's key here, then unlink the successor.
    struct bst **s = &n->child[1];
    while ((*s)->child[0])
      s = &(*s)->child[0];
    struct bst *succ = *s;
    n->key = succ->key;
    n->count = succ->count;
    *s = succ->child[1];
    bst_release(succ);
  } else {
    *root = n->child[0] ? n->child[0] : n->child[1];
    bst_release(n);
  }
  return 1;
}

static int bst_height(const struct bst *t) {
  if (!t)
    return 0;
  int l = bst_height(t->child[0]), r = bst_height(t->child[1]);
  return 1 + (l > r ? l : r);
}

static int bst_valid(const struct bst *t, long long lo, long long hi) {
  if (!t)
    return 1;
  if (t->key <= lo || t->key >= hi)
    return 0;
  return bst_valid(t->child[0], lo, t->key) &&
         bst_valid(t->child[1], t->key, hi);
}

static int keys[BST_POOL];
static int nkeys;
static long long weight;

static void bst_walk(const struct bst *t) {
  if (!t)
    return;
  bst_walk(t->child[0]);
  keys[nkeys++] = t->key;
  weight += (long long)t->key * t->count;
  bst_walk(t->child[1]);
}

static void walk(const struct bst *t) {
  nkeys = 0;
  weight = 0;
  bst_walk(t);
}

static void show_keys(const char *title, int from, int to) {
  char line[128];
  int n = 0;
  for (int i = from; i < to && i < nkeys; i++) {
    int v = keys[i];
    char tmp[12];
    int k = 0;
    do {
      tmp[k++] = (char)('0' + v % 10);
      v /= 10;
    } while (v);
    if (n)
      line[n++] = ' ';
    while (k)
      line[n++] = tmp[--k];
    check = check * 33u + (unsigned)keys[i];
  }
  line[n] = 0;
  put_str(title);
  put_str(line);
}

static unsigned rng = 2463534242u;
static int next_key(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (int)(rng % 1000u);
}

int main(void) {
  int status = binary_trees(4, 10 + zero);
  if (status)
    return status;
  report("pool high water", high_water);
  MSIL_EXPECT(failed_allocs == 0 && top == (1 << 11) - 1);

  bst_pool_init();
  struct bst *root = 0;
  int distinct = 0;
  for (int i = 0; i < 300; i++)
    distinct += bst_insert(&root, next_key());
  walk(root);
  report("distinct keys", distinct);
  report("height", bst_height(root));
  report("weighted sum", weight);
  show_keys("smallest:", 0, 12);
  show_keys("largest:", nkeys - 12, nkeys);
  MSIL_EXPECT(nkeys == distinct && bst_used == distinct);
  MSIL_EXPECT(bst_valid(root, -1, 1000));
  for (int i = 1; i < nkeys; i++)
    MSIL_EXPECT(keys[i - 1] < keys[i]);
  MSIL_EXPECT(bst_find(root, keys[nkeys / 2]) != 0);
  MSIL_EXPECT(bst_find(root, 1000) == 0);

  // Delete the even keys, then every remaining key, in insertion order.
  int deleted = 0;
  for (int k = 0; k < 1000; k += 2)
    deleted += bst_delete(&root, k + zero);
  walk(root);
  report("even keys deleted", deleted);
  report("height after", bst_height(root));
  show_keys("smallest odd:", 0, 12);
  MSIL_EXPECT(bst_valid(root, -1, 1000) && nkeys == distinct - deleted);
  for (int i = 0; i < nkeys; i++)
    MSIL_EXPECT(keys[i] % 2 == 1);
  rng = 2463534242u;
  int gone = 0;
  for (int i = 0; i < 300; i++)
    gone += bst_delete(&root, next_key());
  report("odd keys deleted", gone);
  MSIL_EXPECT(root == 0 && bst_used == 0 && gone == nkeys);
  return (int)((check ^ check >> 8 ^ check >> 16 ^ check >> 24) & 0xff);
}
