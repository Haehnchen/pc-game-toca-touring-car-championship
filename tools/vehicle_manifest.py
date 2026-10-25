"""Validate the registered canonical vehicle sources and runtime destinations."""

import json
from pathlib import Path, PurePosixPath
import re

from vehicle_model import PROJECT

DEFAULT_MANIFEST = PROJECT / "assets/models/mega/vehicles.json"
HIGH_MEMBERS = (1, 4, 7, 13, 16, 19, 22, 25, 28, 31, 10)
PHYSICAL_IDS = (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 2)
HIGH_CRC32 = ("3562e854", "1fd3403d", "943e9422", "c577ebac", "eda6480d", "73b8aae9",
              "c6af623c", "e0fffe5d", "3d3ea7c9", "3234277d", "e0ce56fe")


def relative_path(value, prefix):
    if not isinstance(value, str) or "\\" in value or ":" in value:
        raise ValueError("Manifest paths must be safe relative POSIX paths")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or path.as_posix() != value or not value.startswith(prefix + "/"):
        raise ValueError(f"Manifest path must stay below {prefix}/")
    resolved = (PROJECT / value).resolve()
    if not resolved.is_relative_to(PROJECT / prefix):
        raise ValueError("Manifest path escapes its managed directory")
    return resolved


def validate_manifest(document):
    if not isinstance(document, dict) or document.get("version") != 1 or not isinstance(document.get("vehicles"), list):
        raise ValueError("Expected vehicle manifest version 1")
    keys, outputs, render_ids = set(), set(), set()
    for entry in document["vehicles"]:
        if not isinstance(entry, dict):
            raise ValueError("Vehicle entry must be an object")
        key, render_id, physical_id = (entry.get(field) for field in ("key", "render_id", "physical_id"))
        if not isinstance(key, str) or re.fullmatch(r"[a-z][a-z0-9-]*", key) is None or key in keys:
            raise ValueError("Vehicle keys must be unique lowercase identifiers")
        if type(render_id) is not int or render_id not in range(11) or render_id in render_ids:
            raise ValueError("Vehicle render IDs must be unique canonical IDs 0..10")
        if type(physical_id) is not int or physical_id != PHYSICAL_IDS[render_id]:
            raise ValueError("Physical ID does not match the canonical render family")
        if entry.get("mode") not in ("mega", "reference"):
            raise ValueError("Vehicle mode must be mega or reference")
        output = entry.get("output")
        relative_path(output, "assets/models/mega")
        if output != f"assets/models/mega/car-{render_id}.glb" or output in outputs:
            raise ValueError("Vehicle output must uniquely match its canonical render ID")
        relative_path(entry.get("definition"), f"assets/models/vehicles/{key}")
        if entry.get("texture_directory") is not None:
            relative_path(entry["texture_directory"], f"assets/models/vehicles/{key}")
        source = entry.get("source")
        if not isinstance(source, dict):
            raise ValueError("Vehicle source must describe its canonical archive member")
        if source.get("archive") != "data/allcars.bfp" or source.get("archive_crc32") != "92273ec9":
            raise ValueError("Only the unchanged canonical 1.1a ALLCARS archive is permitted")
        if type(source.get("member")) is not int or source["member"] != HIGH_MEMBERS[render_id]:
            raise ValueError("Source member does not match the canonical render ID")
        if source.get("member_crc32") != HIGH_CRC32[render_id]:
            raise ValueError("Source member checksum does not match the canonical render ID")
        name = source.get("member_name")
        if not isinstance(name, str) or not name or len(name.encode("ascii")) > 16:
            raise ValueError("Expected the complete stored BFP member name")
        keys.add(key)
        outputs.add(output)
        render_ids.add(render_id)
    return document


def load_manifest(path=DEFAULT_MANIFEST):
    return validate_manifest(json.loads(Path(path).read_text(encoding="utf-8")))


def load_definition(entry):
    path = relative_path(entry["definition"], f"assets/models/vehicles/{entry['key']}")
    data = path.read_bytes()
    definition = json.loads(data)
    geometry = definition.get("geometry")
    if geometry not in ("renault", "honda", "audi", "vectra", "volvo", "ford", "nissan", "peugeot", "tank", "flexmobile", "source") or (entry["mode"] == "reference") != (geometry == "source"):
        raise ValueError("Definition geometry must match its manifest mode")
    if geometry == "renault" and (entry["key"] != "renault" or entry["render_id"] != 7):
        raise ValueError("Renault geometry requires the canonical Renault source")
    if geometry == "honda" and (entry["key"] != "honda" or entry["render_id"] != 0):
        raise ValueError("Honda geometry requires the canonical Honda source")
    if geometry == "audi" and (entry["key"] != "audi" or entry["render_id"] != 1):
        raise ValueError("Audi geometry requires the canonical Audi source")
    if geometry == "vectra" and (entry["key"], entry["render_id"]) not in (("vectra", 2), ("vectra2", 10)):
        raise ValueError("Vectra geometry requires the canonical Vectra source")
    if geometry == "volvo" and (entry["key"] != "volvo" or entry["render_id"] != 3):
        raise ValueError("Volvo geometry requires the canonical Volvo source")
    if geometry == "ford" and (entry["key"] != "ford" or entry["render_id"] != 4):
        raise ValueError("Ford geometry requires the canonical Ford source")
    if geometry == "nissan" and (entry["key"] != "nissan" or entry["render_id"] != 5):
        raise ValueError("Nissan geometry requires the canonical Nissan source")
    if geometry == "peugeot" and (entry["key"] != "peugeot" or entry["render_id"] != 6):
        raise ValueError("Peugeot geometry requires the canonical Peugeot source")
    if geometry == "tank" and (entry["key"] != "tank" or entry["render_id"] != 8):
        raise ValueError("Tank geometry requires the canonical Tank source")
    if geometry == "flexmobile" and (entry["key"] != "flexmobile" or entry["render_id"] != 9):
        raise ValueError("Flexmobile geometry requires the canonical Flexmobile source")
    return definition
