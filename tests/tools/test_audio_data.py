"""Check audio archive extraction without an original installation or encoder."""

import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/unpack-audio-assets.py"
SPEC = importlib.util.spec_from_file_location("toca_test_audio_data", TOOL)
DATA = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DATA)


@unittest.skipUnless(shutil.which("zstd"), "zstd is required for the committed audio packs")
class AudioDataTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.assets = self.root / "audio"
        self.assets.mkdir()
        self.output = self.root / "generated/audio"
        self.bundle = self.root / "runtime/original-data"

    def compress(self, payload, name="example.tar.zst"):
        result = subprocess.run(["zstd", "-q", "-c"], input=payload, capture_output=True, check=True)
        (self.assets / name).write_bytes(result.stdout)

    def archive(self, members, name="example.tar.zst"):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w") as archive:
            for member, payload in members:
                archive.addfile(member, io.BytesIO(payload) if payload is not None else None)
        self.compress(stream.getvalue(), name)

    def file(self, name, payload):
        member = tarfile.TarInfo(name)
        member.size = len(payload)
        return member, payload

    def test_source_independent_cli_extracts_and_bundles_committed_archives(self):
        source = ROOT / "assets/audio"
        result = subprocess.run([sys.executable, "-B", str(TOOL), "--source-dir", str(source),
                                 "--output-dir", str(self.output), "--bundle-dir", str(self.bundle)],
                                cwd=self.root, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = set()
        for package in source.glob("*.tar.zst"):
            with tempfile.TemporaryFile() as decoded:
                subprocess.run(["zstd", "-q", "-d", "-c", "--", str(package)], stdout=decoded, check=True)
                decoded.seek(0)
                with tarfile.open(fileobj=decoded, mode="r:") as archive:
                    for member in archive:
                        if member.isfile():
                            expected.add(member.name)
                            payload = archive.extractfile(member).read()
                            self.assertEqual((self.output / member.name).read_bytes(), payload)
                            self.assertEqual((self.bundle / member.name).read_bytes(), payload)
        self.assertTrue(expected)
        for root in (self.output, self.bundle):
            self.assertEqual({path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}, expected)

    def test_discovers_packages_and_uses_archive_paths_without_an_inventory(self):
        directory = tarfile.TarInfo("Some directory/")
        directory.type = tarfile.DIRTYPE
        expected = {"Some directory/custom.raw": b"arbitrary audio", "extra/new.ogg": b"another payload"}
        self.archive([(directory, None), self.file("Some directory/custom.raw", expected["Some directory/custom.raw"])])
        self.archive([self.file("extra/new.ogg", expected["extra/new.ogg"])], "another.tar.zst")
        paths = DATA.unpack(self.assets, self.output, bundle_dir=self.bundle)
        self.assertEqual(paths, sorted(Path(name) for name in expected))
        for name, payload in expected.items():
            self.assertEqual((self.output / name).read_bytes(), payload)
            self.assertEqual((self.bundle / name).read_bytes(), payload)

    def test_decoder_and_tar_errors_are_reported(self):
        package = self.assets / "broken.tar.zst"
        package.write_bytes(b"not a zstd stream")
        with self.assertRaisesRegex(DATA.AudioDataError, "Cannot decode"):
            DATA.unpack(self.assets, self.output)
        self.compress(b"not a tar archive", package.name)
        with self.assertRaisesRegex(DATA.AudioDataError, "Cannot unpack"):
            DATA.unpack(self.assets, self.output)
        with self.assertRaisesRegex(DATA.AudioDataError, "Cannot unpack"):
            DATA.unpack(self.assets, self.output, zstd="decoder-does-not-exist")

    def test_archive_paths_cannot_escape_the_output(self):
        for name in ("../escape.ogg", "/absolute.ogg", "nested/../../escape.ogg", "C:/escape.ogg", "..\\escape.ogg"):
            with self.subTest(name=name):
                self.archive([self.file(name, b"escape")])
                with self.assertRaisesRegex(DATA.AudioDataError, "Unsafe audio archive path"):
                    DATA.unpack(self.assets, self.output)
                self.assertFalse((self.root / "escape.ogg").exists())

    def test_archive_links_and_special_files_are_rejected(self):
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.CHRTYPE):
            with self.subTest(kind=kind):
                member = tarfile.TarInfo("link.ogg")
                member.type = kind
                member.linkname = "../outside"
                self.archive([(member, None)])
                with self.assertRaisesRegex(DATA.AudioDataError, "Unsupported audio archive member"):
                    DATA.unpack(self.assets, self.output)

    def test_output_and_bundle_symlinks_cannot_redirect_extraction(self):
        self.archive([self.file("nested/cue.ogg", b"audio")])
        outside = self.root / "outside"
        outside.mkdir()
        for root in (self.output, self.bundle):
            with self.subTest(root=root):
                root.mkdir(parents=True, exist_ok=True)
                (root / "nested").symlink_to(outside, target_is_directory=True)
                with self.assertRaisesRegex(DATA.AudioDataError, "symlinks"):
                    DATA.unpack(self.assets, self.output, bundle_dir=self.bundle)
                self.assertEqual(list(outside.iterdir()), [])
                (root / "nested").unlink()


if __name__ == "__main__":
    unittest.main(verbosity=2)
