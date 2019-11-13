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

/// Matches construction expressions which "trivially" initialise something from
/// a pointer.
static const auto ConstructExprWithPtrDereference =
    ignoringElidableConstructorCall(
        cxxConstructExpr(argumentCountIs(1), hasArgument(0, PtrDereferenceM)));

// FIXME: Match aggregate initialisation through initlists (TrivialAggregate in test/...pre17.cpp)
static const auto VarInitFromPtrDereference =
    varDecl(anyOf(hasInitializer(ignoringParenImpCasts(
                      anyOf(PtrDereferenceM, ConstructExprWithPtrDereference))),
                  hasDescendant(expr(ConstructExprWithPtrDereference))))
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

/// Get the code text that initialises a variable.
/// If the initialisation happens entirely through a macro, returns empty, or
/// empty parens, depending on OuterParen's value.
static std::string getVarInitExprCode(const VarDecl *Var, const ASTContext &Ctx,
                                      bool OuterParen = true) {
  return (Twine(OuterParen ? "(" : "") +
          Lexer::getSourceText(
              CharSourceRange::getCharRange(
                  Var->getInit()->getBeginLoc(),
                  Lexer::getLocForEndOfToken(Var->getInit()->getEndLoc(), 0,
                                             Ctx.getSourceManager(),
                                             Ctx.getLangOpts())),
              Ctx.getSourceManager(), Ctx.getLangOpts()) +
          Twine(OuterParen ? ")" : ""))
      .str();
}

// FIXME: The real end goal of this check is to find a pair of ptrs created
//        by dereferencing the first.

// FIXME: Introduce a % option for variable pollution (only report if # of
//        superfluous ptr vars are higher than % of all (or ptr-only?) vars.

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

    LLVM_DEBUG(PtrVar->dump(llvm::dbgs());
               llvm::dbgs() << "\n\tusages for object: " << PointeeUsages.size()
                            << "\n\tusages for pointer (guards): "
                            << PointerUsages.size() << '\n';);

    if (PointeeUsages.size() > 1) {
      LLVM_DEBUG(PtrVar->dump(llvm::dbgs());
                 llvm::dbgs()
                 << "\n has multiple (non-annotation) usages -- ignoring!\n";);
      continue;
    }
    if (PointerUsages.size() > 1) {
      // Currently, "Pointer(-only) usages" are if() guards, from which if there
      // multiple, no automatic rewriting seems sensible enough.
      LLVM_DEBUG(PtrVar->dump(llvm::dbgs());
                 llvm::dbgs()
                 << "\n has multiple (annotation) usages -- ignoring!\n";);
      continue;
    }

    const PtrUsage *TheUsage = PointeeUsages.front();
    const auto *TheUseExpr = TheUsage->getUsageExpr();
    // Different type of diagnostics should be created if there is an annotating
    // guard statement on the pointer's value.
    bool HasPointerAnnotatingUsages = !PointerUsages.empty();

    if (const auto *DerefForVarInit = dyn_cast<PtrDerefVarInit>(TheUsage)) {
      const VarDecl *InitedVar = DerefForVarInit->getInitialisedVar();

      const Type *VarTy = InitedVar->getType()->getUnqualifiedDesugaredType();
      LLVM_DEBUG(llvm::dbgs() << "Initialised variable " << InitedVar->getName() << " has type:\n";
                   InitedVar->getType().dump(llvm::dbgs()); llvm::dbgs() << '\n';
      VarTy->dump(llvm::dbgs()); llvm::dbgs() << '\n';);

      if (const auto* Record = VarTy->getAsCXXRecordDecl()) {
        LLVM_DEBUG(llvm::dbgs() << "Initialised variable " << InitedVar->getName() << " has record type.\n";);
        LLVM_DEBUG(llvm::dbgs() << Record->hasDefaultConstructor() << ' ' << Record->hasTrivialDefaultConstructor() << ' ' << Record->hasUserProvidedDefaultConstructor() << ' ' << Record->hasNonTrivialDefaultConstructor() << '\n';);
        if (!Record->hasDefaultConstructor()) {
          LLVM_DEBUG(llvm::dbgs() << "the default ctor is deleted.\n";);
          continue;
        }
      }

      emitMainDiagnostic(PtrVar);

      diag(TheUseExpr->getLocation(),
           "usage: %0 dereferenced in the initialisation of %1",
           DiagnosticIDs::Note)
          << PtrVar << InitedVar;

      if (!HasPointerAnnotatingUsages)
        emitConsiderUsingInitCodeDiagnostic(PtrVar, TheUsage);
      else {
        if (const auto *Guard = dyn_cast<PtrGuard>(PointerUsages.front())) {
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
            assert(Callee && "CallExpr should represent a function call");
            if (Callee->isNoReturn())
              EarlyFlowType = "program termination";
          }

          const char *GuardDiagMsg;
          if (EarlyFlowType.empty())
            GuardDiagMsg = "the value of %0 is guarded by this branch";
          else
            GuardDiagMsg =
                "the value of %0 is guarded by this branch, resulting in '%1'";

          diag(Guard->getGuardStmt()->getIfLoc(), GuardDiagMsg,
               DiagnosticIDs::Note)
              << PtrVar << EarlyFlowType;
        }

        if (!LOpts.CPlusPlus || (LOpts.CPlusPlus && !LOpts.CPlusPlus17)) {
          // Pre-C++17 this case cannot be reasonably rewritten, as the
          // initialising statement would appear and execute twice, which, e.g.
          // for an allocation, would immediately cause a memory leak.
          // FIXME: Perhaps don't warn for this all the time and hide it behind
          //        an option?
          diag(PtrVar->getLocation(),
               "consider putting the pointer, the branch, and the assignment "
               "to %0 into an inner scope (between {brackets})",
               DiagnosticIDs::Note)
              << InitedVar;
        } else if (LOpts.CPlusPlus17) {
          // FIXME: This should be a rewrite.
          diag(PtrVar->getLocation(), "post-cpp17", DiagnosticIDs::Error);
        }
      }
    } else if (isa<PtrDereference>(TheUsage) || isa<PtrArgument>(TheUsage)) {
      if (HasPointerAnnotatingUsages)
        // Guarded versions of dereferences and passing of the pointer cannot be
        // reasonably rewritten.
        continue;

      emitMainDiagnostic(PtrVar);

      const char *UsageDescription;
      if (isa<PtrDereference>(TheUsage))
        UsageDescription = "usage: %0 dereferenced here";
      else
        UsageDescription = "usage: %0 used in an expression";

      diag(TheUseExpr->getLocation(), UsageDescription, DiagnosticIDs::Note)
          << PtrVar;

      emitConsiderUsingInitCodeDiagnostic(PtrVar, TheUsage);
    }
  }
}

