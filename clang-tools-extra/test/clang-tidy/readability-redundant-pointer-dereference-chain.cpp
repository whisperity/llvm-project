// RUN: %check_clang_tidy %s readability-redundant-pointer-dereference-chain %t

struct S {
  int i;
  S *tp;
};

S *salloc();

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
};
struct U {
  Q *x;
  W *y;
};
struct W {
  X *z;
  Y *aa;
};

void free(void *ptr);

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

  free(p9); // p9 "unused" in terms of deref
  free(p8); // p8 ditto.
  free(p7); // p7 not needed after the usages.
}
