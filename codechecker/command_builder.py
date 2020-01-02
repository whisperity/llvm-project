import json
import subprocess
import sys
import traceback


def get_json_output(args, product_url=None):
    command = ["CodeChecker"] + args + ['-o', "json"] + \
              (['--url', product_url] if product_url else [])

    process = subprocess.run(command,
                             stdout=subprocess.PIPE)
    try:
        return json.loads(process.stdout)
    except OSError as ose:
        print("[ERROR] Command", command, "could not execute:",
              file=sys.stderr)
        traceback.print_exc(file=sys.stderr)
        raise
    except json.decoder.JSONDecodeError as jsonde:
        if not process.stdout:
            # No output from process, consider it empty JSON.
            return []

        print("[ERROR] Command", command, "executed but returned invalid "
              "JSON:", file=sys.stderr)
        print(jsonde, file=sys.stderr)
        print("[DEBUG] The process output was:", process.stdout,
              file=sys.stderr)
        raise
