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
    proc = subprocess.run(["percol", '--auto-match', '--match-method=regex'] +
                          (['--prompt=' + prompt + '> '] if prompt else []),
                          encoding='utf8',
                          input='\n'.join(options),
                          stdout=subprocess.PIPE)
    return proc.stdout.rstrip()


def __main(product_url=None, all_runs=False):
    cmdline_client.set_product(product_url)

    vCheck, V = check_version.check_version()
    if not vCheck:
        print("Error: Version %s CodeChecker required, but got %s"
              % (check_version.REQUIRED_VERSION, V),
              file=sys.stderr)
        sys.exit(2)

    projects = cmdline_client.get_projects()[:1]
    if not all_runs:
        projects = [pick(projects, "Project")]
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
    ARGS.add_argument('-a',
                      dest='all_runs',
                      action='store_true',
                      default=False,
                      required=False,
                      help="Do measurement for all stored projects. If not "
                           "specified, the user is interactively prompted to "
                           "select.")


if __name__ == '__main__':
    __register_args()
    args = ARGS.parse_args()
    __main(args.product_url, args.all_runs)
