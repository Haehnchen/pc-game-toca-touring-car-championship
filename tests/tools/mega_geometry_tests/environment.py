"""Versioned CM3D model tools used by the scenery fixtures."""


from pathlib import Path
import sys

PROJECT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PROJECT / 'tools'))
import mega_geometry as detail

import vehicle_model as MODEL
