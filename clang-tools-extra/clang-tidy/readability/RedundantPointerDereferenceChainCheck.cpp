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
  using ChainVec = SmallVector<const VarDecl *, 4>;

  explicit Chain(const VarDecl *Head) { append(Head); }
  Chain(const VarDecl *Head, const VarDecl *Last) {
    append(Head);
    append(Last);
  }
  Chain(const VarDecl *Head, const Chain &Elems) {
    // [H, C1, C2, ...]
    append(Head);
    append(Elems);
    HasPtrUsages = Elems.HasPtrUsages;
  }
  const VarDecl *head() const { return Elements[0]; }
  const VarDecl *last() const { return Elements[Elements.size() - 1]; }
  const VarDecl *at(size_t I) const {
    assert(I < size());
    return Elements[I];
  }

  llvm::iterator_range<typename ChainVec::const_iterator> range() const {
    return {Elements.begin(), Elements.end()};
  }

  size_t size() const { return Elements.size(); }

  void append(const VarDecl *VD) { Elements.push_back(VD); }
  void append(const Chain &C) {
    for_each(C.Elements, [this](const VarDecl *VD) { append(VD); });
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
using ChainMap = DenseMap<const VarDecl *, SmallVector<Chain, 4>>;

} // namespace

template <typename T>
static SmallVector<const T *, 4> getUsagesCast(const UsageMap &Usages,
                                               const VarDecl *Var) {
  auto It = Usages.find(Var);
  if (It == Usages.end())
    return {};

  const UsageCollection::UseVector &V = It->second.getUsagesOfKind<T>();
  SmallVector<const T *, 4> Ret;
  transform(V, std::back_inserter(Ret),
            [](const PtrUsage *PU) { return cast<const T>(PU); });
  return Ret;
}

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
  LLVM_DEBUG(llvm::dbgs() << ">>>>>>> buildChainsFrom() called for "
                          << Var->getName() << '\n');
  if (Chains.find(Var) != Chains.end()) {
    LLVM_DEBUG(llvm::dbgs()
               << "Var " << Var->getName() << " had been visited already.\n");
    LLVM_DEBUG(llvm::dbgs() << "buildChainsFrom <<<<<<< returning.\n");
    return;
  }

  auto UIt = Usages.find(Var);
  if (UIt == Usages.end()) {
    LLVM_DEBUG(llvm::dbgs() << "Var " << Var->getName() << " is not used.\n");
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
    LLVM_DEBUG(llvm::dbgs() << "Var used in initialisation of "
                            << InitedVar->getName() << '\n');

    // Check if potential continuation of chain has been calculated already.
    auto CIt = Chains.find(InitedVar);
    if (CIt == Chains.end()) {
      LLVM_DEBUG(llvm::dbgs() << ">>>>>>> Doing a sick recursive call!\n");
      buildChainsFrom(Usages, Chains, InitedVar);
    }
    CIt = Chains.find(InitedVar); // Recursive call might invalidate iterator.

    // Now after the recursion ended, the "tails" of the chains starting from
    // InitedVar should be calculated. How to combine these into a chain of
    // [Var, InitedVar, ...]?
    Optional<Chain> NewChain;

    if (CIt->second.size() == 1) {
      // If this initialisation (InitVar = *Var;) is the only **usage**, and Var
      // hasn't multiple chains, the chain merges trivially.
      LLVM_DEBUG(llvm::dbgs() << "Chaining chain of " << InitedVar->getName()
                              << " after " << Var->getName() << '\n');
      NewChain.emplace(Var, CIt->second.front());

      // Keep InitedVar marked as visited, but the chain is consumed by the
      // merge.
      Chains[InitedVar].clear();
    } else {
      // If "InitedVar = *Var;" is the only initialisation, but Var is used
      // multiple times or is not used as a dereference (i.e. forms no
      // chain), the current chain has to end here, with InitedVar's chains
      // remaining intact.
      LLVM_DEBUG(llvm::dbgs()
                 << "Forming chain [" << Var->getName() << ", "
                 << InitedVar->getName() << "], but nothing more, as "
                 << InitedVar->getName() << " has " << CIt->second.size()
                 << " chains.\n");
      NewChain.emplace(Var, InitedVar);
    }

    if (HasPtrUsage)
      NewChain->setHavingPtrUsages();

    if (PointeeUsages.size() > 1) {
      // If Var is used multiple times, then the chains can't be
      // trivially merged, as Var could not be elided from the code.
      LLVM_DEBUG(llvm::dbgs()
                 << "Var " << Var->getName() << " used in "
                 << PtrVarInitDerefs.size() << " VarInits, now handling "
                 << InitedVar->getName() << '\n');
      NewChain->markFirstElementNonElidable();
    }

    // Store the calculated chain.
    Chains[Var].emplace_back(std::move(*NewChain));
  }

  Chains[Var]; // Mark Var visited.

  LLVM_DEBUG(llvm::dbgs() << "buildChainsFrom <<<<<<< returning.\n");
}

