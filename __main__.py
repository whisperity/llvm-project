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
        proc = subprocess.run(["percol", '--auto-match', '--match-method=regex'] +
                              (['--prompt=' + prompt + '> '] if prompt else []),
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


def __main(product_url=None, project=None, all_runs=False):
    cmdline_client.set_product(product_url)

    vcheck, ver = check_version.check_version()
    if not vcheck:
        print("Error: Version %s CodeChecker required, but got %s"
              % (check_version.REQUIRED_VERSION, ver),
              file=sys.stderr)
        sys.exit(2)

    projects = cmdline_client.get_projects()
    if not all_runs and not project:
        projects = [pick(projects, "Project")]
    elif project:
        if project in projects:
            projects = [project]
        else:
            print("[ERROR] No such project: %s!" % project, file=sys.stderr)
            sys.exit(2)

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
                           "will use the default provided by CodeChecker.")

    runs = ARGS.add_mutually_exclusive_group(required=False)
    runs.add_argument('-a', '--all',
                      dest='all_runs',
                      action='store_true',
                      default=False,
                      required=False,
                      help="Do measurement for all stored projects. If not "
                           "specified, the user is interactively prompted to "
                           "select.")
    runs.add_argument('-n', '--name', '--project', '--project-name',
                      dest='project',
                      type=str,
                      required=False,
                      help="The project to run the tool for. If not specified, "
                           "and '--all' isn't specified either, the user is "
                           "interactively prompted.")


if __name__ == '__main__':
    __register_args()
    args = ARGS.parse_args()
    __main(args.product_url, args.project, args.all_runs)
