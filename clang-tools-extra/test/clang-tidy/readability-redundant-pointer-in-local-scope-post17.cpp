// RUN: %check_clang_tidy -std=c++17-or-later %s readability-redundant-pointer-in-local-scope %t

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

struct TrivialAggregate {
  int m;
};

class HasDefault {
public:
  int m;
  HasDefault() : m(0) {}
  HasDefault(int i) : m(i) {
  }
  HasDefault(int i, int j) : m(i * j) {}
};

class NoDefault {
public:
  int m;

  NoDefault() = delete;
  NoDefault(int i) : m(i) {}
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

void complex_init_stmt(bool b) {
  T *t10 = b ? create<T>() : try_create<T>();
  std::free(t10);
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't10' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:13: note: usage: 't10' used in an expression
  // CHECK-MESSAGES: :[[@LINE-3]]:13: note: consider using the code that initialises 't10' here
}

void single_memfn_call() {
  T *t11 = create<T>();
  t11->f();
  // CHECK-MESSAGES: :[[@LINE-2]]:6: warning: local pointer variable 't11' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-2]]:3: note: usage: 't11' dereferenced here
  // CHECK-MESSAGES: :[[@LINE-3]]:3: note: consider using the code that initialises 't11' here
}

bool just_a_guard() {
  T *t10 = try_create<T>();
  if (!t10)
    return false;
  return true;
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

// FIXME: Multiple hints are generated for the user's consideration, but these
//        cannot be properly tested because Tidy only applies the first...

void single_checked_initialising_dereference() {
  T *t14 = try_create<T>();
  // HINT: {{^  }}int i;{{$}}
  if (!t14)
    // HINT: {{^  }}if (T *t14 = try_create<T>(); (!t14) || ((i = {t14->i}), void(), false)){{$}}
    return;
  int i = t14->i;
  // HINT: {{^  }};{{$}}
  // CHECK-MESSAGES: :[[@LINE-7]]:6: warning: local pointer variable 't14' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-3]]:11: note: usage: 't14' dereferenced in the initialisation of 'i'
  // CHECK-MESSAGES: :[[@LINE-7]]:3: note: the value of 't14' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-10]]:6: note: consider declaring the variable 'i' (for the dereference's result) in the "outer" scope
  // CHECK-MESSAGES: :[[@LINE-9]]:3: note: consider scoping 't14' into the branch, and assign to 'i' during the guarding condition
  // CHECK-MESSAGES: :[[@LINE-7]]:7: note: after the changes, the definition for 'i' here is no longer needed
  i += 1;
}

void single_checked_ctor_initialising_dereference_1() {
  T *t15 = try_create<T>();
  if (!t15)
    return;
  NoDefault ND = t15->i;
  // NO-WARN: 'NoDefault' has no default ctor, so swapping the order of
  // initialisation won't work.
}

void single_checked_ctor_initialising_dereference_2a() {
  T *t16 = try_create<T>();
  // HINT: {{^  }}HasDefault HDa;{{$}}
  if (!t16)
    // HINT: {{^  }}if (T *t16 = try_create<T>(); (!t16) || ((HDa = {t16->i}), void(), false)){{$}}
    return;
  HasDefault HDa = t16->i;
  // HINT: {{^  }};{{$}}
  // CHECK-MESSAGES: :[[@LINE-7]]:6: warning: local pointer variable 't16' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-3]]:20: note: usage: 't16' dereferenced in the initialisation of 'HDa'
  // CHECK-MESSAGES: :[[@LINE-7]]:3: note: the value of 't16' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-10]]:6: note: consider declaring the variable 'HDa' (for the dereference's result) in the "outer" scope
  // CHECK-MESSAGES: :[[@LINE-9]]:3: note: consider scoping 't16' into the branch, and assign to 'HDa' during the guarding condition
  // CHECK-MESSAGES: :[[@LINE-7]]:14: note: after the changes, the definition for 'HDa' here is no longer needed
}

void single_checked_ctor_initialising_dereference_2b() {
  T *t17 = try_create<T>();
  // HINT: {{^  }}HasDefault HDb;{{$}}
  if (!t17)
    // HINT: {{^  }}if (T *t17 = try_create<T>(); (!t17) || ((HDb = {t17->i}), void(), false)){{$}}
    return;
  HasDefault HDb(t17->i);
  // HINT: {{^  }};{{$}}
  // CHECK-MESSAGES: :[[@LINE-7]]:6: warning: local pointer variable 't17' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-3]]:18: note: usage: 't17' dereferenced in the initialisation of 'HDb'
  // CHECK-MESSAGES: :[[@LINE-7]]:3: note: the value of 't17' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-10]]:6: note: consider declaring the variable 'HDb' (for the dereference's result) in the "outer" scope
  // CHECK-MESSAGES: :[[@LINE-9]]:3: note: consider scoping 't17' into the branch, and assign to 'HDb' during the guarding condition
  // CHECK-MESSAGES: :[[@LINE-7]]:14: note: after the changes, the definition for 'HDb' here is no longer needed
}

void single_checked_ctor_initialising_dereference_2c() {
  T *t18 = try_create<T>();
  // HINT: {{^  }}HasDefault HDc;{{$}}
  if (!t18)
    // HINT: {{^  }}if (T *t18 = try_create<T>(); (!t18) || ((HDc = {t18->i}), void(), false)){{$}}
    return;
  HasDefault HDc{t18->i};
  // HINT: {{^  }};{{$}}
  // CHECK-MESSAGES: :[[@LINE-7]]:6: warning: local pointer variable 't18' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-3]]:18: note: usage: 't18' dereferenced in the initialisation of 'HDc'
  // CHECK-MESSAGES: :[[@LINE-7]]:3: note: the value of 't18' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-10]]:6: note: consider declaring the variable 'HDc' (for the dereference's result) in the "outer" scope
  // CHECK-MESSAGES: :[[@LINE-9]]:3: note: consider scoping 't18' into the branch, and assign to 'HDc' during the guarding condition
  // CHECK-MESSAGES: :[[@LINE-7]]:14: note: after the changes, the definition for 'HDc' here is no longer needed
}

void single_checked_ctor_initialising_dereference_2d() {
  T *t19 = try_create<T>();
  // HINT: {{^  }}TrivialAggregate ta;{{$}}
  if (!t19)
    // HINT: {{^  }}if (T *t19 = try_create<T>(); (!t19) || ((ta = {t19->i}), void(), false)){{$}}
    return;
  TrivialAggregate ta{t19->i};
  // HINT: {{^  }};{{$}}
  // CHECK-MESSAGES: :[[@LINE-7]]:6: warning: local pointer variable 't19' might be redundant as it is only used once [readability-redundant-pointer-in-local-scope]
  // CHECK-MESSAGES: :[[@LINE-3]]:23: note: usage: 't19' dereferenced in the initialisation of 'ta'
  // CHECK-MESSAGES: :[[@LINE-7]]:3: note: the value of 't19' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-10]]:6: note: consider declaring the variable 'ta' (for the dereference's result) in the "outer" scope
  // CHECK-MESSAGES: :[[@LINE-9]]:3: note: consider scoping 't19' into the branch, and assign to 'ta' during the guarding condition
  // CHECK-MESSAGES: :[[@LINE-7]]:20: note: after the changes, the definition for 'ta' here is no longer needed
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

/* Reduced case from LLVM IteratorChecker. */
struct TemplateParamsRange {
  unsigned size() const;
  void *getParam(unsigned Index) const;
};
struct Template {
  TemplateParamsRange *getTemplateParameters() const;
};
struct Function {
  Template *getPrimaryTemplate() const;
  void useForSomething() const;
};
Function *getFunction();

void template_param() {
  const auto *Func = getFunction();
  Func->useForSomething();

  const auto *Templ = Func->getPrimaryTemplate();
  if (!Templ)
    return;

  const auto *TParams = Templ->getTemplateParameters();
  // CHECK-MESSAGES: :[[@LINE-5]]:15: warning: local pointer variable 'Templ' might be redundant as it is only used once
  // CHECK-MESSAGES: :[[@LINE-2]]:25: note: usage: 'Templ' dereferenced in the initialisation of 'TParams'
  // CHECK-MESSAGES: :[[@LINE-6]]:3: note: the value of 'Templ' is guarded by this branch, resulting in 'return'
  // CHECK-MESSAGES: :[[@LINE-8]]:15: note: consider declaring the variable 'TParams' (for the dereference's result) in the "outer" scope
  // CHECK-MESSAGES: :[[@LINE-8]]:3: note: consider scoping 'Templ' into the branch, and assign to 'TParams' during the guarding condition
  // CHECK-MESSAGES: :[[@LINE-6]]:15: note: after the changes, the definition for 'TParams' here is no longer needed
  for (unsigned I = 0; I < TParams->size(); ++I) {
  }
  // CHECK-MESSAGES: :[[@LINE-9]]:15: warning: local pointer variable 'TParams' might be redundant as it is only used once
  // CHECK-MESSAGES: :[[@LINE-3]]:28: note: usage: 'TParams' dereferenced here
  // CHECK-MESSAGES: :[[@LINE-4]]:28: note: consider using the code that initialises 'TParams' here
}
