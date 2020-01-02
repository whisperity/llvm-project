import json
import sys
import numpy
from scipy import stats

from codechecker import cmdline_client
from .bugreport import BugReport


def handle_configuration(project, min_length, cvr=False, implicit=False):
    try:
        results = cmdline_client.get_results(project, min_length,
                                             cvr, implicit)
    except cmdline_client.NoRunException:
        raise

    reports = [BugReport(report) for report in results]

    print("**Total number of findings: `%d`**\n" % len(reports))

    lengths = [R.length for R in reports]
    lengths_count = {x: lengths.count(x) for x in lengths}
    print("Length distribution for the project:\n")
    print("~~~~\n%s\n~~~~"
          % json.dumps(lengths_count, sort_keys=True, indent=1))

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

    print("\n### Breakdown of findings")

    def _finding_breakdown(range_length=None):
        reps = [R for R in reports if R.length == range_length] \
            if range_length else reports
        print("\n * Number of trivials (adjacent arguments with "
              "*exact same* type): %d" % len([1 for R in reps if R.is_exact]))
        print(" * Number of non-trivials: "
              "%d" % len([1 for R in reps if not R.is_exact]))
        print("    * Number of reports involving a `typedef`: "
              "%d" % len([1 for R in reps if R.has_typedef]))
        len_bind = len([1 for R in reps if R.has_bindpower])
        len_ref_bind = len([1 for R in reps if R.has_ref_bind])
        print("    * Number of reports involving a bind power (CVR or `&`): "
              "%d" % len_bind)
        print("        * Number of reports involving a reference (`&`) bind: "
              "%d" % len_ref_bind)
        print("    * Number of reports involving implicit conversions: "
              "%d" % len([1 for R in reps if R.is_implicit]))
        print("        * Number of reports with any **bidirectional** "
              "implicity: %d"
              % len([1 for R in reps if R.has_implicit_bidir]))
        print("        * Number of reports with any **unidirectional** "
              "implicity: %d"
              % len([1 for R in reps if R.has_implicit_unidir]))

    def _type_breakdown():
        print()
        involved_type_categs, uncategorisable_types = list(), list()
        for R in reports:
            categories, uncategorised = R.get_involved_types_categories()
            involved_type_categs += categories
            uncategorisable_types += uncategorised

        type_categories_count = {c: involved_type_categs.count(c)
                                 for c in involved_type_categs}
        type_categories_count = sorted(type_categories_count.items(),
                                       key=lambda item: item[1],
                                       reverse=True)
        print("Distribution of types involved in mixup:\n")
        [print(' * %s: *%d*' % (categ, cnt))
         for (categ, cnt) in type_categories_count]

        if uncategorisable_types:
            print("\n\n**[WARNING]** The following *types* from the reports "
                  "were not categorised:\n")
            [print(' * `%s`' % t)
             for t in sorted(list(set(uncategorisable_types)))]

    print("#### Entire project")
    _finding_breakdown(None)
    _type_breakdown()

    for length in range(min_length, max(lengths) + 1):
        print("\n#### For reports of length `%d`" % length)
        if length not in lengths_count.keys():
            print("No findings of this length.")
        else:
            _finding_breakdown(length)


def __try_configuration(prompt, project, min_length,
                        cvr=False, implicit=False):
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
