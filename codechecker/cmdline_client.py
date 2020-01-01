import subprocess

from .command_builder import get_json_output

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
    length_tags = map(lambda s: s.split('__')[1].split('-')[0], runs_for_project)
    lengths = map(lambda s: int(s.replace('len', '')), length_tags)
    return min(lengths)


def format_run_name(project, min_length=2, cvr=False, implicit=False):
    return "%s__len%d%s%s" % (project, min_length,
                              '-cvr' if cvr else '',
                              '-imp' if implicit else '')


def get_results(project, min_length, cvr, implicit):
    run_name = format_run_name(project, min_length, cvr, implicit)
    if run_name not in __RUNS:
        raise KeyError("No run %s found!" % run_name)

    return get_json_output(['cmd', 'results', run_name, '--details'], PRODUCT)
