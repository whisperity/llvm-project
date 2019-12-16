// RUN: %check_clang_tidy -std=c++11,c++14 %s readability-redundant-pointer-in-local-scope %t

namespace std {
int rand();
void free(void *p);

template <typename T>
struct vector {
  T *begin() const;
  T *end() const;
};

template <typename T>
struct list {
  struct iterator {
    T &operator*() const;
    T *operator->() const;
    iterator operator++() const;
  };

  iterator begin() const;
  iterator end() const;
};
} // namespace std

class T {
public:
  int i;
  T *tp;

  void f();
};

struct TrivialAggregate {
  int m;
};

class HasDefault {
public:
  int m;
  HasDefault() : m(0) {}
  HasDefault(int i) : m(i) {}
  HasDefault(int i, int j) : m(i * j) {}
};

struct NoDefault {
public:
  int m;

  NoDefault() = delete;
  NoDefault(int i) : m(i) {}
};

typedef NoDefault ND;

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
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't2' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:9: note: usage: 't2' dereferenced here
  // CHECK-MESSAGES: :[[@LINE-3]]:9: note: consider using the code that initialises 't2' here
}

void ptr_of_auto_dereference() {
  auto *t3 = create<T>();
  (void)t3->tp;
  // CHECK-MESSAGES: :[[@LINE-2]]:9: warning: local pointer variable 't3' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
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
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't5' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:13: note: usage: 't5' used in an expression
  // CHECK-MESSAGES: :[[@LINE-3]]:13: note: consider using the code that initialises 't5' here
}

void single_member_to_variable() {
  T *t6 = create<T>();
  int i = t6->i;
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't6' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:11: note: usage: 't6' dereferenced in the initialisation of 'i'
  // CHECK-MESSAGES: :[[@LINE-3]]:11: note: consider using the code that initialises 't6' here
}

void single_member_to_auto_variable_1() {
  T *t7 = create<T>();
  auto i = t7->i;
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't7' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:12: note: usage: 't7' dereferenced in the initialisation of 'i'
  // CHECK-MESSAGES: :[[@LINE-3]]:12: note: consider using the code that initialises 't7' here
}

void single_member_to_auto_variable_2() {
  T *t8 = create<T>();
  auto n = t8->tp;
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't8' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
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

bool just_a_guard() {
  T *t10 = try_create<T>();
  if (!t10)
    return false;
  return true;
}

void complex_init_stmt(bool b) {
  T *t11 = b ? create<T>() : try_create<T>();
  std::free(t11);
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't11' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:13: note: usage: 't11' used in an expression
  // CHECK-MESSAGES: :[[@LINE-3]]:13: note: consider using the code that initialises 't11' here
}

void single_memfn_call() {
  T *t12 = create<T>();
  t12->f();
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't12' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: usage: 't12' dereferenced here
  // CHECK-MESSAGES: :[[@LINE-3]]:3: note: consider using the code that initialises 't12' here
}

void single_checked_passing() {
  T *t13 = try_create<T>();
  if (!t13)
    return;
  std::free(t13);
  // NO-WARN: This example cannot be reasonably rewritten.
}

void single_checked_dereference() {
  T *t13 = try_create<T>();
  if (!t13)
    return;
  std::free(t13->tp);
  // NO-WARN: This example cannot be reasonably rewritten.
}

void single_checked_initialising_dereference() {
  T *t14 = try_create<T>();
  if (!t14)
    return;
  int i = t14->i;
  i += 1;
  // CHECK-MESSAGES: :[[@LINE-5]]:6: warning: local pointer variable 't14' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-3]]:11: note: usage: 't14' dereferenced in the initialisation of 'i'
  // CHECK-MESSAGES: :[[@LINE-6]]:3: note: the value of 't14' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-8]]:6: note: consider putting 't14', the branch, and the assignment of 'i' into an inner scope (between {brackets})
}

void single_checked_ctor_initialising_dereference_1() {
  T *t15 = try_create<T>();
  if (!t15)
    return;
  ND NDv = t15->i;
  // NO-WARN: 'NoDefault' has no default ctor, so swapping the order of
  // initialisation won't work.
}

void single_checked_ctor_initialising_dereference_2a() {
  T *t16 = try_create<T>();
  if (!t16)
    return;
  HasDefault HDa = t16->i;
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't16' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:20: note: usage: 't16' dereferenced in the initialisation of 'HDa'
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't16' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-7]]:6: note: consider putting 't16', the branch, and the assignment of 'HDa' into an inner scope (between {brackets})
}

void single_checked_ctor_initialising_dereference_2b() {
  T *t17 = try_create<T>();
  if (!t17)
    return;
  HasDefault HDb(t17->i);
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't17' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:18: note: usage: 't17' dereferenced in the initialisation of 'HDb'
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't17' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-7]]:6: note: consider putting 't17', the branch, and the assignment of 'HDb' into an inner scope (between {brackets})
}

void single_checked_ctor_initialising_dereference_2c() {
  T *t18 = try_create<T>();
  if (!t18)
    return;
  HasDefault HDc{t18->i};
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't18' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:18: note: usage: 't18' dereferenced in the initialisation of 'HDc'
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't18' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-7]]:6: note: consider putting 't18', the branch, and the assignment of 'HDc' into an inner scope (between {brackets})
}

