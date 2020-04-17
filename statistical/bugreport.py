import json
import re
import sys

PATTERN_FUNCTION_NAME = re.compile(
    r"^(\d+) adjacent parameters for '(.*?)' of")
PATTERN_EXACT_TYPE = re.compile(r"similar type \('(.*?)'\)")
PATTERN_TYPEDEF = re.compile(r"type of parameter '.*?' is '(.*?)'")
PATTERN_BINDPOWER = re.compile(
    r"'(.*?)' might bind with same force as '(.*?)'")
PATTERN_IMPLICIT_BIDIR = re.compile(
    r"'(.*?)' and '(.*?)' can suffer implicit")
PATTERN_IMPLICIT_UNIDIR = re.compile(
    r"'(.*?)' can be implicitly converted (?:from|to) '(.*?)'")


def match_all_to_list(pattern, string):
    out = list()
    for res in pattern.findall(string):
        if isinstance(res, tuple):
            out += [x for x in res]
        else:
            out.append(res)
    return out


def sanitise_typename(typename):
    typename = re.sub(r"(un)?signed (char|short|int|long|long long)",
                      r"\2", typename)

    typename = re.sub(r"^(const )?(.*?) (\*)?&$", r'\2', typename)

    typename = re.sub(r"^const volatile (.*?) \*", r"\1 *", typename)
    typename = re.sub(r"^volatile (.*?) \*", r"\1 *", typename)
    typename = re.sub(r"^const (.*?) \*", r"\1 *", typename)
    typename = re.sub(r"^([\w\d_:]*?) \*( )?const volatile (?:__)restrict",
                      r"\1 \*", typename)
    typename = re.sub(r"^([\w\d_:]*?) \*( )?const volatile", r"\1 *",
                      typename)
    typename = re.sub(r"^([\w\d_:]*?) \*( )?const (?:__)restrict", r"\1 *",
                      typename)
    typename = re.sub(
        r"^([\w\d_:]*?) \*( )?volatile (?:__)restrict", r"\1 *", typename)
    typename = re.sub(r"^([\w\d_:]*?) \*( )?(?:__)restrict", r"\1 *",
                      typename)
    typename = re.sub(r"^([\w\d_:]*?) \*( )?volatile", r"\1 *", typename)
    typename = re.sub(r"^([\w\d_:]*?) \*( )?const", r"\1 *", typename)
    typename = re.sub(r"^const volatile ([\w\d_:]*?)$", r"\1", typename)
    typename = re.sub(r"^volatile ([\w\d_:]*?)$", r"\1", typename)
    typename = re.sub(r"^const ([\w\d_:]*?)$", r"\1", typename)

    return typename


class BugReport:
    def __init__(self, report):
        """
        Parses the given CodeChecker report to a meaningful implicit conversion
        datum.
        """
        def _dump():
            print(json.dumps(report, sort_keys=True, indent=2))

        # These values are elaborated later.
        self.has_typedef = False
        self.has_bindpower = False
        self.has_ref_bind = False
        self.has_implicit_bidir = False
        self.has_implicit_unidir = False

        msg_match = PATTERN_FUNCTION_NAME.match(report['checkerMsg'])
        self.length, self.function_name = int(msg_match[1]), msg_match[2]

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
        # Ignore first ("last argument in range" marker) and last (the
        # check message) "bug path steps".
        steps = steps[1:-1]
        if self.is_implicit:
            # Unique the steps as implicit conversion emits a diagnostic for
            # each implicitly convertible pair, which e.g. for (int, int, long)
            # is like 3 messages (int<->int, int<->long, int<->long).
            steps = list(set(steps))

        if not self.exact_type:
            self.has_typedef = any('after resolving type aliases' in step
                                   for step in steps)
            self.has_bindpower = any('might bind with same force as' in step
                                     for step in steps)

            for step in steps:
                types_for_step = \
                    match_all_to_list(PATTERN_TYPEDEF, step) + \
                    match_all_to_list(PATTERN_BINDPOWER, step)

                if self.is_implicit:
                    bidirs = match_all_to_list(PATTERN_IMPLICIT_BIDIR, step)
                    unidirs = match_all_to_list(PATTERN_IMPLICIT_UNIDIR, step)
                    if bidirs:
                        self.has_implicit_bidir = True
                    if unidirs:
                        self.has_implicit_unidir = True
                    types_for_step += bidirs + unidirs

                if any(t.endswith(' &') for t in types_for_step):
                    self.has_ref_bind = True

                self.involved_types += types_for_step

        self.involved_types = {sanitise_typename(t)
                               for t in set(self.involved_types)}

        if not self.involved_types:
            print("[WARNING] For the following report, 'involved_types' "
                  "remained empty?!", file=sys.stderr)
            print(json.dumps(report, sort_keys=True, indent=2),
                  file=sys.stderr)

    def get_involved_types_categories(self):
        """
        Categorises the types in `self.involved_types` and returns the
        categories found.
        """
        ret = list()
        uncategorised = list()
        for t in self.involved_types:
            ptr_depth = 0
            while t.endswith('*'):
                ptr_depth += 1
                t = t.rstrip('* ')

            category = "pointer to " if ptr_depth else ''

            if t == 'void' and ptr_depth or \
                    t in ['ArrayRef', 'uintptr_t', 'BUFFER', 'Buffer']:
                category = "buffer (void-ptr or templated)"
            elif re.search(r'[\d]$', t):
                category += "buffer (C-style array)"
            elif t == 'FILE' and ptr_depth == 1:
                category = "C File"
            elif t in ['bool', '_Bool', 'boolean', 'short', 'int', 'long',
                       'long long', 'size_t', 'ssize_t', 'ptrdiff_t']:
                category += "fundamental integral"
            elif t in ['BOOL', 'int8', 'int8_t', 'uint8', 'uint8_t',
                       'char16_t', 'int16', 'int16_t', 'uint16', 'uint16_t',
                       'int32', 'int32_t', 'uint32', 'uint32_t', 'int64',
                       'int64_t', 'uint64', 'uint64_t', 'uint256', '__m128i',
                       '__m256', '__m256i', '__m512i', 'quint64',
                       'uchar', 'u_char', 'uint', 'u_int', 'ushort', 'uInt',
                       'uint_fast32_t', 'uint_fast64_t',
                       'GLboolean', 'GLenum', 'GLint', 'GLsizei', 'GLuint']:
                category += "custom integral"
            elif t in ['SOCKET', 'time_t'] \
                    or (t.startswith('Q') and t[1:].istitle()):
                category += "framework type"
            elif t in ['float', 'double', 'long double']:
                category += "fundamental floating"
            elif t in ['float4', 'float8', 'FPOINT', 'LONG_DOUBLE',
                       'GLdouble', 'GLfloat']:
                category += "custom floating"
            elif 'string' in t.lower() or t in ['Twine']:
                category += "string-like"
            elif t in ['const char', 'char', 'schar', 'wchar_t']:
                # NOTE: Outer pointer-ness potentially removed already.
                if ptr_depth > 1:
                    category += "strings"
                elif ptr_depth == 1:
                    category = "strings of buffer (char-ptr)"
                else:
                    category += "fundamental integral"
            else:
                uncategorised.append(
                    "%s%s" % (t, ' ' + '*' * ptr_depth if ptr_depth else ''))
                category += "unknown?"
            ret.append(category)

        return ret, uncategorised
