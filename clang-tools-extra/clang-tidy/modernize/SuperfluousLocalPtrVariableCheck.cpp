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

static const StatementMatcher PtrDereferenceM = anyOf(
    memberExpr(isArrow(), hasDescendant(PtrVarUsage.bind(DereferencedPtrId)))
        .bind(DerefUsageExprId),
    unaryOperator(hasOperatorName("*"),
                  hasDescendant(PtrVarUsage.bind(DereferencedPtrId)))
        .bind(DerefUsageExprId));

static const auto VarInitFromPtrDereference =
    varDecl(hasInitializer(ignoringParenImpCasts(PtrDereferenceM)))
        .bind(InitedVarId);

// FIXME: Match all [[noreturn]] functions, and perhaps also usages in an
//        assert() call.
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
static const auto PtrGuardM =
    ifStmt(hasCondition(hasDescendant(PtrVarUsage.bind(UsedPtrId))),
           hasThen(anyOf(EarlyReturnLike,
                         compoundStmt(statementCountIs(1),
                                      hasAnySubstatement(EarlyReturnLike)))),
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
  for (const auto &RefData : Usages) {
    LLVM_DEBUG(
        llvm::dbgs() << "Usages for ptr var " << RefData.first << '\n';

        RefData.first->dump(llvm::dbgs()); llvm::dbgs() << '\n';

        for (const PtrUsage *Usage
             : RefData.second.getUsages()) {
          if (const auto *PassUsage = dyn_cast<PtrArgument>(Usage)) {
            llvm::dbgs() << "Parameter was passed (\"read\") in the following "
                            "expression:\n";

            PassUsage->getUsageExpr()->dump(llvm::dbgs());
            llvm::dbgs() << '\n';
          }
          if (const auto *DerefUsage = dyn_cast<PtrDereference>(Usage)) {
            llvm::dbgs() << "A usage in a dereference. The dereference "
                            "expression looks like this:\n";

            if (const auto *UnaryOpExpr = DerefUsage->getUnaryOperator())
              UnaryOpExpr->dump(llvm::dbgs());
            else if (const auto *MemExpr = DerefUsage->getMemberExpr())
              MemExpr->dump(llvm::dbgs());
            llvm::dbgs() << '\n';
          }
          if (const auto *VarInitUsage = dyn_cast<PtrDerefVarInit>(Usage)) {
            llvm::dbgs() << "This dereference initialises another variable!\n";

            VarInitUsage->getInitialisedVar()->dump(llvm::dbgs());
            llvm::dbgs() << '\n';
          }
          if (const auto *Guard = dyn_cast<PtrGuard>(Usage)) {
            llvm::dbgs() << "This usage forms a guard check on ptr value!\n";
            Guard->getGuardStmt()->dump(llvm::dbgs());
            llvm::dbgs() << '\n';

            llvm::dbgs() << "The control flow is changed by:\n";
            Guard->getFlowStmt()->dump(llvm::dbgs());
            llvm::dbgs() << '\n';
          }

          llvm::dbgs()
              << "\n----------------------------------------------------\n";
        });

    const UsageCollection &UsageColl = RefData.second;
    const UsageCollection::UseVector &PointeeUsages =
        UsageColl.getUsages<PointeePtrUsage>();
    const UsageCollection::UseVector &PointerUsages =
        UsageColl.getUsages<PointerPtrUsage>();

    if (PointeeUsages.size() > 1) {
      LLVM_DEBUG(RefData.first->dump(llvm::dbgs());
                 llvm::dbgs()
                 << "\n has multiple (non-annotation) usages -- ignoring!\n";);
      continue;
    }

    const PtrUsage *UI = PointeeUsages.front();
    const auto *UseExpr = UI->getUsageExpr();
    const auto *UsedDecl = UseExpr->getDecl();
    const char *OutMsg = nullptr;
    if (isa<PtrDereference>(UI))
      OutMsg = "local pointer variable %0 only participates in one dereference";
    else if (isa<PtrArgument>(UI))
      OutMsg = "local pointer variable %0 only used once";

    assert(OutMsg && "Unhandled 'PtrUsage' kind for diag message");

    diag(UseExpr->getLocation(), OutMsg) << UsedDecl;

    diag(UsedDecl->getLocation(), "%0 defined here", DiagnosticIDs::Note)
        << UsedDecl;

    for (const PtrUsage *AnnotUI : PointerUsages) {
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
          assert(Callee &&
                 "Flow matcher of call didn't match a function call?");
          assert(Callee->getName() == "longjmp" && "Flow matched improperly!");
          EarlyFlowType = "longjmp";
        } else
          llvm_unreachable("Unhandled kind of Early Flow Stmt in diag!");

        diag(Guard->getFlowStmt()->getBeginLoc(),
             "... resulting in an early %0", DiagnosticIDs::Note)
            << EarlyFlowType;
      }
    }
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
