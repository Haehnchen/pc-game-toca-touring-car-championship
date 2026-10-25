#!/usr/bin/env python3
"""Scenery type identity, source bindings and reproducible canonical metadata."""

import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest


PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from git_game_data import configured_data_root
SPEC = importlib.util.spec_from_file_location("scenery_inventory", PROJECT / "tools/inventory-scenery.py")
INVENTORY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INVENTORY)


def triangle(slot=0, uv=0):
    vertices = [(0, 0, 0, uv, -65536, 0, 0, 65536),
                (65536, 0, 0, 65536, -65536, 0, 0, 65536),
                (0, 65536, 0, 0, 0, 0, 0, 65536)]
    face = struct.pack("<H3Hh4B10x", 0, 0, 1, 2, slot, 255, 255, 255, 255)
    raw = bytes(48) + b"".join(struct.pack("<8i", *v) for v in vertices) + face
    return dict(raw=raw, vertices=vertices, faces=[face])


class TypeIdentityTests(unittest.TestCase):
    def test_global_slot_number_does_not_change_material_identity(self):
        first = INVENTORY.identity(triangle(0), ["12345678", "87654321"])
        moved_slot = INVENTORY.identity(triangle(1), ["87654321", "12345678"])
        self.assertEqual(first, moved_slot)
        self.assertNotEqual(first, INVENTORY.identity(triangle(0), ["87654321"]))
        self.assertNotEqual(first, INVENTORY.identity(triangle(0, uv=1), ["12345678"]))

    def test_name_candidates_route_exclusively_to_their_package(self):
        self.assertEqual(INVENTORY.semantic_package("TRIS04B_1", 5, 0),
                         ("M01d-a", "large-near-tree", "plan-confirmed"))
        self.assertEqual(INVENTORY.semantic_package("TRIS02B_1", 5, 0),
                         ("M01d-b", "near-tree", "plan-confirmed"))
        self.assertEqual(INVENTORY.semantic_package("TRIE01_1", 5, 6),
                         ("M01d-c", "narrow-near-tree", "plan-confirmed"))
        self.assertEqual(INVENTORY.semantic_package("TRGS18_1", 5, 3),
                         ("M01d-d", "tree-group", "plan-confirmed"))
        self.assertEqual(INVENTORY.semantic_package("TRGS22_1", 5, 3),
                         ("M01d-e", "tree-group", "plan-confirmed"))
        self.assertEqual(INVENTORY.semantic_package("TRGS20_1", 5, 3),
                         ("M01d-f", "tree-group", "plan-confirmed"))
        for name in ("CRAN3_1", "LVECTRA_1", "BH17HEL_1", "TH36APLN_1", "CR03BUS2_1"):
            self.assertEqual(INVENTORY.semantic_package(name, 5, 3)[0], "T0")
        self.assertEqual(INVENTORY.semantic_package("CARROW_1", 5, 1),
                         ("T0", "parked-car-row", "source-texture-confirmed"))
        self.assertEqual(INVENTORY.semantic_package("LRED_1", 8, 1), ("M08", "cone", "source-family"))
        self.assertEqual(INVENTORY.semantic_package("LTALLCON_1", 9, 1), ("M08", "post", "source-family"))
        self.assertEqual(INVENTORY.semantic_package("LPIT_1", 10, 1), ("M08", "track-sign", "source-family"))
        self.assertEqual(INVENTORY.semantic_package("GARG_1", 8, 9), ("M05e", "gargoyle", "plan-confirmed"))
        self.assertEqual(INVENTORY.semantic_package("CARROW_1", 5, 3)[0], "M08")
        for track in (1, 3):
            self.assertEqual(INVENTORY.semantic_package("BH17HEL_1", 5, track),
                             ("T0", "static-helicopter", "source-texture-confirmed"))
        for name, track in (("TV_1", 1), ("TV01_1", 2), ("TV_1", 6), ("TV_1", 8)):
            self.assertEqual(INVENTORY.semantic_package(name, 5, track),
                             (f"M07-{track}", "advertising-or-broadcast-mast", "source-texture-confirmed"))
        self.assertEqual(INVENTORY.semantic_package("TH03AIRP_1", 5, 2),
                         ("M07-2", "airport-building", "source-texture-confirmed"))
        for name in ("TH36APLN_1", "TH37APLN_1"):
            self.assertEqual(INVENTORY.semantic_package(name, 5, 2),
                             ("T0", "static-aircraft", "source-texture-confirmed"))
        for track in (2, 4):
            self.assertEqual(INVENTORY.semantic_package("CRAN3_1", 5, track),
                             ("T0", "vehicle-mounted-work-platform", "source-texture-confirmed"))
        self.assertEqual(INVENTORY.semantic_package("LHOTDOG_1", 5, 8),
                         ("M05c", "tent-table-or-stall-candidate", "name-candidate"))
        for track in (3, 6, 7, 8):
            self.assertEqual(INVENTORY.semantic_package("LVECTRA_1", 5, track),
                             ("T0", "promotional-box-truck", "source-texture-confirmed"))
        for name, tracks, kind in (
                ("LVOLVO_1", (3, 6, 7, 8), "transport-box-truck"),
                ("LAMBULAN_1", (3, 6, 7), "rescue-van"),
                ("LFIRETRK_1", (7, 8), "fire-rescue-vehicle"),
                ("LRACJEEP_1", (8,), "track-support-4x4"),
                ("LCAR1_1", (3,), "parked-car-row"),
                ("CR11CVAN_1", (6,), "static-caravan"),
                ("CR03BUS2_1", (6,), "double-decker-bus"),
                ("BH06WT2_1", (6,), "race-team-transporter")):
            for track in tracks:
                self.assertEqual(INVENTORY.semantic_package(name, 5, track),
                                 ("T0", kind, "source-texture-confirmed"))

    def test_flags_unresolved_parent_preserves_previous_actor_flags(self):
        root = dict(parent=-1, flags=0x0100003c, variants=[], matrix=[0] * 16, root=0)
        child = dict(parent=15, flags=1, variants=[2, 3, 4], matrix=[1] * 16, root=0)
        source = [root, child]
        before = copy.deepcopy(source)
        resolved = INVENTORY.materialized_flags(source)
        self.assertEqual(source, before)
        self.assertEqual(len(resolved), 1)
        self.assertEqual((resolved[0]["parent"], resolved[0]["flags"], resolved[0]["source"]), (-1, 0x01000001, 1))
        self.assertEqual(resolved[0]["variants"], [2, 3, 4])

    def test_authored_acceptance_matches_both_package_and_exact_source_type(self):
        rows = [dict(package=package, type=tid, status="source-inventoried")
                for package, tid in (("M01a", "12345678"), ("M01a", "87654321"), ("M01b", "12345678"))]
        document = dict(version=1, packages=[dict(id="M01a", types=["12345678"], status="accepted")])
        INVENTORY.apply_acceptance(rows, document)
        self.assertEqual(rows[0]["status"], "accepted")
        self.assertEqual([row["status"] for row in rows[1:]], ["source-inventoried", "source-inventoried"])


class CanonicalInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = INVENTORY.inventory(configured_data_root())

    def test_all_source_bindings_resolve(self):
        document = self.document
        self.assertEqual(json.loads(INVENTORY.encode(document)), document)
        self.assertEqual(document["coverage"], {
            "trees": {"slots": 15, "ai_slots": 15}, "fence": {"slots": 97, "ai_slots": 93},
            "objects": {"slots": 1090, "ai_slots": 1076}, "cones": {"slots": 11, "ai_slots": 11},
            "posts": {"slots": 9, "ai_slots": 9}, "signs": {"slots": 58, "ai_slots": 58},
            "flags": {"slots": 188, "ai_slots": 158}})
        for row in document["meshes"]:
            resource = document["resources"][document["archives"][row["track"]]["members"][str(row["member"])]]
            for node, variant in row["nodes"]:
                self.assertEqual(resource["nodes"][node][2][variant], row["mesh"])
            self.assertIn(row["type"], document["types"])

    def test_static_fleet_closure_accepts_all_confirmed_types_and_keeps_scalar_source(self):
        document = self.document
        fleet = [row for row in document['meshes'] if row['package'] == 'T0']
        pending = [row for row in fleet if row['status'] != 'accepted']
        self.assertEqual([(row['track'], row['name'], row['type'], row['nodes']) for row in pending],
                         [(6, 'BH06WT2_1', 'd217fa62', [[131, 0], [133, 0], [145, 0], [147, 0]])])
        rows = [row for row in fleet if row['status'] == 'accepted']
        self.assertEqual((len(rows), len({r['type'] for r in rows}), len({r['name'] for r in rows})), (30, 27, 15))
        self.assertTrue(all(row['member'] == 5 and row['status'] == 'accepted' for row in rows))
        material_ids = {mid for row in rows for mid, _ in document['types'][row['type']]['materials']}
        materials = [document['materials'][mid] for mid in material_ids]
        self.assertEqual(len({m['crc32'] for m in materials}), 118)
        self.assertEqual({(m['name'], m['crc32']) for m in materials if m['ai'] is None}, {('D15SHL7', '1c07ca8e')})
        brands = next(row for row in rows if row['track'] == 3 and row['name'] == 'BH17HEL_1')
        self.assertEqual((brands['mesh'], brands['type'], brands['nodes']), (34, '2ae9351e', [[111, 0], [113, 0], [115, 0], [505, 0]]))

    def test_silverstone_inventory_covers_small_props_without_accepting_vehicles(self):
        rows = [row for row in self.document["meshes"] if row["track"] == 1]
        scenery = [row for row in rows if row["package"] != "T0"]
        self.assertEqual((len(rows), len(scenery), sum(len(row["nodes"]) for row in scenery)), (173, 171, 419))
        self.assertEqual({row["name"] for row in rows if row["package"] == "T0"}, {"BH17HEL_1", "CARROW_1"})
        self.assertEqual({member: (sum(row["member"] == member for row in scenery),
                                   sum(len(row["nodes"]) for row in scenery if row["member"] == member))
                          for member in (8, 9, 10)}, {8: (1, 15), 9: (1, 10), 10: (8, 26)})
        self.assertEqual({row["name"] for row in scenery if row["member"] in (8, 9, 10)},
                         {"LRED_1", "LTALLCON_1", "L200_1", "L100_1", "L50_1", "LARROW5_1", "LPIT_1", "LARROW3_1", "LARROW4_1", "LARROW21"})

    def test_confirmed_donington_source_types_keep_scenery_and_vehicles_with_their_owners(self):
        expected = {
            "DA12SPTF_1": ("e8a3b83b", "T0"),
            "DA41TRA_1": ("7653b1bc", "T0"),
            "CRAN3_1": ("2b9bfb4e", "T0"),
            "DA44TRNS_1": ("d08662a7", "M07"),
            "DA80FOSC_1": ("eb461a8f", "M07"),
        }
        rows = [row for row in self.document["meshes"] if row["track"] in (0, 5) and row["name"] in expected]
        self.assertEqual({(row["track"], row["name"]) for row in rows},
                         {(track, name) for track in (0, 5) for name in expected})
        for row in rows:
            source_type, owner = expected[row["name"]]
            self.assertEqual(row["type"], source_type)
            self.assertEqual(row["package"], f'M07-{row["track"]}' if owner == "M07" else owner)
            self.assertEqual(row["evidence"], "source-texture-confirmed")

    def test_pilot_source_has_crossed_planes_shared_donington_type_and_distinct_uv_variants(self):
        document = self.document
        pilots = {row["track"]: row for row in document["meshes"] if row["name"] == "TRIS04_1"}
        self.assertEqual(set(pilots), set(range(9)))
        self.assertEqual([len(pilots[i]["nodes"]) for i in range(9)], [28, 4, 2, 40, 114, 28, 13, 1, 5])
        self.assertEqual(pilots[0]["type"], pilots[5]["type"])
        self.assertEqual(pilots[0]["source_crc32"], "fe593fe9")
        self.assertEqual(len({row["type"] for row in pilots.values()}), 8)
        for track, row in pilots.items():
            info = document["types"][row["type"]]
            self.assertEqual((info["vertices"], info["triangles"], info["shape"]["kind"]), (20, 16, "crossed-planes"))
            self.assertEqual(info["bounds_16_16"][2], [-712156, -91])
            self.assertEqual(info["uv_16_16"][1], [-65536, 0] if track == 3 else [-65535, 1])
        snet = document["resources"][document["archives"][8]["members"]["5"]]
        for node, _ in pilots[8]["nodes"]:
            self.assertEqual(snet["nodes"][snet["nodes"][node][4]][1] & 0x20000000, 0)


if __name__ == "__main__":
    unittest.main()
