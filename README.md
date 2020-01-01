`cppcoreguidelines-avoid-adjacent-arguments-of-same-type` measurement scripts
=============================================================================

Prerequisites
-------------

To be able to emit warnings for error-prone constructs:

 * `cppcoreguidelines-avoid-adjacent-arguments-of-same-type` checker patched
   to *Clang-Tidy* and built according to LLVM Documentation.
   * See other branches named
     `clang-tidy/cppcoreguidelines-avoid-adjacent-arguments-of-same-type` in
     this project for the code.
   * Preferably use the branch that also models *implicit conversions*.

To run the analysis easily and run the measurements:

 * At least **release version `v6.11`** of
   [CodeChecker](http://github.com/Ericsson/CodeChecker) downloaded, installed
   and in `PATH`.
 * Refer to CodeChecker guidelines for details on how to run analysis on a
   particular project.
   * To run only the checker, specify the following checkers to
   `CodeChecker analyze`:
   ~~~~
   --analyzers clang-tidy
   --disable Weverything
   --enable cppcoreguidelines-avoid-adjacent-arguments-of-same-type
   ~~~~
