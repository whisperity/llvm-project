Test projects directory
=======================

This directory contains all the projects the analysis is supposed to run on.

Please structure the directory as follows:
  - Each project should have a uniquely named directory.
    The project's source code shall be checked out into that directory.

JSON Compilation Database
-------------------------

The projects must be prepared with a JSON Compilation Database before analysis
could be run.
The compilation databases are needed so that _Clang-Tidy_ knows with what
compiler configuration can the files be properly analysed.

The easiest way to obtain a compilation database is to execute the build of
the project, and log the executing compilers.
CodeChecker (in `~/CodeChecker`) offers such a functionality.

> **NOTE:** Executing the build for a project might take a considerable amount
> of time depending on the project's size!

Once the build is "configured" (refer to the projects' instructions at hand),
when you would call the build system (most commonly `make` for C/C++
projects), instead do the following:

    CodeChecker log -b "make" -o "compile_commands.json"

This will execute the build normally to completion, but also log the executed
compile commands to the given file.

Put the `compile_commands.json` to the project's directory.
I.e. if you are setting up project `foo`, the compilation database **MUST** be
located at `foo/compile_commands.json`.

Projects we analysed
--------------------

The project we run our analysis on as detailed in the paper are not checked
into the image to reduce size.
The scripts with the projects' names should automatically download, configure,
build and log the projects, so the `TestProjects` directory is set up as
needed by the analysis scripts.