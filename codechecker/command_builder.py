import json
import subprocess


def get_json_output(args, product_url=None):
    process = subprocess.run(["CodeChecker"] + args + ['-o', "json"] +
                             (['--url', product_url] if product_url else []),
                             stdout=subprocess.PIPE)
    return json.loads(process.stdout)
