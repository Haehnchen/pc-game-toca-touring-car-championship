#!/usr/bin/env python3
"""Read original published car slots, without replaying or advancing ghosts."""

import json
import math
import struct

MAX_ACTORS, MAX_NODES = 16, 256
LOD_TABLES = (0x4c39d0, 0x4c34f8, 0x4c3650)
COCKPIT_ROOTS = (0x4d5410, 0x4d5470, 0x4d551c, 0x4d5340, 0x4d5518)
IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"Captured actor {label} is outside its source field")
    return value


def matrix(values, label):
    if not isinstance(values, (tuple, list)) or len(values) != 16:
        raise ValueError(f"Captured actor {label} requires sixteen float32 values")
    for value in values:
        if type(value) not in (int, float) or not math.isfinite(value):
            raise ValueError(f"Captured actor {label} contains a nonfinite value")
        try:
            exact = struct.unpack("<f", struct.pack("<f", value))[0]
        except (OverflowError, struct.error):
            raise ValueError(f"Captured actor {label} exceeds float32") from None
        if exact != value:
            raise ValueError(f"Captured actor {label} lost published float32 precision")


def capture_load(debugger, stack):
    """44dc6d entry arguments identify actual archive resource and ghost material."""
    caller, slot, resource, ordinary = struct.unpack("<4I", debugger.memory(stack, 16))
    integer(slot, 0, 15, "loader slot")
    integer(resource, 0, 10, "loader resource")
    integer(ordinary, 0, 1, "loader material")
    return slot, {"model_id": resource, "ordinary_material": ordinary,
                  "caller": caller, "provenance": "44dc6d-entry"}


def capture_livery(debugger, stack):
    caller, first, second, number_a, number_b, flags = struct.unpack("<6I", debugger.memory(stack, 24))
    if not first or not second or first == second:
        raise ValueError("Original livery remap requires two distinct physical roots")
    integer(flags, 0, 3, "livery remap flags")
    return {"first_root": first, "second_root": second, "number_a": number_a,
            "number_b": number_b, "flags": flags, "caller": caller, "provenance": "49a780-entry"}


def resource_bindings(loads, mode, count):
    """Only44df6e's uncalled odd slots inherit its observed even-slot clone."""
    if mode not in (0, 1, 2):
        raise ValueError("Captured actor import supports Single Race/Championship/Time Trial")
    integer(count, 1, MAX_ACTORS, "allocation count")
    if mode == 2 and count != 3:
        raise ValueError("Time Trial must retain its three actual render buffers")
    result = {}
    for slot in range(count):
        if slot in loads:
            binding = dict(loads[slot])
        elif mode != 2 and slot % 2 and slot - 1 in loads:
            binding = {**loads[slot - 1], "provenance": "44df6e-odd-slot-clone", "cloned_from": slot - 1}
        else:
            raise ValueError("Captured actor lacks an observed source model loader")
        integer(binding.get("model_id"), 0, 10, "resource ID")
        integer(binding.get("ordinary_material"), 0, 1, "material role")
        if binding["ordinary_material"] != int(mode != 2 or slot == 0):
            raise ValueError("Captured actor material role disagrees with its source owner")
        result[slot] = binding
    return result


def texture_bindings(debugger):
    renderer = debugger.word(0x4b9b94)
    count = debugger.word(0x5abbd8)
    if count > 8192:
        raise ValueError("Original texture registry exceeds its bounded table")
    result = {}
    for index in range(count):
        wrapper = debugger.word(renderer + 0x2240 + index * 4)
        if not wrapper:
            continue
        record = debugger.memory(wrapper, 0x28)
        name = record[0x1a:0x26].split(b"\0", 1)[0].decode("ascii")
        handle = struct.unpack_from("<I", record, 0x14)[0]
        material = struct.unpack_from("<I", record, 0xc)[0]
        surface = debugger.word(material + 4) if material else 0
        key = (handle, surface)
        if key in result and result[key] != name:
            raise ValueError("Original personal-name texture binding is ambiguous")
        result[key] = name
    return result


