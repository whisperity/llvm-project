`experimental-cppcoreguidelines-avoid-adjacent-parameters-of-the-same-type` measurement scripts
===============================================================================================


Prerequisites
-------------

To be able to emit warnings for error-prone constructs:

 * `experimental-cppcoreguidelines-avoid-adjacent-parameters-of-the-same-type`
   checker patched to *Clang-Tidy* and built according to LLVM Documentation.
   * See other branches named
     `clang-tidy/cppcoreguidelines-avoid-adjacent-arguments-of-same-type` in
     this project for the code.
   * Preferably use the `_merged` branch that models both *implicit conversions*
     and *relatedness*.

To run the analysis easily:

 * **Release version `v6.11`** of
   [CodeChecker](http://github.com/Ericsson/CodeChecker) downloaded, installed
   and in `PATH`.

To run the measurement scripts:

 * `CodeChecker` as discussed above
 * `pip install percol scipy tabulate`

To format the output to HTML:

 * The measurement script's requirements discussed above
 * `sudo apt install pandoc`


Execute action
--------------

### Analysis (manually)

 * Refer to
   [CodeChecker guidelines](http://codechecker.readthedocs.io/en/latest/analyzer/user_guide/)
   for details on how to run analysis on a particular project.
   * To run only the checker, specify the following checkers to
   `CodeChecker analyze`:
   ~~~~
   --analyzers clang-tidy
   --disable Weverything
   --enable experimental-cppcoreguidelines-avoid-adjacent-parameters-of-the-same-type
   ~~~~
   * Tweak the checker options `MinimumLength`, `CVRMixPossible`,
     `CheckRelatedness` and `ImplicitConversion` by specifying a `--tidyargs`
     file.
 * Store analysis results with the following nomenclature. These names are
   **hard** requirements posed by the measurement script.
   * `project__lenX`: `project` is the name of the project, `X` is the
     `MinimumLength` value
   * `project__lenX-cvr`: if `CVRMixPossible` was set to true (`1`)
   * `project__lenX-imp`: if `ImplicitConversion` was set to true (`1`)
   * `project__lenX-cvr-imp`: if both `CVRMixPossible` and `ImplicitConversion`
     were set to true (`1`)
   * `project__lenX-rel`: Same as above, but *relatedness* check is enabled.
   * `project__lenX-cvr-rel`: Same as above, but *relatedness* check is enabled.
   * `project__lenX-imp-rel`: Same as above, but *relatedness* check is enabled.
   * `project__lenX-cvr-imp-rel`: Same as above, but *relatedness* check is
     enabled.

### Analysis (automatically)

 * Create a directory `TestProjects` under `execute-analysis`.
 * For each project you want to analyse, check them out under this
   `TestProjects` directory - e.g. `TestProjects/linux`.
   * Obtain a `compile_commands.json` some way for the project's build, and
     put this `compile_commands.json` **to the main directory** of the project,
     e.g. `TestProjects/linux/compile_commands.json`.
     * CMake can generate compilation databases automatically.
     * Autotools/Makefile projects can use
       [`CodeChecker log`](http://codechecker.readthedocs.io/en/latest/analyzer/user_guide/#log)
       to register build commands.
 * Start a CodeChecker server on the machine and leave it running during the
   process.
 * Execute `run-aa.sh`, which will run the analysis for each projects for each
   configuration, and store the results to the server.

Most projects rely on build artifacts generated *during the build* to build
properly, so even if you used CMake to obtain the build database, it is
suggested to run the build to completion.

The results can be viewed in the [web browser](http://localhost:8001/Default).

### Measurement

Keep a `CodeChecker server` that contains the analysis results uploaded running
in the background.

Start `__main__.py`.

> See the `--help` option on extra arguments, such as overriding *product-URL*
> if needed)

Results are emitted to the *standard output* in *Markdown* format.
You can save the results by piping the output to a file:

~~~~{.sh}
python3 __main__.py > output.md
~~~~

To format results into HTML reports, one per *project* on the server, execute
`html-report.sh`.
