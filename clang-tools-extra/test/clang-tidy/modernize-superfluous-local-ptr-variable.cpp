// RUN: %check_clang_tidy %s modernize-superfluous-local-ptr-variable %t

class T {
public:
  int i;
  T *tp;
};
template <typename T>
T *create();

void free(void *p);

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

void test_single_access_auto_type() {
  auto instance = create<T>();
  auto member = instance->i;
  // CHECK-MESSAGES: :[[@LINE-1]]:17: warning: local pointer variable 'instance' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:8: note: 'instance' defined here
}

void test_multiple_declref() {
  T *t3 = create<T>(); // "allocates"
  T *t3next = t3->tp;
  free(t3);
  // NO-WARN: t3 passed to function call, this snippet could not be refactored.
}

int test_initialise_nonmember_deref() {
  T *t4 = create<T>();
  T t_inst = *t4; // dereference, but not member access.
  // FIXME: Report the dereference?
  return t_inst.i;
}

int test_noninitalising_deref() {
  T *t5 = create<T>();
  int i;
  i = 0;
  i = i + t5->i;
  // CHECK-MESSAGES: :[[@LINE-1]]:11: warning: local pointer variable 't5' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-5]]:6: note: 't5' defined here
  return i;
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
// FIXME: Handle if statements not as usages and parameter passes but in a
//        special way, registering they denote a null-check.
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
