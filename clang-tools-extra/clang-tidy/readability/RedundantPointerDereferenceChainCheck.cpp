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

using UsageMap = RedundantPointerCheck::UsageMap;

class Chain {
public:
  using ChainVec = llvm::SmallVector<const VarDecl *, 4>;

  explicit Chain(const VarDecl *Head) { append(Head); }
  Chain(const VarDecl *Head, const Chain &Elems) {
    // [H, C1, C2, ...]
    append(Head);
    append(Elems);
    HasPtrUsages = Elems.HasPtrUsages;
  }
  const VarDecl *head() const { return *Elements.begin(); }
  const VarDecl *tail() const { return *(Elements.end() - 1); }
  llvm::iterator_range<typename ChainVec::const_iterator> range() const {
    return {Elements.begin(), Elements.end()};
  }

  void append(const VarDecl *VD) { Elements.push_back(VD); }
  void append(const Chain &C) {
    llvm::for_each(Elements, [this](const VarDecl *VD) { append(VD); });
  }

  void markFirstElementNonElidable() { FirstElementElidable = false; }
  bool firstElementElidable() const { return FirstElementElidable; }
  void setHavingPtrUsages() { HasPtrUsages = true; }
  bool hasPtrUsages() const { return HasPtrUsages; }

private:
  // The list of elements of the chain. The first variable
  // dereference-initialises the second, the second the third, etc.
  ChainVec Elements;

  // Indicates if the first variable of the chain is really redundant, and
  // perhaps removable by the user.
  bool FirstElementElidable = true;

  // Indicates a <base checker>::PointerPtrUsage was bound to any of the chain
  // elements.
  bool HasPtrUsages = false;
};

/// Map of chains indexed by first element of the chain.
using ChainMap = llvm::DenseMap<const VarDecl *, llvm::SmallVector<Chain, 4>>;

} // namespace

template <typename T>
static T *getOnlyUsage(const UsageMap &Usages, const VarDecl *Var) {
  auto It = Usages.find(Var);
  if (It == Usages.end())
    return nullptr;

  const UsageCollection::UseVector &V = It->second.getUsagesOfKind<T>();
  return V.size() == 1 ? cast<T>(V.front()) : nullptr;
}

static void buildChainsFrom(const UsageMap &Usages, ChainMap &Chains,
                            const VarDecl *Var) {
  LLVM_DEBUG(llvm::dbgs() << "buildChainsFrom() called for " << Var << '\n';
             Var->dump(llvm::dbgs()); llvm::dbgs() << '\n';);
  if (Chains.find(Var) != Chains.end()) {
    LLVM_DEBUG(llvm::dbgs() << "Var " << Var << " had been visited already.\n");
    LLVM_DEBUG(llvm::dbgs() << "buildChainsFrom <<<<<<< returning.\n");
  }

  auto UIt = Usages.find(Var);
  if (UIt == Usages.end()) {
    LLVM_DEBUG(llvm::dbgs() << "Var " << Var << " is not used.\n");
    Chains[Var]; // Mark Var visited.
    LLVM_DEBUG(llvm::dbgs() << "buildChainsFrom <<<<<<< returning.\n");
    return;
  }

  bool HasPtrUsage = !UIt->second.getUsagesOfKind<PointerPtrUsage>().empty();
  const UsageCollection::UseVector &PointeeUsages =
      UIt->second.getUsagesOfKind<PointeePtrUsage>();
  const UsageCollection::UseVector &PtrVarInitDerefs =
      UIt->second.getUsagesOfKind<PtrDerefVarInit>();
  for (const PtrUsage *PU : PtrVarInitDerefs) {
    const VarDecl *InitedVar = cast<PtrDerefVarInit>(PU)->getInitialisedVar();
    LLVM_DEBUG(llvm::dbgs() << "Var used in initialisation of \n";
               InitedVar->dump(llvm::dbgs()); llvm::dbgs() << '\n');

    // Check if potential continuation of chain has been calculated already.
    auto CIt = Chains.find(InitedVar);
    if (CIt == Chains.end()) {
      LLVM_DEBUG(llvm::dbgs() << "Doing a sick recursive call!\n");
      buildChainsFrom(Usages, Chains, InitedVar);
    }
    CIt = Chains.find(InitedVar); // Recursive call might invalidate iterator.

    // Now after the recursion ended, the "tails" of the chains starting from
    // InitedVar should be calculated. How to combine these into a chain of
    // [Var, InitedVar, ...]?
    if (PointeeUsages.size() == 1 && PtrVarInitDerefs.size() == 1 &&
        CIt->second.size() == 1) {
      // If this initialisation (InitVar = *Var;) is the only usage, and Var
      // hasn't multiple chains, the chain merges trivially.
      LLVM_DEBUG(llvm::dbgs() << "Chaning chain of " << InitedVar << " after "
                              << Var << '\n');
      Chain C{Var, CIt->second.front()};
      if (HasPtrUsage)
        C.setHavingPtrUsages();

      Chains[Var].emplace_back(
          std::move(C));         // Store chain [Var, InitedVar, ...] for Var.
      Chains[InitedVar].clear(); // Keep InitedVar marked as visited, but the
                                 // chain is consumed.

    } else if (false /* FIXME: What if the above conditions don't apply */) {
    }
  }

  Chains[Var]; // Mark Var visited.

  LLVM_DEBUG(llvm::dbgs() << "buildChainsFrom <<<<<<< returning.\n");
}

void RedundantPointerDereferenceChainCheck::onEndOfTranslationUnit() {
  // const LangOptions &LOpts = getLangOpts();
  ChainMap Chains{Usages.size()};
  for (const auto &Usage : Usages)
    buildChainsFrom(Usages, Chains, Usage.first);

#if 0
  // DEBUG.
  for (const auto &Chain : Chains) {
    LLVM_DEBUG(llvm::dbgs() << "Chain for " << Chain.first << '\n';
               Chain.first->dump(llvm::dbgs()); llvm::dbgs() << '\n';);
    if (!Chain.second) {
      LLVM_DEBUG(llvm::dbgs()
                 << "No chain, dead end, isn't used as dereference, or "
                    "incorporated into longer chain.\n");
    } else {
      if (Chain.second->empty()) {
        LLVM_DEBUG(llvm::dbgs() << "Empty chain.\n");
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
  }
  // END DEBUG.

  for (const auto &E : Chains) {
    if (!E.second || E.second->empty())
      continue;
    const VarChain &Chain = *E.second;

    diag(Chain.back()->getLocation(),
         "%0 initialised through dereference chain of %1 pointers, each only "
         "used in a single dereference")
        << Chain.back() << static_cast<unsigned>(Chain.size());

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
  }
#endif
}

} // namespace readability
} // namespace tidy
} // namespace clang
