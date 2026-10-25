#!/usr/bin/env python3
"""Unpack the bundled original1.1a non-audio BFP assets."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


class GameDataError(ValueError):
    """The source or destination is unsafe, or an archive cannot be decoded."""


def require(condition, message):
    if not condition:
        raise GameDataError(message)


def no_symlinks(path):
    path = Path(os.path.abspath(path))
    for part in (path, *path.parents):
        require(not part.is_symlink(), f"Game data path must not contain symlinks: {part}")
    return path


def unpack(source_dir, output_dir, zstd="zstd"):
    """Decode sorted *.bfp.zst inputs and return their output paths."""
    source_dir = no_symlinks(source_dir)
    require(source_dir.is_dir(), f"Game data source is not a directory: {source_dir}")
    output = no_symlinks(output_dir)
    for forbidden in (ROOT / "references", ROOT / "assets", source_dir):
        require(not output.is_relative_to(forbidden), f"Game data output must stay outside {forbidden}")
    require(not source_dir.is_relative_to(output), "Game data output must not contain its source directory")
    sources = []
    for source in sorted(source_dir.glob("*.bfp.zst")):
        source = no_symlinks(source)
        require(source.is_file(), f"Game data source is not a regular file: {source}")
        target = no_symlinks(output / "data" / source.stem)
        require(not target.exists() or target.is_file(), f"Game data output is not a regular file: {target}")
        sources.append((source, target))
    for source, target in sources:
        temporary = None
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(prefix="." + target.name + ".", dir=target.parent, delete=False) as stream:
                temporary = Path(stream.name)
                result = subprocess.run([str(zstd), "-q", "-d", "-c", "--", str(source)],
                                        stdout=stream, stderr=subprocess.PIPE, check=False)
            require(result.returncode == 0, f"Cannot decode game data {source}: {result.stderr.decode(errors='replace').strip()}")
            temporary.chmod(0o644)
            os.replace(temporary, target)
        except OSError as error:
            raise GameDataError(f"Cannot unpack game data {source}: {error}") from error
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    return [target for _, target in sources]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--zstd", default="zstd")
    arguments = parser.parse_args()
    try:
        files = unpack(arguments.source_dir, arguments.output_dir, arguments.zstd)
    except (GameDataError, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Unpacked {len(files)} bundled original1.1a game data archives")
    return 0


if __name__ == "__main__":
    sys.exit(main())
