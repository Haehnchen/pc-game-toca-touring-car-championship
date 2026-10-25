#!/usr/bin/env python3
"""Build targets selected by CTest, then run those tests with the same arguments."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


PROFILE_EXCLUSION = "^integration$|^extended$"
SELECTION_OPTIONS = {"--tests-regex", "--exclude-regex", "--label-regex", "--label-exclude",
                     "--tests-information", "--union", "--rerun-failed", "--tests-from-file",
                     "--exclude-from-file"}
SHORT_SELECTION_OPTIONS = ("-LE", "-R", "-E", "-L", "-I", "-U")


def profile_arguments(ctest_args, profile):
    if profile not in {"auto", "quick", "full"}:
        raise ValueError(f"Unknown test profile: {profile}")
    if profile == "auto":
        explicit = any(arg.partition("=")[0] in SELECTION_OPTIONS
                       or any(arg.startswith(option) for option in SHORT_SELECTION_OPTIONS)
                       for arg in ctest_args)
        profile = "full" if explicit else "quick"
    if profile == "full":
        return profile, list(ctest_args)
    if any(arg.partition("=")[0] in {"--rerun-failed", "--union"} or arg.startswith("-U")
           for arg in ctest_args):
        raise ValueError("quick cannot use --rerun-failed or --union because CTest bypasses label filters; "
                         "use --profile auto or --profile full")

    arguments = []
    excluded_labels = False
    index = 0
    while index < len(ctest_args):
        arg = ctest_args[index]
        if arg in {"-LE", "--label-exclude"}:
            index += 1
            if index == len(ctest_args):
                raise ValueError(f"{arg} requires a label regular expression")
            exclusion = ctest_args[index]
        elif arg.startswith("--label-exclude="):
            exclusion = arg.partition("=")[2]
        elif arg.startswith("-LE"):
            exclusion = arg[3:]
        else:
            arguments.append(arg)
            index += 1
            continue
        # Multiple CTest -LE filters exclude their intersection. Union the
        # profile into each filter to preserve that intersection and all of the
        # user's regex groups: (P | A) & (P | B) == P | (A & B).
        arguments.extend(["-LE", PROFILE_EXCLUSION + "|" + exclusion])
        excluded_labels = True
        index += 1
    if not excluded_labels:
        arguments.extend(["-LE", PROFILE_EXCLUSION])
    return profile, arguments


def status(code):
    return code if code >= 0 else 128 - code


def check(build_dir, config, cmake, ctest, build_jobs, test_jobs, build_targets, ctest_args, profile="auto"):
    cache = Path(build_dir) / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8").splitlines():
            if line.startswith("BUILD_TESTING:BOOL="):
                value = line.partition("=")[2].strip().upper()
                if value in {"", "0", "OFF", "FALSE", "NO", "N", "IGNORE", "NOTFOUND"} or value.endswith("-NOTFOUND"):
                    raise ValueError("BUILD_TESTING is disabled in CMakeCache.txt; configure with -DBUILD_TESTING=ON before checking tests")
    profile, ctest_args = profile_arguments(ctest_args, profile)
    command = [ctest, "--test-dir", str(build_dir), "--build-config", config,
               "--output-on-failure", "--no-tests=error", "--parallel", str(test_jobs), *ctest_args]
    # CTest owns every selection rule (regex, labels, exclusions, index ranges,
    # rerun-failed). The final show-only option overrides a user's listing mode
    # for this query; the original mode is preserved for the actual invocation.
    selected = subprocess.run([*command, "--show-only=json-v1"], capture_output=True, text=True)
    if selected.returncode:
        sys.stdout.write(selected.stdout)
        sys.stderr.write(selected.stderr)
        return status(selected.returncode)
    tests = json.loads(selected.stdout)["tests"]
    print(f"Test profile: {profile} ({len(tests)} selected)", flush=True)
    if not tests:
        raise ValueError("CTest selected no tests; check TESTS/CTEST_ARGS and BUILD_TESTING")
    if any(arg == "-N" or arg == "--show-only" or arg.startswith("--show-only=") for arg in ctest_args):
        return status(subprocess.run(command).returncode)

    if build_targets is None:
        mapping_path = Path(build_dir) / "test-targets.json"
        mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
        build_targets = []
        for test in tests:
            name = test["name"]
            if name not in mapping:
                raise ValueError(f"Selected CTest case has no build-target mapping: {name}")
            targets = mapping[name]
            if not isinstance(targets, list) or any(not isinstance(target, str) or not target for target in targets):
                raise ValueError(f"Invalid build-target mapping for CTest case: {name}")
            for target in targets:
                if target not in build_targets:
                    build_targets.append(target)
    if build_targets:
        print("Building selected test targets: " + " ".join(build_targets), flush=True)
        build = subprocess.run([cmake, "--build", str(build_dir), "--config", config,
                                "--parallel", str(build_jobs), "--target", *build_targets])
        if build.returncode:
            return status(build.returncode)
    return status(subprocess.run(command).returncode)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--config", default="Debug")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--build-jobs", type=int, default=6)
    parser.add_argument("--test-jobs", type=int, default=6)
    parser.add_argument("--profile", choices=("auto", "quick", "full"), default="auto",
                        help="auto uses quick unless CTest selection is explicit; quick excludes integration/extended")
    parser.add_argument("--build-target", action="append", help="explicit BUILD_TARGETS override; repeat per target")
    parser.add_argument("ctest_args", nargs=argparse.REMAINDER, help="CTest arguments after --")
    args = parser.parse_args(argv)
    ctest_args = args.ctest_args
    if ctest_args[:1] == ["--"]:
        ctest_args = ctest_args[1:]
    try:
        return check(args.build_dir, args.config, args.cmake, args.ctest, args.build_jobs,
                     args.test_jobs, args.build_target, ctest_args, args.profile)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"check: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
