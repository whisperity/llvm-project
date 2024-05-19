from collections import Counter
import json
import sys
import numpy
from scipy import stats
from tabulate import tabulate

from codechecker import cmdline_client
from .bugreport import BugReport, canonicalise_type
from .function_match import FunctionMatch


def handle_configuration(project, min_length, cvr=False, implicit=False,
                         relatedness=False, filtering=False):
    try:
        results = cmdline_client.get_results(project, min_length,
                                             cvr, implicit, relatedness,
                                             filtering)
    except cmdline_client.NoRunError:
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
        print("\n * Number of **unique** functions reported upon: %d"
              % len({R.function_name for R in reps}))
        print("\n * Number of trivials (adjacent arguments with "
              "*exact same* type): %d" % sum([1 for R in reps if R.is_exact]))
        print(" * Number of non-trivials: "
              "%d" % sum([1 for R in reps if not R.is_exact]))
        print("    * Number of reports involving a `typedef`: "
              "%d" % sum([1 for R in reps if R.has_typedef]))
        len_bind = sum([1 for R in reps if R.has_bindpower])
        len_ref_bind = sum([1 for R in reps if R.has_ref_bind])
        print("    * Number of reports involving a bind power (CVR or `&`): "
              "%d" % len_bind)
        print("        * Number of reports involving a reference (`&`) bind: "
              "%d" % len_ref_bind)
        print("    * Number of reports involving implicit conversions: "
              "%d" % sum([1 for R in reps if R.is_implicit]))
        print("        * Number of reports with any **bidirectional** "
              "implicity: %d"
              % sum([1 for R in reps if R.has_implicit_bidir]))

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
            print("\n\n**[WARNING]** The following **types** from the reports "
                  "were not categorised:\n")
            [print(' * `%s`' % t)
             for t in sorted(list(set(uncategorisable_types)))]

        print("\nNumber of times a **concrete** type was involved in a "
              "diagnostic:\n")
        cnt = Counter()
        for R in reports:
            for T in R.raw_involved_types:
                cnt.update({T: 1})
        [print(' * `%s`: %d' % (T, n)) for T, n in cnt.most_common()]

        print("\nNumber of times a **type construction blueprint** was "
              "involved in a diagnostic:\n")
        cnt = Counter()
        for R in reports:
            for T in R.canonical_involved_types:
                cnt.update({T: 1})
        [print(' * `%s`: %d' % (T, n)) for T, n in cnt.most_common()]

    print("#### Entire project")
    _finding_breakdown(None)
    _type_breakdown()

    for length in range(min_length, max(lengths) + 1):
        print("\n#### For reports of length `%d`" % length)
        if length not in lengths_count.keys():
            print("No findings of this length.")
        else:
            _finding_breakdown(length)

    return reports


def __try_configuration(prompt, project, min_length, cvr=False, implicit=False,
                        relatedness=False, filtering=False):
    head = "Configuration: %s" % prompt
    print("\n%s" % head)
    print("-" * len(head) + '\n')
    try:
        reports = handle_configuration(project, min_length,
                                       cvr, implicit, relatedness, filtering)
        return len(reports)
    except cmdline_client.NoRunError as nre:
        print("> **[ERROR]** This measurement was not (properly) stored to "
              "the server!")
        print(nre, file=sys.stderr)
        return 0


def handle_functions(project, min_length, cvr=False, implicit=False,
                     relatedness=False, filtering=False):
    try:
        results = cmdline_client.get_functions(project, min_length,
                                               cvr, implicit, relatedness,
                                               filtering)
    except cmdline_client.NoRunError:
        raise

    reports = [FunctionMatch(report) for report in results]
    num_analysed = sum([1 for R in reports if R.is_analysed_function])
    num_matched = sum([1 for R in reports if R.is_matched_function])

    print("\n")
    print(" * Total number of **functions analysed: `%d`**" % num_analysed)
    print("    * from this, **functions matched: `%d`**" % num_matched)

    print("\n")
    return num_matched


