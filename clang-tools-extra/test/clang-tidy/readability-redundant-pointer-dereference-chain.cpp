// RUN: %check_clang_tidy %s readability-redundant-pointer-dereference-chain %t

struct T {
  int i;
  T *tp;
};

T *talloc();

void dummy() {
  T *t1 = talloc();
  if (!t1)
    return;
  T *t1n = t1->tp;
  if (!t1n)
    return;
  int i = t1n->i;
}

void dummy2() {
  T *t1 = talloc();
  T *t1n = t1->tp;
  int i = t1n->i;
}
