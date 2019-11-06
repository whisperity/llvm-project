// RUN: %check_clang_tidy %s modernize-superfluous-local-ptr-variable %t

namespace std {
struct jmp_buf {
};
int setjmp(jmp_buf &env);
void longjmp(jmp_buf env, int status);
} // namespace std

class T {
public:
  int i;
  T *tp;

  void f();
};

template <typename T>
T *create();

template <typename T>
void recreate(T **ptr);

template <typename T>
void something(T &t);

void free(void *p);

int incr(const int i);

/*
void test() {
  T *t = create<T>();
  free(t);
  recreate(&t);
  something(*t);

  (void)t->tp;

  T &tr = *t;

  T *tp = t->tp;
  T *tp2 = (*tp).tp;
  free(tp2);
  free(tp);
}
*/

void test_local_variable() {
  T *t = create<T>();
  // NO-WARN: Compiler, IDE warning exists for unused variables.
}

// FIXME: Figure out better warning messages and make these tests show what we want.

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

void test_single_nonderef_declref() {
  T *t3 = create<T>();
  free(t3);
  // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: local pointer variable 't3' only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:6: note: 't3' defined here
}

void test_outofline_init_of_ptrvar() {
  T *t4;
  t4 = create<T>();
  free(t4);
}

void test_outofline_init_of_ptrvar_unused() {
  // FIXME: This case really, really feels like a stupid false positive...
  T *t5;
  t5 = create<T>();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: local pointer variable 't5' only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:6: note: 't5' defined here
}

void test_outofline_init_of_ptrvar_guard(bool b) {
  T *t6;
  if (b)
    t6 = create<T>();
  free(t6);
  // NO-WARN: "t6 =" constitutes a use and as such, the potential rewrites don't apply.
}

void test_multiple_declref() {
  T *t7 = create<T>();
  T *t7next = t7->tp;
  free(t7);
  // NO-WARN: t7 used multiple times.
}

void test_checked_usage() {
  T *t8 = create<T>();
  if (!t8)
    return;
  free(t8);
  // NCHECK-MESSAGES: :[[@LINE-??]]:??: warning: local pointer variable 't8' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // NCHECK-MESSAGES: :[[@LINE-??]]:??: note: 't8' defined here
}

void test_memfn_call() {
  T *t9 = create<T>();
  t9->f();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: local pointer variable 't9' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:6: note: 't9' defined here
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
void if_stmts(T *t) {
  if (t)
    return;

  if (t > reinterpret_cast<T *>(0x7FFFFFFF)) {
    return;
  }

  std::jmp_buf JE;
  setjmp(JE);
  if (t < reinterpret_cast<T *>(0x99AA99AA)) {
    longjmp(JE, static_cast<int>(reinterpret_cast<unsigned long long>(t - 0xFFFFFF)));
  }

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

  if (t != 0)
    int i = 0;
  else
    int j = 0;
}