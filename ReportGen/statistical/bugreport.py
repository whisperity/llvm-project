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


_REFS = re.compile(r"((?:(?:&) *)+)$")
_PTR_QUALS = re.compile(r"(\*) *((?:(?:const|volatile|(?:__)?restrict) *)*)$")
_T_QUALS = re.compile(r"^ *((?:(?:const|volatile|(?:__)?restrict) *)+)")
_QUALS_T = re.compile(r"((?:(?:const|volatile|(?:__)?restrict) *)+)$")
_FUNCTION_PTR_REF = re.compile(r"^(.*?) *\((.*?)\) *\((.*?)\)$")
_FUNCTION_PTR_REF_WITHOUT_MAYBE_NAME = re.compile(
    r".*?"
    r"((?:(?:\*)? *(?:(?:const|volatile|(?:__)?restrict) *)*)*(?:(?:&) *)*)"
    r".*?")
_FUNCTION = re.compile(r"^(.*?) *\((.*?)\)$")

def canonicalise_type(t):
    """
    Tear off, from the point of view of this script, unnecessary names of types
    from the checker output, and keep only the generic type constructions,
    effectively reducing it to "qualifier[s] T* qualifier[s]&"-like sequences.

    >>> canonicalise_type("int")
    'T'
    >>> canonicalise_type("struct user_defined_foo")
    'T'
    >>> canonicalise_type("int*")
    'T*'
    >>> canonicalise_type("int *")
    'T*'
    >>> canonicalise_type("const int *")
    'const T*'
    >>> canonicalise_type("int * const volatile")
    'T* const volatile'
    >>> canonicalise_type("const int *const")
    'const T* const'
    >>> canonicalise_type("const char **")
    'const T**'
    >>> canonicalise_type("const char * *")
    'const T**'
    >>> canonicalise_type("struct blame_entry***")
    'T***'
    >>> canonicalise_type("struct blame_entry * * *")
    'T***'
    >>> canonicalise_type("int const")
    'const T'
    >>> canonicalise_type("const std::string&")
    'const T&'
    >>> canonicalise_type("std::string const &")
    'const T&'
    >>> canonicalise_type("const cc::service::core::FileInfo &")
    'const T&'
    >>> canonicalise_type("volatile enum colour_diff * const")
    'volatile T* const'
    >>> canonicalise_type("volatile signed char * const &")
    'volatile T* const&'
    >>> canonicalise_type("const volatile struct waffle_iron &")
    'const volatile T&'
    >>> canonicalise_type("const volatile unsigned int * const restrict")
    'const volatile T* const restrict'
    >>> canonicalise_type("bool (clang::CXXRecordDecl::*)()")
    'T (*)()'
    >>> canonicalise_type("int (*main)(int argc, char* argv[])")
    'T (*)()'
    >>> canonicalise_type("int (* const main)(int argc, char* argv[])")
    'T (* const)()'
    >>> canonicalise_type("int (&main)(int argc, char* argv[])")
    'T (&)()'
    >>> canonicalise_type("int (*)(POLLINFO*, short *)")
    'T (*)()'
    >>> canonicalise_type("int (&&main)(int argc, char* argv[])")
    'T (&&)()'
    >>> canonicalise_type("int (*&&main)(int argc, char* argv[])")
    'T (*&&)()'
    >>> canonicalise_type("T*const &&")
    'T* const&&'
    >>> canonicalise_type("int (*const &&main)(int argc, char* argv[])")
    'T (* const&&)()'
    >>> canonicalise_type("char * (const struct _zend_encoding *, char *)")
    'T ()()'
    >>> canonicalise_type("void (struct job *)")
    'T ()()'
    >>> canonicalise_type("void (void*, const char*)")
    'T ()()'
    """
    # print(">>> canonicalise_type(" + t + ")", file=sys.stderr)

    # First, unsigned and signed are not interesting at all, and easy to
    # replace.
    t = t.strip().replace("unsigned ", '').replace("signed ", '')

    # Check if the type is of a function type signature, or a function pointer
    # or reference.
    match = _FUNCTION_PTR_REF.search(t)
    if match:
        # print("--- Function ptr/reference:", match.groups(), file=sys.stderr)
        t2 = match.group(2).strip()
        match2 = _FUNCTION_PTR_REF_WITHOUT_MAYBE_NAME.search(t2)
        if match2 and match2.group(1):
            # print("--- Function ptr/ref structure matched:", match2.groups(),
            #       file=sys.stderr)
            # Need to inject a dummy 'T', as the matched group will only be
            # something like "* const* volatile *restrict *&&" to canonicalise.
            t2 = 'T' + match2.group(1).strip()
        # print(">-> Canonicalising [" + t2 + "]", file=sys.stderr)
        # Remove the leading T, we only need the appropriate parentheses.
        t2_canonical = canonicalise_type(t2).lstrip('T')
        # print("<<< Function pointer/reference [T (" + t2_canonical + ")()]",
        #       file=sys.stderr)
        return "T (" + t2_canonical + ")()"

    match = _FUNCTION.search(t)
    if match:
        # print("--- Function (proto)type:", match.groups(), file=sys.stderr)
        return "T ()()"

    # Check if the type is a reference. If so, the reference can not be
    # qualified, only the referred type.
    match = _REFS.search(t)
    if match:
        # print("--- Reference:", match.groups(), file=sys.stderr)
        refs = match.group(1).count('&')
        t2 = _REFS.sub('', t).strip()
        # print(">-> Canonicalising [" + t2 + "]", file=sys.stderr)
        t2_canonical = canonicalise_type(t2)
        # print("<<< [" + ('&' * refs) + "] reference to [" + t2_canonical + "]",
        #       file=sys.stderr)
        return t2_canonical + ('&' * refs)

    # Check if the type is a pointer. If so, the pointer itself might be
    # qualified.
    match = _PTR_QUALS.search(t)
    if match:
        # print("--- Potentially qual'd ptr match:", match.groups(),
        #       file=sys.stderr)
        quals = match.group(2)
        const, volatile, restrict = tuple(map(
            lambda q: q in quals, ["const", "volatile", "restrict"]))
        t2 = _PTR_QUALS.sub('', t).strip()
        # print(">-> Canonicalising [" + t2 + "]", file=sys.stderr)
        t2_canonical = canonicalise_type(t2)
        # print("<<< [" + quals + "] pointer to [" + t2_canonical + "]",
        #       file=sys.stderr)
        return t2_canonical + '*' + \
            (" const" if const else '') + \
            (" volatile" if volatile else '') + \
            (" restrict" if restrict else '')

    # Search for local qualifiers on the type (which is no longer a pointer!)
    # either at the front...
    match = _T_QUALS.search(t)
    t2 = _T_QUALS.sub('', t).strip()
    if not match:
        # ... or the back.
        match = _QUALS_T.search(t)
        t2 = _QUALS_T.sub('', t).strip()
    if match:
        # print("--- Qualifier match:", match.groups(), file=sys.stderr)
        quals = match.group(1)
        const, volatile, restrict = tuple(map(
            lambda q: q in quals, ["const", "volatile", "restrict"]))
        # print(">-> Canonicalising [" + t2 + "]", file=sys.stderr)
        t2_canonical = canonicalise_type(t2)
        # print("<<< [" + quals + "] qualified [" + t2_canonical + "]",
        #       file=sys.stderr)
        return ("const " if const else '') + \
            ("volatile " if volatile else '') + \
            ("restrict" if restrict else '') + t2_canonical

    # If nothing else matches, replace whatever the type was with a pure 'T'.
    # print("<<< Final rewrite: [" + t + "] --> [" + 'T' + "]",
    #       file=sys.stderr)
    return 'T'


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
        self.raw_involved_types = [self.exact_type] if exact_type else []
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

                self.raw_involved_types += types_for_step

        self.involved_types = {sanitise_typename(t)
                               for t in self.raw_involved_types}
        self.canonical_involved_types = {canonicalise_type(t)
                                         for t in self.raw_involved_types}

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
