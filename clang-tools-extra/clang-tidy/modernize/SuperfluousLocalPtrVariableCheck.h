//===--- SuperfluousLocalPtrVariableCheck.h - clang-tidy --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_SUPERFLUOUSLOCALPTRVARIABLECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_SUPERFLUOUSLOCALPTRVARIABLECHECK_H

#include "../ClangTidyCheck.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"

namespace clang {
namespace tidy {
namespace modernize {

// QUESTION: Should these things be put into a namespace under modernize?

/// Base class of a usage info for pointer variables. This class acts as a
/// small (few pointers only) hook tieing nodes of an AST together for the
/// purpose of this check and its diagnostics.
struct PtrVarDeclUsageInfo {
  enum DUIKind {
    DUIK_ParamPassing, //< Represents a "read" of the pointer as an argument.
    DUIK_Dereference,  //< Represents a "read" where the ptr is dereferenced.
    DUIK_VarInit       //< Represents a dereference used in an initialisation.
  };

  DUIKind getKind() const { return Kind; }
  const DeclRefExpr *getUsageExpr() const { return RefExpr; }

protected:
  PtrVarDeclUsageInfo(const DeclRefExpr *UsageRef, DUIKind K)
      : Kind(K), RefExpr(UsageRef) {}

private:
  const DUIKind Kind;
  const DeclRefExpr *RefExpr;
};

/// Pointer variable passed as argument:
///     free(p)
struct PtrVarParamPassing : PtrVarDeclUsageInfo {
  PtrVarParamPassing(const DeclRefExpr *Usage)
      : PtrVarDeclUsageInfo(Usage, DUIK_ParamPassing) {}

  static bool classof(const PtrVarDeclUsageInfo *I) {
    return I->getKind() == DUIK_ParamPassing;
  }
};

/// Pointer dereferenced in some context:
///     send_bytes(t->numBytes);
///     dump(*t);
struct PtrVarDereference : PtrVarDeclUsageInfo {
  PtrVarDereference(const DeclRefExpr *UsageRef, const Expr *UsageExpr)
      : PtrVarDereference(UsageRef, UsageExpr, DUIK_Dereference) {}

  const UnaryOperator *getUnaryOperator() const { return UnaryDeref; }
  const MemberExpr *getMemberExpr() const { return MemberRef; }

  static bool classof(const PtrVarDeclUsageInfo *I) {
    return I->getKind() >= DUIK_Dereference;
  }

protected:
  PtrVarDereference(const DeclRefExpr *UsageRef, const Expr *UsageExpr,
                    DUIKind K)
      : PtrVarDeclUsageInfo(UsageRef, K) {
    MemberRef = dyn_cast<MemberExpr>(UsageExpr);
    UnaryDeref = dyn_cast<UnaryOperator>(UsageExpr);

    assert((MemberRef || UnaryDeref) && "dereference usage must be t-> or *t");
  }

private:
  const MemberExpr *MemberRef = nullptr;
  const UnaryOperator *UnaryDeref = nullptr;
};

/// Pointer dereferenced in a context which initialises a variable:
///     int i = t->someIntVal;
///     auto *next = node->next;
struct PtrVarDerefInit : PtrVarDereference {
  PtrVarDerefInit(const DeclRefExpr *UsageRef, const Expr *DerefExpr,
                  const VarDecl *InitVal)
      : PtrVarDereference(UsageRef, DerefExpr, DUIK_VarInit),
        InitedVarDecl(InitVal) {}

  const VarDecl *getInitialisedVar() const { return InitedVarDecl; }

  static bool classof(const PtrVarDeclUsageInfo *I) {
    return I->getKind() == DUIK_VarInit;
  }

private:
  const VarDecl *InitedVarDecl;
};

// FIXME: Perhaps multiple usages should also be tracked?
//          T* t = ...;
//          if (!t) return;
//          U* u = t->u;
//          W* w = t->w;
//        still doesn't warrant 't' to be a thing, one could still just do
//          U* u = t ? t->u : nullptr;  // t?->u
//          W* w = t ? t->w : nullptr;  // t?->w
/// Holds information about usages (referencing expressions) of a declaration.
///
/// This data structure is used to store in which context (expression or
/// declaration) a previous pointer variable declaration is used.
class PtrVarDeclUsageCollection {
public:
  using UseVector = llvm::SmallVector<PtrVarDeclUsageInfo *, 4>;

  ~PtrVarDeclUsageCollection();

  /// Adds the given usage info to the list of usages collected by the instance.
  /// \returns true if an insertion took place, false otherwise (the UsageExpr
  /// in UsageInfo is ignored, or this UsageInfo is already added).
  /// \note Ownership of UsageInfo is transferred to the collection!
  bool addUsage(PtrVarDeclUsageInfo *UsageInfo);

  /// Adds the given new usage info into the list of usages collected in place
  /// of the old info. If a replacement can take place, the instance pointed to
  /// by OldInfo is destroyed in the process. Otherwise, OldInfo stays valid
  /// and in the data structure.
  /// \returns true if a replace took place, false otherwise (the UsageExpr
  /// in NewInfo is ignored or NewInfo is already added).
  /// \note Ownership of NewInfo is transferred to the collection!
  bool replaceUsage(PtrVarDeclUsageInfo *OldInfo, PtrVarDeclUsageInfo *NewInfo);

  bool hasUsages() const { return !CollectedUses.empty(); }
  bool hasMultipleUsages() const { return CollectedUses.size() > 1; }

  const UseVector &getUsages() const { return CollectedUses; }

  template <PtrVarDeclUsageInfo::DUIKind Kind>
  PtrVarDeclUsageInfo *getNthUsageOfKind(size_t N) const;

  void ignoreUsageRef(const DeclRefExpr *DRE) { IgnoredUses.insert(DRE); }
  bool isIgnored(const DeclRefExpr *DRE) { return IgnoredUses.count(DRE); }

private:
  UseVector CollectedUses;
  llvm::SmallPtrSet<const DeclRefExpr *, 4> IgnoredUses;
};

using ReferencingMap =
    llvm::DenseMap<const VarDecl *, PtrVarDeclUsageCollection>;

/// FIXME: Write a short description.
///        T* tp = ...;
///        if (!tp) return; // This should be ignored.
///        U* up = tp->something;
///
///        Having tp here is superfluous, use initializing if or ?-> :P
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/modernize-superfluous-local-ptr-variable.html
class SuperfluousLocalPtrVariableCheck : public ClangTidyCheck {
public:
  SuperfluousLocalPtrVariableCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onEndOfTranslationUnit() override;

private:
  ReferencingMap References;
};

} // namespace modernize
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_SUPERFLUOUSLOCALPTRVARIABLECHECK_H
