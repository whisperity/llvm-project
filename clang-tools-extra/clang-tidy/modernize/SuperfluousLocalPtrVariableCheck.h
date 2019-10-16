//===--- SuperfluousLocalPtrVariableCheck.h - clang-tidy --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_SUPERFLUOUSLOCALPTRVARIABLECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_SUPERFLUOUSLOCALPTRVARIABLECHECK_H

#include "../ClangTidyCheck.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TinyPtrVector.h"

namespace clang {
namespace tidy {
namespace modernize {

/// Holds information about usages (referencing expressions) of a declaration.
class DeclUsageInfo {
public:
  bool hasUsage() const { return !Usage.empty(); }
  const DeclRefExpr *getUsage() const {
    return hasUsage() ? Usage.front() : nullptr;
  }
  void setUsage(const DeclRefExpr *DRE);
  void ignoreUsage(const DeclRefExpr *DRE);

private:
  // Size is 1 is as we normally only care for variables that are referenced
  // only once.
  llvm::TinyPtrVector<const DeclRefExpr *> Usage;
  llvm::SmallVector<const DeclRefExpr *, 2> IgnoredUsages;
};

using ReferencingMap = llvm::DenseMap<const VarDecl *, DeclUsageInfo>;

/// FIXME: Write a short description.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/modernize-superfluous-local-ptr-variable.html
class SuperfluousLocalPtrVariableCheck : public ClangTidyCheck {
public:
  SuperfluousLocalPtrVariableCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onEndOfTranslationUnit() override;

private:
  ReferencingMap References;
};

} // namespace modernize
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_SUPERFLUOUSLOCALPTRVARIABLECHECK_H
