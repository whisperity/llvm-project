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
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/Casting.h"

namespace clang {
namespace tidy {
namespace modernize {

/// Holds information about usages (referencing expressions) of a declaration.
///
/// This data structure is used to store in which context (expression or
/// declaration) a previous declaration -- for the sake of this check, a
/// dereference of a pointer variable declaration -- ???

// QUESTION: Should these things be put into a namespace under modernize?

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
};

/// Pointer dereferenced in some context:
///     send_bytes(t->numBytes);
///     dump(*t);
struct PtrVarDereference : PtrVarDeclUsageInfo {
  PtrVarDereference(const DeclRefExpr *UsageRef, const Expr *UsageExpr)
      : PtrVarDereference(UsageRef, UsageExpr, DUIK_Dereference) {}

  bool isUnaryDereference() const { return UnaryDeref; }
  const UnaryOperator *getUnaryOperator() const {
    assert(UnaryDeref && "not a unary dereference usage.");
    return UnaryDeref;
  }

  bool isMemberAccessDereference() const { return MemberRef; }
  const MemberExpr *getMemberExpr() const {
    assert(MemberRef && "not a member access usage.");
    return MemberRef;
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
// FIXME: This should be changed to use the classes above.
class PtrVarDeclUsageCollection {
public:
  bool hasUsage() const { return !Usage.empty(); }
  const DeclRefExpr *getUsage() const {
    return hasUsage() ? Usage.front() : nullptr;
  }
  void setUsage(const DeclRefExpr *DRE);
  void ignoreUsage(const DeclRefExpr *DRE);

private:
  // Size is 1 is as we normally only care for variables that are referenced
  // only once.
  llvm::TinyPtrVector<const DeclRefExpr *> Usage;
  llvm::SmallVector<const DeclRefExpr *, 2> IgnoredUsages; // FIXME: SmallPtrSet
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