/// Helper function that emits the main "local ptr variable may be superfluous"
/// warning for the given variable.
void SuperfluousLocalPtrVariableCheck::emitMainDiagnostic(const VarDecl *Ptr) {
  // FIXME: Mention visibility.
  diag(Ptr->getLocation(), "local pointer variable %0 might be "
                           "superfluous as it is only used once")
      << Ptr <<
      // Create a "dummy" FixIt (changing the var's name to itself). This is
      // done so that later FixIt hints (offered as suggestions) do NOT get
      // applied if '--fix' is specified to Tidy.
      FixItHint::CreateReplacement(
          CharSourceRange::getCharRange(
              Ptr->getLocation(), Lexer::getLocForEndOfToken(
                                      Ptr->getLocation(), 0,
                                      Ptr->getASTContext().getSourceManager(),
                                      Ptr->getASTContext().getLangOpts())),
          Ptr->getName());
}

/// Helper function that emits a note diagnostic for the usage of a pointer
/// variable suggesting the user writes the code text that the pointer was
/// initialised with to the point of use instead.
void SuperfluousLocalPtrVariableCheck::emitConsiderUsingInitCodeDiagnostic(
    const VarDecl *Ptr, const PtrUsage *Usage) {
  std::string PtrVarInitExprCode =
      getVarInitExprCode(Ptr, Ptr->getASTContext(), /* OuterParen =*/true);
  LLVM_DEBUG(llvm::dbgs()
                 << "Generated initialisation expression-statement code for\n";
             Ptr->dump(llvm::dbgs());
             llvm::dbgs() << "\n as: " << PtrVarInitExprCode << '\n';);
  if (PtrVarInitExprCode == "()")
    // If fetching the code text for the initialisation of the used var,
    // make sure no "hint" is created for the note.
    PtrVarInitExprCode.clear();

  auto ConsiderNote = diag(Usage->getUsageExpr()->getLocation(),
                           "consider using the code that initialises %0 here",
                           DiagnosticIDs::Note)
                      << Ptr;

  if (!PtrVarInitExprCode.empty())
    ConsiderNote << FixItHint::CreateReplacement(
        Usage->getUsageExpr()->getSourceRange(), PtrVarInitExprCode);
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