void RedundantPointerDereferenceChainCheck::onEndOfModelledChunk(
    const UsageMap &Usages) {
  // const LangOptions &LOpts = getLangOpts();
  ChainMap Chains{Usages.size()};
  for (const auto &Usage : Usages)
    buildChainsFrom(Usages, Chains, Usage.first);

  for (const auto &E : Chains) {
    // LLVM_DEBUG(llvm::dbgs() << "Chains for " << E.first->getName() << '\n';);
    if (E.second.empty())
      continue;

    for (const auto &C : E.second) {
      LLVM_DEBUG(llvm::dbgs() << "\n>>> NEW CHAIN from " << C.head()->getName()
                              << " to " << C.last()->getName() << " <<<\n");
      assert(C.head() == E.first &&
             "Bogus modelling: chain stored for wrong VarDecl!");

      if (!C.firstElementElidable())
        LLVM_DEBUG(llvm::dbgs() << "The first element cannot be elided.\n");
      if (C.hasPtrUsages())
        LLVM_DEBUG(llvm::dbgs() << "There are guard statements.\n");

      for (const auto *VD : C.range()) {
        LLVM_DEBUG(llvm::dbgs() << "Element of chain: " << VD;
                   // llvm::dbgs() << '\n';
                   llvm::dbgs() << " " << VD->getName();
                   // VD->dump(llvm::dbgs());
                   llvm::dbgs() << '\n';);
      }
    }
  }

  for (const auto &ChainsForVar : Chains) {
    if (ChainsForVar.second.empty())
      continue;

    for (const Chain &Chain : ChainsForVar.second) {
      if (Chain.size() < 3)
        // Chains of length 2 (single unused ptr) is handled by another check.
        continue;

      diag(Chain.last()->getLocation(),
           "%0 initialised from dereference chain of %1 pointers, %2 only "
           "used in a single dereference")
          << Chain.last() << static_cast<unsigned>(Chain.size() - 1)
          << (Chain.firstElementElidable() ? "each" : "most");

      const char *BeginMsg;
      if (Chain.firstElementElidable())
        BeginMsg = "chain begins with %0";
      else
        BeginMsg = "chain begins with %0, but that variable cannot be elided";
      diag(Chain.head()->getLocation(), BeginMsg, DiagnosticIDs::Note)
          << Chain.head();

      for (size_t I = 1; I < Chain.size(); ++I) {
        const VarDecl *Var = Chain.at(I - 1);
        const VarDecl *InitedVar = Chain.at(I);
        LLVM_DEBUG(llvm::dbgs()
                   << "chain contains dereference of " << Var->getName()
                   << " in initialisation of " << InitedVar->getName() << '\n');
        const auto &DerefUsages = getUsagesCast<PtrDerefVarInit>(Usages, Var);
        const auto &DerefIt =
            find_if(DerefUsages, [&InitedVar](const PtrDerefVarInit *PDVI) {
              return PDVI->getInitialisedVar() == InitedVar;
            });
        assert(DerefIt != DerefUsages.end() &&
               "No usage found for chain element built from usage.");
        diag((*DerefIt)->getUsageExpr()->getLocation(),
             "contains a dereference of %0 in initialisation of %1",
             DiagnosticIDs::Note)
            << Var << InitedVar;

#if 0
        if (const auto* G = getOnlyUsage<PtrGuard>(Usages, VD))
          diag(G->getGuardStmt()->getIfLoc(), "%0 is guarded by this branch",
               DiagnosticIDs::Note)
            << VD;
#endif
      }

#if 0
      if (const auto *G = getOnlyUsage<PtrGuard>(Usages, E.first))
        diag(G->getGuardStmt()->getIfLoc(), "%0 is guarded by this branch",
             DiagnosticIDs::Note)
            << E.first;
#endif
    }
  }
}

} // namespace readability
} // namespace tidy
} // namespace clang
