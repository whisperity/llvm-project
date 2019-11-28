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
  T *t;
  short s;
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
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't2nn' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't2nn' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
}

void chain_len2_use() {
  T *t3 = talloc();
  T *t3n = t3->f;
  T *t3nn = t3n->f;
  std::free(t3nn);
}

void chain_len2_deref() {
  T *t4 = talloc();
  T *t4n = t4->f;
  T *t4nn = t4n->f;
  std::free(t4nn->a);
}

void chain_len2_1guard() {
  T *t5 = talloc();
  if (!t5)
    return;
  T *t5n = t5->f;
  T *t5nn = t5n->f;
}

void chain_len2_2guard() {
  T *t6 = talloc();
  if (!t6)
    return;
  T *t6n = t6->f;
  if (!t6n)
    return;
  T *t6nn = t6n->f;
}

void chain_len2_2guard_use() {
  T *t7 = talloc();
  if (!t7)
    return;
  T *t7n = t7->f;
  if (!t7n)
    return;
  T *t7nn = t7n->f;
  std::free(t7nn);
}

void chain_len2_2guard_deref() {
  T *t8 = talloc();
  if (!t8)
    return;
  T *t8n = t8->f;
  if (!t8n)
    return;
  T *t8nn = t8n->f;
  std::free(t8nn->f);
}

void chain_len3() {
  T *t9 = talloc();
  T *t9n = t9->f;
  T *t9nn = t9n->f;
  T *t9nnn = t9nn->f;
}

void chain_len3_use() {
  T *t10 = talloc();
  T *t10n = t10->f;
  T *t10nn = t10n->f;
  T *t10nnn = t10nn->f;
  std::free(t10nnn);
}

void chain_len3_deref() {
  T *t11 = talloc();
  T *t11n = t11->f;
  T *t11nn = t11n->f;
  T *t11nnn = t11nn->f;
  std::free(t11nnn->a);
}

void multiple_chains_nouse() {
  T *mc1a = talloc();
  T *mc1b = mc1a->f;
  T *mc1c = mc1b->f;

  U *mc1u = mc1c->a;
  Q *mc1q = mc1u->x;

  U *mc1u2 = mc1c->b;
  W *mc1w = mc1u2->y;
}

void multiple_chains_use() {
  T *mc2a = talloc();
  T *mc2b = mc2a->f;
  T *mc2c = mc2b->f;

  U *mc2u = mc2c->a;
  Q *mc2q = mc2u->x;

  U *mc2u2 = mc2c->b;
  W *mc2w = mc2u2->y;

  std::free(mc2w);
  std::free(mc2q);
}

void multiple_chains_deref() {
  T *mc3a = talloc();
  T *mc3b = mc3a->f;
  T *mc3c = mc3b->f;

  U *mc3u = mc3c->a;
  Q *mc3q = mc3u->x;

  U *mc3u2 = mc3c->b;

  std::free(mc3u2->x);
  std::free(mc3u2->y);
}

void branching() {
  T *bt = talloc();
  T *bt2 = bt->f;
  T *bt3 = bt2->f;

  // Branch 1.
  U *bu = bt3->a;
  W *bw = bu->y;
  std::free(bw);

  // Branch 2.
  U *bu2 = bt3->b;
  Q *bq = bu2->x;
  bq;

  // Branch 3.
  U *bu3 = bt3->f->a;
  T *bt4 = bu3->t;
  T *bt5 = bt4->f;
  U *bu4 = bt5->a;
  std::free(bu4->t);
}