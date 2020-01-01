import json
import re

PATTERN_EXACT_TYPE = re.compile(r"similar type \('(.*?)'\)")
PATTERN_TYPEDEF = re.compile(r"type of argument '.*?' is '(.*?)'")
PATTERN_BINDPOWER = re.compile(
    r"'(.*?)' might bind with same force as '(.*?)'")


def _match_all_to_list(pattern, string):
    out = list()
    for res in pattern.findall(string):
        if isinstance(res, tuple):
            out += [x for x in res]
        else:
            out.append(res)
    return out


class BugReport:
    def __init__(self, report):
        """
        Parses the given CodeChecker report to a meaningful implicit conversion
        datum.
        """
        def _dump():
            print(json.dumps(report, sort_keys=True, indent=2))

        self.has_typedef = False
        self.has_bindpower = False
        self.has_ref_bind = False

        # Output format for
        # 'cppcoreguidelines-avoid-adjacent-arguments-of-same-type'
        # as of commit 78c01e7fa41f3b3631ec32dedf7236bd62c62a29.
        self.length = int(report['checkerMsg'].split(' ')[0])
        self.is_implicit = 'convertible types may' in report['checkerMsg']
        self.is_exact = 'similar type (' in report['checkerMsg']
        # non-exact: 'similar type are' (without the type's name)
        # implicit => non-exact
        assert(not self.is_implicit or (not self.is_exact))

        exact_type = PATTERN_EXACT_TYPE.search(report['checkerMsg'])
        self.exact_type = exact_type.group(1) if exact_type else None
        self.involved_types = [self.exact_type] if exact_type else []
        assert(bool(self.exact_type) == self.is_exact)

        steps = [e['msg'] for e in report['details']['pathEvents']]
        # print(json.dumps(report, sort_keys=True, indent=4))

        if not self.exact_type:
            self.has_typedef = any('after resolving type aliases' in step
                                   for step in steps)
            self.has_bindpower = any('might bind with same force as' in step
                                     for step in steps)

            for step in steps[1:-1]:
                # Ignore first ("last argument in range" marker) and last (the
                # check message) "bug path steps".
                types_for_step = \
                    _match_all_to_list(PATTERN_TYPEDEF, step) + \
                    _match_all_to_list(PATTERN_BINDPOWER, step)

                if any(t.endswith(' &') for t in types_for_step):
                    self.has_ref_bind = True

                self.involved_types += types_for_step

        # Sanitize the type names printed in involved_types.
        def _sanitize_typename(typename):
            typename = re.sub(r"const (.*?) &", r'\1', typename)
            return typename
        self.involved_types = {_sanitize_typename(t) for t in set(self.involved_types)}
