from .command_builder import get_json_output

REDUNDANT_PTR = 'readability-redundant-pointer-in-local-scope'
DEREFERENCE_CHAIN = 'readability-redundant-pointer-dereference-chain'
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

    return sorted(list(set(__RUNS)))


class NoRunError(Exception):
    def __init__(self, run_name):
        super(Exception, self).__init__("No run with the name: %s!" % run_name)


def get_results(project, checker):
    if project not in __RUNS:
        raise NoRunError(project)

    return get_json_output(['cmd', 'results', project,
                            '--details',
                            '--checker-name', checker,
                            '--uniqueing', "off"],
                           PRODUCT)
