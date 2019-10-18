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

#if 0
const char DeclId[] = "ptr-decl";
const char UsageId[] = "usage";
#endif

const char InitedVarId[] = "inited-var";
const char UsedPtrId[] = "used-ptr";
const char DereferencedPtrIdInInit[] = "derefed-ptr";
const char MemberExprId[] = "mem-expr";

/// Matches pointer-type variables that are local to the function.
// TODO: Later on this check could be broadened to work with references, too.
const auto PointerLocalVarDecl =
    varDecl(anyOf(hasType(pointerType()),
                  hasType(autoType(hasDeducedType(pointerType())))),
            unless(parmVarDecl()));

/// Matches every usage of a local pointer variable.
const auto PtrVarUsage = declRefExpr(to(PointerLocalVarDecl)).bind(UsedPtrId);

/// Matches variables that are initialised by dereferencing a local ptr.
const auto VarInitFromPtrDereference =
    varDecl(hasInitializer(ignoringParenImpCasts(
                memberExpr(isArrow(),
                           hasDescendant(declRefExpr(to(PointerLocalVarDecl))
                                             .bind(DereferencedPtrIdInInit)))
                    .bind(MemberExprId))))
        .bind(InitedVarId);

// ifStmt(hasCondition(hasDescendant(declRefExpr(to(varDecl(hasType(pointerType())))).bind("A"))))

// FIXME: Ignore usages in trivial (early-return or early-continue) 'if's.
// FIXME: The real end goal of this check is to find a pair of ptrs created
//        by dereferencing the first.

} // namespace

void SuperfluousLocalPtrVariableCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(VarInitFromPtrDereference, this);
  Finder->addMatcher(PtrVarUsage, this);

  // FIXME: Consider dereferencing as a "different" kind of usage, as that's
  //        the one that can be modernized, passing the ptr forward as an
  //        argument cannot.
}

void SuperfluousLocalPtrVariableCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *VarInit = Result.Nodes.getNodeAs<VarDecl>(InitedVarId)) {
    const auto *DeferExpr =
        Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrIdInInit);
    const auto *DerefPtrVar = cast<VarDecl>(DerefDre->getDecl());

    References[DerefPtrVar].addUsage(new PtrVarDerefInit)

        if (!trySetDefiniteUsagePoint(References, DerefVar, DerefDre)) {
      // Multiple usages have been found, ignore this VarDecl.

      /*llvm::errs() << "Multiple usages found for var ";
      DerefVar->dump();
      llvm::errs() << "\n previous usage at: ";
      ReferenceInfo.getUsage()->dump();
      llvm::errs() << "\nMarking var as multiusage.\n";*/

      References.erase(DerefVar);
    }

    return;
  }

  if (const auto *PtrDRE = Result.Nodes.getNodeAs<DeclRefExpr>(UsedPtrId)) {
    const auto *DerefVar = cast<VarDecl>(PtrDRE->getDecl());
    if (!trySetDefiniteUsagePoint(References, DerefVar, PtrDRE)) {
      // Multiple usages have been found, ignore this VarDecl.

      /*llvm::errs() << "Multiple usages found for var ";
      DerefVar->dump();
      llvm::errs() << "\n previous usage at: ";
      ReferenceInfo.getUsage()->dump();
      llvm::errs() << "\nMarking var as multiusage.\n";*/

      References.erase(DerefVar);
    }

    return;
  }

#if 0
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
#endif
}

void SuperfluousLocalPtrVariableCheck::onEndOfTranslationUnit() {
  /*for (const auto &RefPair : References) {
    const DeclRefExpr *Usage = RefPair.second.getUsage();
    if (!Usage)
      continue;

    const VarDecl *Variable = RefPair.first;

    diag(Usage->getLocation(),
         "local pointer variable %0 only participates in one dereference")
        << Usage->getDecl();
    //<< FixItHint::CreateReplacement(
    //       Usage->getSourceRange(),
    //       (Twine(Variable->getName()) + "?").str());
    diag(Variable->getLocation(), "%0 defined here", DiagnosticIDs::Note)
        << Variable;
  }*/
}

bool PtrVarDeclUsageCollection::addUsage(PtrVarDeclUsageInfo *UsageInfo) {
  assert(UsageInfo && "provide a valid UsageInfo instance");
  if (isIgnored(UsageInfo->getUsageExpr()))
    return false;

  for (const PtrVarDeclUsageInfo *DUI : CollectedUses)
    if (DUI == UsageInfo || DUI->getUsageExpr() == UsageInfo->getUsageExpr())
      return false;

  CollectedUses.push_back(UsageInfo);
  return true;
}

bool PtrVarDeclUsageCollection::replaceUsage(PtrVarDeclUsageInfo *OldInfo,
                                             PtrVarDeclUsageInfo *NewInfo) {
  assert(OldInfo && NewInfo && "provide valid UsageInfo instances");
  assert(OldInfo != NewInfo && "replacement of usage info with same instance");

  if (isIgnored(NewInfo->getUsageExpr()))
    return false;

  size_t OldInfoIdx = 0;
  bool OldInfoFound = false;
  for (size_t Idx = 0; Idx < CollectedUses.size(); ++Idx) {
    if (CollectedUses[Idx] == NewInfo ||
        CollectedUses[Idx]->getUsageExpr() == NewInfo->getUsageExpr())
      return false;

    if (CollectedUses[Idx] == OldInfo) {
      OldInfoFound = true;
      OldInfoIdx = Idx;
    }
  }
  assert(OldInfoFound && "replacement of usage that was not added before");

  CollectedUses[OldInfoIdx] = NewInfo;
  delete OldInfo;
}

template <PtrVarDeclUsageInfo::DUIKind Kind>
PtrVarDeclUsageInfo *
PtrVarDeclUsageCollection::getNthUsageOfKind(size_t N) const {
  size_t Counter = 0;
  for (PtrVarDeclUsageInfo *DUI : CollectedUses) {
    if (DUI->getKind() == Kind) {
      if (++Counter == N)
        return DUI;
    }
  }
  return nullptr;
}

PtrVarDeclUsageCollection::~PtrVarDeclUsageCollection() {
  for (PtrVarDeclUsageInfo *DUI : CollectedUses)
    delete DUI;
}

} // namespace modernize
} // namespace tidy
} // namespace clang
