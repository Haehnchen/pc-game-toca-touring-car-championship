"""Read only versioned tools/assets; a reference installation is never a fixture."""

from pathlib import Path
import sys

PROJECT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PROJECT / "tools"))
import mega_vegetation as vegetation
from git_game_data import archive_bytes, configured_data_root
