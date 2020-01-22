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

#include "llvm/Support/Format.h"

#define DEBUG_TYPE "RedundantPtr"
#include "llvm/Support/Debug.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace readability {

static const char UsedVarId[] = "used-var";
static const char DerefUsageExprId[] = "usage-stmt";
static const char DereferencedVarId[] = "deref-var";
static const char InitedVarId[] = "inited-var";
static const char GuardId[] = "guard";
static const char EarlyReturnStmtId[] = "early-ret";

namespace matchers {

static const auto DereferenceableType =
    cxxRecordDecl(anyOf(hasMethod(cxxMethodDecl(hasOverloadedOperatorName("*"),
                                                parameterCountIs(0))),
                        hasMethod(cxxMethodDecl(hasOverloadedOperatorName("->"),
                                                parameterCountIs(0)))));

static const auto PointerVarDecl = varDecl(anyOf(
    hasType(pointerType()), hasType(autoType(hasDeducedType(pointerType())))));

static const auto DereferenceableVarDecl =
    varDecl(anyOf(hasType(DereferenceableType),
                  hasType(autoType(hasDeducedType(
                      recordType(hasDeclaration(DereferenceableType)))))));

static const auto PointerLikeVarDecl =
    anyOf(PointerVarDecl, DereferenceableVarDecl);

/// Matches every usage of a local pointer-like variable.
static const auto VarUsage = declRefExpr(to(PointerLikeVarDecl));

static const auto VarUsingMemberExpr =
    memberExpr(hasDescendant(VarUsage.bind(DereferencedVarId)));

static const auto Dereference = stmt(
    anyOf(VarUsingMemberExpr.bind(DerefUsageExprId),
          cxxMemberCallExpr(has(VarUsingMemberExpr)).bind(DerefUsageExprId),
          unaryOperator(hasOperatorName("*"),
                        hasDescendant(VarUsage.bind(DereferencedVarId)))
              .bind(DerefUsageExprId)));

/// Matches construction expressions which "trivially" initialise something from
/// a pointer.
static const auto ConstructExprWithDereference =
    ignoringElidableConstructorCall(
        cxxConstructExpr(argumentCountIs(1), hasArgument(0, Dereference)));

static const auto VarInitFromDereference =
    varDecl(anyOf(hasInitializer(ignoringParenImpCasts(anyOf(
                      // Directly initialise from dereference: int i = p->i
                      Dereference,
                      // Assign-initialise through ctor: T t = p->t;
                      ConstructExprWithDereference,
                      initListExpr(hasDescendant(
                          // Aggregate initialise: S s = {p->i};
                          Dereference))))),
                  hasDescendant(
                      // Initialise with ctor call: T t(p->t);
                      expr(ConstructExprWithDereference))))
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
// FIXME: Don't match cases like "if (!somePredicate(ptr))" as it should be
//        marked as a VarUsage!
static const auto Guard =
    ifStmt(hasCondition(allOf(hasDescendant(VarUsage.bind(UsedVarId)),
                              unless(hasDescendant(Dereference)))),
           hasThen(anyOf(FlowBreakingStmt,
                         compoundStmt(statementCountIs(1),
                                      hasAnySubstatement(FlowBreakingStmt)))),
           unless(hasElse(stmt())))
        .bind(GuardId);

static const auto LoopLike = stmt(anyOf(forStmt(), cxxForRangeStmt()));

static const auto HasLoopParent = varDecl(
    anyOf(hasParent(LoopLike), hasParent(declStmt(hasParent(LoopLike)))));

} // namespace matchers

