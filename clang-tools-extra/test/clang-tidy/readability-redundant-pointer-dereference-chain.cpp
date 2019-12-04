// RUN: %check_clang_tidy %s readability-redundant-pointer-dereference-chain %t

namespace std {
void free(void *ptr);
} // namespace std

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
  // (Not a chain because t1 isn't initialised from a dereference of a var, but rather a "whatever" expr.)
}

void chain_len2() {
  T *t2 = talloc();
  T *t2n = t2->f;
  T *t2nn = t2n->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't2nn' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-4]]:6: note: chain begins with 't2'
  // CHECK-MESSAGES: :[[@LINE-4]]:12: note: contains a dereference of 't2' in initialisation of 't2n'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 't2n' in initialisation of 't2nn'
}

void chain_len2_use() {
  T *t3 = talloc();
  T *t3n = t3->f;
  T *t3nn = t3n->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't3nn' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-4]]:6: note: chain begins with 't3'
  // CHECK-MESSAGES: :[[@LINE-4]]:12: note: contains a dereference of 't3' in initialisation of 't3n'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 't3n' in initialisation of 't3nn'
  std::free(t3nn);
}

void chain_len2_deref() {
  T *t4 = talloc();
  T *t4n = t4->f;
  T *t4nn = t4n->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't4nn' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-4]]:6: note: chain begins with 't4'
  // CHECK-MESSAGES: :[[@LINE-4]]:12: note: contains a dereference of 't4' in initialisation of 't4n'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 't4n' in initialisation of 't4nn'
  std::free(t4nn->a);
}

void chain_len2_1guard() {
  T *t5 = talloc();
  if (!t5)
    return;
  T *t5n = t5->f;
  T *t5nn = t5n->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't5nn' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-6]]:6: note: chain begins with 't5'
  // CHECK-MESSAGES: :[[@LINE-6]]:3: note: 't5' is guarded by this branch
  // CHECK-MESSAGES: :[[@LINE-4]]:12: note: contains a dereference of 't5' in initialisation of 't5n'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 't5n' in initialisation of 't5nn'
}

void chain_len2_2guard() {
  T *t6 = talloc();
  if (!t6)
    return;
  T *t6n = t6->f;
  if (!t6n)
    return;
  T *t6nn = t6n->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't6nn' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-8]]:6: note: chain begins with 't6'
  // CHECK-MESSAGES: :[[@LINE-8]]:3: note: 't6' is guarded by this branch
  // CHECK-MESSAGES: :[[@LINE-7]]:12: note: contains a dereference of 't6' in initialisation of 't6n'
  // CHECK-MESSAGES: :[[@LINE-7]]:3: note: 't6n' is guarded by this branch
  // CHECK-MESSAGES: :[[@LINE-6]]:13: note: contains a dereference of 't6n' in initialisation of 't6nn'
}

void chain_len2_2guard_use() {
  T *t7 = talloc();
  if (!t7)
    return;
  T *t7n = t7->f;
  if (!t7n)
    return;
  T *t7nn = t7n->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't7nn' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-8]]:6: note: chain begins with 't7'
  // CHECK-MESSAGES: :[[@LINE-8]]:3: note: 't7' is guarded by this branch
  // CHECK-MESSAGES: :[[@LINE-7]]:12: note: contains a dereference of 't7' in initialisation of 't7n'
  // CHECK-MESSAGES: :[[@LINE-7]]:3: note: 't7n' is guarded by this branch
  // CHECK-MESSAGES: :[[@LINE-6]]:13: note: contains a dereference of 't7n' in initialisation of 't7nn'
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
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't8nn' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-8]]:6: note: chain begins with 't8'
  // CHECK-MESSAGES: :[[@LINE-8]]:3: note: 't8' is guarded by this branch
  // CHECK-MESSAGES: :[[@LINE-7]]:12: note: contains a dereference of 't8' in initialisation of 't8n'
  // CHECK-MESSAGES: :[[@LINE-7]]:3: note: 't8n' is guarded by this branch
  // CHECK-MESSAGES: :[[@LINE-6]]:13: note: contains a dereference of 't8n' in initialisation of 't8nn'
  std::free(t8nn->f);
}

