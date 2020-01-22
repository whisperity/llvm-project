import json
import re

PATTERN_CHAIN_LENGTH = re.compile(
    r"initialised from dereference chain of (\d+) variables")


class RedundantPtrBugReport:
    PARAM_PASSING = 1
    DEREFERENCE = 2
    VAR_INIT_DEREFERENCE = 4

    def __init__(self, report):
        """
        Parses the given CodeChecker report.
        """
        def _dump():
            print(json.dumps(report, sort_keys=True, indent=2))

        # These values are elaborated later.
        self.usage = None
        self.guarded = False

        # Output format for
        # 'readability-redundant-pointer-in-local-scope'
        # as of commit e6fdca25f5254c112f3cc4eccafa4a7ec77cb12a.
        self.is_ptr = "pointer variable" in report['checkerMsg']
        self.is_dereferencable = "dereferenceable variable" \
                                 in report['checkerMsg']
        assert(self.is_ptr ^ self.is_dereferencable)

        steps = [e['msg'] for e in report['details']['pathEvents']]
        for step in steps:
            if step.startswith("usage: "):
                if "used in an expression" in step:
                    self.usage = self.PARAM_PASSING
                elif "dereferenced here" in step:
                    self.usage = self.DEREFERENCE
                elif "dereferenced in the initialisation of" in step:
                    self.usage = self.VAR_INIT_DEREFERENCE
            elif "is guarded by this branch" in step:
                self.guarded = True

    @property
    def is_param_passing(self):
        return self.usage == self.PARAM_PASSING

    @property
    def is_dereference(self):
        return self.usage == self.DEREFERENCE

    @property
    def is_varinit(self):
        return self.usage == self.VAR_INIT_DEREFERENCE


class ChainBugReport:
    def __init__(self, report):
        """
        Parses the given CodeChecker report.
        """
        def _dump():
            print(json.dumps(report, sort_keys=True, indent=2))

        # These values are elaborated later.
        self.first_element_special = False
        self.guard_count = 0

        # Output format for
        # 'readability-redundant-pointer-dereference-chain'
        # as of commit e6fdca25f5254c112f3cc4eccafa4a7ec77cb12a.

        # The chain reported by the checker does NOT count the final variable
        # itself.
        self.length = int(PATTERN_CHAIN_LENGTH.search(
            report['checkerMsg'])[1]) + 1

        steps = [e['msg'] for e in report['details']['pathEvents']]
        for step in steps:
            if ", but that variable cannot be elided" in step:
                self.first_element_special = True
            elif "is guarded by this branch" in step:
                self.guard_count += 1
