//===-- llvm/Instrinsics.h - LLVM Intrinsic Function Handling ---*- C++ -*-===//
//
// This file defines a set of enums which allow processing of intrinsic
// functions.  Values of these enum types are returned by
// Function::getIntrinsicID.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_INTRINSICS_H
#define LLVM_INTRINSICS_H

/// LLVMIntrinsic Namespace - This namespace contains an enum with a value for
/// every intrinsic/builtin function known by LLVM.  These enum values are
/// returned by Function::getIntrinsicID().
///
namespace LLVMIntrinsic {
  enum ID {
    not_intrinsic = 0,   // Must be zero

    va_start,       // Used to represent a va_start call in C
    va_end,         // Used to represent a va_end call in C
    va_copy,        // Used to represent a va_copy call in C

    setjmp,         // Used to represent a setjmp call in C
    longjmp,        // Used to represent a longjmp call in C

    //===------------------------------------------------------------------===//
    // This section defines intrinsic functions used to represent Alpha
    // instructions...
    //
    alpha_ctlz,     // CTLZ (count leading zero): counts the number of leading
                    // zeros in the given ulong value
    alpha_cttz,     // CTTZ (count trailing zero): counts the number of trailing
                    // zeros in the given ulong value 
    alpha_ctpop,    // CTPOP (count population): counts the number of ones in
                    // the given ulong value 
    alpha_umulh,    // UMULH (unsigned multiply quadword high): Takes two 64-bit
                    // (ulong) values, and returns the upper 64 bits of their
                    // 128 bit product as a ulong
  };
}

#endif
