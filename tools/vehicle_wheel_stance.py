"""Apply presentation wheel-node outsets without changing geometry or physics."""

import math
import struct


def apply(model, definition):
    """Move only the four wheel-node Z translations, symmetrically per axle.

    Body nodes, wheel meshes and physical car data retain their source values.
    Apply before wheel-well construction so its fixed body mouths connect to
    an inner throat centered around the adjusted presentation wheels.
    """
    offsets = definition.get('wheel_outset', [0, 0])
    if (not isinstance(offsets, list) or len(offsets) != 2 or
            any(type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= .1 for value in offsets)):
        raise ValueError('Wheel outset requires front/rear distances between 0 and 0.1 meters')
    fixed = [round(value * 65536) for value in offsets]
    output = bytearray(model)
    offset, end = struct.unpack_from('<2I', model, 20)
    found = set()
    while offset < end:
        size = struct.unpack_from('<I', model, offset)[0]
        node_id = struct.unpack_from('<I', model, offset + 72)[0] & 0xffffff
        if node_id in (1, 2, 3, 4):
            z = struct.unpack_from('<i', model, offset + 64)[0]
            side = 1 if node_id in (1, 3) else -1
            if z * side <= 0 or node_id in found:
                raise ValueError('Wheel outset requires the four canonical left/right wheel nodes')
            struct.pack_into('<i', output, offset + 64, z + side * fixed[0 if node_id <= 2 else 1])
            found.add(node_id)
        offset += size
    if found != {1, 2, 3, 4}:
        raise ValueError('Wheel outset requires all four canonical wheel nodes')
    result = bytes(output)
    return result, {'wheel_outset': [value / 65536 for value in fixed]}
