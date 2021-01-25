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
   * Preferably use the `_function-count` branch that has every feature merged
     in, and has the "matched function count" analysis bolted on top.
     The data measurement scripts understand the results of such an analysis.

To run the analysis easily:

 * **Release version `v6.15`** of
   [CodeChecker](http://github.com/Ericsson/CodeChecker) downloaded, installed
   and in `PATH`.

To run the measurement scripts:

 * `CodeChecker` as discussed above
 * Python **3.6.8** or newer and the packages in [`requirements.txt`](/requirements.txt).

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
     `CheckRelatedness`, `ImplicitConversion`, `IgnoredNames`, `IgnoredTypes`,
     and `NameBasedAffixFilterThreshold` by specifying a `--tidy-config` file.
 * Store analysis results with the following nomenclature. These names are
   **hard** requirements posed by the measurement script.
   * `project__lenX`: `project` is the name of the project, `X` is the
     `MinimumLength` value
   * `project__lenX-cvr`: if `CVRMixPossible` was set to true (`1`)
   * `project__lenX-imp`: if `ImplicitConversion` was set to true (`1`)
   * `project__lenX-rel`: if `CheckRelatedness` was set to true (`1`)
   * `project__lenX-fil`: if `NameBasedAffixFilterThreshold` was set to a
     non-zero value, enabling the feature
   * `project__lenX-cvr-imp`, `project__lenX-cvr-rel-fil`, etc.: the
      combination of the above options.

### Analysis (automatically)

 * Create a directory `TestProjects`.
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
 * Execute `Analysis-Configuration/run-aa.sh`, which will run the analysis for
   each projects for each configuration, and store the results to the server.

Most projects rely on build artifacts generated *during the build* to build
properly, so even if you used CMake to obtain the build database, it is
suggested to run the build to completion.

The results can be viewed in the [web browser](http://localhost:8001/Default).

### Measurement

Keep a `CodeChecker server` that contains the analysis results uploaded running
in the background.

Start `ReportGen/__main__.py`.

> See the `--help` option on extra arguments, such as overriding *product-URL*
> if needed)

Results are emitted to the *standard output* in *Markdown* format.
You can save the results by piping the output to a file:

~~~~{.sh}
python3 ReportGen/__main__.py > output.md
~~~~

To format results into HTML reports, one per *project* on the server, execute
`ReportGen/html-report.sh`.

### Helper scripts

A few automation scripts reside in the repository, which helps to start the
CodeChecker server, auto-execute the analysis, create the results, offer an
HTTP server to view the results, etc.
