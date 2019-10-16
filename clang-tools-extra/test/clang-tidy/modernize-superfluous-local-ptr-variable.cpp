// RUN: %check_clang_tidy %s modernize-superfluous-local-ptr-variable %t

class T {
public:
  T *tp;
};
template <typename T>
T *create();

void test_trivial_member_access() {
  T *t = create<T>();
  (void)t->tp;
  // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: local pointer variable 't' only participates in a single dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:6: note: 't' defined here
}

void test_trivial_auto() {
  auto *t = create<T>();
  (void)t->tp;
  // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: local pointer variable 't' only participates in a single dereference [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:9: note: 't' defined here
}

void test_parameter(T *t) {
  (void)t->tp;
  // NO-WARN: Removal of parameter would change API, which we don't want.
}

void test_no_uses() {
  T *t = create<T>();
  // NO-WARN: There are plenty of other checks and lint for unused variables.
}

void test_multiple_uses() {
  T *t = create<T>();
  (void)t->tp;
  (void)t->tp;
}
