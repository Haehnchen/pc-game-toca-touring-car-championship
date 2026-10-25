"""Simple closed roof and sign backing for canonical Donington DA28CBO2_1.

CBO2 has its own uneven COMA front, COMB upper wall/end and slanted rear
roof return. Preserve all 61 source vertices, 32 faces and five atlas slots;
only give the existing roof and projecting PIT3 sheet useful thickness.
"""

import struct

import mega_cbo4 as shared
import vehicle_model as geometry


VERTICES_CRC32 = '86ded412'
TOPOLOGY_CRC32 = '1a6a0cae'
MATERIALS = ('DA28PIT3', 'DA28COM9', 'DA28COMA', 'DA28COMB', 'DA02GSD9')
ROOF_FACES = range(4, 14)
SIGN_FACES = range(4)
ROOF_DEPTH = .06
ROOF_OFFSET = (0., -.03, ROOF_DEPTH)
SIGN_DEPTH = .07


def matches(original, texture_names):
    if not shared._source_matches(original, b'DA28CBO2_1', 61, 32, VERTICES_CRC32, TOPOLOGY_CRC32):
        return False
    names = shared._names(texture_names)
    expected = ('DA28PIT3',) * 4 + ('DA28COM9',) * 10 + ('DA28COMA',) * 8 + ('DA28COMB',) * 8 + ('DA02GSD9',) * 2
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    return all(0 <= slot < len(names) and names[slot] == name for slot, name in zip(slots, expected))


def _roof_shell(mesh, original):
    # Source Z points down. One common inward offset keeps all shared corners
    # connected. The negative Y component separates the nearly vertical rear
    # return too; a Z-only offset would leave its reverse almost coplanar.
    depth = ROOF_OFFSET
    for face_index in ROOF_FACES:
        samples, normal, texture = shared._triangle(original, face_index)
        underside = [(geometry.add(point, depth), uv) for point, uv in samples]
        shared._polygon(mesh, underside, texture, geometry.mul(normal, -1))
    for first, second, _, texture in shared._boundary(original, ROOF_FACES):
        back_first = (geometry.add(first[0], depth), first[1])
        back_second = (geometry.add(second[0], depth), second[1])
        shared._polygon(mesh, (first, back_first, back_second, second), texture)


def _sign_backing(mesh, original):
    # Keep the original lettered front. The new reverse and side faces sample
    # only the confirmed unlettered red margin of the same accepted PIT3 image.
    # One offset for all four triangles also keeps the tiny source plane
    # variations from opening seams at their shared vertices.
    _, front_normal, _ = shared._triangle(original, SIGN_FACES[0])
    depth = geometry.mul(front_normal, -SIGN_DEPTH)
    for face_index in SIGN_FACES:
        samples, normal, texture = shared._triangle(original, face_index)
        back = [(geometry.add(point, depth), shared.SIGN_BLANK_UV)
                for point, _ in samples]
        shared._polygon(mesh, back, texture, geometry.mul(normal, -1))
    for first, second, normal, texture in shared._boundary(original, SIGN_FACES):
        a = (geometry.add(first[0], depth), shared.SIGN_BLANK_UV)
        b = (geometry.add(second[0], depth), shared.SIGN_BLANK_UV)
        outward = geometry.cross(geometry.sub(second[0], first[0]), normal)
        shared._polygon(mesh, ((first[0], shared.SIGN_BLANK_UV), a, b,
                              (second[0], shared.SIGN_BLANK_UV)), texture, outward)


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    stats = dict(donington_cbo2=0, cbo2_roof_shells=0, cbo2_sign_backings=0, added_triangles=0)
    if not matches(original, texture_names):
        return original, stats
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    _roof_shell(mesh, original)
    _sign_backing(mesh, original)
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(donington_cbo2=1, cbo2_roof_shells=1, cbo2_sign_backings=1, added_triangles=added)
    return mesh, stats
