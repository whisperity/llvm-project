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

To run the analysis easily:

 * **Release version `v6.11`** of
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
   * Tweak the checker options `MinimumLength`, `CVRMixPossible` and
     `ImplicitConversion` by specifying a `--tidyargs` file.
 * Store analysis results with the following nomenclature. These names are
   **hard** requirements posed by the measurement script.
   * `project__lenX`: `project` is the name of the project, `X` is the
     `MinimumLength` value
   * `project__lenX-cvr`: if `CVRMixPossible` was set to true (`1`)
   * `project__lenX-imp`: if `ImplicitConversion` was set to true (`1`)
   * `project__lenX-cvr-imp`: if both `CVRMixPossible` and `ImplicitConversion`
     were set to true (`1`)

> ***TODO:*** Get `run-aacheck.sh` into this repo!

To run the measurement scripts:

 * CodeChecker as discussed above
 * `pip install percol`
