#!/usr/bin/env python3
import argparse
import subprocess
import sys

from codechecker import check_version, cmdline_client
from statistical.do_output import handle


ARGS = argparse.ArgumentParser(
    description="Emit numeric information about the analysis reports for the "
                "'cppcoreguidelines-avoid-adjacent-arguments-of-same-type' "
                "checker. The output is a Markdown formatted document.")


def pick(options, prompt=None):
    try:
        proc = subprocess.run(["percol",
                               '--auto-match', '--match-method=regex'] +
                              (['--prompt=' + prompt + '> %q']
                               if prompt else []),
                              encoding='utf8',
                              input='\n'.join(options),
                              stdout=subprocess.PIPE)
        return proc.stdout.rstrip()
    except FileNotFoundError:
        print("[ERROR] Please 'pip install percol' for interactive prompt, "
              "specify '--all' runs option, or  '--project-name'!",
              file=sys.stderr)
        print("Available projects are:\n%s" % '\n'.join(options))
        sys.exit(1)


def _obtain_project_list():
    projects = cmdline_client.get_projects()
    if not projects:
        print("[ERROR] No runs with the proper naming scheme were found on "
              "the server, or the connection failed.", file=sys.stderr)
        sys.exit(2)
    return projects


def __main(projects=None):
    for project in projects:
        handle(project)


def __register_args():
    ARGS.add_argument('--url',
                      dest='product_url',
                      type=str,
                      required=False,
                      default=None,
                      help="Product URL that specifies which running "
                           "CodeChecker server to connect to. If empty, "
                           "will use the defaults provided by the CodeChecker "
                           "install found in PATH.")

    ARGS.add_argument('--functions-url',
                      dest='functions_url',
                      type=str,
                      required=False,
                      default=None,
                      help="Product URL that specifies which running "
                           "CodeChecker server to connect to to fetch "
                           "function count runs. If empty, function counts "
                           "will not be tallied.")

    proj = ARGS.add_mutually_exclusive_group(required=False)
    proj.add_argument('-a', '--all',
                      dest='all_projects',
                      action='store_true',
                      default=False,
                      required=False,
                      help="Do measurement for all stored projects. If not "
                           "specified, the user is interactively prompted to "
                           "select.")
    proj.add_argument('-n', '--name', '--project', '--project-name',
                      dest='project',
                      type=str,
                      required=False,
                      help="The project to run the tool for. If not "
                           "specified, and '--all' isn't specified either, "
                           "the user is interactively prompted.")
    proj.add_argument('-l', '--list',
                      dest='just_list',
                      action='store_true',
                      default=False,
                      required=False,
                      help="List the available projects on the server and "
                           " exit, without doing measurements.")


if __name__ == '__main__':
    vcheck, ver = check_version.check_version()
    if not vcheck:
        print("Error: Version %s CodeChecker required, but got %s"
              % (check_version.REQUIRED_VERSION, ver),
              file=sys.stderr)
        sys.exit(2)

    __register_args()
    args = ARGS.parse_args()
    cmdline_client.set_product(args.product_url)
    cmdline_client.set_product_for_function_match(args.functions_url)

    all_projects = _obtain_project_list()
    if args.just_list:
        print("Projects available on the server specified:", file=sys.stderr)
        print("-------------------------------------------", file=sys.stderr)
        [print('%s' % proj) for proj in sorted(all_projects)]
        sys.exit(0)

    projects = list()
    if not args.all_projects and not args.project:
        projects = [pick(all_projects, "Project")]
    elif args.all_projects:
        projects = all_projects
    elif args.project:
        if args.project in all_projects:
            projects = [args.project]
        else:
            print("[ERROR] No such project: %s!" % args.project,
                  file=sys.stderr)
            sys.exit(2)
    else:
        print("[[Internal error.]]")
        sys.exit(-4)

    __main(projects)