/// Helper class that handles match callbacks for pointer usages within a
/// function.
class RedundantPointerCheck::PtrUseModelCallback
    : public MatchFinder::MatchCallback {
public:
#define ASTNODE_FROM_MACRO(N) N->getSourceRange().getBegin().isMacroID()
  void run(const MatchFinder::MatchResult &Result) override {
    // PointerPtrUsages:
    if (const auto *GuardIf = Result.Nodes.getNodeAs<IfStmt>(GuardId)) {
      const auto *FlowStmt = Result.Nodes.getNodeAs<Stmt>(EarlyReturnStmtId);
      const auto *DRefExpr = Result.Nodes.getNodeAs<DeclRefExpr>(UsedVarId);
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
      DRE = Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedVarId);
      const auto *RefPtrVar = cast<VarDecl>(DRE->getDecl());
      if (ASTNODE_FROM_MACRO(VarInit) || ASTNODE_FROM_MACRO(DerefExpr) ||
          ASTNODE_FROM_MACRO(DRE) || ASTNODE_FROM_MACRO(RefPtrVar))
        return;

      Added = Usages[RefPtrVar].addUsage(
          new PtrDerefVarInit{DRE, DerefExpr, VarInit});
    } else if ((DRE = Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedVarId))) {
      const auto *RefPtrVar = cast<VarDecl>(DRE->getDecl());
      const auto *DerefExpr = Result.Nodes.getNodeAs<Expr>(DerefUsageExprId);
      if (ASTNODE_FROM_MACRO(DRE) || ASTNODE_FROM_MACRO(RefPtrVar) ||
          ASTNODE_FROM_MACRO(DerefExpr))
        return;

      Added = Usages[RefPtrVar].addUsage(new PtrDereference{DRE, DerefExpr});
    } else if ((DRE = Result.Nodes.getNodeAs<DeclRefExpr>(UsedVarId))) {
      const auto *RefPtrVar = cast<VarDecl>(DRE->getDecl());
      if (ASTNODE_FROM_MACRO(DRE) || ASTNODE_FROM_MACRO(RefPtrVar))
        return;

      Added = Usages[RefPtrVar].addUsage(new PtrArgument{DRE});
    }

    // Save potential bit flags of the pointer-like variable.
    calculateVarDeclFlags(cast<VarDecl>(DRE->getDecl()));

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

  void calculateVarDeclFlags(const VarDecl *Var) {
    LLVM_DEBUG(llvm::dbgs()
               << "calculateVarDeclFlags(" << Var->getName() << ")\n");
    using namespace matchers;

    LLVM_DEBUG(llvm::dbgs()
               << llvm::format_hex(Usages[Var].flags(), 3) << '\n');
    if (Usages[Var].flags() != PVF_None)
      // Only allow calculating the flags once per variable.
      return;

    if (!match(PointerVarDecl, *Var, Var->getASTContext()).empty()) {
      LLVM_DEBUG(llvm::dbgs()
                 << "Var " << Var->getName() << " is a pointer.\n");
      Usages[Var].flags() |= PVF_Pointer;
    } else {
      LLVM_DEBUG(llvm::dbgs() << "Var " << Var->getName()
                              << " is a * or -> capable record.\n");
      Usages[Var].flags() |= PVF_Dereferenceable;
    }

    if (!match(HasLoopParent, *Var, Var->getASTContext()).empty()) {
      LLVM_DEBUG(llvm::dbgs()
                 << "Var " << Var->getName() << " is a loop variable.\n");
      Usages[Var].flags() |= PVF_LoopVar;
    }

    if (isa<ParmVarDecl>(Var)) {
      LLVM_DEBUG(llvm::dbgs()
                 << "Var " << Var->getName() << " is an argument.\n");
      Usages[Var].flags() |= PVF_ParmVar;
    }

    if (Var->getInit()) {
      LLVM_DEBUG(llvm::dbgs()
                 << "Var " << Var->getName() << " has an initialiser.\n");
      Usages[Var].flags() |= PVF_Initialiser;
    }

    LLVM_DEBUG(llvm::dbgs()
               << llvm::format_hex(Usages[Var].flags(), 5) << '\n');
  }

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
                                 matchers::VarUsage.bind(UsedVarId)),
                     UsageCB);
  Finder->addMatcher(
      stmt(unless(isExpansionInSystemHeader()), matchers::Dereference),
      UsageCB);
  Finder->addMatcher(varDecl(unless(isExpansionInSystemHeader()),
                             matchers::VarInitFromDereference),
                     UsageCB);
  Finder->addMatcher(
      ifStmt(unless(isExpansionInSystemHeader()), matchers::Guard), UsageCB);
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

UsageCollection::UsageCollection(UsageCollection &&UC) : Flags(UC.Flags) {
  CollectedUses.insert(CollectedUses.end(), UC.CollectedUses.begin(),
                       UC.CollectedUses.end());
  UC.CollectedUses.clear();
}

UsageCollection &UsageCollection::operator=(UsageCollection &&UC) {
  if (&UC == this)
    return *this;

  Flags = UC.Flags;
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
