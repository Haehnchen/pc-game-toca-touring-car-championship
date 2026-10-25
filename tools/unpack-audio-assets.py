#!/usr/bin/env python3
"""Unpack the shipped Vorbis audio archives without re-encoding."""

import argparse
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import shutil
import subprocess
import sys
import tarfile
import tempfile


class AudioDataError(ValueError):
    """An audio archive could not be decoded or safely extracted."""


def no_symlinks(path):
    path = Path(os.path.abspath(path))
    for part in (path, *path.parents):
        if part.is_symlink():
            raise AudioDataError(f"Audio asset path must not contain symlinks: {part}")
    return path


def member_path(root, name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or PureWindowsPath(name).drive:
        raise AudioDataError(f"Unsafe audio archive path: {name}")
    return no_symlinks(root / path)


def unpack(source_dir, output_dir, zstd="zstd", bundle_dir=None):
    """Extract *.tar.zst archives and return their file paths relative to output_dir."""
    source = no_symlinks(source_dir)
    output = no_symlinks(output_dir)
    bundle = no_symlinks(bundle_dir) if bundle_dir is not None else None
    produced = set()
    try:
        if not source.is_dir():
            raise AudioDataError(f"Audio source directory does not exist: {source}")
        output.mkdir(parents=True, exist_ok=True)
        for package in sorted(source.glob("*.tar.zst")):
            no_symlinks(package)
            with tempfile.TemporaryFile() as decoded:
                result = subprocess.run([str(zstd), "-q", "-d", "-c", "--", str(package)],
                                        stdout=decoded, stderr=subprocess.PIPE, check=False)
                if result.returncode:
                    raise AudioDataError(f"Cannot decode audio pack {package}: "
                                         f"{result.stderr.decode(errors='replace').strip()}")
                decoded.seek(0)
                with tarfile.open(fileobj=decoded, mode="r:") as archive:
                    for member in archive:
                        target = member_path(output, member.name)
                        if member.isdir():
                            target.mkdir(parents=True, exist_ok=True)
                            continue
                        if not member.isfile():
                            raise AudioDataError(f"Unsupported audio archive member: {member.name}")
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with archive.extractfile(member) as payload, target.open("wb") as destination:
                            shutil.copyfileobj(payload, destination)
                        produced.add(target.relative_to(output))
        paths = sorted(produced)
        if bundle is not None and bundle != output:
            for path in paths:
                target = member_path(bundle, path.as_posix())
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(output / path, target)
        return paths
    except (OSError, tarfile.TarError) as error:
        raise AudioDataError(f"Cannot unpack audio assets: {error}") from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--zstd", default="zstd")
    parser.add_argument("--bundle-dir", type=Path)
    arguments = parser.parse_args()
    try:
        paths = unpack(arguments.source_dir, arguments.output_dir, arguments.zstd, arguments.bundle_dir)
    except AudioDataError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Unpacked {len(paths)} audio files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
