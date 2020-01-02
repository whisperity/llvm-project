from .command_builder import get_json_output

CHECKER_NAME = 'cppcoreguidelines-avoid-adjacent-arguments-of-same-type'
PRODUCT = None


def set_product(product_url):
    global PRODUCT
    PRODUCT = product_url


# Some caching.
__RUNS = list()


def get_projects():
    global __RUNS
    if not __RUNS:
        runs_native = get_json_output(['cmd', 'runs'], PRODUCT)
        __RUNS = [list(name.keys())[0] for name in runs_native]

    projects = map(lambda s: s.split('__')[0], __RUNS)
    return sorted(list(set(projects)))


def minimum_length_for_project(project):
    runs_for_project = filter(lambda s: s.startswith(project), __RUNS)
    length_tags = map(lambda s: s.split('__')[1].split('-')[0],
                      runs_for_project)
    lengths = map(lambda s: int(s.replace('len', '')), length_tags)
    return min(lengths)


def format_run_name(project, min_length=2, cvr=False, implicit=False):
    return "%s__len%d%s%s" % (project, min_length,
                              '-cvr' if cvr else '',
                              '-imp' if implicit else '')


class NoRunError(Exception):
    def __init__(self, run_name):
        super(Exception, self).__init__("No run with the name: %s!" % run_name)


def get_results(project, min_length, cvr, implicit):
    run_name = format_run_name(project, min_length, cvr, implicit)
    if run_name not in __RUNS:
        raise NoRunError(run_name)

    return get_json_output(['cmd', 'results', run_name,
                            '--details',
                            '--checker-name', CHECKER_NAME,
                            '--uniqueing', "off"],
                           PRODUCT)


NEW_FINDINGS = 2
DISAPPEARED_FINDINGS = 4
FINDINGS_IN_BOTH = 8


def get_difference(project, min_length_1, cvr_1, implicit_1,
                   min_length_2, cvr_2, implicit_2, direction):
    run_name_base = format_run_name(project, min_length_1, cvr_1, implicit_1)
    run_name_new = format_run_name(project, min_length_2, cvr_2, implicit_2)

    if run_name_base not in __RUNS:
        raise NoRunError(run_name_base)
    if run_name_new not in __RUNS:
        raise NoRunError(run_name_new)

    if direction == NEW_FINDINGS:
        direction_opt = '--new'
    elif direction == DISAPPEARED_FINDINGS:
        direction_opt = '--resolved'
    elif direction == FINDINGS_IN_BOTH:
        direction_opt = '--unresolved'
    else:
        raise NotImplementedError("Wrong 'direction' argument: '%s'"
                                  % direction)

    return get_json_output(['cmd', 'diff',
                            '--basename', run_name_base,
                            '--newname', run_name_new,
                            direction_opt,
                            '--checker-name', CHECKER_NAME,
                            '--uniqueing', "off"],
                           PRODUCT)
