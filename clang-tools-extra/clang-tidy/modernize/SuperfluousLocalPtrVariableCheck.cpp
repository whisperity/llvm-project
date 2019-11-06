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

static const char InitedVarId[] = "inited-var";
static const char UsedPtrId[] = "used-ptr";
static const char DereferencedPtrId[] = "deref-ptr";
static const char DerefUsageExprId[] = "usage-stmt";
static const char PtrGuardId[] = "ptr-guard";
static const char EarlyReturnStmtId[] = "early-ret";

/// Matches pointer-type variables that are local to the function.
// TODO: Later on this check could be broadened to work with references, too.
static const auto PointerLocalVarDecl =
    varDecl(anyOf(hasType(pointerType()),
                  hasType(autoType(hasDeducedType(pointerType())))),
            unless(parmVarDecl()));

/// Matches every usage of a local pointer variable.
static const auto PtrVarUsage = declRefExpr(to(PointerLocalVarDecl));

static const StatementMatcher PtrDereference = anyOf(
    memberExpr(isArrow(), hasDescendant(PtrVarUsage.bind(DereferencedPtrId)))
        .bind(DerefUsageExprId),
    unaryOperator(hasOperatorName("*"),
                  hasDescendant(PtrVarUsage.bind(DereferencedPtrId)))
        .bind(DerefUsageExprId));

static const auto VarInitFromPtrDereference =
    varDecl(hasInitializer(ignoringParenImpCasts(PtrDereference)))
        .bind(InitedVarId);

static const auto EarlyReturnLike =
    stmt(anyOf(returnStmt(), continueStmt(), breakStmt(), gotoStmt(),
               cxxThrowExpr(),
               callExpr(callee(functionDecl(hasName("longjmp"))))))
        .bind(EarlyReturnStmtId);

/// Matches conditional checks on a pointer variable where the condition results
/// in breaking control flow, such as early return, continue, or throwing.
///
/// Trivial example of findings:
///     if (P) return;
///     if (!P) { continue; }
static const auto PtrGuard =
    ifStmt(hasCondition(hasDescendant(PtrVarUsage.bind(UsedPtrId))),
           hasThen(anyOf(EarlyReturnLike,
                         compoundStmt(statementCountIs(1),
                                      hasAnySubstatement(EarlyReturnLike)))),
           unless(hasElse(stmt())))
        .bind(PtrGuardId);

// ifStmt(hasCondition(hasDescendant(declRefExpr(to(varDecl(hasType(pointerType())))).bind("A"))))

// FIXME: Ignore usages in trivial (early-return or early-continue) 'if's.
// FIXME: The real end goal of this check is to find a pair of ptrs created
//        by dereferencing the first.

void SuperfluousLocalPtrVariableCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(PtrVarUsage.bind(UsedPtrId), this);
  Finder->addMatcher(PtrDereference, this);
  Finder->addMatcher(VarInitFromPtrDereference, this);
  Finder->addMatcher(PtrGuard, this);
}

void SuperfluousLocalPtrVariableCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *GuardIf = Result.Nodes.getNodeAs<IfStmt>(PtrGuardId)) {
    LLVM_DEBUG(GuardIf->dump(llvm::dbgs()); llvm::dbgs() << '\n';);

    return;
  }

  if (const auto *VarInit = Result.Nodes.getNodeAs<VarDecl>(InitedVarId)) {
    const auto *DerefExpr = Result.Nodes.getNodeAs<Expr>(DerefUsageExprId);
    const auto *DRefExpr =
        Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrId);
    const auto *RefPtrVar = cast<VarDecl>(DRefExpr->getDecl());

    References[RefPtrVar].addUsage(
        new PtrVarDerefInit{DRefExpr, DerefExpr, VarInit});
    return;
  }

  if (const auto *PtrDRE =
          Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrId)) {
    const auto *RefPtrVar = cast<VarDecl>(PtrDRE->getDecl());
    const auto *DerefExpr = Result.Nodes.getNodeAs<Expr>(DerefUsageExprId);

    References[RefPtrVar].addUsage(new PtrVarDereference{PtrDRE, DerefExpr});
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
            llvm::dbgs() << "A usage in a dereference. The dereference "
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

    const PtrVarDeclUsageCollection &Usages = RefData.second;
    if (Usages.hasMultipleUsages()) {
      LLVM_DEBUG(RefData.first->dump(llvm::dbgs());
                 llvm::dbgs() << "\n has multiple usages -- ignoring!\n";);
      continue;
    }

    const PtrVarDeclUsageInfo *UI = Usages.getNthUsage(1);
    const auto *UseExpr = UI->getUsageExpr();
    const auto *UsedDecl = UseExpr->getDecl();
    const char *OutMsg = nullptr;
    if (isa<PtrVarDereference>(UI))
      OutMsg = "local pointer variable %0 only participates in one dereference";
    else if (isa<PtrVarParamPassing>(UI))
      OutMsg = "local pointer variable %0 only used once";

    assert(OutMsg && "Unhandled 'PtrVarDeclUsageInfo' kind for diag message");

    diag(UseExpr->getLocation(), OutMsg) << UsedDecl;

    diag(UsedDecl->getLocation(), "%0 defined here", DiagnosticIDs::Note)
        << UsedDecl;
  }
}

bool PtrVarDeclUsageCollection::addUsage(PtrVarDeclUsageInfo *UsageInfo) {
  assert(UsageInfo && "provide a valid UsageInfo instance");
  LLVM_DEBUG(
      llvm::dbgs() << "Adding usage " << UsageInfo->getUsageExpr() << '\n';
      UsageInfo->getUsageExpr()->dump(llvm::dbgs()); llvm::dbgs() << '\n';);

  if (isIgnored(UsageInfo->getUsageExpr())) {
    LLVM_DEBUG(llvm::dbgs() << "Adding usage " << UsageInfo->getUsageExpr()
                            << " but it is on ignore list!\n";);
    return false;
  }

  for (const PtrVarDeclUsageInfo *DUI : CollectedUses)
    if (DUI == UsageInfo || DUI->getUsageExpr() == UsageInfo->getUsageExpr()) {
      LLVM_DEBUG(llvm::dbgs() << "Adding usage " << DUI->getUsageExpr()
                              << " but it has already been found!\n";);
      return false;
    }

  CollectedUses.push_back(UsageInfo);
  return true;
}

bool PtrVarDeclUsageCollection::replaceUsage(PtrVarDeclUsageInfo *OldInfo,
                                             PtrVarDeclUsageInfo *NewInfo) {
  assert(OldInfo && NewInfo && "provide valid UsageInfo instances");
  assert(OldInfo != NewInfo && "replacement of usage info with same instance");

  LLVM_DEBUG(llvm::dbgs() << "Replacing usage " << OldInfo->getUsageExpr()
                          << " with " << NewInfo->getUsageExpr() << '\n';
             OldInfo->getUsageExpr()->dump(llvm::dbgs()); llvm::dbgs() << '\n';
             NewInfo->getUsageExpr()->dump(llvm::dbgs());
             llvm::dbgs() << '\n';);

  if (isIgnored(NewInfo->getUsageExpr())) {
    LLVM_DEBUG(llvm::dbgs() << "Replacing usage " << OldInfo->getUsageExpr()
                            << " with " << NewInfo->getUsageExpr()
                            << " but it is on ignore list!\n";);
    return false;
  }

  size_t OldInfoIdx = 0;
  bool OldInfoFound = false;
  for (size_t Idx = 0; Idx < CollectedUses.size(); ++Idx) {
    if (CollectedUses[Idx] == NewInfo ||
        CollectedUses[Idx]->getUsageExpr() == NewInfo->getUsageExpr()) {
      LLVM_DEBUG(llvm::dbgs() << "Replacing usage " << OldInfo->getUsageExpr()
                              << " with " << NewInfo->getUsageExpr()
                              << " but it is already added!\n";);
      return false;
    }

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
PtrVarDeclUsageCollection::getNthUsageOfKind(std::size_t N) const {
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
