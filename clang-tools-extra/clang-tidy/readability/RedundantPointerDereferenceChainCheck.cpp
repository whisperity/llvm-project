//===--- RedundantPointerDereferenceChainCheck.cpp - clang-tidy -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RedundantPointerDereferenceChainCheck.h"
#include "clang/AST/ASTContext.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace readability {

void RedundantPointerDereferenceChainCheck::onEndOfTranslationUnit() {
  const LangOptions &LOpts = getLangOpts();

  for (const auto &Usage : Usages) {
    const VarDecl *PtrVar = Usage.first;
    diag(PtrVar->getLocation(), "foo bar baz qux");
  }
}

} // namespace readability
} // namespace tidy
} // namespace clang