void chain_len3() {
  T *t9 = talloc();
  T *t9n = t9->f;
  T *t9nn = t9n->f;
  T *t9nnn = t9nn->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't9nnn' initialised from dereference chain of 3 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-5]]:6: note: chain begins with 't9'
  // CHECK-MESSAGES: :[[@LINE-5]]:12: note: contains a dereference of 't9' in initialisation of 't9n'
  // CHECK-MESSAGES: :[[@LINE-5]]:13: note: contains a dereference of 't9n' in initialisation of 't9nn'
  // CHECK-MESSAGES: :[[@LINE-5]]:14: note: contains a dereference of 't9nn' in initialisation of 't9nnn'
}

void chain_len3_use() {
  T *t10 = talloc();
  T *t10n = t10->f;
  T *t10nn = t10n->f;
  T *t10nnn = t10nn->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't10nnn' initialised from dereference chain of 3 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-5]]:6: note: chain begins with 't10'
  // CHECK-MESSAGES: :[[@LINE-5]]:13: note: contains a dereference of 't10' in initialisation of 't10n'
  // CHECK-MESSAGES: :[[@LINE-5]]:14: note: contains a dereference of 't10n' in initialisation of 't10nn'
  // CHECK-MESSAGES: :[[@LINE-5]]:15: note: contains a dereference of 't10nn' in initialisation of 't10nnn'
  std::free(t10nnn);
}

void chain_len3_deref() {
  T *t11 = talloc();
  T *t11n = t11->f;
  T *t11nn = t11n->f;
  T *t11nnn = t11nn->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 't11nnn' initialised from dereference chain of 3 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-5]]:6: note: chain begins with 't11'
  // CHECK-MESSAGES: :[[@LINE-5]]:13: note: contains a dereference of 't11' in initialisation of 't11n'
  // CHECK-MESSAGES: :[[@LINE-5]]:14: note: contains a dereference of 't11n' in initialisation of 't11nn'
  // CHECK-MESSAGES: :[[@LINE-5]]:15: note: contains a dereference of 't11nn' in initialisation of 't11nnn'
  std::free(t11nnn->a);
}

void multiple_chains_nouse() {
  T *mc1a = talloc();
  T *mc1b = mc1a->f;
  T *mc1c = mc1b->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'mc1c' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-4]]:6: note: chain begins with 'mc1a'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc1a' in initialisation of 'mc1b'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc1b' in initialisation of 'mc1c'

  U *mc1u = mc1c->a;
  Q *mc1q = mc1u->x;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'mc1q' initialised from dereference chain of 2 pointers, most only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-9]]:6: note: chain begins with 'mc1c', but that variable cannot be elided
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc1c' in initialisation of 'mc1u'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc1u' in initialisation of 'mc1q'

  // Whereas the code in single() did not form a *chain*, mc1c->mc1q and mc1c->mc1w does: the
  // variable "in the middle" (mc1u and mc1u2, respectively), could be elided from the scope.

  U *mc1u2 = mc1c->b;
  W *mc1w = mc1u2->y;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'mc1w' initialised from dereference chain of 2 pointers, most only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-19]]:6: note: chain begins with 'mc1c', but that variable cannot be elided
  // CHECK-MESSAGES: :[[@LINE-4]]:14: note: contains a dereference of 'mc1c' in initialisation of 'mc1u2'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc1u2' in initialisation of 'mc1q'
}

void multiple_chains_use() {
  T *mc2a = talloc();
  T *mc2b = mc2a->f;
  T *mc2c = mc2b->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'mc2c' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-4]]:6: note: chain begins with 'mc2a'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc2a' in initialisation of 'mc2b'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc2b' in initialisation of 'mc2c'

  U *mc2u = mc2c->a;
  Q *mc2q = mc2u->x;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'mc2q' initialised from dereference chain of 2 pointers, most only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-9]]:6: note: chain begins with 'mc2c', but that variable cannot be elided
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc2c' in initialisation of 'mc2u'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc2u' in initialisation of 'mc2q'

  U *mc2u2 = mc2c->b;
  W *mc2w = mc2u2->y;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'mc2w' initialised from dereference chain of 2 pointers, most only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-16]]:6: note: chain begins with 'mc2c', but that variable cannot be elided
  // CHECK-MESSAGES: :[[@LINE-4]]:14: note: contains a dereference of 'mc2c' in initialisation of 'mc2u2'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc2u2' in initialisation of 'mc2q'

  std::free(mc2w);
  std::free(mc2q);
}

