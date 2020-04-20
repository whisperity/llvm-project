import json
import re

PATTERN_FUNCTION_ANALYSED = re.compile(
    r"^function '(.*?)' matched preliminary requirements")
PATTERN_FUNCTION_MATCHED = re.compile(
    r"^function '(.*?)' has mixable parameter ranges")


class FunctionMatch:
    def __init__(self, report):
        """
        Parses the given CodeChecker report of function analysis and match
        records.
        """
        def _dump():
            print(json.dumps(report, sort_keys=True, indent=2))

        # These values are elaborated later.
        self.is_analysed_function = False
        self.is_matched_function = False

        msg_analyse = PATTERN_FUNCTION_ANALYSED.match(report['checkerMsg'])
        if msg_analyse:
            self.is_analysed_function = True
            self.function_name = msg_analyse[1]

        msg_match = PATTERN_FUNCTION_MATCHED.match(report['checkerMsg'])
        if msg_match:
            self.is_matched_function = True
            self.function_name = msg_match[1]

        assert(not (self.is_analysed_function and self.is_matched_function))
        assert(self.is_analysed_function or self.is_matched_function)
