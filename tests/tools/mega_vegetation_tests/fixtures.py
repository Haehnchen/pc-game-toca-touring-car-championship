"""Fresh geometry for each test and one canonical-data loader per class."""

import importlib.util
import unittest

from .environment import PROJECT, configured_data_root, vegetation
from .geometry import Mesh


class VegetationCase(unittest.TestCase):
    def setUp(self):
        self.source = self.source_factory()
        if self.source_name is not None:
            self.source.header = self.source_name.encode("ascii").ljust(48, b"\0")
        self.result, self.stats = vegetation.enhance(
            self.source, Mesh, [self.texture_name], 1, foliage_index=self.foliage_slot)

    source_name = None


class CanonicalDataCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data_root = configured_data_root()
        spec = importlib.util.spec_from_file_location("scenery_inventory", PROJECT / "tools/inventory-scenery.py")
        cls.inventory = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.inventory)
