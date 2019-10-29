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

// clang-format off
// FIXME: Remove debug things.
#define  DEBUG_TYPE "SuperfluousPtr"
#include "llvm/Support/Debug.h"
// clang-format on

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace modernize {

namespace {

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
}

void SuperfluousLocalPtrVariableCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *VarInit = Result.Nodes.getNodeAs<VarDecl>(InitedVarId)) {
    const auto *RefExpr =
        Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrIdInInit);
    const auto *MemExpr = Result.Nodes.getNodeAs<MemberExpr>(MemberExprId);
    const auto *RefPtrVar = cast<VarDecl>(RefExpr->getDecl());

    References[RefPtrVar].addUsage(
        new PtrVarDerefInit{RefExpr, MemExpr, VarInit});
    return;
  }

  if (const auto *PtrDRE = Result.Nodes.getNodeAs<DeclRefExpr>(UsedPtrId)) {
    const auto *RefPtrVar = cast<VarDecl>(PtrDRE->getDecl());
    References[RefPtrVar].addUsage(new PtrVarParamPassing{PtrDRE});
    return;
  }
}

void SuperfluousLocalPtrVariableCheck::onEndOfTranslationUnit() {
  for (const auto &RefData : References) {
    LLVM_DEBUG(
        llvm::dbgs() << "Usages for ptr var " << RefData.first << '\n';
        RefData.first->dump(llvm::dbgs()); llvm::dbgs() << '\n';

        for (const PtrVarDeclUsageInfo *Usage
             : RefData.second.getUsages()) {
          if (const auto *PassUsage = dyn_cast<PtrVarParamPassing>(Usage)) {
            llvm::dbgs() << "Parameter was passed (\"read\") in the following "
                            "expression:\n";
            PassUsage->getUsageExpr()->dump(llvm::dbgs());
            llvm::dbgs() << '\n';
          }
          if (const auto *DerefUsage = dyn_cast<PtrVarDereference>(Usage)) {
            llvm::dbgs() << "This usage is a dereference. The dereference "
                            "expression looks like this:\n";
            if (const auto *UnaryOpExpr = DerefUsage->getUnaryOperator())
              UnaryOpExpr->dump(llvm::dbgs());
            else if (const auto *MemExpr = DerefUsage->getMemberExpr())
              MemExpr->dump(llvm::dbgs());
            llvm::dbgs() << '\n';
          }
          if (const auto *VarInitUsage = dyn_cast<PtrVarDerefInit>(Usage)) {
            llvm::dbgs() << "This dereference initialises another variable!\n";
            VarInitUsage->getInitialisedVar()->dump(llvm::dbgs());

            llvm::dbgs() << '\n';
          }

          llvm::dbgs()
              << "\n----------------------------------------------------\n";
        });
  }

#if 0
  for (const auto &RefPair : References) {
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
  }
#endif
}

// FIXME: Add debug prints to these data structure manipulators.

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
  return true;
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
