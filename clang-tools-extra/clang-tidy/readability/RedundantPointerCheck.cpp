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

// FIXME: Don't match loop variables of any kind as they can't be elided.

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
    ifStmt(hasCondition(allOf(hasDescendant(PtrVarUsage.bind(UsedPtrId)),
                              unless(hasDescendant(PtrDereference)))),
           hasThen(anyOf(FlowBreakingStmt,
                         compoundStmt(statementCountIs(1),
                                      hasAnySubstatement(FlowBreakingStmt)))),
           unless(hasElse(stmt())))
        .bind(PtrGuardId);

} // namespace matchers

/// Helper class that handles match callbacks for pointer usages within a
/// function.
class RedundantPointerCheck::PtrUseModelCallback
    : public MatchFinder::MatchCallback {
public:
#define ASTNODE_FROM_MACRO(N) N->getSourceRange().getBegin().isMacroID()
  void run(const MatchFinder::MatchResult &Result) override {
    // PointerPtrUsages:
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

    // PointeePtrUsages:
    const DeclRefExpr *DRE;
    bool Added = false;

    if (const auto *VarInit = Result.Nodes.getNodeAs<VarDecl>(InitedVarId)) {
      const auto *DerefExpr = Result.Nodes.getNodeAs<Expr>(DerefUsageExprId);
      DRE = Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrId);
      const auto *RefPtrVar = cast<VarDecl>(DRE->getDecl());
      if (ASTNODE_FROM_MACRO(VarInit) || ASTNODE_FROM_MACRO(DerefExpr) ||
          ASTNODE_FROM_MACRO(DRE) || ASTNODE_FROM_MACRO(RefPtrVar))
        return;

      Added = Usages[RefPtrVar].addUsage(
          new PtrDerefVarInit{DRE, DerefExpr, VarInit});
    } else if ((DRE = Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrId))) {
      const auto *RefPtrVar = cast<VarDecl>(DRE->getDecl());
      const auto *DerefExpr = Result.Nodes.getNodeAs<Expr>(DerefUsageExprId);
      if (ASTNODE_FROM_MACRO(DRE) || ASTNODE_FROM_MACRO(RefPtrVar) ||
          ASTNODE_FROM_MACRO(DerefExpr))
        return;

      Added = Usages[RefPtrVar].addUsage(new PtrDereference{DRE, DerefExpr});
    } else if ((DRE = Result.Nodes.getNodeAs<DeclRefExpr>(UsedPtrId))) {
      const auto *RefPtrVar = cast<VarDecl>(DRE->getDecl());
      if (ASTNODE_FROM_MACRO(DRE) || ASTNODE_FROM_MACRO(RefPtrVar))
        return;

      Added = Usages[RefPtrVar].addUsage(new PtrArgument{DRE});
    }

    // If a new usage is added when ptr-only usage lexically after the usage is
    // found (due to ptr-only usage statements matching earlier, and their
    // sub-statements ignored by addUsage()), these usages must be turned into a
    // base class usage so the diagnostic builder don't consider, e.g. an if()
    // *after* a use guarding the use itself.
    if (Added) {
      if (isPointerOnlyUseFoundAlreadyFor<PtrGuard>(DRE))
        turnSubtypeUsesToBase<PtrArgument, PtrGuard>(DRE);
    }
  }
#undef ASTNODE_FROM_MACRO

  RedundantPointerCheck::UsageMap &getUsages() { return Usages; }
  void reset() { Usages.clear(); }

private:
  RedundantPointerCheck::UsageMap Usages;

  template <typename PtrOnlyUseTy>
  SourceLocation getAlreadyFoundPtrUsageLocation(const VarDecl *PtrVar) {
    auto It = Usages.find(PtrVar);
    if (It == Usages.end())
      return {};
    const UsageCollection::UseVector &PtrUsages =
        It->second.getUsagesOfKind<PtrOnlyUseTy>();
    return PtrUsages.empty() ? SourceLocation{}
                             : PtrUsages.front()->getUsageExpr()->getLocation();
  }

  template <typename PtrOnlyUseTy>
  bool isPointerOnlyUseFoundAlreadyFor(const DeclRefExpr *CurDRE) {
    SourceLocation L = getAlreadyFoundPtrUsageLocation<PtrOnlyUseTy>(
        cast<VarDecl>(CurDRE->getDecl()));
    if (L.isInvalid())
      return false;
    return CurDRE->getLocation() < L;
  }

  template <typename BaseT, typename DerT, typename... Args>
  void turnSubtypeUsesToBase(const DeclRefExpr *DRE, Args &&... args) {
    UsageCollection &Coll = Usages[cast<VarDecl>(DRE->getDecl())];
    const UsageCollection::UseVector &DerTUses = Coll.getUsagesOfKind<DerT>();

    for (const PtrUsage *Usage : DerTUses) {
      Coll.removeUsage(Usage);
      Coll.addUsage(new BaseT{DRE, std::forward<Args>(args)...});
    }
  }
};

RedundantPointerCheck::RedundantPointerCheck(StringRef Name,
                                             ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context), UsageCB(new PtrUseModelCallback{}) {}

RedundantPointerCheck::~RedundantPointerCheck() { delete UsageCB; }

void RedundantPointerCheck::registerMatchers(MatchFinder *Finder) {
  // On the boundaries of C(++) functions, diagnostics should be emitted.
  Finder->addMatcher(
      functionDecl(isDefinition(),
                   unless(ast_matchers::isTemplateInstantiation())),
      this);

  Finder->addMatcher(
      functionDecl(isDefinition(), isExplicitTemplateSpecialization()), this);

  // Set up the callbacks for the modelling callback instance.
  Finder->addMatcher(declRefExpr(unless(isExpansionInSystemHeader()),
                                 matchers::PtrVarUsage.bind(UsedPtrId)),
                     UsageCB);
  Finder->addMatcher(
      stmt(unless(isExpansionInSystemHeader()), matchers::PtrDereference),
      UsageCB);
  Finder->addMatcher(varDecl(unless(isExpansionInSystemHeader()),
                             matchers::VarInitFromPtrDereference),
                     UsageCB);
  Finder->addMatcher(
      ifStmt(unless(isExpansionInSystemHeader()), matchers::PtrGuard), UsageCB);
}

void RedundantPointerCheck::forAllCollected() {
  // At every function boundary, the diagnostics should be calculated and
  // flushed. This can't happen *inside* a function, as this check itself models
  // information that can only be calculated from visiting the entire function.
  const UsageMap &PreviousFunCollectedUsages = UsageCB->getUsages();
  if (!PreviousFunCollectedUsages.empty())
    onEndOfModelledChunk(PreviousFunCollectedUsages);
  UsageCB->reset();
}

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

void UsageCollection::removeUsage(const PtrUsage *UsageInfo) {
  assert(UsageInfo && "provide a valid UsageInfo instance");

  for (auto It = CollectedUses.begin(); It != CollectedUses.end(); ++It) {
    if (*It == UsageInfo) {
      delete *It;
      CollectedUses.erase(It);
      break;
    }
  }
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
