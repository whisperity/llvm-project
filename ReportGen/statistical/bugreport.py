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


def match_all_to_list(pattern, string):
    out = list()
    for res in pattern.findall(string):
        if isinstance(res, tuple):
            out += [x for x in res]
        else:
            out.append(res)
    return out


def _tear_tagged_type(t):
    return re.sub(r"^(struct|class|enum)( )?(.*)$", r'\3', t)


def sanitise_typename(typename):
    """
    Tear off, from the point of view of this script, unnecessary qualifiers
    from the type names found in the checker output.

    >>> sanitise_typename("T")
    'T'
    >>> sanitise_typename("int")
    'int'
    >>> sanitise_typename("const T&")
    'T'
    >>> sanitise_typename("T const&")
    'T'
    >>> sanitise_typename("int *")
    'int *'
    >>> sanitise_typename("const int *")
    'int *'
    >>> sanitise_typename("int *const")
    'int *'
    >>> sanitise_typename("T *const")
    'T *'
    >>> sanitise_typename("T* const")
    'T *'
    >>> sanitise_typename("const struct utf8_data *")
    'utf8_data *'
    >>> sanitise_typename("volatile signed char* const&")
    'char *'
    >>> sanitise_typename("const volatile unsigned int * const restrict")
    'int *'
    """

    def tear_signed_unsigned(t):
        return re.sub(r"^(un)?signed (.*)", r'\2', t)

    def tear_qualifier_left(t):
        return re.sub(r"( )?(const|volatile) (.*?)", r'\3', t)

    def tear_ptrref_qualifier_right(t):
        return re.sub(r"(.*?)( \*?)( )?(const|volatile|(?:__)?restrict)(&?)",
                      r'\1\2\5', t)

    def fix_ptr_at_end(t):
        return re.sub(r"(\S)\*$", r'\1 *', t)

    def tear_reference(t):
        return re.sub(r"( )?&$", '', t)

    while True:
        before = typename

        typename = typename.strip()
        typename = tear_signed_unsigned(typename)
        typename = _tear_tagged_type(typename)
        typename = tear_qualifier_left(typename)
        typename = tear_ptrref_qualifier_right(typename)
        typename = tear_reference(typename)
        typename = fix_ptr_at_end(typename)

        if before == typename:
            # No change, fix point reached.
            break

    return typename


def mask_typename_keep_type_constructors(typename):
    """
    Tear off, from the point of view of this script, unnecessary names of types
    from the checker output, and keep only the generic type constructions.

    >>> mask_typename_keep_type_constructors("T")
    'T'
    >>> mask_typename_keep_type_constructors("int")
    'T'
    >>> mask_typename_keep_type_constructors("const T&")
    'const T&'
    >>> mask_typename_keep_type_constructors("T const&")
    'const T&'
    >>> mask_typename_keep_type_constructors("int *")
    'T*'
    >>> mask_typename_keep_type_constructors("const int *")
    'const T*'
    >>> mask_typename_keep_type_constructors("int *const")
    'T* const'
    >>> mask_typename_keep_type_constructors("T *const")
    'T* const'
    >>> mask_typename_keep_type_constructors("T* const")
    'T* const'
    >>> mask_typename_keep_type_constructors("const struct utf8_data *")
    'const T*'
    >>> mask_typename_keep_type_constructors("volatile enum colour_diff * const")
    'volatile T* const'
    >>> mask_typename_keep_type_constructors("volatile signed char * const &")
    'volatile T* const&'
    >>> mask_typename_keep_type_constructors("const volatile struct waffle_iron &")
    'const volatile T&'
    >>> mask_typename_keep_type_constructors("const volatile unsigned int * const restrict")
    'const volatile T* const restrict'
    """

    def mask_to_T(t):
        return re.sub(r"^([^\*&]*)( )*([\*&]*)( )*(.*?)$", r"T\3\5", t)

    def extract_left_qualifiers(t):
        match = re.match(r"( )?(((const|volatile)( )?)*)(.*?)", t)
        if match:
            return match.group(2).strip(), \
                re.sub(r"^((const|volatile)( )?)*(.*?)", r'\4', t)
        return "", t

    def extract_right_qualifiers(t):
        match = re.match(
            r"(.*?)( \*?)( )?(((const|volatile|(?:__)?restrict)( )?)*)(&?)",
            t)
        if match:
            return match.group(4).strip(), re.sub(
                r"(.*?)( \*?)( )?((const|volatile|(?:__)?restrict)( )?)*(&?)$",
                r'\1\2\6\7',
                t)
        return "", t

    def strip_ptr_ref(t):
        match = re.match(r"^(.*?)( )?((\**( )?)*)((&( )?)*)$", t)
        if not match:
            return t
        ptrs, refs = match.group(3).count('*'), match.group(6).count('&')
        return match.group(1).rstrip(), ptrs, refs

    while True:
        before = typename

        typename = typename.strip()
        typename = typename.replace("signed ", '').replace("unsigned ", '')
        typename = _tear_tagged_type(typename)
        lquals, typename = extract_left_qualifiers(typename)
        typename = _tear_tagged_type(typename)
        rquals, typename = extract_right_qualifiers(typename)
        typename = mask_to_T(typename)
        typename, ptrs, refs = strip_ptr_ref(typename)

        if ptrs and not refs:
            typename = lquals + ' ' + typename + ('*' * ptrs) + ' ' + rquals
        elif refs and not ptrs:
            typename = lquals + rquals + ' ' + typename + ('&' * refs)
        elif ptrs and refs:
            typename = lquals + ' ' + typename + ('*' * ptrs) + ' ' + rquals + ('&' * refs)
        else:
            typename = lquals + ' ' + typename + ' ' + rquals

        typename = typename.strip()

        if before == typename:
            # No change, fix point reached.
            break

    return typename