void single_checked_ctor_initialising_dereference_2d() {
  T *t19 = try_create<T>();
  if (!t19)
    return;
  TrivialAggregate ta{t19->i};
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't19' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:23: note: usage: 't19' dereferenced in the initialisation of 'ta'
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't19' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-7]]:6: note: consider putting 't19', the branch, and the assignment of 'ta' into an inner scope (between {brackets})
}

void single_checked_ctor_initialising_dereference_3a() {
  T *t20 = try_create<T>();
  if (!t20)
    return;
  HasDefault HD3a(t20->i, 1);
  // NO-WARN: The variable is not "directly" initialised from a pointer dereference as the constructor used takes multiple arguments.
}

void single_checked_ctor_initialising_dereference_3b() {
  T *t20 = try_create<T>();
  if (!t20)
    return;
  HasDefault HD3b{t20->i, 1};
  // NO-WARN: The variable is not "directly" initialised from a pointer dereference as the constructor used takes multiple arguments.
}

#define PTR_DEREF(alloc) \
  T *ptr = alloc;        \
  (void)ptr->i;

void test_macro() {
  PTR_DEREF(create<T>());
  // NO-WARN: 'ptr' comes from a macro.
}

#undef PTR_DEREF

const char *test_get_value() {
  const char *Str = "Hello Local!";
  return Str;
  // CHECK-MESSAGES: :[[@LINE-2]]:15: warning: local pointer variable 'Str' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:10: note: usage: 'Str' used in an expression
  // CHECK-MESSAGES: :[[@LINE-3]]:10: note: consider using the code that initialises 'Str' here
}

struct HasStaticMember {
  const char *getMember() { return StrM; } // Usage point.
  // NO-WARN: The declaration referred by the usage point does not have an initialiser.
private:
  static const char *StrM; // Referenced VarDecl.
};
const char *HasStaticMember::StrM = "Hello Member!"; // Initialiser.

int dereference_is_not_a_guard() {
  T *t21 = create<T>();
  T *t21n = t21->tp;
  if (t21->i < 0)
    return 1;
  return t21n->i + t21n->i;
  // NO-WARN: if (t21->i < 0) is NOT a "guard statement", but a use!
  // NO-WARN: Thus both 't21' and 't21n' are used multiple (2) times.
}

void if_after_usage_is_not_a_guard() {
  T *t21 = create<T>();
  T *t21n = t21->tp;
  if (!t21)
    return;
  // NO-WARN: The "guard-like" **after** the usage point is not a guard.
}

int loop_variable(const std::vector<T *> V) {
  int i = 0;
  for (T **tp = V.begin(); tp != V.end(); ++tp) {
    i += (*tp)->i;
  }
  return i;
  // NO-WARN: Loop variables can't be refactored, they should be ignored.
}

int loop_variable_multiple(const std::vector<T *> V) {
  int i = 0;
  for (T **tp = V.begin(), **tp2 = V.begin() + 1; tp != V.end() && tp2 != V.end() - 1; ++tp, --tp2) {
    i += (*tp)->i;
    i -= (*tp2)->i;
  }
  return i;
  // NO-WARN: Loop variables can't be refactored, they should be ignored.
}

int foreach_loop_variable(const std::vector<T *> V) {
  int i = 0;
  for (const T *tit : V) {
    i += tit->i;
  }
  return i;
  // NO-WARN: Loop variables can't be refactored, they should be ignored.
}

int foreach_loop_auto_variable(const std::vector<T *> V) {
  int i = 0;
  for (const auto tit : V) {
    i += tit->i;
  }
  return i;
  // NO-WARN: Loop variables can't be refactored, they should be ignored.
}

void ignore_function_parameters(T *t22 = nullptr) {
  T *t22n = t22->tp;
  // NO-WARN: Function parms should be ignored as they can't be refactored.
}

int ignore_ptrs_without_init() {
  T *t23;
  T *t23n = t23->tp;
  return t23n->i + t23n->i;
  // NO-WARN: t23 does not have an initialiser expression, and thus can't be
  // rewritten.
}

int match_dereferenceables(const std::list<T> &L) {
  typename std::list<T>::iterator LIt = L.begin();
  T *LItn = LIt->tp;
  // CHECK-MESSAGES: :[[@LINE-2]]:35: warning: local dereferenceable variable 'LIt' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:13: note: usage: 'LIt' dereferenced in the initialisation of 'LItn'
  // CHECK-MESSAGES: :[[@LINE-3]]:13: note: consider using the code that initialises 'LIt' here
}

int match_dereferenceables_auto(const std::list<T> &L) {
  auto LIt2 = L.begin();
  T *LIt2n = LIt2->tp;
  // CHECK-MESSAGES: :[[@LINE-2]]:8: warning: local dereferenceable variable 'LIt2' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:14: note: usage: 'LIt2' dereferenced in the initialisation of 'LIt2n'
  // CHECK-MESSAGES: :[[@LINE-3]]:14: note: consider using the code that initialises 'LIt2' here
}
