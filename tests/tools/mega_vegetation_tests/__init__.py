"""Selectable Mega vegetation geometry, UV, alpha and canonical bindings test domains with fixture imports excluded from discovery."""

import importlib
import unittest

SUITES = ('broad', 'tall', 'stepped', 'groves', 'canonical')


def load_suite(names=SUITES):
    suite = unittest.TestSuite()
    loader = unittest.defaultTestLoader
    for name in names:
        module = importlib.import_module(f"{__name__}.{name}")
        for candidate in vars(module).values():
            if (isinstance(candidate, type) and issubclass(candidate, unittest.TestCase)
                    and candidate.__module__ == module.__name__):
                suite.addTests(loader.loadTestsFromTestCase(candidate))
    return suite
