import json
import sys

from codechecker import cmdline_client


def handle_configuration(project, min_length, cvr=False, implicit=False):
    try:
        results = cmdline_client.get_results(project, min_length, cvr, implicit)
    except KeyError:
        raise

    print(json.dumps(results, sort_keys=True, indent=2))


def __try_configuration(prompt, project, min_length, cvr=False, implicit=False):
    head = "Configuration: %s" % prompt
    print("\n%s" % head)
    print("-" * len(head) + '\n')
    try:
        handle_configuration(project, min_length, cvr, implicit)
    except KeyError as ke:
        print("> **[ERROR]** This measurement was not (properly) stored to "
              "the server!")
        print(ke, file=sys.stderr)


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
    # __try_configuration("Generous `const`/`volatile`/`restrict` mixing",
    #                     project, min_arg_length, cvr=True)
    # __try_configuration("Implicit conversions",
    #                     project, min_arg_length, implicit=True)
    # __try_configuration("CVR mix *and* Implicit conversions",
    #                     project, min_arg_length, cvr=True, implicit=True)
