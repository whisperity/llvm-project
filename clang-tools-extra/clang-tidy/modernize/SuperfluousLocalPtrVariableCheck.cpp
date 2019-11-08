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
    varDecl(hasInitializer(expr()),
            anyOf(hasType(pointerType()),
                  hasType(autoType(hasDeducedType(pointerType())))),
            unless(parmVarDecl()));

/// Matches every usage of a local pointer variable.
static const auto PtrVarUsage = declRefExpr(to(PointerLocalVarDecl));

static const StatementMatcher PtrDereferenceM = anyOf(
    memberExpr(isArrow(), hasDescendant(PtrVarUsage.bind(DereferencedPtrId)))
        .bind(DerefUsageExprId),
    unaryOperator(hasOperatorName("*"),
                  hasDescendant(PtrVarUsage.bind(DereferencedPtrId)))
        .bind(DerefUsageExprId));

static const auto VarInitFromPtrDereference =
    varDecl(hasInitializer(ignoringParenImpCasts(PtrDereferenceM)))
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
static const auto PtrGuardM =
    ifStmt(hasCondition(hasDescendant(PtrVarUsage.bind(UsedPtrId))),
           hasThen(anyOf(FlowBreakingStmt,
                         compoundStmt(statementCountIs(1),
                                      hasAnySubstatement(FlowBreakingStmt)))),
           unless(hasElse(stmt())))
        .bind(PtrGuardId);

// FIXME: The real end goal of this check is to find a pair of ptrs created
//        by dereferencing the first.

void SuperfluousLocalPtrVariableCheck::registerMatchers(MatchFinder *Finder) {
  // FIXME: Match pointers with UsedPtrId iff they are passed as an argument!
  Finder->addMatcher(PtrVarUsage.bind(UsedPtrId), this);
  Finder->addMatcher(PtrDereferenceM, this);
  Finder->addMatcher(VarInitFromPtrDereference, this);
  Finder->addMatcher(PtrGuardM, this);
}

void SuperfluousLocalPtrVariableCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *GuardIf = Result.Nodes.getNodeAs<IfStmt>(PtrGuardId)) {
    LLVM_DEBUG(GuardIf->dump(llvm::dbgs()); llvm::dbgs() << '\n';);
    const auto *FlowStmt = Result.Nodes.getNodeAs<Stmt>(EarlyReturnStmtId);
    const auto *DRefExpr = Result.Nodes.getNodeAs<DeclRefExpr>(UsedPtrId);
    const auto *RefPtrVar = cast<VarDecl>(DRefExpr->getDecl());

    Usages[RefPtrVar].addUsage(new PtrGuard{DRefExpr, GuardIf, FlowStmt});
    return;
  }

  if (const auto *VarInit = Result.Nodes.getNodeAs<VarDecl>(InitedVarId)) {
    const auto *DerefExpr = Result.Nodes.getNodeAs<Expr>(DerefUsageExprId);
    const auto *DRefExpr =
        Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrId);
    const auto *RefPtrVar = cast<VarDecl>(DRefExpr->getDecl());

    Usages[RefPtrVar].addUsage(
        new PtrDerefVarInit{DRefExpr, DerefExpr, VarInit});
    return;
  }

  if (const auto *PtrDRE =
          Result.Nodes.getNodeAs<DeclRefExpr>(DereferencedPtrId)) {
    const auto *RefPtrVar = cast<VarDecl>(PtrDRE->getDecl());
    const auto *DerefExpr = Result.Nodes.getNodeAs<Expr>(DerefUsageExprId);

    Usages[RefPtrVar].addUsage(new PtrDereference{PtrDRE, DerefExpr});
    return;
  }

  if (const auto *PtrDRE = Result.Nodes.getNodeAs<DeclRefExpr>(UsedPtrId)) {
    const auto *RefPtrVar = cast<VarDecl>(PtrDRE->getDecl());
    Usages[RefPtrVar].addUsage(new PtrArgument{PtrDRE});
    return;
  }
}

