#!/usr/bin/env python3
"""Build, package and verify the complete Windows release on Linux."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    environment = os.environ.copy()
    for name in ("CC", "CXX"):
        environment.pop(name, None)

    def run(*command):
        print("Running:", " ".join(map(str, command)), flush=True)
        subprocess.run(list(map(str, command)), cwd=ROOT, env=environment, check=True)

    host = ROOT / "build/windows-host"
    build = ROOT / "build/windows"
    output = ROOT / "dist/windows"
    run("cmake", "--preset", "distribution", "-B", host, "-DSDL_UNIX_CONSOLE_BUILD=ON")
    run("cmake", "--build", host, "--target", "shadercross", "toca-meshopt-encode", "--parallel", args.jobs)
    lock = json.loads((ROOT / "cmake/dependencies.lock.json").read_text())
    revision = lock["archives"]["shadercross"]["revision"]
    run("cmake", "--preset", "distribution", "-B", build,
        f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'cmake/toolchains/mingw-w64-x86_64.cmake'}",
        "-DTOCA_BUILD_SHADERCROSS=OFF", f"-DTOCA_SHADERCROSS_REVISION={revision}",
        f"-DTOCA_SHADERCROSS={host / 'dependencies/shadercross/shadercross'}",
        f"-DTOCA_HOST_MESHOPT_ENCODER={host / 'toca-meshopt-encode'}", "-DTOCA_MEGA_MODELS=ON")
    run("cmake", "--build", build, "--target", "toca-game", "--parallel", args.jobs)
    run(sys.executable, ROOT / "tools/package_release.py", "--build-dir", build, "--output-dir", output)
    run(sys.executable, ROOT / "tools/verify-release.py", "--output-dir", output, "--platform", "Windows")

    return 0


if __name__ == "__main__":
    sys.exit(main())
