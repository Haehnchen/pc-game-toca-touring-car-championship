#!/usr/bin/env python3
"""Final vehicle artwork: one slot input, original alpha and exact WebP bytes."""

import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import vehicle_textures as textures
import vehicle_glb
import vehicle_manifest
import vehicle_model


def record(name, data):
    return name.encode().ljust(12, b"\0") + struct.pack("<I", len(data)) + data


class WebPContainerTests(unittest.TestCase):
    @staticmethod
    def chunk(kind, payload):
        return kind + struct.pack('<I', len(payload)) + payload + bytes(len(payload) % 2)

    @staticmethod
    def container(*chunks):
        payload = b'WEBP' + b''.join(chunks)
        return b'RIFF' + struct.pack('<I', len(payload)) + payload

    def test_static_structure_rejects_ambiguous_or_invalid_image_headers(self):
        frame = b'\0\0\0\x9d\x01\x2a' + struct.pack('<HH', 16, 8)
        image = self.chunk(b'VP8 ', frame)
        canvas = self.chunk(b'VP8X', bytes(4) + (15).to_bytes(3, 'little') + (7).to_bytes(3, 'little'))
        self.assertEqual(textures.webp_dimensions(self.container(canvas, image)), (16, 8))
        malformed = {
            'duplicate images': self.container(image, image),
            'interframe': self.container(self.chunk(b'VP8 ', b'\1' + frame[1:])),
            'duplicate canvas': self.container(canvas, canvas, image),
            'late canvas': self.container(image, canvas),
            'animation': self.container(self.chunk(b'ANIM', bytes(6)), image),
            'missing image': self.container(canvas),
            'zero width': self.container(self.chunk(b'VP8 ', frame[:6] + struct.pack('<HH', 0, 8))),
            'truncated chunk': self.container(image[:-1]),
        }
        for name, payload in malformed.items():
            with self.subTest(name=name), self.assertRaises(ValueError):
                textures.webp_dimensions(payload)

    def test_shared_parser_applies_caller_budgets_and_codec_policy(self):
        frame = b'\0\0\0\x9d\x01\x2a' + struct.pack('<HH', 1024, 8)
        image = self.container(self.chunk(b'VP8 ', frame))
        self.assertEqual(textures.webp_dimensions(image), (1024, 8))
        with self.assertRaisesRegex(ValueError, 'at most512'):
            textures.webp_dimensions(image, max_dimension=512)
        with self.assertRaises(ValueError):
            textures.webp_dimensions(image, max_bytes=len(image) - 1)
        lossless = self.container(self.chunk(b'VP8L', b'\x2f' + struct.pack('<I', 15 | (7 << 14))))
        self.assertEqual(textures.webp_dimensions(lossless), (16, 8))
        with self.assertRaisesRegex(ValueError, 'lossy WebP'):
            textures.webp_dimensions(lossless, lossy_only=True)


