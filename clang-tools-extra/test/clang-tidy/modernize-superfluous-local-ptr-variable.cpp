// RUN: %check_clang_tidy %s modernize-superfluous-local-ptr-variable %t

class T {
public:
  int i;
  T *tp;
};
template <typename T>
T *create();

void test_single_member_access() {
  T *t1 = create<T>();
  (void)t1->i;
  // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: local pointer variable 't1' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:6: note: 't1' defined here
}

void test_single_member_to_variable() {
  T *t2 = create<T>();
  int i = t2->i;
  // CHECK-MESSAGES: :[[@LINE-1]]:11: warning: local pointer variable 't2' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:6: note: 't2' defined here
}

/*void F()
{
  T* t = create<T>();
  auto i = t->i;
  auto tp = t->tp;
}

void G()
{
  T* t = create<T>();
  auto i = t->i;
}*/

/* Some if statements: */
/*
 if (t)
    return;

  if (t == nullptr)
    return;

  if (t != nullptr)
    return;

  if (t->i < 0)
    return;

  if (!t)
    return;

  for (;;) {
    if (t)
      continue;

    if (t) {
      continue;
    }
  }

  if (t)
    ++t->i;
*/
