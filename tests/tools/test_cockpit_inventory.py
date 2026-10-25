#!/usr/bin/env python3
"""Cockpit identities distinguish complete payloads, UVs and decoded alpha."""

import importlib.util
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
from git_game_data import configured_data_root
SPEC = importlib.util.spec_from_file_location('cockpit_inventory', PROJECT / 'tools/inventory-cockpits.py')
inventory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory)


class CockpitMetadataTests(unittest.TestCase):
    def test_black_key_uses_decoded_color_and_payload_padding_is_not_an_image_alias_gate(self):
        bmp = bytearray(66)
        struct.pack_into('<2sI4xI', bmp, 0, b'BM', len(bmp), 62)
        struct.pack_into('<IiiHHII', bmp, 14, 40, 2, 1, 1, 8, 0, 4)
        struct.pack_into('<I', bmp, 46, 2)
        bmp[54:62] = bytes((8, 0, 0, 0, 0, 0, 0, 0))
        bmp[62:66] = bytes((0, 1, 0, 0))
        original = inventory.bitmap_metadata(bytes(bmp))
        self.assertEqual((original['opaque_pixels'], original['transparent_pixels']), (1, 1))
        changed = inventory.bitmap_metadata(bytes(bmp) + b'\0\0')
        self.assertNotEqual(original['bmp_crc32'], changed['bmp_crc32'])
        self.assertEqual(original['decoded_rgba_crc32'], changed['decoded_rgba_crc32'])
        self.assertEqual(original['alpha_mask_crc32'], changed['alpha_mask_crc32'])

    def test_bfp_labels_end_at_first_null_even_with_original_exporter_padding(self):
        directory = b''.join(struct.pack('<16sII', b'cabin\\dash.c3d\0d', i, 1) for i in range(5))
        raw = struct.pack('<III', 5, 120, 0) + directory + b'abcde'
        self.assertEqual(inventory.archive_members(raw), [('cabin\\dash.c3d', bytes((c,))) for c in b'abcde'])


class CanonicalCockpitInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = inventory.inventory(configured_data_root())

    def test_nine_archives_and_all_five_roles_resolve_shared_metadata(self):
        d = self.document
        self.assertEqual(d['summary'], {'archives': 9, 'members': 45, 'unique_complete_payloads': 29, 'dashboard_xyz_groups': 2})
        self.assertEqual({a['car_id'] for a in d['archives']}, {0, 1, 2, 3, 4, 5, 6, 7, 9})
        for archive in d['archives']:
            self.assertEqual([r['role'] for r in archive['members']], list(inventory.ROLES))
            for member in archive['members']:
                model = d['models'][member['model']]
                for mesh in model['meshes']:
                    record = d['mesh_records'][mesh['record']]
                    self.assertEqual(len(record['vertex_uvs_16_16']), record['vertices'])
                    for ni, vi in mesh['bindings']:
                        self.assertEqual(model['nodes'][ni]['variants'][vi], mesh['mesh'])
                for texture in model['textures']:
                    self.assertIn(texture['bmp_crc32'], d['bitmaps'])

    def test_driver_sides_exact_reuse_and_nissan_uv_exception_are_separate_contracts(self):
        d = self.document
        self.assertEqual({a['car_id'] for a in d['archives'] if a['source_side_flag'] == 1}, {1, 2, 4, 7})
        for archive in d['archives']:
            self.assertEqual(archive['driver_side'], 'left' if archive['source_side_flag'] else 'right')
            for index, checksum in ((3, 'a958a105'), (4, 'bb153bcc')):
                self.assertEqual(archive['members'][index]['model'], checksum)
        dashboards = {a['car_id']: d['models'][a['members'][2]['model']] for a in d['archives']}
        self.assertEqual({x['mesh_section_crc32'] for i, x in dashboards.items() if i in (0, 3, 6, 9)}, {'0380884d'})
        self.assertEqual({x['mesh_section_crc32'] for i, x in dashboards.items() if i in (1, 2, 4, 7)}, {'7eb32f8a'})
        self.assertEqual(dashboards[5]['xyz_crc32'], dashboards[0]['xyz_crc32'])
        self.assertEqual(dashboards[5]['normal_crc32'], dashboards[0]['normal_crc32'])
        self.assertNotEqual(dashboards[5]['uv_crc32'], dashboards[0]['uv_crc32'])
        self.assertEqual(len({x['texture_section_crc32'] for x in dashboards.values()}), 9)
        honda = dashboards[0]
        self.assertEqual([d['bitmaps'][t['bmp_crc32']]['transparent_pixels'] for t in honda['textures']], [0, 0, 0, 0])


if __name__ == '__main__':
    unittest.main()
