//===--------------- RedundantPointerCheck.cpp - clang-tidy ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RedundantPointerCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace readability {

static const char InitedVarId[] = "inited-var";
static const char UsedPtrId[] = "used-ptr";
static const char DereferencedPtrId[] = "deref-ptr";
static const char DerefUsageExprId[] = "usage-stmt";
static const char PtrGuardId[] = "ptr-guard";
static const char EarlyReturnStmtId[] = "early-ret";

namespace matchers {

/// Matches pointer-type variables that are local to the function.
static const auto PointerLocalVarDecl =
    varDecl(hasInitializer(expr()),
            anyOf(hasType(pointerType()),
                  hasType(autoType(hasDeducedType(pointerType())))),
            unless(parmVarDecl()));

/// Matches every usage of a local pointer variable.
static const auto PtrVarUsage = declRefExpr(to(PointerLocalVarDecl));

static const auto PtrDereference = stmt(anyOf(
    memberExpr(isArrow(), hasDescendant(PtrVarUsage.bind(DereferencedPtrId)))
        .bind(DerefUsageExprId),
    unaryOperator(hasOperatorName("*"),
                  hasDescendant(PtrVarUsage.bind(DereferencedPtrId)))
        .bind(DerefUsageExprId)));

/// Matches construction expressions which "trivially" initialise something from
/// a pointer.
static const auto ConstructExprWithPtrDereference =
    ignoringElidableConstructorCall(
        cxxConstructExpr(argumentCountIs(1), hasArgument(0, PtrDereference)));

static const auto VarInitFromPtrDereference =
    varDecl(anyOf(hasInitializer(ignoringParenImpCasts(anyOf(
                      // Directly initialise from dereference: int i = p->i
                      PtrDereference,
                      // Assign-initialise through ctor: T t = p->t;
                      ConstructExprWithPtrDereference,
                      initListExpr(hasDescendant(
                          // Aggregate initialise: S s = {p->i};
                          PtrDereference))))),
                  hasDescendant(
                      // Initialise with ctor call: T t(p->t);
                      expr(ConstructExprWithPtrDereference))))
        .bind(InitedVarId);

static const auto FlowBreakingStmt =
    stmt(anyOf(returnStmt(), continueStmt(), breakStmt(), gotoStmt(),
               cxxThrowExpr(), callExpr(callee(functionDecl(isNoReturn())))))
        .bind(EarlyReturnStmtId);

/// Matches conditional checks on a pointer variable where the condition results
/// in breaking control flow, such as early return, continue, or throwing.
///
/// Trivial example of findings:
///     if (P) return;
///     if (!P) { continue; }
static const auto PtrGuard =
    ifStmt(hasCondition(hasDescendant(PtrVarUsage.bind(UsedPtrId))),
           hasThen(anyOf(FlowBreakingStmt,
                         compoundStmt(statementCountIs(1),
                                      hasAnySubstatement(FlowBreakingStmt)))),
           unless(hasElse(stmt())))
        .bind(PtrGuardId);

} // namespace matchers

void RedundantPointerCheck::registerMatchers(MatchFinder *Finder) {
  using namespace matchers;

  Finder->addMatcher(declRefExpr(unless(isExpansionInSystemHeader()),
                                 PtrVarUsage.bind(UsedPtrId)),
                     this);
  Finder->addMatcher(
      stmt(unless(isExpansionInSystemHeader()), matchers::PtrDereference),
      this);
  Finder->addMatcher(
      varDecl(unless(isExpansionInSystemHeader()), VarInitFromPtrDereference),
      this);
  Finder->addMatcher(
      ifStmt(unless(isExpansionInSystemHeader()), matchers::PtrGuard), this);
}

#define ASTNODE_FROM_MACRO(N) N->getSourceRange().getBegin().isMacroID()

void RedundantPointerCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *GuardIf = Result.Nodes.getNodeAs<IfStmt>(PtrGuardId)) {
    const auto *FlowStmt = Result.Nodes.getNodeAs<Stmt>(EarlyReturnStmtId);
    const auto *DRefExpr = Result.Nodes.getNodeAs<DeclRefExpr>(UsedPtrId);
    const auto *RefPtrVar = cast<VarDecl>(DRefExpr->getDecl());

    if (ASTNODE_FROM_MACRO(GuardIf) || ASTNODE_FROM_MACRO(FlowStmt) ||
        ASTNODE_FROM_MACRO(DRefExpr) || ASTNODE_FROM_MACRO(RefPtrVar))
      return;
    Usages[RefPtrVar].addUsage(new PtrGuard{DRefExpr, GuardIf, FlowStmt});
    return;
  }

  if (const auto *VarInit = Result.Nodes.getNodeAs<VarDecl>(InitedVarId)) {
    const auto *DerefExpr = Result.Nodes.getNodeAs<Expr>(DerefUsageExprId);
    const auto *DRefExpr =
        Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrId);
    const auto *RefPtrVar = cast<VarDecl>(DRefExpr->getDecl());

    if (ASTNODE_FROM_MACRO(VarInit) || ASTNODE_FROM_MACRO(DerefExpr) ||
        ASTNODE_FROM_MACRO(DRefExpr) || ASTNODE_FROM_MACRO(RefPtrVar))
      return;
    Usages[RefPtrVar].addUsage(
        new PtrDerefVarInit{DRefExpr, DerefExpr, VarInit});
    return;
  }

  if (const auto *PtrDRE =
          Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrId)) {
    const auto *RefPtrVar = cast<VarDecl>(PtrDRE->getDecl());
    const auto *DerefExpr = Result.Nodes.getNodeAs<Expr>(DerefUsageExprId);

    if (ASTNODE_FROM_MACRO(PtrDRE) || ASTNODE_FROM_MACRO(RefPtrVar) ||
        ASTNODE_FROM_MACRO(DerefExpr))
      return;
    Usages[RefPtrVar].addUsage(new PtrDereference{PtrDRE, DerefExpr});
    return;
  }

  if (const auto *PtrDRE = Result.Nodes.getNodeAs<DeclRefExpr>(UsedPtrId)) {
    const auto *RefPtrVar = cast<VarDecl>(PtrDRE->getDecl());
    if (ASTNODE_FROM_MACRO(PtrDRE) || ASTNODE_FROM_MACRO(RefPtrVar))
      return;
    Usages[RefPtrVar].addUsage(new PtrArgument{PtrDRE});
    return;
  }
}

#undef ASTNODE_FROM_MACRO

bool UsageCollection::addUsage(PtrUsage *UsageInfo) {
  assert(UsageInfo && "provide a valid UsageInfo instance");

  for (const PtrUsage *DUI : CollectedUses) {
    if (DUI == UsageInfo)
      return false;
    if (DUI->getUsageExpr() == UsageInfo->getUsageExpr()) {
      delete UsageInfo;
      return false;
    }
  }

  CollectedUses.push_back(UsageInfo);
  return true;
}

UsageCollection::UsageCollection(UsageCollection &&UC) {
  CollectedUses.insert(CollectedUses.end(), UC.CollectedUses.begin(),
                       UC.CollectedUses.end());
  UC.CollectedUses.clear();
}

UsageCollection &UsageCollection::operator=(UsageCollection &&UC) {
  if (&UC == this)
    return *this;

  CollectedUses.insert(CollectedUses.end(), UC.CollectedUses.begin(),
                       UC.CollectedUses.end());
  UC.CollectedUses.clear();
  return *this;
}

UsageCollection::~UsageCollection() {
  llvm::for_each(CollectedUses, [](PtrUsage *UI) { delete UI; });
}

} // namespace readability
} // namespace tidy
} // namespace clang