void multiple_chains_deref() {
  T *mc3a = talloc();
  T *mc3b = mc3a->f;
  T *mc3c = mc3b->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'mc3c' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-4]]:6: note: chain begins with 'mc3a'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc3a' in initialisation of 'mc3b'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc3b' in initialisation of 'mc3c'

  U *mc3u = mc3c->a;
  Q *mc3q = mc3u->x;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'mc3q' initialised from dereference chain of 2 pointers, most only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-9]]:6: note: chain begins with 'mc3c', but that variable cannot be elided
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc3c' in initialisation of 'mc3u'
  // CHECK-MESSAGES: :[[@LINE-4]]:13: note: contains a dereference of 'mc3u' in initialisation of 'mc3q'

  U *mc3u2 = mc3c->b;
  // NO-WARN on 'mc3u2': used multiple times.

  std::free(mc3u2->x);
  std::free(mc3u2->y);
}

void multiple_chain_different_usages() {
  T *bt1 = talloc();
  T *bt2 = bt->f;
  T *bt3 = bt2->f;
  // CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'bt3' initialised from dereference chain of 2 pointers, each only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-4]]:6: note: chain begins with 'bt1'
  // CHECK-MESSAGES: :[[@LINE-4]]:12: note: contains a dereference of 'bt1' in initialisation of 'bt2'
  // CHECK-MESSAGES: :[[@LINE-4]]:12: note: contains a dereference of 'bt2' in initialisation of 'bt3'

  U *bu = bt3->a;
  W *bw = bu->y;
  std::free(bw);
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: 'bw' initialised from dereference chain of 2 pointers, most only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-10]]:6: note: chain begins with 'bt3', but that variable cannot be elided
  // CHECK-MESSAGES: :[[@LINE-5]]:11: note: contains a dereference of 'bt3' in initialisation of 'bu'
  // CHECK-MESSAGES: :[[@LINE-5]]:11: note: contains a dereference of 'bu' in initialisation of 'bw'

  // Branch 2.
  U *bu2 = bt3->b;
  Q *bq = bu2->x;
  bq;
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: 'bq' initialised from dereference chain of 2 pointers, most only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-19]]:6: note: chain begins with 'bt3', but that variable cannot be elided
  // CHECK-MESSAGES: :[[@LINE-5]]:12: note: contains a dereference of 'bt3' in initialisation of 'bu2'
  // CHECK-MESSAGES: :[[@LINE-5]]:11: note: contains a dereference of 'bu2' in initialisation of 'bq'

  // Branch 3.
  U *bu3 = bt3->f->a;
  T *bt4 = bu3->t;
  T *bt5 = bt4->f;
  U *bu4 = bt5->a;
  std::free(bu4->t);
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: 'bu4' initialised from dereference chain of 4 pointers, most only used in a single dereference [readability-redundant-pointer-dereference-chain]
  // CHECK-MESSAGES: :[[@LINE-30]]:6: note: chain begins with 'bt3', but that variable cannot be elided
  // CHECK-MESSAGES: :[[@LINE-7]]:12: note: contains a dereference of 'bt3' in initialisation of 'bu3'
  // CHECK-MESSAGES: :[[@LINE-7]]:12: note: contains a dereference of 'bu3' in initialisation of 'bt4'
  // CHECK-MESSAGES: :[[@LINE-7]]:12: note: contains a dereference of 'bt4' in initialisation of 'bt5'
  // CHECK-MESSAGES: :[[@LINE-7]]:12: note: contains a dereference of 'bt5' in initialisation of 'bu4'
}