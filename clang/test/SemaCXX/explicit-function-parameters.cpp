// RUN: %clang_cc1 -fsyntax-only -verify -fexplicit-function-params %s

// expected-no-diagnostics

void int_double(explicit int a, explicit double b) {}