void SuperfluousLocalPtrVariableCheck::onEndOfTranslationUnit() {
  const LangOptions &LOpts = getLangOpts();

  for (const auto &Usage : Usages) {
    const VarDecl *PtrVar = Usage.first;
    const UsageCollection::UseVector &PointeeUsages =
        Usage.second.getUsages<PointeePtrUsage>();
    const UsageCollection::UseVector &PointerUsages =
        Usage.second.getUsages<PointerPtrUsage>();

    if (PointeeUsages.size() > 1) {
      LLVM_DEBUG(PtrVar->dump(llvm::dbgs());
                 llvm::dbgs()
                 << "\n has multiple (non-annotation) usages -- ignoring!\n";);
      continue;
    }

    const SourceManager &SM = PtrVar->getASTContext().getSourceManager();
    const PtrUsage *TheUsage = PointeeUsages.front();
    const auto *TheUseExpr = TheUsage->getUsageExpr();

    diag(PtrVar->getLocation(), "local pointer variable %0 might be "
                                "superfluous as it is only used once")
        << PtrVar <<
        // Create a "dummy" FixIt (changing the var's name to itself). This is
        // done so that later FixIt hints (offered as suggestions) do NOT get
        // applied if '--fix' is specified to Tidy.
        FixItHint::CreateReplacement(
            CharSourceRange::getCharRange(
                PtrVar->getLocation(),
                Lexer::getLocForEndOfToken(PtrVar->getLocation(), 0, SM,
                                           LOpts)),
            PtrVar->getName());

    StringRef PtrVarInitExprCode = Lexer::getSourceText(
        CharSourceRange::getCharRange(
            PtrVar->getInit()->getBeginLoc(),
            Lexer::getLocForEndOfToken(PtrVar->getInit()->getEndLoc(), 0, SM,
                                       LOpts)),
        SM, LOpts);

    if (const auto *DerefForVarInit = dyn_cast<PtrDerefVarInit>(TheUsage)) {
      // FIXME: Offer a good note here.
    } else if (isa<PtrDereference>(TheUsage)) {
      diag(TheUseExpr->getLocation(), "usage: %0 dereferenced here",
           DiagnosticIDs::Note)
          << PtrVar;
      diag(TheUseExpr->getLocation(),
           "consider using the initialisation of %0 here", DiagnosticIDs::Note)
          << PtrVar
          << FixItHint::CreateReplacement(TheUseExpr->getSourceRange(),
                                          PtrVarInitExprCode);
    } else if (isa<PtrArgument>(TheUsage)) {
      diag(TheUseExpr->getLocation(), "usage: %0 used in an expression",
           DiagnosticIDs::Note)
          << PtrVar;
      diag(TheUseExpr->getLocation(),
           "consider using the initialisation of %0 here", DiagnosticIDs::Note)
          << PtrVar
          << FixItHint::CreateReplacement(TheUseExpr->getSourceRange(),
                                          PtrVarInitExprCode);
    }

    /*for (const PtrUsage *AnnotUI : PointerUsages) {
      if (const auto *Guard = dyn_cast<PtrGuard>(AnnotUI)) {
        diag(Guard->getGuardStmt()->getIfLoc(),
             "the value of %0 is guarded by this condition ...",
             DiagnosticIDs::Note)
            << UsedDecl;

        std::string EarlyFlowType;
        const Stmt *FlowStmt = Guard->getFlowStmt();
        if (isa<ReturnStmt>(FlowStmt))
          EarlyFlowType = "return";
        else if (isa<ContinueStmt>(FlowStmt))
          EarlyFlowType = "continue";
        else if (isa<BreakStmt>(FlowStmt))
          EarlyFlowType = "break";
        else if (isa<GotoStmt>(FlowStmt))
          EarlyFlowType = "goto";
        else if (isa<CXXThrowExpr>(FlowStmt))
          EarlyFlowType = "throw";
        else if (const auto *CE = dyn_cast<CallExpr>(FlowStmt)) {
          const auto *Callee = dyn_cast<FunctionDecl>(CE->getCalleeDecl());
          assert(Callee && Callee->isNoReturn() &&
                 "Flow matcher of call didn't match a proper function call?");
          EarlyFlowType = "program termination";
        } else
          llvm_unreachable("Unhandled kind of Early Flow Stmt in diag!");

        diag(Guard->getFlowStmt()->getBeginLoc(),
             "... resulting in an early %0", DiagnosticIDs::Note)
            << EarlyFlowType;
      }
    }*/
  }
}

bool UsageCollection::addUsage(PtrUsage *UsageInfo) {
  assert(UsageInfo && "provide a valid UsageInfo instance");
  LLVM_DEBUG(
      llvm::dbgs() << "Adding usage " << UsageInfo->getUsageExpr() << '\n';
      UsageInfo->getUsageExpr()->dump(llvm::dbgs()); llvm::dbgs() << '\n';);

  for (const PtrUsage *DUI : CollectedUses)
    if (DUI == UsageInfo || DUI->getUsageExpr() == UsageInfo->getUsageExpr()) {
      LLVM_DEBUG(llvm::dbgs() << "Adding usage " << DUI->getUsageExpr()
                              << " but it has already been found!\n";);
      return false;
    }

  CollectedUses.push_back(UsageInfo);
  return true;
}

bool UsageCollection::replaceUsage(PtrUsage *OldInfo, PtrUsage *NewInfo) {
  assert(OldInfo && NewInfo && "provide valid UsageInfo instances");
  assert(OldInfo != NewInfo && "replacement of usage info with same instance");

  LLVM_DEBUG(llvm::dbgs() << "Replacing usage " << OldInfo->getUsageExpr()
                          << " with " << NewInfo->getUsageExpr() << '\n';
             OldInfo->getUsageExpr()->dump(llvm::dbgs()); llvm::dbgs() << '\n';
             NewInfo->getUsageExpr()->dump(llvm::dbgs());
             llvm::dbgs() << '\n';);

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

template <typename PtrUsageTypes>
UsageCollection::UseVector UsageCollection::getUsages() const {
  return UseVector{llvm::make_filter_range(
      CollectedUses, [](PtrUsage *UI) { return isa<PtrUsageTypes>(UI); })};
}

UsageCollection::~UsageCollection() {
  llvm::for_each(CollectedUses, [](PtrUsage *UI) { delete UI; });
}

} // namespace modernize
} // namespace tidy
} // namespace clang