def capture_nodes(debugger, root, bindings):
    """One LOD subtree, in DFS order, identified by parent/sibling paths."""
    result, visited, pending = [], set(), [(root, -1, 0, None)]
    while pending:
        address, parent, sibling_ordinal, expected_parent = pending.pop()
        if not address or address in visited or len(visited) >= MAX_NODES:
            raise ValueError("Original actor hierarchy is null, cyclic or exceeds256 nodes")
        visited.add(address)
        raw = debugger.memory(address, 0x144)
        sibling, child, actual_parent, material = struct.unpack_from("<4I", raw)
        if expected_parent is not None and actual_parent != expected_parent:
            raise ValueError("Original actor hierarchy changed parent")
        identity = struct.unpack_from("<I", raw, 0x18)[0] & 0xffffff
        variant, names, numbers, materials = -1, [], [], []
        if material:
            selection = debugger.memory(material + 0x14, 4)
            variant, variants = selection[:2]
            if not variants or variant >= variants:
                raise ValueError("Original actor mesh selection exceeds its variant table")
            if identity in (9, 15, 16, 21):
                mesh = debugger.word(material + 4 + variant * 4)
                count = debugger.word(mesh + 0x1d0)
                if count > 4096:
                    raise ValueError("Original actor name mesh has too many groups")
                groups = debugger.memory(mesh + 0x14, count * 16) if count else b""
                for group in range(count):
                    handle, surface = struct.unpack_from("<2I", groups, group * 16)
                    name = bindings.get((handle, surface))
                    binding = {"group": group, "texture": name, "handle": handle, "surface": surface}
                    materials.append(binding)
                    if name is not None:
                        if name.lower().startswith("list"):
                            names.append(binding)
                        elif name.lower().startswith("num") and name[3:].isdigit():
                            numbers.append(binding)
        index = len(result)
        result.append({"address": address, "parent": parent, "sibling": sibling_ordinal, "id": identity,
                       "visible": raw[0x138], "manual_visible": raw[0x140], "variant": variant,
                       "local": list(struct.unpack_from("<16f", raw, 0x5c)), "world": list(struct.unpack_from("<16f", raw, 0x9c)),
                       "name_bindings": names, "number_bindings": numbers, "material_bindings": materials})
        # The LOD root's sibling is another model family, never part of this one.
        if parent >= 0 and sibling:
            pending.append((sibling, parent, sibling_ordinal + 1, expected_parent))
        if child:
            pending.append((child, index, 0, address))
    return result


