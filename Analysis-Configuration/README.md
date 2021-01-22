Analysis executor
=================

This directory contains configuration files and an automatic executor script
which drives the analysis of projects configured in `../TestProjects`.

Each text file is a different configuration of our analysis, as detailed in the
paper.
In total, there are 4 different configuration toggles:

 - CV-mode
 - Implicit mode
 - Relatedness filtering
 - Name-based affix filtering

giving a total of 11 main configurations, shown with the tags `cvr`, `imp`,
`rel`, and `fil` respectively, in addition to the "ignore `bool`s" config.

These files should not be executed manually, see the scripts in the `..`
directory to drive the analysis with proper modes.
