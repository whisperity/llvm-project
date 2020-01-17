//===--- RedundantPointerCheck.h - clang-tidy -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the RedundantPointerCheck class, which
/// is the base class for the "redundant local pointer variable" checks in the
/// readability module.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_REDUNDANTPOINTERCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_REDUNDANTPOINTERCHECK_H

#include "../ClangTidyCheck.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"

namespace clang {
namespace tidy {
namespace readability {

/// Base class of a usage info for pointer variables. This class acts as a
/// small (few pointers only) hook tieing nodes of an AST together for the
/// purpose of this check and its diagnostics.
struct PtrUsage {
  enum PUKind {
    /* Pointee usages. */
    Ptr_Argument,    //< Represents a "read" of the pointer as an argument.
    Ptr_Dereference, //< Represents a "read" where the ptr is dereferenced.
    Ptr_Deref_Init,  //< Represents a dereference used in an initialisation.

    /* Pointer usages. */
    Ptr_Guard //< Represents a "guard" on the pointer's value
              //< (most often a null or non-null check).
  };
  enum AnnotationKind {
    Pointee, //< Represents a "direct use" of the pointee object, an access.
    Pointer  //< Represents uses of a pointer variable that are not direct,
             //< but only concern the pointer itself.
  };
  PUKind getKind() const { return Kind; }
  AnnotationKind getAnnotationKind() const { return AnnotKind; }

  const DeclRefExpr *getUsageExpr() const { return RefExpr; }

protected:
  PtrUsage(const DeclRefExpr *UsageRef, PUKind K, AnnotationKind AK)
      : Kind(K), AnnotKind(AK), RefExpr(UsageRef) {}

private:
  const PUKind Kind;
  const AnnotationKind AnnotKind;
  const DeclRefExpr *RefExpr;
};

/// Tag type and intermittent base class representing direct pointer variable
/// usages which indicate a potential access on the pointee.
struct PointeePtrUsage : PtrUsage {
  static bool classof(const PtrUsage *I) {
    return I->getAnnotationKind() == Pointee;
  }

protected:
  PointeePtrUsage(const DeclRefExpr *UsageRef, PUKind K)
      : PtrUsage(UsageRef, K, Pointee) {}
};

/// Pointer variable passed as argument:
///     free(p)
struct PtrArgument : PointeePtrUsage {
  PtrArgument(const DeclRefExpr *Usage)
      : PointeePtrUsage(Usage, Ptr_Argument) {}

  static bool classof(const PtrUsage *I) {
    return I->getKind() == Ptr_Argument;
  }
};

/// Pointer dereferenced in some context:
///     send_bytes(t->numBytes);
///     read((*t).rbuf);
///     dump(*t);
///     something(ptr->func());
struct PtrDereference : PointeePtrUsage {
  PtrDereference(const DeclRefExpr *UsageRef, const Expr *UsageExpr)
      : PtrDereference(UsageRef, UsageExpr, Ptr_Dereference) {}

  const UnaryOperator *getUnaryOperator() const { return UnaryDeref; }
  const MemberExpr *getMemberExpr() const { return MemberRef; }
  const CXXMemberCallExpr *getCallableMemberExpr() const {
    return CallableMemberRef;
  }

