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

static const auto VarInitFromPtrDereference =
    varDecl(
        anyOf(
            hasInitializer(ignoringParenImpCasts(anyOf(
                PtrDereferenceM, // Directly initialise from dereference:
                                 // int i = p->i
                ConstructExprWithPtrDereference, // Assign-initialise through
                                                 // ctor: T t = p->t;
                initListExpr(hasDescendant(
                    PtrDereferenceM))))), // Aggregate initialise: S s = {p->i};
            hasDescendant(
                expr(ConstructExprWithPtrDereference)))) // Initialise with ctor
                                                         // call: T t(p->t);
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

/// Returns the full code (end inclusive on the whole token) in the input buffer
/// between the given two source locations.
static StringRef getCode(SourceLocation B, SourceLocation E,
                         const ASTContext &Ctx) {
  const SourceManager &SM = Ctx.getSourceManager();
  const LangOptions &LOpts = Ctx.getLangOpts();
  return Lexer::getSourceText(
      CharSourceRange::getCharRange(
          B, Lexer::getLocForEndOfToken(E, 0, SM, LOpts)),
      SM, LOpts);
}

/// Get the code text that initialises a variable.
/// If the initialisation happens entirely through a macro, returns empty, or
/// empty parens (i.e. "()"), depending on OuterParen's value.
static std::string getVarInitExprCode(const VarDecl *Var, const ASTContext &Ctx,
                                      bool OuterParen = true) {
  return (Twine(OuterParen ? "(" : "") +
          getCode(Var->getInit()->getBeginLoc(), Var->getInit()->getEndLoc(),
                  Ctx) +
          Twine(OuterParen ? ")" : ""))
      .str();
}

