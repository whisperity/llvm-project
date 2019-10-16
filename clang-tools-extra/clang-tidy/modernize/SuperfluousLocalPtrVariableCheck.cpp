//===--- SuperfluousLocalPtrVariableCheck.cpp - clang-tidy ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SuperfluousLocalPtrVariableCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace modernize {

namespace {

const char DeclId[] = "ptr-decl";
const char UsageId[] = "usage";

const auto PtrDeclMatcher = varDecl(anyOf(
    hasType(pointerType()), hasType(autoType(hasDeducedType(pointerType())))));

// FIXME: Ignore usages in trivial (early-return or early-continue) 'if's.
// FIXME: The real end goal of this check is to find a pair of ptrs created
//        by dereferencing the first, i.e. this:
//
//        T* tp = ...;
//        if (!tp) return; // This should be ignored.
//        U* up = tp->something;
//
//        Having tp here is superfluous, use initializing if or ?-> :P

} // namespace

void SuperfluousLocalPtrVariableCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(PtrDeclMatcher.bind(DeclId), this);
  // FIXME: Consider dereferencing as a "different" kind of usage, as that's
  //        the one that can be modernized, passing the ptr forward as an
  //        argument cannot.
  Finder->addMatcher(declRefExpr(to(PtrDeclMatcher)).bind(UsageId), this);
}

void SuperfluousLocalPtrVariableCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *MatchedVar = Result.Nodes.getNodeAs<VarDecl>(DeclId)) {
    if (isa<ParmVarDecl>(MatchedVar))
      // Function parameters should not match for this check as changing
      // arguments incurs API break.
      return;

    References.FindAndConstruct(MatchedVar);
    return;
  }

  if (const auto *MatchedUsage = Result.Nodes.getNodeAs<DeclRefExpr>(UsageId)) {
    const auto *ReferencedVar = dyn_cast<VarDecl>(MatchedUsage->getDecl());
    if (!ReferencedVar)
      llvm_unreachable(
          "Matcher expression to VarDecl failed to match a VarDecl!");

    auto UsageInfoIt = References.find(ReferencedVar);
    if (UsageInfoIt == References.end())
      // Referenced variable was found to have multiple references already.
      return;

    if (UsageInfoIt->second.hasUsage()) {
      // If multiple usages are found, stop tracking the variable.
      References.erase(UsageInfoIt);
      return;
    }

    UsageInfoIt->second.setUsage(MatchedUsage);
    return;
  }
}

void SuperfluousLocalPtrVariableCheck::onEndOfTranslationUnit() {
  for (const auto &RefPair : References) {
    const DeclRefExpr *Usage = RefPair.second.getUsage();
    if (!Usage)
      continue;

    const VarDecl *Variable = RefPair.first;

    diag(Usage->getLocation(),
         "local pointer variable %0 only participates in a single dereference")
        << Usage->getDecl()
        << FixItHint::CreateReplacement(
               Usage->getSourceRange(),
               (Twine(Variable->getName()) + "?").str());
    diag(Variable->getLocation(), "%0 defined here", DiagnosticIDs::Note)
        << Variable;
  }
}

void DeclUsageInfo::setUsage(const DeclRefExpr *DRE) {
  // QUESTION: SmallVector has no .find()?
  for (const auto &E : IgnoredUsages)
    if (E == DRE)
      return;

  if (hasUsage())
    Usage.pop_back();
  Usage.push_back(DRE);
}

void DeclUsageInfo::ignoreUsage(const DeclRefExpr *DRE) {
  IgnoredUsages.push_back(DRE);
}

} // namespace modernize
} // namespace tidy
} // namespace clang
