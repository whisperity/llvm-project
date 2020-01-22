import json
import re
import sys


def match_all_to_list(pattern, string):
    out = list()
    for res in pattern.findall(string):
        if isinstance(res, tuple):
            out += [x for x in res]
        else:
            out.append(res)
    return out


class RedundantPtrBugReport:
    def __init__(self, report):
        """
        Parses the given CodeChecker report.
        """
        def _dump():
            print(json.dumps(report, sort_keys=True, indent=2))

        # These values are elaborated later.


        # Output format for
        # 'readability-redundant-pointer-in-local-scope'
        # as of commit e6fdca25f5254c112f3cc4eccafa4a7ec77cb12a.
        print(report['checkerMsg'])
        #self.length = int(report['checkerMsg'].split(' ')[0])
        self.length = 0

        steps = [e['msg'] for e in report['details']['pathEvents']]

        for step in steps:
            print("STEP:", step)


class ChainBugReport:
    def __init__(self, report):
        """
        Parses the given CodeChecker report.
        """
        def _dump():
            print(json.dumps(report, sort_keys=True, indent=2))

        # These values are elaborated later.


        # Output format for
        # 'readability-redundant-pointer-dereference-chain'
        # as of commit e6fdca25f5254c112f3cc4eccafa4a7ec77cb12a.
        print(report['checkerMsg'])
        #self.length = int(report['checkerMsg'].split(' ')[0])
        self.length = 0

        steps = [e['msg'] for e in report['details']['pathEvents']]

        for step in steps:
            print("STEP:", step)
