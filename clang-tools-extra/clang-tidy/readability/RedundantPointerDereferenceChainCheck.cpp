//===--- RedundantPointerDereferenceChainCheck.cpp - clang-tidy -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RedundantPointerDereferenceChainCheck.h"
#include "clang/AST/ASTContext.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"

#include <utility>

// clang-format off
#define  DEBUG_TYPE "PtrChain"
#include "llvm/Support/Debug.h"
// clang-format on

using namespace clang::ast_matchers;
using namespace llvm;

namespace clang {
namespace tidy {
namespace readability {

namespace {

// Variable initialisation chains from pointer dereference are single chains,
// as the modelling base class does not model if a pointer has multiple uses,
// like two or more dereferences.

/// Contains a chain of variables initialised from each other in order of
/// dereference.
using VarChain = SmallVector<const VarDecl *, 4>;

/// Contains which variable was used as the head element to initialise which
/// chain.
/// If value is an empty Optional, the variable is not used in an
/// initialisation which is useful for this check.
/// A non-empty Optional containing an empty VarChain vector indicates the
/// value isn't dereferenced any further, marking the end of the chain.
using ChainMap = DenseMap<const VarDecl *, Optional<VarChain>>;

using UsageMap = RedundantPointerCheck::UsageMap;

} // namespace

static void buildChainsFrom(const UsageMap &Usages, ChainMap &Chains,
                            const VarDecl *Var) {
  auto UIt = Usages.find(Var);
  if (UIt == Usages.end()) {
    // If the variable is not used any further, it signifies the end of any
    // chain.
    Chains[Var].emplace(VarChain{});
  }

  LLVM_DEBUG(llvm::dbgs() << "Build chain for " << Var << '\n');

  if (Chains.find(Var) != Chains.end())
    // This variable has already been handled.
    return;

  const UsageCollection::UseVector &PointeeUses =
      UIt->second.getUsagesOfKind<PointeePtrUsage>();
  // const UsageCollection::UseVector& PointerUses =
  // UIt->second.getUsagesOfKind<PointerPtrUsage>();
  if (PointeeUses.size() != 1) {
    LLVM_DEBUG(llvm::dbgs() << Var << " has non-single usages...\n");
    (void)
        Chains[Var]; // Default construct an Optional to "empty/nothing/false".
    return;
  }
  const auto *PtrVarInitUse = dyn_cast<PtrDerefVarInit>(PointeeUses.front());
  if (!PtrVarInitUse) {
    LLVM_DEBUG(llvm::dbgs() << Var << "'s usage is not a ptr dereference...\n");
    (void)Chains[Var];
    return;
  }

  // Try to see if the variable initialised from Var has been checked already.
  const VarDecl *InitedVar = PtrVarInitUse->getInitialisedVar();
  LLVM_DEBUG(llvm::dbgs() << Var << " initialises " << InitedVar << '\n');
  auto CIt = Chains.find(InitedVar);
  if (CIt != Chains.end() && !CIt->second) {
    LLVM_DEBUG(llvm::dbgs() << InitedVar << " has been calculated already\n");
    // The variable initialised by *Var is a dead end from chaining
    // perspective. Because Var is only used to initialise the InitedVar,
    // Var should mark a dead end too.
    LLVM_DEBUG(llvm::dbgs() << InitedVar << " is a dead end. Marking " << Var
                            << " as dead end too...\n");
    (void)Chains[Var];
    return;
  }

  if (CIt == Chains.end()) {
    // If the initialised variable is not found in the chains, calculate it.
    buildChainsFrom(Usages, Chains, InitedVar);
    CIt = Chains.find(InitedVar); // DenseMap iterator invalidated in recursion.
  }

  // The variable initialised from *Var now has a chain registered, a new
  // chain can be formed by "prepending" Var to it.
  LLVM_DEBUG(llvm::dbgs() << InitedVar << " is not a dead end.\n");
  LLVM_DEBUG(llvm::dbgs() << InitedVar << " has a chain of size: "
                          << CIt->second->size() << "\n");
  VarChain Chain{InitedVar};
  if (CIt->second->empty()) {
    LLVM_DEBUG(llvm::dbgs() << "Creating a chain with one element...\n");
  } else {
    LLVM_DEBUG(llvm::dbgs() << InitedVar << " has a chain of size: "
                            << CIt->second->size() << "\n");
    LLVM_DEBUG(llvm::dbgs() << "Creating longer chain...\n");
    Chain.insert(Chain.end(), CIt->second->begin(), CIt->second->end());
    LLVM_DEBUG(llvm::dbgs()
               << Var << " gets a chain of size: " << Chain.size() << "\n");
  }
  Chains[Var].emplace(std::move(Chain));

  // The chain for InitedVar had been incorporated into the chain for Var.
  Chains[InitedVar].reset(); // Mark empty Optional.
}

template <typename T>
static T *getOnlyUsage(const UsageMap &Usages, const VarDecl *Var) {
  auto It = Usages.find(Var);
  if (It == Usages.end())
    return nullptr;

  const UsageCollection::UseVector &V = It->second.getUsagesOfKind<T>();
  return V.size() == 1 ? cast<T>(V.front()) : nullptr;
}

void RedundantPointerDereferenceChainCheck::onEndOfTranslationUnit() {
  // const LangOptions &LOpts = getLangOpts();
  ChainMap Chains{Usages.size()};
  for (const auto &Usage : Usages)
    buildChainsFrom(Usages, Chains, Usage.first);

  for (const auto &E : Chains) {
    if (!E.second || E.second->empty())
      continue;
    const VarChain &Chain = *E.second;

    diag(Chain.back()->getLocation(),
         "%0 initialised through dereference chain of %1 pointers, each only "
         "used in a single dereference")
        << Chain.back() << static_cast<unsigned>(Chain.size() + 1);

    diag(E.first->getLocation(), "chain begins with %0", DiagnosticIDs::Note)
        << E.first;
    if (const auto *G = getOnlyUsage<PtrGuard>(Usages, E.first))
      diag(G->getGuardStmt()->getIfLoc(), "%0 is guarded by this branch",
           DiagnosticIDs::Note)
          << E.first;
    const auto *D = getOnlyUsage<PtrDerefVarInit>(Usages, E.first);
    assert(D &&
           "Tried to emit chain element without dereference forming chain?");

    for (const VarDecl *const &VD : Chain) {
      diag(D->getUsageExpr()->getLocation(),
           "contains a dereference of %0 in initialisation of %1",
           DiagnosticIDs::Note)
          << D->getUsageExpr()->getDecl() << D->getInitialisedVar();
      if (const auto *G = getOnlyUsage<PtrGuard>(Usages, VD))
        diag(G->getGuardStmt()->getIfLoc(), "%0 is guarded by this branch",
             DiagnosticIDs::Note)
            << VD;

      D = getOnlyUsage<PtrDerefVarInit>(Usages, VD);
      assert((VD == Chain.back() || (VD != Chain.back() && D)) &&
             "Tried to emit chain element without dereference forming chain?");
    }
    /*
        // LLVM_DEBUG(llvm::dbgs() << "Chain for " << Chain.first << '\n';
        // Chain.first->dump(llvm::dbgs()); llvm::dbgs() << '\n';);
        if (!Chain.second) {
          // LLVM_DEBUG(llvm::dbgs() << "No chain, dead end, isn't used as
          // dereference, or incorporated into longer chain.\n");
        } else {
          if (Chain.second->empty()) {
            // LLVM_DEBUG(llvm::dbgs() << "Empty chain.\n");
          } else {
            LLVM_DEBUG(llvm::dbgs() << "Chain for " << Chain.first << '\n';
                       Chain.first->dump(llvm::dbgs()); llvm::dbgs() << '\n';);
            for (const auto &Var : *Chain.second) {
              LLVM_DEBUG(llvm::dbgs() << "initialises variable " << Var << '\n';
                         Var->dump(llvm::dbgs()); llvm::dbgs() << '\n';);
              LLVM_DEBUG(llvm::dbgs() << "which in turn...\n");
            }
            LLVM_DEBUG(llvm::dbgs() << "initialises noone.\n");
          }
        }
    */
  }
}

} // namespace readability
} // namespace tidy
} // namespace clang
