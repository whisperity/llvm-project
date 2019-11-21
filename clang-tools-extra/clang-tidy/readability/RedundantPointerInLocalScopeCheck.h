//===--- RedundantPointerInLocalScopeCheck.h - clang-tidy -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_REDUNDANTPOINTERINLOCALSCOPECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_REDUNDANTPOINTERINLOCALSCOPECHECK_H

#include "RedundantPointerCheck.h"

namespace clang {
namespace tidy {
namespace readability {

/// FIXME: Write a short description.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/readability-redundant-pointer-in-local-scope.html
class RedundantPointerInLocalScopeCheck : public RedundantPointerCheck {
public:
  RedundantPointerInLocalScopeCheck(StringRef Name, ClangTidyContext *Context)
      : RedundantPointerCheck(Name, Context) {}
  void onEndOfTranslationUnit() override;

private:
  void emitMainDiagnostic(const VarDecl *Ptr);
  void emitConsiderUsingInitCodeDiagnostic(const VarDecl *Ptr,
                                           const PtrUsage *Usage,
                                           const std::string &InitCode);
  void emitGuardDiagnostic(const PtrGuard *Guard);
  bool tryEmitPtrDerefInitGuardRewrite(const PtrDerefVarInit *Init,
                                       const PtrGuard *Guard);
  bool tryEmitReplacePointerWithDerefResult(const VarDecl *Ptr,
                                            const PtrDerefVarInit *Init);
};

} // namespace readability
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_READABILITY_REDUNDANTPOINTERINLOCALSCOPECHECK_H