@unittest.skipUnless(shutil.which("magick") or shutil.which("convert"), "ImageMagick is unavailable")
class VehicleTextureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bmp = textures.rgb_bmp(2, 2, [(0, 0, 0), (8, 8, 8), (255, 220, 0), (255, 255, 255)])
        cls.webp = textures.webp_texture(cls.bmp)
        cls.source_rgba = bytes((12, 34, 56, 0, 8, 8, 8, 255, 255, 220, 0, 255, 255, 255, 255, 255))
        converter = shutil.which("magick") or shutil.which("convert")
        cls.lossless = subprocess.run(
            [converter, "-size", "2x2", "-depth", "8", "rgba:-", "-define", "webp:lossless=true",
             "-define", "webp:exact=true", "-define", "webp:method=6", "webp:-"],
            input=cls.source_rgba, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout

    def test_lossless_work_source_enters_preparation_with_exact_rgb(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "work").mkdir()
            (root / "work/PAINT.webp").write_bytes(self.lossless)
            calls = []
            def prepare(name, original, source):
                calls.append(name)
                self.assertEqual(original, self.bmp)
                self.assertEqual(source, root / "work/PAINT.webp")
                width, height, pixels = textures.bmp_pixels(textures.generated_bmp(source))
                self.assertEqual((width, height), (2, 2))
                self.assertEqual(pixels, [(12, 34, 56), (8, 8, 8), (255, 220, 0), (255, 255, 255)])
                return self.webp
            output = textures.replace_textures(record("PAINT", self.bmp), 1, root, prepare=prepare)
            self.assertEqual(calls, ["PAINT"])
            self.assertEqual(textures.texture_records(output, 1)[0][2], self.webp)

    def test_source_callback_skips_discarded_bmp_conversion_and_validates_bounds(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "work").mkdir()
            source = root / "work/PAINT.webp"
            source.write_bytes(self.lossless)
            def prepare(name, original, path):
                self.assertEqual(path, source)
                self.assertEqual(path.read_bytes(), self.lossless)
                return self.webp
            with patch.object(textures, "generated_bmp", side_effect=AssertionError("Unused BMP conversion")):
                output = textures.replace_textures(record("PAINT", self.bmp), 1, root, prepare=prepare)
            self.assertEqual(textures.texture_records(output, 1)[0][2], self.webp)
            with self.assertRaisesRegex(ValueError, "aspect ratio"):
                textures.replace_textures(record("PAINT", self.bmp), 1, root,
                                          prepare=lambda *args: textures.rgb_bmp(4, 2, [(1, 2, 3)] * 8))
            source.unlink()
            # A bounded header check must reject oversized PNGs before invoking
            # the consumer's decoder, even when the callback is custom.
            (root / "PAINT.png").write_bytes(b"\x89PNG\r\n\x1a\n\0\0\0\rIHDR" + struct.pack(">II", 4096, 4096))
            with patch.object(textures, "generated_bmp", side_effect=AssertionError("Unexpected decode")):
                with self.assertRaisesRegex(ValueError, "2048"):
                    textures.replace_textures(record("PAINT", self.bmp), 1, root,
                                              prepare=lambda *args: self.fail("Unbounded source reached decoder"))

    def test_shared_finalization_preserves_provenance_quality_and_verified_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "textures"
            (package / "work").mkdir(parents=True)
            (package / "metadata.json").write_text('{"final_dimensions":{"PAINT":[2,2]}}')
            master = package / "work/PAINT.webp"
            master.write_bytes(self.lossless)
            provenance = {"source_note": "preserve original provenance", "assets": [
                {"file": "work/PAINT.webp", "crc32": textures.crc32(self.lossless)}]}
            path = package / "provenance.json"
            path.write_text(json.dumps(provenance))
            entry = {"key": "fixture", "texture_directory": "textures", "source": {}}
            qualities, staged = [], []
            def generate(source, definition, working, *, quality):
                qualities.append(quality)
                self.assertEqual((working / "work/PAINT.webp").read_bytes(), self.lossless)
                header = bytearray(32)
                struct.pack_into("<4sII", header, 0, b"CM3D", 100, 32)
                struct.pack_into("<H", header, 16, 1)
                return vehicle_model.pack(header, b"", [], record("PAINT", self.webp)), {}
            def stage(source, destination):
                staged.append(source.name)
                shutil.copyfile(source, destination)
            with (patch.object(vehicle_model, "PROJECT", root),
                  patch.object(vehicle_model, "read_source", return_value=b"canonical model"),
                  patch.object(vehicle_manifest, "load_definition", return_value={})):
                report = textures.finalize_artwork(entry, generate, stage_source=stage)
                self.assertEqual(qualities, [70])
                self.assertEqual(staged, ["PAINT.webp"])
                self.assertEqual((package / "PAINT.webp").read_bytes(), self.webp)
                finalized = json.loads(path.read_text())
                self.assertEqual(finalized["source_note"], provenance["source_note"])
                self.assertEqual(finalized["assets"], provenance["assets"])
                # Existing packages may retain their historical quality80 recipe.
                finalized["finalization"]["quality"] = 80
                path.write_text(json.dumps(finalized))
                before = {p.relative_to(package): p.read_bytes() for p in package.rglob("*") if p.is_file()}
                verified = textures.finalize_artwork(entry, generate, verify=True)
                self.assertTrue(verified["verified"])
                self.assertEqual(qualities, [70, 80])
                self.assertEqual(before, {p.relative_to(package): p.read_bytes() for p in package.rglob("*") if p.is_file()})
                master.write_bytes(b"corrupt master")
                with self.assertRaisesRegex(ValueError, "work-source path/CRC mismatch"):
                    textures.finalize_artwork(entry, generate, verify=True)
                self.assertEqual(qualities, [70, 80])

    def test_finished_export_takes_precedence_over_work_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "work").mkdir()
            (root / "work/PAINT.webp").write_bytes(self.lossless)
            (root / "PAINT.webp").write_bytes(self.webp)
            with patch.object(textures.subprocess, "run", side_effect=AssertionError("Final export was re-encoded")):
                output = textures.replace_textures(record("PAINT", self.bmp), 1, root)
            self.assertEqual(textures.texture_records(output, 1)[0][2], self.webp)

    def test_work_sources_require_lossless_webp_and_stay_inside_the_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work = root / "work"
            work.mkdir()
            path = work / "PAINT.webp"
            path.write_bytes(self.webp)
            with self.assertRaisesRegex(ValueError, "lossless WebP"):
                textures.replace_textures(record("PAINT", self.bmp), 1, root)
            path.unlink()
            (work / "PAINT.png").write_bytes(self.bmp)
            with self.assertRaisesRegex(ValueError, "lossless WebP"):
                textures.replace_textures(record("PAINT", self.bmp), 1, root)
            (work / "PAINT.png").unlink()
            outside = root.parent / (root.name + "-outside.webp")
            try:
                outside.write_bytes(self.lossless)
                path.symlink_to(outside)
                with self.assertRaisesRegex(ValueError, "escapes"):
                    textures.replace_textures(record("PAINT", self.bmp), 1, root)
            finally:
                outside.unlink(missing_ok=True)

    def test_final_webp_bypasses_masking_and_lossy_encoding(self):
        original = record("PAINT", self.bmp) + record("GLASS", self.bmp)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "PAINT.webp").write_bytes(self.webp)
            def unexpected(*args, **kwargs):
                self.fail("Final WebP must bypass both preparation and subprocess conversion")
            with patch.object(textures.subprocess, "run", unexpected):
                replaced = textures.replace_textures(original, 2, root, prepare=unexpected)
                records = textures.texture_records(replaced, 2)
                self.assertEqual(records[0][2], self.webp)
                self.assertEqual(records[1][2], self.bmp)
                self.assertIs(textures.webp_texture(records[0][2]), records[0][2])

    def test_glb_embeds_final_payload_without_reencoding(self):
        header = bytearray(32)
        struct.pack_into("<H", header, 16, 1)
        with patch.object(textures.subprocess, "run", side_effect=AssertionError("Unexpected re-encode")):
            glb, stats = vehicle_glb.encode(header, b"", [], record("PAINT", self.webp))
        self.assertIn(self.webp, glb)
        self.assertEqual(stats["embedded_webp_bytes"], len(self.webp))

    def test_original_duplicate_slots_survive_and_only_ambiguous_overrides_fail(self):
        other = textures.rgb_bmp(2, 2, [(255, 255, 255)] * 4)
        repeated = record("PAINT", self.bmp) * 2
        distinct = record("PAINT", self.bmp) + record("PAINT", other)
        self.assertEqual(len(textures.texture_records(distinct, 2)), 2)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertEqual(textures.replace_textures(distinct, 2, root), distinct)
            (root / "PAINT.webp").write_bytes(self.webp)
            replaced = textures.texture_records(textures.replace_textures(repeated, 2, root), 2)
            self.assertEqual([payload for _, _, payload in replaced], [self.webp, self.webp])
            with self.assertRaisesRegex(ValueError, "Ambiguous replacement"):
                textures.replace_textures(distinct, 2, root)

    def test_all_input_format_conflicts_and_unknown_slots_are_rejected(self):
        for suffix in (".bmp", ".png"):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / "PAINT.webp").write_bytes(self.webp)
                (root / ("PAINT" + suffix)).write_bytes(self.bmp)
                with self.assertRaisesRegex(ValueError, "one generated image"):
                    textures.replace_textures(record("PAINT", self.bmp), 1, root)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("OTHER.webp", "paint.webp", "PAINT.WEBP"):
                path = root / name
                path.write_bytes(self.webp)
                with self.assertRaisesRegex(ValueError, "Unknown vehicle"):
                    textures.replace_textures(record("PAINT", self.bmp), 1, root)
                path.unlink()

    def test_final_webp_requires_matching_aspect_and_local_regular_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "PAINT.webp").write_bytes(self.webp)
            original = textures.rgb_bmp(4, 2, [(255, 255, 255)] * 8)
            with self.assertRaisesRegex(ValueError, "aspect ratio"):
                textures.replace_textures(record("PAINT", original), 1, root)
            outside = root / "working"
            outside.mkdir()
            (outside / "GLASS.webp").symlink_to(root / "PAINT.webp")
            with self.assertRaisesRegex(ValueError, "escapes"):
                textures.replace_textures(record("GLASS", self.bmp), 1, outside)

    def test_webp_container_bounds_and_dimensions_are_checked(self):
        self.assertEqual(textures.webp_dimensions(self.webp), (2, 2))
        for payload in (self.webp[:-1], self.webp + b"extra", b"RIFF" + bytes(40)):
            with self.subTest(payload=payload[:12]), self.assertRaises(ValueError):
                textures.webp_dimensions(payload)

    def test_alpha_survives_final_input_with_black_number_ink_opaque(self):
        converter = shutil.which("magick") or shutil.which("convert")
        # Encode explicitly opaque black ink alongside fully transparent black.
        rgba = bytes((0, 0, 0, 255, 0, 0, 0, 0, 255, 255, 255, 255, 255, 220, 0, 255))
        final = subprocess.run([converter, "-size", "2x2", "-depth", "8", "rgba:-", "-quality", "70", "webp:-"],
                               input=rgba, stdout=subprocess.PIPE, check=True).stdout
        self.assertEqual(textures.webp_texture(final), final)
        decoded = subprocess.run([converter, "webp:-", "-depth", "8", "rgba:-"], input=final,
                                 stdout=subprocess.PIPE, check=True).stdout
        self.assertEqual(list(decoded[3::4]), [255, 0, 255, 255])


if __name__ == "__main__":
    unittest.main()
