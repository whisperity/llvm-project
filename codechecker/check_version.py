from distutils.version import StrictVersion

from .command_builder import get_json_output

REQUIRED_VERSION = '6.11'


def check_version():
    out = get_json_output(["web-version"])
    ver = StrictVersion(out["Base package version"])

    return ver >= StrictVersion(REQUIRED_VERSION), ver
