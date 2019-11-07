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

/// Base class of a usage info for pointer variables. This class acts as a
/// small (few pointers only) hook tieing nodes of an AST together for the
/// purpose of this check and its diagnostics.
struct PtrUsage {
  enum PUKind {
    Ptr_Argument,    //< Represents a "read" of the pointer as an argument.
    Ptr_Dereference, //< Represents a "read" where the ptr is dereferenced.
    Ptr_Deref_Init,  //< Represents a dereference used in an initialisation.
    Ptr_Guard        //< Represents a "guard" on the pointer's value
                     //< (most often a null or non-null check).
  };

  PUKind getKind() const { return Kind; }
  const DeclRefExpr *getUsageExpr() const { return RefExpr; }

protected:
  PtrUsage(const DeclRefExpr *UsageRef, PUKind K)
      : Kind(K), RefExpr(UsageRef) {}

private:
  const PUKind Kind;
  const DeclRefExpr *RefExpr;
};

/// Pointer variable passed as argument:
///     free(p)
struct PtrArgument : PtrUsage {
  PtrArgument(const DeclRefExpr *Usage) : PtrUsage(Usage, Ptr_Argument) {}

  static bool classof(const PtrUsage *I) {
    return I->getKind() == Ptr_Argument;
  }
};

/// Pointer dereferenced in some context:
///     send_bytes(t->numBytes);
///     read((*t).rbuf);
///     dump(*t);
struct PtrDereference : PtrUsage {
  PtrDereference(const DeclRefExpr *UsageRef, const Expr *UsageExpr)
      : PtrDereference(UsageRef, UsageExpr, Ptr_Dereference) {}

  const UnaryOperator *getUnaryOperator() const { return UnaryDeref; }
  const MemberExpr *getMemberExpr() const { return MemberRef; }

  static bool classof(const PtrUsage *I) {
    return I->getKind() >= Ptr_Dereference && I->getKind() <= Ptr_Deref_Init;
  }

protected:
  PtrDereference(const DeclRefExpr *UsageRef, const Expr *UsageExpr, PUKind K)
      : PtrUsage(UsageRef, K) {
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
struct PtrVarDerefInit : PtrDereference {
  PtrVarDerefInit(const DeclRefExpr *UsageRef, const Expr *DerefExpr,
                  const VarDecl *InitVal)
      : PtrDereference(UsageRef, DerefExpr, Ptr_Deref_Init),
        InitedVarDecl(InitVal) {}

  const VarDecl *getInitialisedVar() const { return InitedVarDecl; }

  static bool classof(const PtrUsage *I) {
    return I->getKind() == Ptr_Deref_Init;
  }

private:
  const VarDecl *InitedVarDecl;
};

/// Guard with an early control flow redirect (return, continue, ...) on a
/// pointer variable:
///     if (p) return;
struct PtrGuard : PtrUsage {
  PtrGuard(const DeclRefExpr *UsageRef, const IfStmt *Guard, const Stmt *FlowS)
      : PtrUsage(UsageRef, Ptr_Guard), GuardStmt(Guard), FlowStmt(FlowS) {}

  const IfStmt *getGuardStmt() const { return GuardStmt; }
  const Stmt *getFlowStmt() const { return FlowStmt; }

  static bool classof(const PtrUsage *I) { return I->getKind() == Ptr_Guard; }

private:
  const IfStmt *GuardStmt;
  const Stmt *FlowStmt;
};

/// Holds information about usages (expressions that reference) of a
/// declaration.
///
/// This data structure is used to store in which context (expression or
/// declaration) a previous pointer variable declaration is used.
// FIXME: This needs a refactoring... again!
class UsageCollection {
public:
  using UseVector = llvm::SmallVector<PtrUsage *, 4>;

  ~UsageCollection();

  /// Adds the given usage info to the list of usages collected by the instance.
  /// \returns true if an insertion took place, false otherwise (UsageInfo is
  /// already added).
  /// \note Ownership of UsageInfo is transferred to the collection!
  bool addUsage(PtrUsage *UsageInfo);

  // FIXME: Do we need all these data structure manipulators after all?

  /// Adds the given new usage info into the list of usages collected in place
  /// of the old info. If a replacement can take place, the instance pointed to
  /// by OldInfo is destroyed in the process. Otherwise, OldInfo stays valid
  /// and in the data structure.
  /// \returns true if a replace took place, false otherwise (NewInfo is
  /// already added).
  /// \note Ownership of NewInfo is transferred to the collection!
  // FIXME: Maybe unneeded.
  bool replaceUsage(PtrUsage *OldInfo, PtrUsage *NewInfo);

  bool hasUsages() const { return !CollectedUses.empty(); }

  // FIXME: Maybe unneeded.
  bool hasMultipleUsages() const { return CollectedUses.size() > 1; }

  const UseVector &getUsages() const { return CollectedUses; }

  /// Get all usages in order which are of the given usage type (isa<T>). This
  /// is a filtering operation, which can be costly!
  template <typename PtrUsageType> UseVector getUsages() const;

private:
  UseVector CollectedUses;
};

using UsageMap = llvm::DenseMap<const VarDecl *, UsageCollection>;

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
  UsageMap Usages;
};

} // namespace modernize
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_SUPERFLUOUSLOCALPTRVARIABLECHECK_H
