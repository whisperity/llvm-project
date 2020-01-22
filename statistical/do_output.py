import json
import sys
import numpy
from scipy import stats
from tabulate import tabulate

from codechecker import cmdline_client
from .bugreport import ChainBugReport, RedundantPtrBugReport


def _handle_redundantptr(cch_results):
    print("`readability-redundant-pointer-in-local-scope`")
    print("----------------------------------------------")
    print('\n')

    reports = [RedundantPtrBugReport(report) for report in cch_results]
    print("**Total number of findings: `%d`**\n" % len(reports))

    print("\n * True pointers: %d" % len([1 for R in reports if R.is_ptr]))
    print(" * Other dereferenceable: %d" %
          len([1 for R in reports if R.is_dereferencable]))
    print(" * Guarded: %d" %
          len([1 for R in reports if R.guarded]))


    print(" * Number of parameter passing usages: %d" %
          len([1 for R in reports if R.is_param_passing]))
    print("    * From which guarded: %d" %
          len([1 for R in reports if R.is_param_passing and R.guarded]))
    print(" * Number of dereference usages: %d" %
          len([1 for R in reports if R.is_dereference]))
    print("    * From which guarded: %d" %
          len([1 for R in reports if R.is_dereference and R.guarded]))
    print(" * Number of variable initialising dereference usages: %d" %
          len([1 for R in reports if R.is_varinit]))
    print("    * From which guarded: %d" %
          len([1 for R in reports if R.is_varinit and R.guarded]))


def _handle_chains(cch_results):
    print("`readability-redundant-pointer-dereference-chain`")
    print("-------------------------------------------------")
    print('\n')

    reports = [ChainBugReport(report) for report in cch_results]
    print("**Total number of findings: `%d`**\n" % len(reports))

    lengths = [R.length for R in reports]
    lengths_count = {x: lengths.count(x) for x in lengths}
    print("Length distribution for the project:\n")
    print("~~~~\n%s\n~~~~"
          % json.dumps(lengths_count, sort_keys=True, indent=1))

    guards = [R.guard_count for R in reports]
    guards_count = {x: guards.count(x) for x in guards}
    print("No. of guarded pointers' distribution for the project:\n")
    print("~~~~\n%s\n~~~~"
          % json.dumps(guards_count, sort_keys=True, indent=1))

    print("\n * Number of special cases (1st element not elidible): %d" %
          len([1 for R in reports if R.first_element_special]))


def handle(project):
    head = "Project: `%s`" % project
    print("\n\n%s" % head)
    print("=" * len(head))

    try:
        results_redundant = cmdline_client.get_results(
            project, cmdline_client.REDUNDANT_PTR)
        results_chains = cmdline_client.get_results(
            project, cmdline_client.DEREFERENCE_CHAIN)
    except cmdline_client.NoRunError as nre:
        print("> **[ERROR]** This measurement was not (properly) stored to "
              "the server!")
        print(nre, file=sys.stderr)
        return 0

    print('\n')
    _handle_redundantptr(results_redundant)
    print('\n')
    _handle_chains(results_chains)
