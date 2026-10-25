"""Selectable Reference trace capture test domains with fixture imports excluded from discovery."""

import importlib
import unittest

SUITES = ('memory', 'clocks', 'recorders', 'capture', 'warmup', 'driving', 'lifecycle', 'verification', 'scene_journal')


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
