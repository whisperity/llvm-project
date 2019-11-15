// NO: %check_clang_tidy -std=c++17-or-later %s modernize-superfluous-local-ptr-variable %t

namespace std {
int rand();
void free(void *p);

struct jmp_buf {
};
int setjmp(jmp_buf &env);
[[noreturn]] void longjmp(jmp_buf env, int status);

[[noreturn]] void exit(int exit_code);
} // namespace std

class T {
public:
  int i;
  T *tp;

  void f();
};

class HasDefault {
public:
  int m;
  HasDefault() : m(0) {}
  HasDefault(int i) : m(i) {}
};

class NoDefault {
public:
  int m;

  NoDefault() = delete;
  NoDefault(int i) : m(i) {}
};

class HasUDComma {
public:
  int m;

  HasUDComma() : m(0) {}
  HasUDComma(int i) : m(i) {}

  bool operator,(bool b) { return !b; }
};

// Definitely and maybe allocates and constructs a T... just used to make the
// tests better organised. Do not *ever* try to check *how* exactly the ptr
// var was created...
template <typename T>
T *create();
template <typename T>
T *try_create();

void unused_local_variable() {
  T *t1 = create<T>();
  // NO-WARN: Plenty compiler and IDE warnings exist for unused variables...
}

void single_member_access() {
  T *t2 = create<T>();
  // Make sure the FixIt introduced by the check doesn't actually change the code.
  // CHECK-FIXES: {{^  }}T *t2 = create<T>();{{$}}
  (void)t2->i;
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't2' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:9: note: usage: 't2' dereferenced here
  // CHECK-MESSAGES: :[[@LINE-3]]:9: note: consider using the code that initialises 't2' here
}

void ptr_of_auto_dereference() {
  auto *t3 = create<T>();
  (void)t3->tp;
  // CHECK-MESSAGES: :[[@LINE-2]]:9: warning: local pointer variable 't3' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:9: note: usage: 't3' dereferenced here
  // CHECK-MESSAGES: :[[@LINE-3]]:9: note: consider using the code that initialises 't3' here
}

void single_member_multiple_access() {
  T *t4 = create<T>();
  (void)(t4->i + t4->i);
}

void multiple_member_access() {
  T *t4 = create<T>();
  (void)t4->i;
  (void)t4->tp;
}

void passing_the_pointer() {
  T *t5 = create<T>();
  std::free(t5);
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't5' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:13: note: usage: 't5' used in an expression
  // CHECK-MESSAGES: :[[@LINE-3]]:13: note: consider using the code that initialises 't5' here
}

void single_member_to_variable() {
  T *t6 = create<T>();
  int i = t6->i;
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't6' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:11: note: usage: 't6' dereferenced in the initialisation of 'i'
  // CHECK-MESSAGES: :[[@LINE-3]]:11: note: consider using the code that initialises 't6' here
}

void single_member_to_auto_variable_1() {
  T *t7 = create<T>();
  auto i = t7->i;
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't7' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:12: note: usage: 't7' dereferenced in the initialisation of 'i'
  // CHECK-MESSAGES: :[[@LINE-3]]:12: note: consider using the code that initialises 't7' here
}

void single_member_to_auto_variable_2() {
  T *t8 = create<T>();
  auto n = t8->tp;
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't8' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:12: note: usage: 't8' dereferenced in the initialisation of 'n'
  // CHECK-MESSAGES: :[[@LINE-3]]:12: note: consider using the code that initialises 't8' here
}

void ptrvar_initialised_out_of_line() {
  T *t9;
  t9 = create<T>();
  std::free(t9);
}

void ptrvar_initialised_out_of_line_conditionally() {
  T *t9;
  if (std::rand() > 16384)
    t9 = create<T>();
  else
    t9 = nullptr;
  std::free(t9);
}

void complex_init_stmt(bool b) {
  T *t10 = b ? create<T>() : try_create<T>();
  std::free(t10);
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't10' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:13: note: usage: 't10' used in an expression
  // CHECK-MESSAGES: :[[@LINE-3]]:13: note: consider using the code that initialises 't10' here
}

void single_memfn_call() {
  T *t11 = create<T>();
  t11->f();
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't11' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: usage: 't11' dereferenced here
  // CHECK-MESSAGES: :[[@LINE-3]]:3: note: consider using the code that initialises 't11' here
}

void single_checked_passing() {
  T *t12 = try_create<T>();
  if (!t12)
    return;
  std::free(t12);

  // NO-WARN: This example cannot be reasonably rewritten.
}

void single_checked_dereference() {
  T *t12 = try_create<T>();
  if (!t12)
    return;
  std::free(t12->tp);

  // NO-WARN: This example cannot be reasonably rewritten.
}

#if 0
void single_checked_initialising_dereference() {
  T *t13 = try_create<T>();
  if (!t13)
    return;
  int i = t13->i;
  i += 1;

  // FIXME: Suggest the below example in >= C++17 mode.
}
#endif

void single_checked_ctor_initialising_dereference_1() {
  T *t14 = try_create<T>();
  if (!t14)
    return;
  NoDefault ND = t14->i;

  // NO-WARN: 'NoDefault' has no default ctor, so swapping the order of
  // initialisation won't work.
}

#if 0
void single_checked_ctor_initialising_dereference_2() {
  T *t15 = try_create<T>();
  if (!t15)
    return;
  HasDefault HD = t15->i;

  // FIXME: Test for suggestion on >= C++17 mode.
}

void single_checked_ctor_initialising_dereference_3() {
  T *t15 = try_create<T>();
  if (!t15)
    return;
  HasUDComma HUDC = t15->i;

  // FIXME: The suggestion here has to include the "void()".
}

int SCID_FIX_17() {
  int i;
  if (const T *p = try_create<T>(); !(p && ((i = p->i), void(), true)))
    // clang-format off
                                                     // ^~~~~~~
                                                     // only suggest if decltype(i) has operator,() !!!
    // clang-format on
    return -1;
  return i + 1;
}
#endif

/*
void test_terminate_checked_dereference() {
  T *t11 = create<T>();
  if (!t11)
    std::exit(42);
  int i = t11->i;
  // NCHECK-MESSAGES: :[[@LINE-1]]:11: warning: local pointer variable 't11' only participates in one dereference [modernize-superfluous-local-ptr-variable]
  // NCHECK-MESSAGES: :[[@LINE-5]]:6: note: 't11' defined here
  // NCHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't11' is guarded by this condition ...
  // NCHECK-MESSAGES: :[[@LINE-5]]:5: note: ... resulting in an early program termination
}

void test_check_loop(unsigned max) {
  for (unsigned i = 0; i < max; ++i) {
    T *t12 = create<T>();
    if (!t12)
      continue;
    free(t12);
    // NCHECK-MESSAGES: :[[@LINE-1]]:10: warning: local pointer variable 't12' only used once [modernize-superfluous-local-ptr-variable]
    // NCHECK-MESSAGES: :[[@LINE-5]]:8: note: 't12' defined here
    // NCHECK-MESSAGES: :[[@LINE-5]]:5: note: the value of 't12' is guarded by this condition ...
    // NCHECK-MESSAGES: :[[@LINE-5]]:7: note: ... resulting in an early continue
  }
}
*/