class BugReport:
    def __init__(self, report):
        """
        Parses the given CodeChecker report to a meaningful result of adjacent
        parameter mixup possibility.
        """
        def _dump():
            print(json.dumps(report, sort_keys=True, indent=2))

        # These values are elaborated later.
        self.has_typedef = False
        self.has_bindpower = False
        self.has_ref_bind = False
        self.has_implicit_bidir = False

        msg_match = PATTERN_FUNCTION_NAME.match(report['checkerMsg'])
        self.length, self.function_name = int(msg_match[1]), msg_match[2]

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
                    if bidirs:
                        self.has_implicit_bidir = True
                    types_for_step += bidirs

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
            elif re.search(r'\[\d+\]$', t):
                category += "buffer (C-style array)"
            elif t == 'FILE' and ptr_depth == 1:
                category = "C File"
            elif t in ['bool', '_Bool', 'boolean', 'short', 'int', 'long',
                       'long long', 'size_t', 'ssize_t', 'ptrdiff_t']:
                category += "fundamental integral"
            elif t in ['BOOL', 'int8', 'int8_t', 'uint8', 'uint8_t',
                       'int16', 'int16_t', 'uint16', 'uint16_t',
                       'int32', 'int32_t', 'uint32', 'uint32_t', 'int64',
                       'int64_t', 'uint64', 'uint64_t', 'uint256', '__m128i',
                       '__m256', '__m256i', '__m512i', 'quint64',
                       'uchar', 'u_char', 'uint', 'u_int', 'ushort', 'uInt',
                       'uint_fast32_t', 'uint_fast64_t',
                       'GLboolean', 'GLenum', 'GLint', 'GLsizei', 'GLuint',
                       'XMLSize_t', 'Bigint', 'intmax_t']:
                category += "custom integral"
            elif t in ['SOCKET', 'time_t'] \
                    or (t.startswith('Q') and
                        re.sub('([A-Z])', r' \1', t[1:]).istitle()):
                category += "framework type"
            elif t in ['float', 'double', 'long double']:
                category += "fundamental floating"
            elif t in ['float4', 'float8', 'FPOINT', 'LONG_DOUBLE',
                       'GLdouble', 'GLfloat', 'qreal']:
                category += "custom floating"
            elif 'string' in t.lower() or t in ['Twine']:
                category += "string-like"
            elif t in ['const char', 'char', 'schar', 'wchar_t', 'char16_t',
                       'strbuf', 'utf8_data', 'XMLCh']:
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