def source_text(debugger, address, length):
    raw = debugger.memory(address, (length + 3) // 4 * 4)[:length]
    if b"\0" not in raw:
        raise ValueError("Original personal name is not terminated in its source field")
    text = raw.split(b"\0", 1)[0].decode("ascii")
    if any(not 32 <= ord(character) < 127 for character in text):
        raise ValueError("Original personal name contains unsupported glyphs")
    return text


def validate_name_bindings(resource, nodes, lod=0):
    for node in nodes:
        if node["id"] not in (15, 16, 21) or node["variant"] < 0 or node["name_bindings"]:
            continue
        # MEDIUM Ford/Vectra2 uses baked glass. Renault HIGH e0fffe5d
        # variants2/3 also omit List faces (RENBody30/33/41); solid faces
        # have handle0 and their unused surface word is not a texture binder.
        glass = "wscrn" if node["id"] == 21 else "sideg"
        materials = node["material_bindings"]
        baked = lod > 0 or resource == 10 or (resource == 7 and node["variant"] >= 2)
        static_glass = baked and any(binding["texture"] and binding["texture"].lower() == glass for binding in materials)
        if not static_glass or any(binding["texture"] is None and binding.get("handle") != 0 for binding in materials):
            raise ValueError(f"Original name node has an unclassified active texture binding: model{resource}/LOD{lod}/node{node['id']} {materials}")


def capture_snapshot(debugger, loads, viewport=0, liveries=()):
    config = debugger.word(0x4b0660)
    mode = debugger.word(config + 4)
    local = debugger.word(0x4d37a0) & 255
    integer(viewport, 0, 3, "viewport")
    if debugger.word(config + 0xac) != 1:
        raise ValueError("Captured actor import requires one local viewport")
    count = struct.unpack("<h", debugger.memory(0x4c5d6e, 4)[:2])[0]
    resources = resource_bindings(loads, mode, count)
    integer(local, 0, count - 1, "local owner")
    owner = debugger.word(0x4fa030) & 255 if mode == 2 else -1
    ghost_visible = debugger.word(0x52b3e4) & 255 if mode == 2 else 0
    if mode == 2:
        integer(owner, 1, 2, "selected ghost buffer")
    textures = texture_bindings(debugger)
    shadow_count = debugger.word(0x61e150)
    integer(shadow_count, 0, count, "shadow queue count")
    shadow_queue = debugger.memory(0x61d888, shadow_count * 136) if shadow_count else b""
    shadow_masks = debugger.memory(0x4ed880, 16)
    expected_slots = [slot for slot in range(count) if shadow_masks[slot] & (1 << viewport)]
    if len(expected_slots) != shadow_count:
        raise ValueError("Original shadow queue differs from439280's actor submission masks")
    shadows = {}
    for ordinal, slot in enumerate(expected_slots):
        record = shadow_queue[ordinal * 136:(ordinal + 1) * 136]
        if debugger.memory(0x4db028 + slot * 136, 136) != record:
            raise ValueError("Original shadow queue record differs from its exact submitting actor")
        shadows[slot] = (ordinal, record.hex())
    damage = debugger.memory(0x4c25a8, 17 * 16)
    actors = []
    for slot in range(count):
        physical = debugger.word(0x4c3968 + slot * 4)
        if not physical:
            raise ValueError("Allocated original actor has no physical scene root")
        root_state = debugger.memory(physical + 0x138, 12)
        roots = [debugger.word(table + slot * 4) for table in LOD_TABLES]
        if not all(roots):
            raise ValueError("Allocated original actor has an incomplete LOD family")
        masks = [debugger.memory(root + 0x138, 4)[0] for root in roots]
        manual_masks = [debugger.memory(root + 0x140, 4)[0] for root in roots]
        selected = [lod for lod, mask in enumerate(manual_masks) if mask & (1 << viewport)]
        if len(selected) > 1:
            raise ValueError("Original actor has multiple selected LOD roots in one viewport")
        lod = selected[0] if selected else (root_state[9] >> (viewport * 2)) & 3
        integer(lod, 0, 2, "retained LOD")
        dynamics = debugger.memory(0x4c5da0 + slot * 0x338, 0x338)
        role = 0 if slot == local else slot + 1 if mode == 2 else 1
        name_kind, name = 0, ""
        if slot == local or mode == 2:
            name_kind = 2 if mode == 2 and slot == 1 and debugger.word(config + 0x30) else 1
            name = source_text(debugger, 0x4fa038, 16) if name_kind == 2 else source_text(debugger, config + 0xb0, 10)
        node_records = capture_nodes(debugger, roots[lod], textures)
        if resources[slot]["model_id"] < 8 or resources[slot]["model_id"] == 10:
            validate_name_bindings(resources[slot]["model_id"], node_records, lod)
        remaps = [remap for remap in liveries if physical in (remap["first_root"], remap["second_root"])]
        if len(remaps) > 1:
            raise ValueError("Original actor received ambiguous livery remaps")
        second_driver = bool(remaps and remaps[0]["second_root"] == physical and remaps[0]["flags"] & 2)
        actors.append({"slot": slot, "role": role, "model_id": resources[slot]["model_id"], "resource": resources[slot],
                       "lod": lod, "root_mask": root_state[0], "lod_mask": masks[lod], "lod_masks": masks,
                       "manual_root_mask": root_state[8], "manual_lod_masks": manual_masks,
                       "physical_root": physical, "lod_roots": roots, "second_driver": second_driver, "livery_remaps": remaps,
                       "name_kind": name_kind, "name": name, "nodes": node_records,
                       "object_to_world": list(struct.unpack("<16f", debugger.memory(physical + 0x5c, 64))),
                       "steering_angle": struct.unpack_from("<h", dynamics, 0x274)[0],
                       "wheel_angles": list(struct.unpack("<4h", debugger.memory(0x4d37b8 + slot * 8, 8))),
                       "visual_angles": list(struct.unpack_from("<2h", dynamics, 0x278)),
                       "lights": [int(bool(value)) for value in struct.unpack("<3I", debugger.memory(0x4cd760 + slot * 0x53c + 0x500, 12))],
                       "damage": [damage[index * 16 + slot] for index in range(17)],
                       "broken": list(struct.unpack("<4I", debugger.memory(0x4db8c8 + slot * 16, 16))),
                       "shadow_records_hex": shadows.get(slot, (-1, ""))[1], "shadow_ordinal": shadows.get(slot, (-1, ""))[0]})
    result = {"source_view": debugger.word(0x4c3a66) & 0xffff, "viewport": viewport, "ghost_owner": owner,
              "ghost_visible": ghost_visible, "mode": mode, "local_slot": local, "actors": actors}
    validate_snapshot({"render_actors": result})
    return result


def remap_livery(actor):
    """Derive49a780 pairing from the retained call audit, never actor parity."""
    remaps = actor.get("livery_remaps")
    if not isinstance(remaps, list) or len(remaps) > 1:
        raise ValueError("Captured actor requires an unambiguous livery call audit")
    second = False
    for remap in remaps:
        if not isinstance(remap, dict) or remap.get("provenance") != "49a780-entry":
            raise ValueError("Captured actor livery must identify its original call boundary")
        first = integer(remap.get("first_root"), 1, 0xffffffff, "first livery root")
        other = integer(remap.get("second_root"), 1, 0xffffffff, "second livery root")
        physical = integer(actor.get("physical_root"), 1, 0xffffffff, "physical livery root")
        integer(remap.get("number_a"), 0, 0xffffffff, "first livery number")
        integer(remap.get("number_b"), 0, 0xffffffff, "second livery number")
        integer(remap.get("caller"), 0x400000, 0x4affff, "livery caller")
        flags = integer(remap.get("flags"), 0, 3, "livery flags")
        if first == other or physical not in (first, other):
            raise ValueError("Captured livery audit does not belong to its physical actor")
        second = physical == other and bool(flags & 2)
    if actor.get("second_driver") is not second:
        raise ValueError("Captured second-driver role differs from its livery call audit")
    return bool(remaps)


def validate_snapshot(state):
    snapshot = state.get("render_actors")
    if snapshot is None:
        return None
    if not isinstance(snapshot, dict):
        raise ValueError("Captured actor snapshot must be an object")
    mode = integer(snapshot.get("mode"), 0, 2, "session mode")
    view = integer(snapshot.get("source_view"), 0, 5, "camera view")
    viewport = integer(snapshot.get("viewport"), 0, 3, "viewport")
    owner = integer(snapshot.get("ghost_owner"), -1, 2, "ghost owner")
    visibility = integer(snapshot.get("ghost_visible"), 0, 255, "ghost visibility")
    entries = snapshot.get("actors")
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_ACTORS:
        raise ValueError("Captured actor snapshot requires1..16 allocated slots")
    local = integer(snapshot.get("local_slot"), 0, len(entries) - 1, "local slot")
    if mode == 2 and (len(entries) != 3 or owner not in (1, 2) or local != 0):
        raise ValueError("Captured Time Trial requires player0 and its two original ghost buffers")
    if mode != 2 and (owner != -1 or visibility):
        raise ValueError("Ordinary race actors cannot own a Time Trial ghost")
    shadow_slots = []
    for slot, actor in enumerate(entries):
        if not isinstance(actor, dict) or type(actor.get("slot")) is not int or actor["slot"] != slot:
            raise ValueError("Captured actor slots must retain complete allocation order")
        expected_role = 0 if slot == local else slot + 1 if mode == 2 else 1
        if integer(actor.get("role"), 0, 3, "role") != expected_role:
            raise ValueError("Captured actor role differs from its actual source owner")
        integer(actor.get("model_id"), 0, 10, "resource ID")
        lod = integer(actor.get("lod"), 0, 2, "LOD")
        root_mask = integer(actor.get("root_mask"), 0, 255, "root mask")
        lod_mask = integer(actor.get("lod_mask"), 0, 255, "LOD mask")
        if type(actor.get("second_driver")) is not bool or mode == 2 and actor["second_driver"]:
            raise ValueError("Captured actor second-driver role is invalid")
        paired = remap_livery(actor)
        if mode == 2 and paired:
            raise ValueError("Captured Time Trial cannot have a race-pair livery remap")
        if "manual_root_mask" in actor:
            integer(actor["manual_root_mask"], 0, 255, "manual root mask")
        if mode == 2 and slot and "manual_root_mask" in actor and bool(actor["manual_root_mask"] & (1 << viewport)) != bool(slot == owner and visibility & (1 << viewport)):
            raise ValueError("Captured ghost root mask differs from the selected playback buffer")
        if mode == 2 and slot != owner and slot != local and root_mask & (1 << viewport):
            raise ValueError("Unselected ghost buffer has a published geometry mask")
        if "lod_masks" in actor:
            masks = actor["lod_masks"]
            if not isinstance(masks, list) or len(masks) != 3 or any(type(mask) is not int or not 0 <= mask <= 255 for mask in masks):
                raise ValueError("Captured LOD audit requires three source byte masks")
            if masks[lod] != lod_mask:
                raise ValueError("Captured actor selected LOD differs from its published masks")
        if "manual_lod_masks" in actor:
            masks = actor["manual_lod_masks"]
            if not isinstance(masks, list) or len(masks) != 3 or any(type(mask) is not int or not 0 <= mask <= 255 for mask in masks):
                raise ValueError("Captured manual LOD audit requires three source byte masks")
            selected = [index for index, mask in enumerate(masks) if mask & (1 << viewport)]
            if len(selected) > 1 or selected and selected != [lod]:
                raise ValueError("Captured actor selected LOD differs from its manual eligibility")
        matrix(actor.get("object_to_world"), "root matrix")
        for name, count in (("wheel_angles", 4), ("visual_angles", 2), ("damage", 17), ("broken", 4), ("lights", 3)):
            values = actor.get(name)
            if not isinstance(values, (list, tuple)) or len(values) != count:
                raise ValueError(f"Captured actor {name} has the wrong field count")
            low, high = (0, 3) if name == "damage" else (0, 1) if name in ("broken", "lights") else (-32768, 32767)
            for value in values:
                integer(value, low, high, name)
        integer(actor.get("steering_angle"), -32768, 32767, "steering")
        kind = integer(actor.get("name_kind"), 0, 2, "name owner")
        text = actor.get("name")
        if not isinstance(text, str) or len(text) > (15 if kind == 2 else 9) or any(not 32 <= ord(c) < 127 for c in text) or kind == 0 and text:
            raise ValueError("Captured actor name exceeds its original bounded text field")
        if kind == 2 and (mode != 2 or slot != 1):
            raise ValueError("Captured best name must belong to saved ghost buffer1")
        nodes = actor.get("nodes")
        if not isinstance(nodes, list) or not 1 <= len(nodes) <= MAX_NODES:
            raise ValueError("Captured actor hierarchy requires1..256 source nodes")
        paths, next_sibling = [], {}
        for index, node in enumerate(nodes):
            if not isinstance(node, dict):
                raise ValueError("Captured actor node must be an object")
            parent = integer(node.get("parent"), -1, index - 1, "node parent")
            sibling = integer(node.get("sibling"), 0, MAX_NODES - 1, "node sibling ordinal")
            if (index == 0) != (parent == -1) or sibling != next_sibling.get(parent, 0):
                raise ValueError("Captured actor node path is not a unique ordered subtree")
            next_sibling[parent] = sibling + 1
            path = (() if parent == -1 else paths[parent]) + (sibling,)
            if paths and path <= paths[-1]:
                raise ValueError("Captured actor hierarchy is not in depth-first order")
            paths.append(path)
            integer(node.get("id"), 0, 0xffffff, "node ID")
            integer(node.get("visible"), 0, 255, "node visibility")
            integer(node.get("variant"), -1, 255, "mesh variant")
            matrix(node.get("local"), "node local matrix")
            matrix(node.get("world"), "node world matrix")
        try:
            shadow = bytes.fromhex(actor.get("shadow_records_hex", ""))
        except (TypeError, ValueError):
            raise ValueError("Captured actor shadow is not hexadecimal") from None
        ordinal = integer(actor.get("shadow_ordinal"), -1, MAX_ACTORS - 1, "shadow ordinal")
        if len(shadow) not in (0, 136) or bool(shadow) != (ordinal >= 0):
            raise ValueError("Captured actor shadow requires one exact submitted record")
        if shadow:
            if struct.unpack_from("<I", shadow, 132)[0] > 2 or mode == 2 and slot != local:
                raise ValueError("Captured actor has an invalid shadow type or ghost shadow")
            shadow_slots.append(ordinal)
    if shadow_slots != list(range(len(shadow_slots))):
        raise ValueError("Captured actor shadows do not retain source submission order")
    if "actor" in state and state["actor"] != local or "source_view" in state and state["source_view"] != view:
        raise ValueError("Captured actor snapshot differs from the outer draw owner/view")
    return snapshot


def append_rows(rows, state):
    snapshot = validate_snapshot(state)
    rows.append("0" if snapshot is None else "1")
    if snapshot is None:
        return
    rows.append(" ".join(str(value) for value in (len(snapshot["actors"]), snapshot["source_view"], snapshot["ghost_owner"], snapshot["ghost_visible"], snapshot["viewport"])))
    for actor in snapshot["actors"]:
        shadow = bytes.fromhex(actor["shadow_records_hex"])
        header = (actor["slot"], actor["role"], actor["model_id"], actor["lod"], actor["root_mask"], actor["lod_mask"],
                  int(actor["second_driver"]), int(remap_livery(actor)), actor["name_kind"], len(actor["nodes"]), int(bool(shadow)))
        rows.append(" ".join(str(value) for value in header))
        rows.append(" ".join(str(value) for value in actor["object_to_world"]))
        rows.append(" ".join(str(value) for value in (actor["steering_angle"], *actor["wheel_angles"], *actor["visual_angles"])))
        rows.append(" ".join(str(value) for value in actor["lights"]))
        rows.append(" ".join(str(value) for value in (*actor["damage"], *actor["broken"])))
        rows.append(json.dumps(actor["name"], ensure_ascii=True))
        for node in actor["nodes"]:
            rows.append(" ".join(str(value) for value in (node["parent"], node["sibling"], node["id"], node["visible"], node["variant"], *node["local"], *node["world"])))
        if shadow:
            rows.append(" ".join(str(value) for value in struct.unpack("<33iI", shadow)))


def capture_cockpit(debugger, state):
    if state["source_view"] != 1:
        return None
    if state["car_id"] == 8:
        raise ValueError("Original Tank has no cockpit resource")
    duplicate = debugger.word(0x4d537c) == 0
    if duplicate:
        raise ValueError("Original cockpit requires a second wiper outside the captured single-template contract")
    physical = debugger.word(0x4c3968 + state["actor"] * 4)
    roles = []
    for role, global_address in enumerate(COCKPIT_ROOTS):
        address = debugger.word(global_address)
        if not address:
            raise ValueError("Original cockpit is missing a published role root")
        node = debugger.memory(address, 0x144)
        parent = struct.unpack_from("<I", node, 8)[0]
        parent_local, parent_mask = IDENTITY[:], 255
        if role == 3:
            if not parent or parent == physical:
                raise ValueError("Original wiper is missing its separate authored parent")
            parent_node = debugger.memory(parent, 0x144)
            if struct.unpack_from("<I", parent_node, 8)[0] != physical:
                raise ValueError("Original wiper parent does not belong to the local car")
            parent_local = list(struct.unpack_from("<16f", parent_node, 0x5c))
            parent_mask = parent_node[0x138]
        elif parent != physical:
            raise ValueError("Original cockpit role does not belong to the local car")
        roles.append({"actual_mask": node[0x138] & parent_mask, "manual_mask": node[0x140],
                      "local": list(struct.unpack_from("<16f", node, 0x5c)), "parent_local": parent_local,
                      "address": address, "parent_address": parent, "published_mask": node[0x138], "parent_mask": parent_mask,
                      "nodes": capture_nodes(debugger, address, {})})
    result = {"car_id": state["car_id"], "wheel_angle": struct.unpack("<h", debugger.memory(0x4d54e0, 4)[:2])[0],
              "wiper_angle": struct.unpack("<h", debugger.memory(0x4d5360, 4)[:2])[0], "duplicate_wiper": duplicate, "roles": roles}
    validate_cockpit({**state, "cockpit": result})
    return result


def validate_cockpit(state):
    snapshot = state.get("cockpit")
    if snapshot is None:
        if state.get("source_view") == 1:
            raise ValueError("Captured cockpit view requires its published role poses")
        return None
    if not isinstance(snapshot, dict) or snapshot.get("car_id") != state["car_id"] or state.get("source_view") != 1:
        raise ValueError("Captured cockpit must match its actual car and view")
    integer(snapshot["car_id"], 0, 9, "cockpit resource")
    if snapshot["car_id"] == 8:
        raise ValueError("Captured Tank cannot have a cockpit")
    integer(snapshot.get("wheel_angle"), -32768, 32767, "cockpit wheel angle")
    integer(snapshot.get("wiper_angle"), -32768, 32767, "cockpit wiper angle")
    if snapshot.get("duplicate_wiper") is not False:
        raise ValueError("Captured cockpit requires the observed single-wiper template")
    roles = snapshot.get("roles")
    if not isinstance(roles, list) or len(roles) != 5:
        raise ValueError("Captured cockpit requires its five canonical roles")
    for index, role in enumerate(roles):
        if not isinstance(role, dict):
            raise ValueError("Captured cockpit role must be an object")
        integer(role.get("actual_mask"), 0, 15, "cockpit actual mask")
        integer(role.get("manual_mask"), 0, 15, "cockpit manual mask")
        matrix(role.get("local"), "cockpit role local")
        matrix(role.get("parent_local"), "cockpit parent local")
        if index != 3 and role["parent_local"] != IDENTITY:
            raise ValueError("Captured direct cockpit role cannot invent another parent")
    return snapshot


def append_cockpit_rows(rows, state):
    snapshot = validate_cockpit(state)
    rows.append("0" if snapshot is None else "1")
    if snapshot is None:
        return
    rows.append(f"{snapshot['car_id']} {snapshot['wheel_angle']} {snapshot['wiper_angle']} 0")
    for role in snapshot["roles"]:
        rows.append(" ".join(str(value) for value in (role["actual_mask"], role["manual_mask"], *role["local"], *role["parent_local"])))


def skid_snapshot(state):
    snapshot = state.get("skid_marks")
    if snapshot is None:
        return None
    if not isinstance(snapshot, dict):
        raise ValueError("Captured skid marks require an object")
    viewport = integer(snapshot.get("viewport"), 0, 3, "skid viewport")
    persistent = integer(snapshot.get("persistent_count"), 0, 100, "persistent skid count")
    active = integer(snapshot.get("active_count"), 0, 99, "active skid count")
    disabled = integer(snapshot.get("sector_visibility_disabled"), 0, 0xffffffff, "skid sector filter")
    override = integer(snapshot.get("sector_visibility_override"), 0, 0xffffffff, "skid sector override")
    prefix = integer(snapshot.get("sector_visibility_prefix"), 0, 0xffffffff, "skid sector prefix")
    raw = snapshot.get("sector_visibility_hex")
    if not isinstance(raw, str):
        raise ValueError("Captured skid sector visibility requires exact hexadecimal bytes")
    try:
        raw = bytes.fromhex(raw)
    except ValueError:
        raise ValueError("Captured skid sector visibility is not hexadecimal") from None
    if len(raw) != 256:
        raise ValueError("Captured skid sector visibility requires64 source words")
    sectors = struct.unpack("<64I", raw)
    quads = snapshot.get("quads")
    if not isinstance(quads, list) or len(quads) != persistent + active:
        raise ValueError("Captured skid records disagree with their queue counts")
    for index, quad in enumerate(quads):
        queue, ordinal = (0, index) if index < persistent else (1, index - persistent)
        if not isinstance(quad, dict) or type(quad.get("queue")) is not int or quad["queue"] != queue or type(quad.get("ordinal")) is not int or quad["ordinal"] != ordinal:
            raise ValueError("Captured skid records must retain physical ring then active queue order")
        integer(quad.get("color"), 0, 0xffffffff, "skid ARGB")
        sector = integer(quad.get("sector"), 0, 0xffff, "skid sector")
        if sector >= 2048 and sector != 0xffff:
            raise ValueError("Captured skid sector exceeds the source visibility table")
        mask = integer(quad.get("visibility"), 0, 15, "skid viewport mask")
        vertices = quad.get("vertices_q16")
        if not isinstance(vertices, list) or len(vertices) != 4:
            raise ValueError("Captured skid quad requires four vertices")
        for vertex in vertices:
            if not isinstance(vertex, list) or len(vertex) != 3:
                raise ValueError("Captured skid vertex requires three Q16 coordinates")
            for coordinate in vertex:
                integer(coordinate, -0x80000000, 0x7fffffff, "skid Q16 coordinate")
        # 441d10 sign-extends sector;47db00 reads the preceding word for
        # sentinel-1. Preserve that audit instead of indexing Python's last word.
        bit = bool(prefix & 0x80000000) if sector == 0xffff else bool(sectors[sector >> 5] & (1 << (sector & 31)))
        rendered = bool(mask & (1 << viewport)) and bool(override or bit or not disabled)
        if type(quad.get("rendered")) is not bool or quad["rendered"] != rendered:
            raise ValueError("Captured skid draw eligibility differs from441d10/47db00")
        if rendered and sector == 0xffff:
            raise ValueError("Captured submitted skid has no supported authored sector")
    return snapshot


def append_skid_rows(rows, state):
    snapshot = skid_snapshot(state)
    rows.append("0" if snapshot is None else "1")
    if snapshot is None:
        return
    rows.append(f"{snapshot['persistent_count']} {snapshot['active_count']}")
    for quad in snapshot["quads"]:
        mask = quad["visibility"] if quad["rendered"] else 0
        rows.append(" ".join(str(value) for value in (quad["sector"], mask, quad["color"], *(coordinate for vertex in quad["vertices_q16"] for coordinate in vertex))))


def spark_snapshot(state):
    snapshot = state.get("spark_lines")
    if snapshot is None:
        return None
    if not isinstance(snapshot, dict):
        raise ValueError("Captured spark lines require an object")
    viewport = integer(snapshot.get("viewport"), 0, 3, "spark viewport")
    count = integer(snapshot.get("count"), 0, 64, "exported spark count")
    pool_count = integer(snapshot.get("pool_count"), 0, 64, "spark pool count")
    head = integer(snapshot.get("ring_head"), 0, 63, "spark ring head")
    tail = integer(snapshot.get("ring_tail"), 0, 63, "spark ring tail")
    integer(snapshot.get("color_index"), 0, 2, "spark color index")
    integer(snapshot.get("color"), 0, 0xffffffff, "spark ARGB")
    #418920 exports tail..head, including its full-ring head==tail quirk.
    expected = (head - tail) % 64 if pool_count else 0
    if pool_count != (64 if head == tail and pool_count else expected) or count != expected:
        raise ValueError("Captured spark queue disagrees with418920 ring export")
    lines = snapshot.get("lines")
    if not isinstance(lines, list) or len(lines) != count:
        raise ValueError("Captured spark records disagree with their queue count")
    for ordinal, line in enumerate(lines):
        if not isinstance(line, dict) or type(line.get("ordinal")) is not int or line["ordinal"] != ordinal:
            raise ValueError("Captured spark records must retain source export order")
        mask = integer(line.get("visibility"), 0, 15, "spark viewport mask")
        for field in ("from_q16", "to_q16"):
            coordinates = line.get(field)
            if not isinstance(coordinates, list) or len(coordinates) != 3:
                raise ValueError("Captured spark endpoint requires three Q16 coordinates")
            for coordinate in coordinates:
                integer(coordinate, -0x80000000, 0x7fffffff, "spark Q16 coordinate")
        if type(line.get("rendered")) is not bool or line["rendered"] != bool(mask & (1 << viewport)):
            raise ValueError("Captured spark draw eligibility differs from446f20")
    return snapshot


def append_spark_rows(rows, state):
    snapshot = spark_snapshot(state)
    rows.append("0" if snapshot is None else "1")
    if snapshot is None:
        return
    rows.append(str(snapshot["count"]))
    for line in snapshot["lines"]:
        mask = line["visibility"] if line["rendered"] else 0
        rows.append(" ".join(str(value) for value in (mask, snapshot["color"], *line["from_q16"], *line["to_q16"])))


def queued_lines_snapshot(state):
    snapshot = state.get("queued_lines")
    if snapshot is None:
        return None
    if not isinstance(snapshot, dict):
        raise ValueError("Captured queued world lines require an object")
    count = integer(snapshot.get("count"), 0, 17, "queued world line count")
    lines = snapshot.get("lines")
    if not isinstance(lines, list) or len(lines) != count:
        raise ValueError("Captured world lines disagree with their queue count")
    for ordinal, line in enumerate(lines):
        if not isinstance(line, dict) or type(line.get("ordinal")) is not int or line["ordinal"] != ordinal:
            raise ValueError("Captured world lines must retain442480 queue order")
        integer(line.get("color"), 0, 0xffffffff, "queued line ARGB")
        for field in ("from_q16", "to_q16"):
            coordinates = line.get(field)
            if not isinstance(coordinates, list) or len(coordinates) != 3:
                raise ValueError("Captured world line requires three Q16 coordinates per endpoint")
            for coordinate in coordinates:
                integer(coordinate, -0x80000000, 0x7fffffff, "queued line Q16 coordinate")
    return snapshot


def append_queued_line_rows(rows, state):
    snapshot = queued_lines_snapshot(state)
    rows.append("0" if snapshot is None else "1")
    if snapshot is None:
        return
    rows.append(str(snapshot["count"]))
    for line in snapshot["lines"]:
        rows.append(" ".join(str(value) for value in (line["color"], *line["from_q16"], *line["to_q16"])))