def __try_functions(project, min_length, cvr=False, implicit=False,
                    relatedness=False, filtering=False):
    print("\n\n### Matched functions")
    try:
        count = handle_functions(project, min_length, cvr, implicit,
                                 relatedness, filtering)
        return count
    except cmdline_client.NoRunError as nre:
        print("> **[ERROR]** This measurement for functions was not "
              "(properly) stored to the server!")
        print(nre, file=sys.stderr)
        return 0


CONF_NOR = (False, False, False, False)
CONF_CVR = (True, False, False, False)
CONF_IMP = (False, True, False, False)
CONF_REL = (False, False, True, False)
CONF_FIL = (False, False, False, True)


def conf_or(*confs):
    x = [False, False, False, False]
    for conf in confs:
        for idx, val in enumerate(x):
            x[idx] = val or conf[idx]
    return tuple(x)


def handle(project):
    head = "Project: `%s`" % project
    print("\n\n%s" % head)
    print("=" * len(head))

    min_arg_length = cmdline_client.minimum_length_for_project(project)
    if min_arg_length > 2:
        print("> **[WARNING]** Minimum match length for project is %d. "
              "Potential shorter results were ignored during analysis and "
              "cannot be retrieved!" % min_arg_length)

    # [("Title", (CVR, Imp, Rel, Fil))]
    # [("Title", (CVR1, Imp1, Rel1, Fil1), (CVR2, Imp2, Rel2, Fil2))]
    configurations = {
        "Normal": CONF_NOR,

        "CVR": CONF_CVR,
        "CVR (v. Normal)": (CONF_NOR, CONF_CVR),

        "Imp": CONF_IMP,
        "Imp (v. Normal)": (CONF_NOR, CONF_IMP),

        "CVR + Imp": conf_or(CONF_CVR, CONF_IMP),
        "CVR + Imp (v. CVR)": (CONF_CVR, conf_or(CONF_CVR, CONF_IMP)),
        "CVR + Imp (v. Imp)": (CONF_IMP, conf_or(CONF_CVR, CONF_IMP)),

        "Rel (v. Normal)": (CONF_NOR, CONF_REL),
        "Fil (v. Normal)": (CONF_NOR, CONF_FIL),
        "Rel + Fil (v. Normal)": (CONF_NOR, conf_or(CONF_REL, CONF_FIL)),

        "Rel (v. CVR + Imp)": (conf_or(CONF_CVR, CONF_IMP),
                               conf_or(CONF_CVR, CONF_IMP, CONF_REL)),
        "Fil (v. CVR + Imp)": (conf_or(CONF_CVR, CONF_IMP),
                               conf_or(CONF_CVR, CONF_IMP, CONF_FIL)),
        "Rel + Fil (v. CVR + Imp)": (conf_or(CONF_CVR, CONF_IMP),
                                     conf_or(CONF_CVR, CONF_IMP,
                                             CONF_REL, CONF_FIL)),

        "Normal - (Rel + Fil)": conf_or(CONF_REL, CONF_FIL),
        "(CVR + Imp) - (Rel + Fil)": conf_or(CONF_CVR, CONF_IMP, CONF_REL, CONF_FIL)
    }

    result_count = dict()
    for key, value in configurations.items():
        if len(value) == 2 and isinstance(value[0], tuple) and \
                isinstance(value[1], tuple):
            continue

        result_count[key] = dict()
        result_count[key]['results'] = \
            __try_configuration(key, project, min_arg_length, *value)
        result_count[key]['functions'] = \
            __try_functions(project, min_arg_length, *value)

    print("\nResult count and differences between modes")
    print("------------------------------------------\n")

    headers = ["Configuration", "Result count (difference, where applicable)"]
    rows = []
    for key, value in configurations.items():
        row = [key]
        conf = value

        if isinstance(conf, tuple) and len(conf) == 4:
            # Single configuration shows the count only.
            row.append(result_count[key]['results'])
        elif len(value) == 2 and isinstance(value[0], tuple) and \
                isinstance(value[1], tuple):
            # Diff configuration.
            conf = value[0]
            conf2 = value[1]

            # Calculate the report counts of the diff between the
            # configurations.
            def _diff(direction):
                return cmdline_client.get_difference(
                    project,
                    min_length_1=min_arg_length,
                    min_length_2=min_arg_length,
                    cvr_1=conf[0], cvr_2=conf2[0],
                    implicit_1=conf[1], implicit_2=conf2[1],
                    relatedness_1=conf[2], relatedness_2=conf2[2],
                    filtering_1=conf[3], filtering_2=conf2[3],
                    direction=direction)

            same, new, gone = None, None, None
            try:
                any_error = False
                try:
                    same = _diff(cmdline_client.FINDINGS_IN_BOTH)
                except cmdline_client.NoRunError:
                    same = '?'
                    any_error = True
                try:
                    new = _diff(cmdline_client.NEW_FINDINGS)
                except cmdline_client.NoRunError:
                    new = '?'
                    any_error = True
                try:
                    gone = _diff(cmdline_client.DISAPPEARED_FINDINGS)
                except cmdline_client.NoRunError:
                    gone = '?'
                    any_error = True

                if any_error:
                    raise cmdline_client.NoRunError('')
            except cmdline_client.NoRunError:
                print("> **[ERROR]** The measurement for (%s, %d, %s, %s) was "
                      "not (properly) stored to the server!"
                      % (project, min_arg_length, conf[0], conf2[0]))

            cell = list()
            if same:
                cell.append("**=** %d" % len(same))
            if new:
                cell.append("**+** %d" % len(new))
            if gone:
                cell.append("**-** %d" % len(gone))

            row.append(', '.join(cell))
        rows.append(row)

    print(tabulate(rows, headers, tablefmt='github'))

    print("\nMatched function count and differences between modes")
    print("----------------------------------------------------\n")

    print("Total number of functions **analysed**: `%d`\n"
          % sum([1 for parsed in
                 [FunctionMatch(fun_result) for fun_result in
                  cmdline_client.get_functions(project, min_arg_length,
                                               False, False, False, False)]
                 if parsed.is_analysed_function]))

    rows = []
    for key, value in configurations.items():
        row = [key]
        conf = value

        if isinstance(conf, tuple) and len(conf) == 4:
            # Single configuration shows the count only.
            row.append(result_count[key]['functions'])
        elif len(value) == 2 and isinstance(value[0], tuple) and \
                isinstance(value[1], tuple):
            # Diff configuration.
            conf = value[0]
            conf2 = value[1]

            # Calculate the report counts of the diff between the
            # configurations.
            def _diff(direction):
                diff_results = cmdline_client.get_difference_functions(
                    project,
                    min_length_1=min_arg_length,
                    min_length_2=min_arg_length,
                    cvr_1=conf[0], cvr_2=conf2[0],
                    implicit_1=conf[1], implicit_2=conf2[1],
                    relatedness_1=conf[2], relatedness_2=conf2[2],
                    filtering_1=conf[3], filtering_2=conf2[3],
                    direction=direction)
                diff_parsed = [FunctionMatch(result)
                               for result in diff_results]
                return [R for R in diff_parsed if R.is_matched_function]

            same, new, gone = None, None, None
            try:
                any_error = False
                try:
                    same = _diff(cmdline_client.FINDINGS_IN_BOTH)
                except cmdline_client.NoRunError:
                    same = '?'
                    any_error = True
                try:
                    new = _diff(cmdline_client.NEW_FINDINGS)
                except cmdline_client.NoRunError:
                    new = '?'
                    any_error = True
                try:
                    gone = _diff(cmdline_client.DISAPPEARED_FINDINGS)
                except cmdline_client.NoRunError:
                    gone = '?'
                    any_error = True

                if any_error:
                    raise cmdline_client.NoRunError('')
            except cmdline_client.NoRunError:
                print("> **[ERROR]** The measurement for (%s, %d, %s, %s) was "
                      "not (properly) stored to the server!"
                      % (project, min_arg_length, conf[0], conf2[0]))

            cell = list()
            if same:
                cell.append("**=** %d" % len(same))
            if new:
                cell.append("**+** %d" % len(new))
            if gone:
                cell.append("**-** %d" % len(gone))

            row.append(', '.join(cell))
        rows.append(row)

    print(tabulate(rows, headers, tablefmt='github'))