static bool canBeDefaultConstructed(const CXXRecordDecl *RD) {
  assert(RD->hasDefinition() &&
         "for forward declarations the answer is unknown.");
  LLVM_DEBUG(llvm::dbgs()
                 << "Checking whether this record is default constructible:\n";
             RD->dump(llvm::dbgs()); llvm::dbgs() << '\n';);

  if (RD->isAggregate()) {
    LLVM_DEBUG(llvm::dbgs() << "is-aggregate\n");
    return true;
  }

  if (RD->hasDefaultConstructor()) {
    LLVM_DEBUG(llvm::dbgs() << "has-default-ctor\n");

    for (const auto &D : RD->decls()) {
      LLVM_DEBUG(llvm::dbgs() << "Checking declaration:\n";
                 D->dump(llvm::dbgs()); llvm::dbgs() << '\n';);
      const auto *ConD = dyn_cast<CXXConstructorDecl>(D);
      if (!ConD)
        continue;
      LLVM_DEBUG(llvm::dbgs() << "\tis a ctor.\n");
      if (!ConD->isDefaultConstructor())
        continue;
      LLVM_DEBUG(llvm::dbgs() << "\tis a default ctor.\n");

      if (ConD->isDeleted() || ConD->isDeletedAsWritten()) {
        LLVM_DEBUG(llvm::dbgs() << "\tis deleted / deleted-as-written\n");
        return false;
      }
      if (ConD->isDefaulted() || ConD->isExplicitlyDefaulted()) {
        LLVM_DEBUG(llvm::dbgs() << "\tis defaulted / explicitly defaulted.\n");
        return true;
      }

      // Found a default-ctor, so we can default-construct.
      return true;
    }
  }

  LLVM_DEBUG(llvm::dbgs() << "returning false outside...\n");
  return false;
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

    if (PointeeUsages.empty())
      continue;

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

      if (const auto *RD = VarTy->getAsCXXRecordDecl())
        if (!canBeDefaultConstructed(RD)) {
          // Do not suggest the rewrite as the inited variable couldn't be
          // default-constructed.
          LLVM_DEBUG(llvm::dbgs() << "Variable " << InitedVar->getName()
                                  << " can't be default-ctored.\n");
          continue;
        }

      emitMainDiagnostic(PtrVar);
      diag(TheUseExpr->getLocation(),
           "usage: %0 dereferenced in the initialisation of %1",
           DiagnosticIDs::Note)
          << PtrVar << InitedVar;

      if (!HasPointerAnnotatingUsages)
        emitConsiderUsingInitCodeDiagnostic(PtrVar, TheUsage);
      else {
        const auto *Guard = dyn_cast<PtrGuard>(PointerUsages.front());
        assert(Guard && "Currently the only Pointer-usage kind is a PtrGuard");
        emitGuardDiagnostic(Guard);

        if (!LOpts.CPlusPlus || (LOpts.CPlusPlus && !LOpts.CPlusPlus17)) {
          // Pre-C++17 this case cannot be reasonably rewritten, as the
          // initialising statement would appear and execute twice, which, e.g.
          // for an allocation, would immediately cause a memory leak.
          // FIXME: Perhaps don't warn for this all the time and hide it behind
          //        an option?
          diag(
              PtrVar->getLocation(),
              "consider putting the pointer %0, the branch, and the assignment "
              "of %1 into an inner scope (between {brackets})",
              DiagnosticIDs::Note)
              << PtrVar << InitedVar;
        } else if (LOpts.CPlusPlus17) {
          bool FixItSuccess =
              tryEmitReplacePointerWithDerefResult(PtrVar, DerefForVarInit);
          FixItSuccess &=
              tryEmitPtrDerefInitGuardRewrite(DerefForVarInit, Guard);

          const char *InitedVarNoNeedMsg;
          if (FixItSuccess)
            InitedVarNoNeedMsg = "after the changes, the definition for %0 "
                                 "here is no longer needed";
          else
            InitedVarNoNeedMsg = "after the changes, the definition for %0 "
                                 "here should no longer be needed";

          auto Diag = diag(InitedVar->getLocation(), InitedVarNoNeedMsg,
                           DiagnosticIDs::Note)
                      << InitedVar;
          if (FixItSuccess)
            Diag << FixItHint::CreateRemoval(InitedVar->getSourceRange());
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

void SuperfluousLocalPtrVariableCheck::emitGuardDiagnostic(
    const PtrGuard *Guard) {
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

  diag(Guard->getGuardStmt()->getIfLoc(), GuardDiagMsg, DiagnosticIDs::Note)
      << Guard->getUsageExpr()->getDecl() << EarlyFlowType;
}

/// Create a replacement on the guard statement. We wish to transform:
///     T *p = ...;
///     if (!p) return;    /* guard ::= (!p) */
///     int i = p->foo();
/// into
///     int i;
///     if (T *p = ...; (!p) || ((i = {p->foo()}), void(), false)) return;
/// which results in doing the "return" if the guard matches
/// (i.e. p is null), and if the guard does not match, does the
/// assignment, but doesn't "flow" (return) even if the result
/// of the assignment evaluates to false.
///
/// This function potentially creates the FixItHint diagnostic for the rewrite
/// of the if().
bool SuperfluousLocalPtrVariableCheck::tryEmitPtrDerefInitGuardRewrite(
    const PtrDerefVarInit *Init, const PtrGuard *Guard) {
  const auto *Ptr = cast<VarDecl>(Guard->getUsageExpr()->getDecl());
  const VarDecl *InitedVar = Init->getInitialisedVar();
  const ASTContext &Ctx = Ptr->getASTContext();

  bool CouldCreateFixIt = true;

  const StringRef OriginalPtrDeclCode =
      getCode(Ptr->getBeginLoc(), Ptr->getEndLoc(), Ctx);
  if (OriginalPtrDeclCode.empty())
    CouldCreateFixIt = false;

  const StringRef GuardStmtCode =
      getCode(Guard->getGuardStmt()->getCond()->getBeginLoc(),
              Guard->getGuardStmt()->getCond()->getEndLoc(), Ctx);
  if (GuardStmtCode.empty())
    CouldCreateFixIt = false;

  const std::string InitedVarName = InitedVar->getName().str();
  std::string VarInitCode = getVarInitExprCode(Init->getInitialisedVar(), Ctx,
                                               /* OuterParen =*/false);
  if (VarInitCode == "()")
    CouldCreateFixIt = false;
  else {
    LLVM_DEBUG(llvm::dbgs() << "Original VarInitCode: " << VarInitCode << '\n');
    // Cut the name of the initialized variable at the beginning, if any.

    LLVM_DEBUG(llvm::dbgs() << "InitedVarName: " << InitedVarName << '\n');
    std::size_t VarNameInInitCodePos = VarInitCode.find(InitedVarName);
    LLVM_DEBUG(llvm::dbgs() << "find(" << VarInitCode << ", " << InitedVarName
                            << ") = " << VarNameInInitCodePos << '\n');
    if (VarNameInInitCodePos == 0)
      VarInitCode = VarInitCode.substr(InitedVarName.length());
    LLVM_DEBUG(llvm::dbgs()
               << "VarInitCode after cut of VarName: " << VarInitCode << '\n');

    // Cut the original initialiser statement's parens or brackets.
    if (*VarInitCode.begin() == '(' || *VarInitCode.begin() == '{')
      VarInitCode = VarInitCode.substr(1, VarInitCode.length() - 2);
    LLVM_DEBUG(llvm::dbgs()
               << "VarInitCode after cut of (, {: " << VarInitCode << '\n');
  }

  std::string RewrittenGuardCondition =
      (Twine(OriginalPtrDeclCode) + "; " + Twine('(') + GuardStmtCode +
       Twine(')') + " || (" + Twine('(') + InitedVarName + " = {" +
       VarInitCode + "})" + ", void(), false" + Twine(')'))
          .str();

  auto Diag =
      diag(Guard->getGuardStmt()->getIfLoc(),
           "consider scoping the pointer %0 into the branch, and assign to %1 "
           "during the guarding condition",
           DiagnosticIDs::Note)
      << Ptr << InitedVar;
  if (CouldCreateFixIt)
    Diag << FixItHint::CreateReplacement(
        Guard->getGuardStmt()->getCond()->getSourceRange(),
        RewrittenGuardCondition);

  return CouldCreateFixIt;
}

/// Create a replacement on the pointer variable to the result type.
/// We wish to transform:
///     T *p;
///     int i = p->foo();
/// into
///     int i;
/// The potential guard and dereference is rewritten by other functions.
bool SuperfluousLocalPtrVariableCheck::tryEmitReplacePointerWithDerefResult(
    const VarDecl *Ptr, const PtrDerefVarInit *Init) {
  const VarDecl *InitedVar = Init->getInitialisedVar();

  bool CouldCreateFixIt = true;

  std::string InitedVarDeclWithoutInitCode = Lexer::getSourceText(
      CharSourceRange::getCharRange(InitedVar->getOuterLocStart(),
                                    InitedVar->getInit()->getBeginLoc()),
      InitedVar->getASTContext().getSourceManager(),
      InitedVar->getASTContext().getLangOpts());
  // The string might be "T t =" or "T t = " if the original initialising
  // expression was with =, or "T " (if a '(' or '{' initialisation was used).

  if (InitedVarDeclWithoutInitCode.empty())
    CouldCreateFixIt = false;
  else {
    // Try to cut the ending "= " part off.
    std::size_t EqSignPos = InitedVarDeclWithoutInitCode.rfind("= ");
    if (EqSignPos == std::string::npos)
      EqSignPos = InitedVarDeclWithoutInitCode.rfind('=');
    if (EqSignPos != std::string::npos &&
        EqSignPos >= InitedVarDeclWithoutInitCode.length() - 3)
      InitedVarDeclWithoutInitCode =
          InitedVarDeclWithoutInitCode.substr(0, EqSignPos);
    while (InitedVarDeclWithoutInitCode.back() == ' ')
      InitedVarDeclWithoutInitCode.pop_back();

    const std::string VarName = InitedVar->getName();
    if (InitedVarDeclWithoutInitCode.find(VarName) == std::string::npos) {
      InitedVarDeclWithoutInitCode.push_back(' ');
      InitedVarDeclWithoutInitCode.append(VarName);
    }
  }

  auto Diag = diag(Ptr->getLocation(),
                   "consider declaring the variable %0 (for the dereference's "
                   "result) in the \"outer\" scope",
                   DiagnosticIDs::Note)
              << InitedVar;
  if (CouldCreateFixIt)
    Diag << FixItHint::CreateReplacement(Ptr->getSourceRange(),
                                         InitedVarDeclWithoutInitCode);
  return Diag;
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
