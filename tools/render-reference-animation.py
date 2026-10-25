#!/usr/bin/env python3
"""Read published original scene animation without advancing its controllers."""

import math
import struct

FAMILIES = ("moving", "rotorcraft", "cones", "posts", "signs")
MAX_NODES = 4096  # 47f260's 005a7ba0..005abba0 actor registry.
MAX_FLAGS = 40
MIN_Q16, MAX_Q16 = -(1 << 31), (1 << 31) - 1


def signed_matrix(local):
    if len(local) != 64:
        raise ValueError("Animated local matrix must contain sixteen float32 values")
    values = struct.unpack("<16f", local)
    result = []
    for value in values:
        if not math.isfinite(value):
            raise ValueError("Animated local matrix contains a nonfinite value")
        # INT32_MAX itself rounds to32768 when published as float Q16. Its
        # endpoint remains admissible only when the original float bits match.
        fixed = min(MAX_Q16, max(MIN_Q16, round(value * 65536)))
        if struct.pack("<f", fixed / 65536) != struct.pack("<f", value):
            raise ValueError("Animated local matrix cannot round-trip through signed Q16")
        result.append(fixed)
    return result


def capture_family(debugger, family):
    """Remember addresses at the family's load return, before more siblings load."""
    if family not in FAMILIES:
        raise ValueError("Unknown animated scene family")
    if family == "moving" and not (debugger.word(0x4dc908) & 255):
        return []
    if family == "rotorcraft" and not debugger.word(0x4dcb68):
        return []
    root = debugger.word(0x4dcb70 if family == "rotorcraft" else 0x4c38f0)
    if not root:
        if family in ("moving", "rotorcraft"):
            raise ValueError("Active animated HOR family has no published root")
        return []
    camera = debugger.word(0x4c3478)
    result, visited = [], set()
    pending = [(root, -1, None)]
    while pending:
        address, parent, expected_parent = pending.pop()
        if not address:
            continue
        if address in visited or len(visited) >= MAX_NODES:
            raise ValueError("Animated source forest is cyclic or exceeds its actor registry")
        visited.add(address)
        sibling, child, actual_parent = struct.unpack("<3I", debugger.memory(address, 12))
        if expected_parent is not None and actual_parent != expected_parent:
            raise ValueError("Animated source forest parent mismatch")
        if sibling:
            pending.append((sibling, parent, actual_parent))
        if address == camera:
            continue  # Camera0 and its whole subtree are not model resource nodes.
        index = len(result)
        result.append({"address": address, "parent": parent})
        if child:
            pending.append((child, index, address))
    return result


def capture_snapshot(debugger, families):
    """Read actual local matrices and masks at489069, after scene publication."""
    if not isinstance(families, dict) or any(family not in families for family in FAMILIES):
        raise ValueError("Animated snapshot requires all five captured family identities")
    count = debugger.word(0x4f2a5c)
    if count > MAX_FLAGS:
        raise ValueError("Original flag controllers exceed their forty-slot bound")
    flags = []
    for index in range(count):
        target = debugger.word(0x4d2b20 + index * 0x50)
        if not target:
            flags.append(-1)
            continue
        material = debugger.word(target + 0xc)
        if not material:
            raise ValueError("Original flag animation target has no mesh selection")
        variant = debugger.memory(material + 0x14, 4)[0]
        if variant > 2:
            raise ValueError("Original flag mesh variant is outside its three frames")
        flags.append(variant)
    result = {"flags": flags}
    for family in FAMILIES:
        records = families[family]
        if not isinstance(records, list) or len(records) > MAX_NODES:
            raise ValueError("Animated family identity exceeds the source actor registry")
        addresses = [record["address"] for record in records]
        if len(set(addresses)) != len(addresses):
            raise ValueError("Animated family identity repeats a source node")
        nodes = []
        for index, record in enumerate(records):
            parent = record["parent"]
            if type(parent) is not int or not -1 <= parent < index:
                raise ValueError("Animated identity parent must precede its child")
            node = debugger.memory(record["address"], 0x144)
            actual_parent = struct.unpack_from("<I", node, 8)[0]
            if parent >= 0 and actual_parent != addresses[parent]:
                raise ValueError("Animated node changed parent after its load boundary")
            if parent == -1 and actual_parent in addresses:
                raise ValueError("Animated root acquired a captured parent after loading")
            local = node[0x5c:0x9c]
            nodes.append({"parent": parent, "id": struct.unpack_from("<I", node, 0x18)[0] & 0xffffff,
                          "visible": node[0x140], "transform_q16": signed_matrix(local), "local_float_hex": local.hex()})
        result[family] = nodes
    validate_snapshot({"animated_scenery": result})
    return result


def validate_snapshot(state):
    snapshot = state.get("animated_scenery")
    if snapshot is None:
        return None
    if not isinstance(snapshot, dict):
        raise ValueError("Animated scene snapshot must be an object")
    flags = snapshot.get("flags")
    if (not isinstance(flags, list) or len(flags) > MAX_FLAGS
            or any(type(value) is not int or not -1 <= value <= 2 for value in flags)):
        raise ValueError("Animated flags require at most forty null/three-frame selections")
    total = 0
    for family in FAMILIES:
        nodes = snapshot.get(family)
        if not isinstance(nodes, list) or len(nodes) > MAX_NODES:
            raise ValueError("Animated scene family is missing or exceeds the actor registry")
        total += len(nodes)
        if total > MAX_NODES:
            raise ValueError("Animated scene families exceed the shared actor registry")
        for index, node in enumerate(nodes):
            if not isinstance(node, dict):
                raise ValueError("Animated scene node must be an object")
            parent, identity, visible = (node.get(key) for key in ("parent", "id", "visible"))
            if type(parent) is not int or not -1 <= parent < index:
                raise ValueError("Animated parent must precede its child")
            if type(identity) is not int or not 0 <= identity <= 0xffffff:
                raise ValueError("Animated source ID must fit its low twenty-four bits")
            if type(visible) is not int or not 0 <= visible <= 255:
                raise ValueError("Animated visibility must be a source byte")
            matrix = node.get("transform_q16")
            if (not isinstance(matrix, list) or len(matrix) != 16
                    or any(type(value) is not int or not MIN_Q16 <= value <= MAX_Q16 for value in matrix)):
                raise ValueError("Animated transform requires sixteen signed Q16 integers")
            if "local_float_hex" in node:
                try:
                    local = bytes.fromhex(node["local_float_hex"])
                except (TypeError, ValueError):
                    raise ValueError("Animated float32 audit is not valid hexadecimal") from None
                if len(local) != 64 or local != struct.pack("<16f", *(value / 65536 for value in matrix)):
                    raise ValueError("Animated Q16 transform differs from its published float32 audit")
    return snapshot


def append_rows(rows, state):
    snapshot = validate_snapshot(state)
    rows.append("0" if snapshot is None else "1")
    if snapshot is None:
        return
    rows.append(str(len(snapshot["flags"])))
    if snapshot["flags"]:
        rows.append(" ".join(str(value) for value in snapshot["flags"]))
    for family in FAMILIES:
        rows.append(str(len(snapshot[family])))
        for node in snapshot[family]:
            rows.append(" ".join(str(value) for value in (node["parent"], node["id"], node["visible"], *node["transform_q16"])))
