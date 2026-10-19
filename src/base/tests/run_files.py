#!/usr/bin/env python3
"""Run public file-API cases in separate temporary working directories."""

import argparse
from pathlib import Path
import subprocess
import tempfile


CASES = (
    "create_truncate",
    "append",
    "short_read_and_eof",
    "missing_directory",
    "non_directory",
    "directory",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("cases", nargs="*", metavar="CASE", help=", ".join(CASES))
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    cases = args.cases or CASES
    for case in cases:
        if case not in CASES:
            parser.error(f"unknown case: {case}")
    for case in cases:
        with tempfile.TemporaryDirectory(prefix="base-files-", dir=executable.parent) as directory:
            subprocess.run(
                [str(executable), f"--case:{case}"],
                cwd=directory,
                check=True,
                timeout=10,
            )
    print(f"PASS {len(cases)} public file-API cases", flush=True)


if __name__ == "__main__":
    main()
