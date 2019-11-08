// RUN: %check_clang_tidy %s modernize-superfluous-local-ptr-variable %t -- -- -std=c++17

// FIXME: Run the test with multiple C++ std versions.

#ifdef __has_cpp_attribute
#if __has_cpp_attribute(noreturn)
#define NO_RETURN [[noreturn]]
#else
#define NO_RETURN
#endif
#else
#define NO_RETURN
#endif

namespace std {
struct jmp_buf {
};
int setjmp(jmp_buf &env);
NO_RETURN void longjmp(jmp_buf env, int status);

NO_RETURN void exit(int exit_code);

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
  // FIXME: This test case might break when only param passing is considered a "use", not a re-init to it.
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

void test_memfn_call() {
  T *t8 = create<T>();
  t8->f();
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: local pointer variable 't8' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:6: note: 't8' defined here
}

void test_checked_usage() {
  T *t9 = create<T>();
  if (!t9)
    return;
  free(t9);
  // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: local pointer variable 't9' only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-5]]:6: note: 't9' defined here
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't9' is guarded by this condition ...
  // CHECK-MESSAGES: :[[@LINE-5]]:5: note: ... resulting in an early return
}

void test_checked_dereference() {
  T *t10 = create<T>();
  if (!t10)
    return;
  int i = t10->i;
  // CHECK-MESSAGES: :[[@LINE-1]]:11: warning: local pointer variable 't10' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-5]]:6: note: 't10' defined here
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't10' is guarded by this condition ...
  // CHECK-MESSAGES: :[[@LINE-5]]:5: note: ... resulting in an early return
}

void test_terminate_checked_dereference() {
  T *t11 = create<T>();
  if (!t11)
    std::exit(42);
  int i = t11->i;
  // CHECK-MESSAGES: :[[@LINE-1]]:11: warning: local pointer variable 't11' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-5]]:6: note: 't11' defined here
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't11' is guarded by this condition ...
  // CHECK-MESSAGES: :[[@LINE-5]]:5: note: ... resulting in an early program termination
}

void test_check_loop(unsigned max) {
  for (unsigned i = 0; i < max; ++i) {
    T *t12 = create<T>();
    if (!t12)
      continue;
    free(t12);
    // CHECK-MESSAGES: :[[@LINE-1]]:10: warning: local pointer variable 't12' only used once [modernize-superfluous-local-ptr-variable]
    // CHECK-MESSAGES: :[[@LINE-5]]:8: note: 't12' defined here
    // CHECK-MESSAGES: :[[@LINE-5]]:5: note: the value of 't12' is guarded by this condition ...
    // CHECK-MESSAGES: :[[@LINE-5]]:7: note: ... resulting in an early continue
  }
}
