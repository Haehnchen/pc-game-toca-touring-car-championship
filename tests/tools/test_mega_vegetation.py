"""Mega vegetation geometry, UV, alpha and canonical bindings tests.

Run all domains with this script, or select --suite NAME [-v] [Class.test_method].
Domains: broad, tall, stepped, groves, canonical.
"""

import argparse
import importlib
import sys
import unittest

from mega_vegetation_tests import SUITES, load_suite

SELECTED_SUITES = SUITES


def load_tests(loader, tests, pattern):
    return load_suite(SELECTED_SUITES)


def main():
    global SELECTED_SUITES
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--suite", action="append", choices=SUITES)
    args, remaining = parser.parse_known_args()
    SELECTED_SUITES = tuple(dict.fromkeys(args.suite or SUITES))
    for name in SELECTED_SUITES:
        module = importlib.import_module(f"mega_vegetation_tests.{name}")
        for class_name, candidate in vars(module).items():
            if (isinstance(candidate, type) and issubclass(candidate, unittest.TestCase)
                    and candidate.__module__ == module.__name__):
                globals()[class_name] = candidate
    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
