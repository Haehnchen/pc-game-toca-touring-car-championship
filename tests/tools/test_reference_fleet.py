"""Fleet schema, source-parity and native-handoff tests.

Run every domain: python3 tests/tools/test_reference_fleet.py
Run one domain: python3 tests/tools/test_reference_fleet.py --suite clock -v
Run one test: python3 tests/tools/test_reference_fleet.py --suite clock StartupClockTests.test_roles_follow_generated_extra_transactions_and_compact_sidecar_observes_only_inputs
"""

import argparse
import importlib
import sys
import unittest

from reference_fleet_tests import SUITES, load_suite

SELECTED_SUITES = SUITES


def load_tests(loader, tests, pattern):
    return load_suite(SELECTED_SUITES)


def main():
    global SELECTED_SUITES
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--suite", action="append", choices=SUITES)
    args, remaining = parser.parse_known_args()
    selected = tuple(dict.fromkeys(args.suite or SUITES))
    SELECTED_SUITES = selected
    # Publish selected classes for unittest's existing Class.test selector.
    for name in selected:
        module = importlib.import_module(f"reference_fleet_tests.{name}")
        for class_name, candidate in vars(module).items():
            if (isinstance(candidate, type) and issubclass(candidate, unittest.TestCase)
                    and candidate.__module__ == module.__name__):
                globals()[class_name] = candidate

    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
