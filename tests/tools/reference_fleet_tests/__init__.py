"""Independently selectable fleet-reference test domains."""

import importlib
import unittest

SUITES = ('schema', 'ai', 'session', 'audio', 'random', 'drivers', 'retained_session', 'drivetrain', 'controls', 'physics', 'cold_grid', 'startup_join', 'startup_physics', 'clock', 'scene')


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
