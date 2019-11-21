//===--------- RedundantPointerInLocalScopeCheck.cpp - clang-tidy ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RedundantPointerInLocalScopeCheck.h"
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
  const Expr *InitE = Var->getInit();
  if (!InitE)
    return OuterParen ? "()" : "";

  return (Twine(OuterParen ? "(" : "") +
          getCode(InitE->getBeginLoc(), InitE->getEndLoc(), Ctx) +
          Twine(OuterParen ? ")" : ""))
      .str();
}

static bool canBeDefaultConstructed(const CXXRecordDecl *RD) {
  assert(RD->hasDefinition() &&
         "for forward declarations the answer is unknown.");

  if (RD->isAggregate())
    return true;

  if (RD->hasDefaultConstructor()) {
    for (const auto &D : RD->decls()) {
      const auto *ConD = dyn_cast<CXXConstructorDecl>(D);
      if (!ConD)
        continue;
      if (!ConD->isDefaultConstructor())
        continue;

      if (ConD->isDeleted() || ConD->isDeletedAsWritten())
        return false;
      if (ConD->isDefaulted() || ConD->isExplicitlyDefaulted())
        return true;

      // Found a default-ctor, so we can default-construct.
      return true;
    }
  }

  return false;
}

void RedundantPointerInLocalScopeCheck::registerMatchers(MatchFinder *Finder) {
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

void RedundantPointerInLocalScopeCheck::check(
    const MatchFinder::MatchResult &Result) {
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

void RedundantPointerInLocalScopeCheck::onEndOfTranslationUnit() {
  const LangOptions &LOpts = getLangOpts();

  for (const auto &Usage : Usages) {
    const VarDecl *PtrVar = Usage.first;
    const UsageCollection::UseVector &PointeeUsages =
        Usage.second.getUsages<PointeePtrUsage>();
    const UsageCollection::UseVector &PointerUsages =
        Usage.second.getUsages<PointerPtrUsage>();

    if (PointeeUsages.size() != 1)
      continue;
    if (PointerUsages.size() > 1)
      // Currently, "Pointer(-only) usages" are if() guards, from which if there
      // multiple, no automatic rewriting seems sensible enough.
      continue;

    const PtrUsage *TheUsage = PointeeUsages.front();
    const auto *TheUseExpr = TheUsage->getUsageExpr();
    // Different type of diagnostics should be created if there is an annotating
    // guard statement on the pointer's value.
    bool HasPointerAnnotatingUsages = !PointerUsages.empty();

    // Retrieve the code text the used pointer is created with.
    std::string PtrVarInitExprCode = getVarInitExprCode(
        PtrVar, PtrVar->getASTContext(), /* OuterParen =*/true);
    if (PtrVarInitExprCode == "()")
      // If we don't know how the pointer variable is initialised, bail out.
      continue;

    if (const auto *DerefForVarInit = dyn_cast<PtrDerefVarInit>(TheUsage)) {
      const VarDecl *InitedVar = DerefForVarInit->getInitialisedVar();
      const Type *VarTy = InitedVar->getType()->getUnqualifiedDesugaredType();

      if (const auto *RD = VarTy->getAsCXXRecordDecl())
        if (!canBeDefaultConstructed(RD))
          // Do not suggest the rewrite as the inited variable couldn't be
          // default-constructed.
          continue;

      emitMainDiagnostic(PtrVar);
      diag(TheUseExpr->getLocation(),
           "usage: %0 dereferenced in the initialisation of %1",
           DiagnosticIDs::Note)
          << PtrVar << InitedVar;

      if (!HasPointerAnnotatingUsages)
        emitConsiderUsingInitCodeDiagnostic(PtrVar, TheUsage,
                                            PtrVarInitExprCode);
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

      emitConsiderUsingInitCodeDiagnostic(PtrVar, TheUsage, PtrVarInitExprCode);
    }
  }
}

/// Helper function that emits the main "local ptr variable may be superfluous"
/// warning for the given variable.
void RedundantPointerInLocalScopeCheck::emitMainDiagnostic(const VarDecl *Ptr) {
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
void RedundantPointerInLocalScopeCheck::emitConsiderUsingInitCodeDiagnostic(
    const VarDecl *Ptr, const PtrUsage *Usage, const std::string &InitCode) {
  diag(Usage->getUsageExpr()->getLocation(),
       "consider using the code that initialises %0 here", DiagnosticIDs::Note)
      << Ptr
      << FixItHint::CreateReplacement(Usage->getUsageExpr()->getSourceRange(),
                                      InitCode);
}

void RedundantPointerInLocalScopeCheck::emitGuardDiagnostic(
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
bool RedundantPointerInLocalScopeCheck::tryEmitPtrDerefInitGuardRewrite(
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
    // Cut the name of the initialized variable at the beginning, if any.
    std::size_t VarNameInInitCodePos = VarInitCode.find(InitedVarName);
    if (VarNameInInitCodePos == 0)
      VarInitCode = VarInitCode.substr(InitedVarName.length());

    // Cut the original initialiser statement's parens or brackets.
    if (*VarInitCode.begin() == '(' || *VarInitCode.begin() == '{')
      VarInitCode = VarInitCode.substr(1, VarInitCode.length() - 2);
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
bool RedundantPointerInLocalScopeCheck::tryEmitReplacePointerWithDerefResult(
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

template <typename PtrUsageTypes>
UsageCollection::UseVector UsageCollection::getUsages() const {
  return UseVector{llvm::make_filter_range(
      CollectedUses, [](PtrUsage *UI) { return isa<PtrUsageTypes>(UI); })};
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
