import json
import sys
import numpy
from scipy import stats

from codechecker import cmdline_client
from .bugreport import BugReport


def handle_configuration(project, min_length, cvr=False, implicit=False):
    try:
        results = cmdline_client.get_results(project, min_length, cvr, implicit)
    except cmdline_client.NoRunException as nre:
        raise

    reports = [BugReport(report) for report in results]

    print("**Total number of findings: `%d`**\n" % len(reports))

    lengths = [R.length for R in reports]
    lengths_count = {x:lengths.count(x) for x in lengths}
    print("Length distribution for the project:\n")
    print("~~~~\n%s\n~~~~" % json.dumps(lengths_count, sort_keys=True, indent=1))

    print("\nLength box-plot values:\n")
    q1, med, q3 = numpy.percentile(lengths, [25, 50, 75])
    iqr = stats.iqr(lengths)
    bpmin, bpmax = q1 - 1.5*iqr, q3 + 1.5*iqr
    outliers_min = sorted([l for l in lengths if l < bpmin])
    outliers_max = sorted([l for l in lengths if l > bpmax])
    print("~~~~\n%s\n"
          "bp-minimum: %f\nq1: %f\nmedian: %f\nq3: %f\nbp-maximum: %f"
          "\n%s\n~~~~"
          % ("Outliers below: %s" % outliers_min if outliers_min else '',
             bpmin, q1, med, q3, bpmax,
             "Outliers above: %s" % outliers_max if outliers_max else ''))

    print("\n * Number of trivials (adjacent arguments with exact same type): "
          "%d" % len([1 for R in reports if R.is_exact]))
    print(" * Number of non-trivials: "
          "%d" % len([1 for R in reports if not R.is_exact]))
    print("    * Number of reports involving a `typedef`: "
          "%d" % len([1 for R in reports if R.has_typedef]))
    len_bind = len([1 for R in reports if R.has_bindpower])
    len_ref_bind = len([1 for R in reports if R.has_ref_bind])
    print("    * Number of reports involving a bind power (CVR or `&`): "
          "%d" % len_bind)
    print("        * Number of reports involving a reference (`&`) bind: "
          "%d" % len_ref_bind)
    print("    * Number of reports involving implicit conversions: "
          "%d" % len([1 for R in reports if R.is_implicit]))


def __try_configuration(prompt, project, min_length, cvr=False, implicit=False):
    head = "Configuration: %s" % prompt
    print("\n%s" % head)
    print("-" * len(head) + '\n')
    try:
        handle_configuration(project, min_length, cvr, implicit)
    except cmdline_client.NoRunException as nre:
        print("> **[ERROR]** This measurement was not (properly) stored to "
              "the server!")
        print(nre, file=sys.stderr)


def handle(project):
    head = "Project: `%s`" % project
    print("\n\n%s" % head)
    print("=" * len(head))

    min_arg_length = cmdline_client.minimum_length_for_project(project)
    if min_arg_length > 2:
        print("> **[WARNING]** Minimum match length for project is %d. "
              "Potential shorter results were ignored during analysis and "
              "cannot be retrieved!" % min_arg_length)

    __try_configuration("Normal analysis", project, min_arg_length)
    __try_configuration("Generous `const`/`volatile`/`restrict` mixing",
                        project, min_arg_length, cvr=True)
    __try_configuration("Implicit conversions",
                        project, min_arg_length, implicit=True)
    __try_configuration("CVR mix *and* Implicit conversions",
                        project, min_arg_length, cvr=True, implicit=True)
