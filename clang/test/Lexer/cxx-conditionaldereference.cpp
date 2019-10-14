// NO-RUN: %clang_cc1 %s -verify
// RUN: %clang_cc1 %s -E -o - | FileCheck %s --check-prefix=CHECK

// FIXME: Hide this thing behind an -ffeature.

// expected-no-diagnostics

struct A { int i; };

A a;
A *ap = &a;
auto* ap2 = ap ? ap->i : nullptr;
auto* memp = ap?->i;

// CHECK: preprocess1: ?
// CHECK: preprocess2: ->
// CHECK: preprocess3: :
// CHECK: preprocess4: ?->
