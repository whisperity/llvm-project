//===--- RedundantPointerDereferenceChainCheck.h - clang-tidy ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_REDUNDANTPOINTERDEREFERENCECHAINCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_REDUNDANTPOINTERDEREFERENCECHAINCHECK_H

#include "RedundantPointerCheck.h"

namespace clang {
namespace tidy {
namespace readability {

/// FIXME: Write a short description.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/readability-redundant-pointer-dereference-chain.html
class RedundantPointerDereferenceChainCheck : public RedundantPointerCheck {
public:
  RedundantPointerDereferenceChainCheck(StringRef Name,
                                        ClangTidyContext *Context)
      : RedundantPointerCheck(Name, Context) {}
  void onEndOfTranslationUnit() override;
};

} // namespace readability
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_REDUNDANTPOINTERDEREFERENCECHAINCHECK_H
