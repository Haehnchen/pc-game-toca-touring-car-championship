#!/usr/bin/env python3
"""Refresh unchanged Ninja metadata; fully configure new or changed requests."""

import json
from pathlib import Path
import subprocess
import sys
import zlib


def option(arguments, name):
    for index, value in enumerate(arguments):
        if value == name and index + 1 < len(arguments):
            return arguments[index + 1]
    return None


def metadata(build):
    files = ("CMakeCache.txt", "build.ninja", "CMakeFiles/rules.ninja")
    if any(not (build / name).is_file() for name in files):
        return None
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    testing = next((line.partition("=")[2] for line in cache.splitlines() if line.startswith("BUILD_TESTING:BOOL=")), "OFF")
    if testing.upper() in {"1", "ON", "TRUE", "YES", "Y"}:
        if not all((build / name).is_file() for name in ("CTestTestfile.cmake", "test-targets.json")):
            return None
    names = (*files, "CMakeFiles/VerifyGlobs.cmake", "CTestTestfile.cmake", "test-targets.json")
    return {name: f"{zlib.crc32((build / name).read_bytes()):08x}" if (build / name).is_file() else None for name in names}


def ordinary_request(command):
    index = 1
    while index < len(command):
        value = command[index]
        if value in {"-S", "-B", "-G", "-D"} and index + 1 < len(command):
            index += 2
        elif value.startswith("-D") and len(value) > 2:
            index += 1
        else:
            # --fresh, -U, preload scripts and diagnostic flags must retain
            # their requested configure-time effects on every invocation.
            return False
    return True


def configure(command):
    if not command:
        raise ValueError("Expected the complete CMake configure command after --")
    directory = option(command, "-B")
    generator = option(command, "-G")
    if directory is None or generator != "Ninja" or not ordinary_request(command):
        return subprocess.run(command).returncode
    build = Path(directory).resolve()
    ledger = build / "CMakeFiles/toca-configure-request.json"
    request = {"version": 1, "argv": command, "cwd": str(Path.cwd().resolve())}
    previous = None
    try:
        previous = json.loads(ledger.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        pass
    current = metadata(build)
    unchanged = current is not None and previous == {**request, "metadata": current}
    if unchanged:
        # Ninja checks CMake includes, CONFIGURE_DEPENDS globs and cache inputs
        # before CTest reads its selection and generated target mapping.
        print("Configure: refresh Ninja metadata", flush=True)
        result = subprocess.run([command[0], "--build", directory, "--target", "build.ninja"])
    else:
        print("Configure: full configure", flush=True)
        result = subprocess.run(command)
    if result.returncode == 0:
        current = metadata(build)
        if current is not None:
            ledger.parent.mkdir(parents=True, exist_ok=True)
            temporary = ledger.with_suffix(".tmp")
            temporary.write_text(json.dumps({**request, "metadata": current}, indent=2) + "\n", encoding="utf-8")
            temporary.replace(ledger)
    return result.returncode


def main():
    arguments = sys.argv[1:]
    if arguments[:1] == ["--"]:
        arguments = arguments[1:]
    try:
        result = configure(arguments)
        return result if result >= 0 else 128 - result
    except (OSError, ValueError) as error:
        print(f"configure: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
