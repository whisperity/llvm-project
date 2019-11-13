// RUN: %check_clang_tidy -std=c++11,c++14 %s modernize-superfluous-local-ptr-variable %t

namespace std {
int rand();
void free(void *p);
} // namespace std

class T {
public:
  int i;
  T *tp;

  void f();
};

struct TrivialAggregate { int m; };

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

void single_checked_initialising_dereference() {
  T *t13 = try_create<T>();
  if (!t13)
    return;
  int i = t13->i;
  i += 1;
  // CHECK-MESSAGES: :[[@LINE-5]]:6: warning: local pointer variable 't13' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-3]]:11: note: usage: 't13' dereferenced in the initialisation of 'i'
  // CHECK-MESSAGES: :[[@LINE-6]]:3: note: the value of 't13' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-8]]:6: note: consider putting the pointer, the branch, and the assignment to 'i' into an inner scope (between {brackets})
}

void single_checked_ctor_initialising_dereference_1() {
  T *t14 = try_create<T>();
  if (!t14)
    return;
  ND NDv = t14->i;

  // NO-WARN: 'NoDefault' has no default ctor, so swapping the order of
  // initialisation won't work.
}

void single_checked_ctor_initialising_dereference_2a() {
  T *t15 = try_create<T>();
  if (!t15)
    return;
  HasDefault HDa = t15->i;
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't15' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:20: note: usage: 't15' dereferenced in the initialisation of 'HDa'
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't15' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-7]]:6: note: consider putting the pointer, the branch, and the assignment to 'HDa' into an inner scope (between {brackets})
}

void single_checked_ctor_initialising_dereference_2b() {
  T *t16 = try_create<T>();
  if (!t16)
    return;
  HasDefault HDb(t16->i);
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't16' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:18: note: usage: 't16' dereferenced in the initialisation of 'HDb'
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't16' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-7]]:6: note: consider putting the pointer, the branch, and the assignment to 'HDb' into an inner scope (between {brackets})
}

void single_checked_ctor_initialising_dereference_2c() {
  T *t17 = try_create<T>();
  if (!t17)
    return;
  HasDefault HDc{t17->i};
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't17' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:18: note: usage: 't17' dereferenced in the initialisation of 'HDc'
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't17' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-7]]:6: note: consider putting the pointer, the branch, and the assignment to 'HDc' into an inner scope (between {brackets})
}

/*
void single_checked_ctor_initialising_dereference_2d() {
  T *t18 = try_create<T>();
  if (!t18)
    return;
  TrivialAggregate ta{t18->i};
  // CHECK-MESSAGES: :[[@LINE-4]]:6: warning: local pointer variable 't18' might be superfluous as it is only used once [modernize-superfluous-local-ptr-variable]
  // CHECK-MESSAGES: :[[@LINE-2]]:23: note: usage: 't18' dereferenced in the initialisation of 'ta'
  // CHECK-MESSAGES: :[[@LINE-5]]:3: note: the value of 't18' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-7]]:6: note: consider putting the pointer, the branch, and the assignment to 'ta' into an inner scope (between {brackets})
}
*/

void single_checked_ctor_initialising_dereference_3a() {
  T *t18 = try_create<T>();
  if (!t18)
    return;
  HasDefault HD3a(t18->i, 1);
  // NO-WARN: The variable is not "directly" initialised from a pointer dereference as the constructor used takes multiple arguments.
}

void single_checked_ctor_initialising_dereference_3b() {
  T *t18 = try_create<T>();
  if (!t18)
    return;
  HasDefault HD3b{t18->i, 1};
  // NO-WARN: The variable is not "directly" initialised from a pointer dereference as the constructor used takes multiple arguments.
}
