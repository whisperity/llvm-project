// RUN: %check_clang_tidy %s readability-redundant-pointer-dereference-chain %t

namespace std {
void free(void *ptr);
}

struct S {
  int i;
  S *tp;
};

S *salloc();

struct T;
struct U;
struct Q;
struct W;
struct X;
struct Y;

T *talloc();

struct T {
  T *f;
  U *a;
  U *b;
  long l;
};
struct U {
  Q *x;
  W *y;
  short s;
};
struct W {
  X *z;
  Y *aa;
  char c;
};

void no() {
  T *t1 = talloc();
  // NO-WARN: The variable is not used.
}

void single_use() {
  T *t1 = talloc();
  std::free(t1);
  // NO-WARN: t1 is not dereferenced.
}

void single_use_deref() {
  T *t1 = talloc();
  std::free(t1->f);
  // NO-WARN: This is not a chain.
}

void single() {
  T *t1 = talloc();
  T *t1n = t1->f;
  // NO-WARN: This is not a chain, just a single dereference, there is another checker for this case.
}

void chain_len2() {
  T *t2 = talloc();
  T *t2n = t2->f;
  T *t2nn = t2n->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't2nn' initialised through dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
}

// FIXME: Write more tests.

/*
void dummy() {
  S *s1 = salloc();
  if (!s1)
    return;
  S *s1n = s1->tp;
  if (!s1n)
    return;
  int i = s1n->i;
}

void dummy2() {
  S *s1 = salloc();
  S *s1n = s1->tp;
  int i = s1n->i;
}
*/

#if 0
void longer_example() {
  // FIXME: This isn't working...
  /* begin chain len 3 */
  T *p1 = talloc();
  T *p2 = p1->f;
  T *p3 = p2->f; /* p3 has 2 uses */
  /* end chain */
  // FIXME: Report chain of 3, p1 and p2 not needed.

  /* begin chain len 2 */
  U *p4 = p3->a;
  Q *p5 = p4->x;
  /* end chain len 2 */
  free(p5); // p5 unused any further
  // FIXME: Report chain of 2, p4 not needed.

  /* begin chain len 2 */
  U *p6 = p3->b;
  W *p7 = p6->y; /* p7 has 2 uses */
  /* end chain len 2 */
  // FIXME: Report chain of 2, p6 not needed.

  X *p8 = p7->z;
  Y *p9 = p7->aa;

  free(p9); // p9 "unused" in terms of deref!
  free(p8); // p8 ditto.
  free(p7); // p7 not needed after the usages.
}
#endif
