import json
import sys
import numpy
from scipy import stats
from tabulate import tabulate

from codechecker import cmdline_client
from .bugreport import ChainBugReport, RedundantPtrBugReport


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
    print("`redundant-ptr-in-local-scope`")
    print("------------------------------")
    print('\n')

    reports = [RedundantPtrBugReport(report) for report in results_redundant]
    print("**Total number of findings: `%d`**\n" % len(reports))

    lengths = [R.length for R in reports]
    lengths_count = {x: lengths.count(x) for x in lengths}
    print("Length distribution for the project:\n")
    print("~~~~\n%s\n~~~~"
          % json.dumps(lengths_count, sort_keys=True, indent=1))