  static bool classof(const PtrUsage *I) {
    return I->getKind() >= Ptr_Dereference && I->getKind() <= Ptr_Deref_Init;
  }

protected:
  PtrDereference(const DeclRefExpr *UsageRef, const Expr *UsageExpr, PUKind K)
      : PointeePtrUsage(UsageRef, K) {
    MemberRef = dyn_cast<MemberExpr>(UsageExpr);
    CallableMemberRef = dyn_cast<CXXMemberCallExpr>(UsageExpr);
    UnaryDeref = dyn_cast<UnaryOperator>(UsageExpr);

    assert((MemberRef || CallableMemberRef || UnaryDeref) &&
           "dereference usage must be filled!");
  }

private:
  const MemberExpr *MemberRef = nullptr;
  const CXXMemberCallExpr *CallableMemberRef = nullptr;
  const UnaryOperator *UnaryDeref = nullptr;
};

/// Pointer dereferenced in a context which initialises a variable:
///     int i = t->someIntVal;
///     auto *next = node->next;
struct PtrDerefVarInit : PtrDereference {
  PtrDerefVarInit(const DeclRefExpr *UsageRef, const Expr *DerefExpr,
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

/// Tag type and intermittent base class representing usages of a pointer
/// variable that do not concern the pointee but only the pointer itself.
struct PointerPtrUsage : PtrUsage {
  static bool classof(const PtrUsage *I) {
    return I->getAnnotationKind() == Pointer;
  }

protected:
  PointerPtrUsage(const DeclRefExpr *UsageRef, PUKind K)
      : PtrUsage(UsageRef, K, Pointer) {}
};

/// Guard with an early control flow redirect (return, continue, ...) on a
/// pointer variable:
///     if (p) return;
struct PtrGuard : PointerPtrUsage {
  PtrGuard(const DeclRefExpr *UsageRef, const IfStmt *Guard, const Stmt *FlowS)
      : PointerPtrUsage(UsageRef, Ptr_Guard), GuardStmt(Guard),
        FlowStmt(FlowS) {}

  const IfStmt *getGuardStmt() const { return GuardStmt; }
  const Stmt *getFlowStmt() const { return FlowStmt; }

  static bool classof(const PtrUsage *I) { return I->getKind() == Ptr_Guard; }

private:
  const IfStmt *GuardStmt;
  const Stmt *FlowStmt;
};

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

// FIXME: Use a smaller type (0x is hex literal, turn to bit shift!)
enum PtrVarFlags {
  PVF_None = 0x00000,
  PVF_Pointer = 0x00001,         //< Conventional pointer.
  PVF_Dereferenceable = 0x00010, //< User type that is dereferenceable,
                                 //< such as an iterator.
  PVF_LoopVar = 0x00100,         //< Loop variable.
  PVF_ParmVar = 0x01000,         //< Variable is parameter to the function.
  PVF_Initialiser = 0x10000,     //< Pointer has an initialising expression.

  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ PVF_Initialiser)
};

/// Holds information about usages (expressions that reference) of a
/// variable (declaration).
///
/// This data structure is used to store the contexts (expression or
/// declaration) in which pointer variable declarations are used.
/// One of this type is instantiated for each used variable that matches
/// the search criteria.
class UsageCollection {
public:
  using UseVector = llvm::SmallVector<PtrUsage *, 4>;

  UsageCollection() : Flags(PVF_None){};
  UsageCollection(UsageCollection &&UC);
  UsageCollection &operator=(UsageCollection &&UC);

  ~UsageCollection();

  /// Adds the given usage info to the list of usages collected by the instance.
  /// \returns true if an insertion took place, false otherwise (a reference
  /// position using the same DeclRefExpr as UsageInfo is already added).
  /// \note Ownership of UsageInfo is transferred to the function, and the
  /// collection! If the element could not be added, the argument is destroyed.
  bool addUsage(PtrUsage *UsageInfo);

  /// Removes the usage referring the same `DeclRefExpr` from the collection.
  void removeUsage(const PtrUsage *UsageInfo);

  const UseVector &getUsages() const { return CollectedUses; }

  /// Get all usages in order which are of the given usage type (isa<T>). This
  /// is a filtering operation, which can be costly!
  template <typename PtrUsageType> UseVector getUsagesOfKind() const {
    return UseVector{llvm::make_filter_range(
        CollectedUses, [](PtrUsage *UI) { return isa<PtrUsageType>(UI); })};
  }

  PtrVarFlags &flags() { return Flags; }
  const PtrVarFlags &flags() const { return Flags; }
  bool hasFlag(PtrVarFlags FlagBit) const { return Flags & FlagBit; }

private:
  UseVector CollectedUses;
  PtrVarFlags Flags;
};

/// Base class for the "redundant pointer variable" checks. This base
/// implementation is responsible for a common location of the modelling
/// needed to run the particular checks.
class RedundantPointerCheck : public ClangTidyCheck {
public:
  using UsageMap = llvm::DenseMap<const VarDecl *, UsageCollection>;

  RedundantPointerCheck(StringRef Name, ClangTidyContext *Context);
  ~RedundantPointerCheck() override;

  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) final {
    forAllCollected();
  }
  void onEndOfTranslationUnit() final { forAllCollected(); }

protected:
  /// Emits diagnostics for the groups of collected pointer usages when the
  /// collection is done.
  virtual void onEndOfModelledChunk(const UsageMap &Usages) = 0;

private:
  /// Helper class that acts as a callback for the matches on usage variables.
  class PtrUseModelCallback;
  PtrUseModelCallback *UsageCB;

  /// Clean up and emit diagnostics for the collected information in *UsageCB.
  void forAllCollected();
};

} // namespace readability
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_REDUNDANTPOINTERCHECK_H
