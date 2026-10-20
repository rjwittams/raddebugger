#!/usr/bin/env python3
# Copyright (c) 2026 Robert Wittams
# Licensed under the MIT license (https://opensource.org/license/mit/)

"""Build and run the target-free listener checks, one case per process."""

import argparse
import os
from pathlib import Path
import platform
import shlex
import subprocess

CASES = ("loopback_listener", "occupied_port", "listener_separation", "exclusive_endpoint")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=CASES)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    build = root / "build"
    build.mkdir(exist_ok=True)
    source = root / "src/socket/tests/local_listeners.c"
    system = platform.system()
    extra_flags = shlex.split(os.environ.get("SOCKET_TEST_CFLAGS", ""))
    if system == "Windows":
        binary = build / "local_listeners.exe"
        command = ["cl", "/nologo", "/Z7", "/Od", "/Zc:preprocessor", "/I" + str(root / "src")]
        command += extra_flags + [str(source), "/Fo:" + str(build / "local_listeners.obj"),
                                  "/Fe:" + str(binary), "/link", "/INCREMENTAL:NO"]
    elif system in ("Linux", "Darwin"):
        binary = build / "local_listeners"
        command = [os.environ.get("CC", "clang"), "-g", "-O0", "-D_GNU_SOURCE",
                   "-I", str(root / "src"), "-Wno-unused-function", "-Wno-unused-variable",
                   "-Wno-initializer-overrides", "-Wno-unused-value"]
        if system == "Linux" and platform.machine() in ("x86_64", "amd64"):
            command += ["-msse4.1"]
        command += extra_flags + [str(source), "-lpthread", "-lm"]
        command += ["-lproc"] if system == "Darwin" else ["-ldl", "-lrt"]
        command += ["-o", str(binary)]
    else:
        parser.error("These checks require Windows, Linux or macOS")
    subprocess.run(command, cwd=build, check=True, timeout=120)
    for case in (args.case,) if args.case else CASES:
        subprocess.run([str(binary), "--case:" + case], cwd=build, check=True, timeout=15)


if __name__ == "__main__":
    main()
