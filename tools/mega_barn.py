"""Source-identified Donington DA50BARN_1 detail in local -Z-up space.

GP/Short share the same two barn placements and the same 30-vertex mesh. The
four painted surfaces remain exact; shallow frames follow the original UV
features, while simple flashing and eaves give the gable roof real edge thickness.
No new material, actor or collision surface is introduced.
"""

from dataclasses import dataclass
import math
import struct

import vehicle_model as geometry


VERTICES_CRC32 = 'f8104890'
TOPOLOGY_CRC32 = 'da80292e'
MATERIALS = ('DA50BAR1', 'DA50BAR3', 'DA50BAR4', 'DA50BAR2')
WHITE = (255, 255, 255, 255)
# Top-left, top-right, bottom-right, bottom-left in each original image map.
SIDES = ((3, 1, 2, 0), (7, 5, 6, 4))
FRONT = (11, 9, 10, 8)
REAR = (23, 21, 22, 20)
ROOFS = ((15, 13, 14, 12), (18, 16, 19, 17))
# Pixel rectangles measured in the original64x32 artwork. BAR2's two light
# patchwork panels remain ambiguous; their surfaces gain no glass or lettering.
SIDE_WINDOWS = ((6, 5, 17, 14), (19, 5, 30, 14), (47, 5, 58, 14))
REAR_PANELS = ((6, 5, 20, 19), (22, 5, 31, 19))
DOOR_OUTER = (9, 4, 54, 32)
DOOR_INNER = (11, 8, 52, 32)


def _names(texture_names):
    return [(name.decode('ascii') if isinstance(name, bytes) else name).rstrip('\0').upper()
            for name in texture_names]


def matches(original, texture_names):
    if original.header[:12].rstrip(b'\0') != b'DA50BARN_1' or len(original.vertices) != 30 or len(original.faces) != 14:
        return False
    vertices = b''.join(struct.pack('<8i', *(round(component * 65536) for component in vertex))
                        for vertex in original.vertices)
    topology = b''.join(face[:8] + face[10:] for face in original.faces)
    if geometry.crc32(vertices) != VERTICES_CRC32 or geometry.crc32(topology) != TOPOLOGY_CRC32:
        return False
    names = _names(texture_names)
    expected = ('DA50BAR1',) * 4 + ('DA50BAR3',) * 2 + ('DA50BAR4',) * 4 + ('DA50BAR2',) * 4
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    return all(0 <= slot < len(names) and names[slot] == name for slot, name in zip(slots, expected))


def _uv(vertex):
    return (max(.02, min(.99, vertex[3] - (1 if vertex[3] > 1 else 0))),
            max(.02, min(.99, vertex[4] + 1)) - 1)


@dataclass(frozen=True)
class Surface:
    vertices: tuple
    normal: tuple
    texture: int

    @classmethod
    def source(cls, original, indices, texture):
        vertices = tuple(original.vertices[index] for index in indices)
        # Quantized source lighting normals differ slightly from the measured
        # plane. Use its geometric normal for attachments; original lighting
        # normals remain untouched on every broad source triangle.
        normal = geometry.unit(geometry.cross(geometry.sub(vertices[1][:3], vertices[0][:3]),
                                              geometry.sub(vertices[3][:3], vertices[0][:3])))
        return cls(vertices, normal, texture)

    def sample(self, s, t, height=0.):
        # Every canonical quad uses the TR–BL diagonal. Bilinear UVs would
        # subtly move the roof's unequal .01587/.98412 eave coordinates.
        if s + t <= 1:
            weights = (1 - s - t, s, 0., t)
        else:
            weights = (0., 1 - t, s + t - 1, 1 - s)
        point = tuple(sum(weight * vertex[axis] for weight, vertex in zip(weights, self.vertices)) for axis in range(3))
        uv = tuple(sum(weight * _uv(vertex)[axis] for weight, vertex in zip(weights, self.vertices)) for axis in range(2))
        return geometry.add(point, geometry.mul(self.normal, height)), uv

    def image(self, x, y, height=0.):
        # Wall maps span exactly the source loader's inset rectangle. The
        # normalized image coordinates therefore locate painted features on
        # the actual model, rather than assuming un-inset0..1 coordinates.
        s = (max(.02, min(.99, x / 64)) - .02) / .97
        t = (max(.02, min(.99, y / 32)) - .02) / .97
        return self.sample(s, t, height)


def _polygon(mesh, samples, texture, normal=None):
    geometry.polygon(mesh, [sample[0] for sample in samples], WHITE, normal,
                     [sample[1] for sample in samples], texture)


def _patch(mesh, surface, rectangle, height):
    s0, t0, s1, t1 = rectangle
    corners = ((s0, t0), (s1, t0), (s1, t1), (s0, t1))
    front = [surface.sample(s, t, height) for s, t in corners]
    base = [surface.sample(s, t, .002) for s, t in corners]
    _polygon(mesh, front, surface.texture, surface.normal)
    for i in range(4):
        j = (i + 1) % 4
        _polygon(mesh, (base[i], base[j], front[j], front[i]), surface.texture)


def _frame(mesh, surface, outer, inner, height, bottom=True):
    x0, y0, x1, y1 = outer
    a0, b0, a1, b1 = inner
    outside = ((x0, y0), (x1, y0), (x1, y1), (x0, y1))
    inside = ((a0, b0), (a1, b0), (a1, b1), (a0, b1))
    for i in range(4):
        if i == 2 and not bottom:
            continue
        j = (i + 1) % 4
        outer_front = [surface.image(*outside[k], height) for k in (i, j)]
        inner_front = [surface.image(*inside[k], height) for k in (i, j)]
        outer_base = [surface.image(*outside[k], .003) for k in (i, j)]
        inner_base = [surface.image(*inside[k], .003) for k in (i, j)]
        _polygon(mesh, (outer_front[0], outer_front[1], inner_front[1], inner_front[0]), surface.texture, surface.normal)
        _polygon(mesh, (outer_base[0], outer_base[1], outer_front[1], outer_front[0]), surface.texture)
        _polygon(mesh, (inner_front[0], inner_front[1], inner_base[1], inner_base[0]), surface.texture)


def _sweep(mesh, start, end, offsets, texture):
    """Closed flashing/eave prism; original edge UVs carry through the section."""
    rings = [[(geometry.add(sample[0], offset), sample[1]) for offset in offsets] for sample in (start, end)]
    center = tuple(sum(sample[0][axis] for ring in rings for sample in ring) / (2 * len(offsets)) for axis in range(3))
    for i in range(len(offsets)):
        j = (i + 1) % len(offsets)
        samples = (rings[0][i], rings[1][i], rings[1][j], rings[0][j])
        normal = geometry.cross(geometry.sub(samples[1][0], samples[0][0]), geometry.sub(samples[2][0], samples[0][0]))
        middle = tuple(sum(sample[0][axis] for sample in samples) / 4 for axis in range(3))
        if geometry.dot(normal, geometry.sub(middle, center)) < 0:
            normal = geometry.mul(normal, -1)
        _polygon(mesh, samples, texture, normal)
    axis = geometry.unit(geometry.sub(end[0], start[0]))
    _polygon(mesh, rings[0], texture, geometry.mul(axis, -1))
    _polygon(mesh, list(reversed(rings[1])), texture, axis)


def _roof_edges(mesh, roofs):
    for index, surface in enumerate(roofs):
        eave = 1. if index == 0 else 0.
        side = 1 if index == 0 else -1
        # A shallow roof overhang, fascia and soffit instead of an infinitely
        # thin source edge. The existing pale roof material supplies all faces.
        offsets = ((0., 0., -.025), (side * .09, 0., -.025), (side * .09, 0., .075), (0., 0., .075))
        _sweep(mesh, surface.sample(0., eave), surface.sample(1., eave), offsets, surface.texture)
        for s, sign in ((0., -1), (1., 1)):
            offsets = tuple(geometry.add((0., sign * across, 0.), geometry.mul(surface.normal, height))
                            for across, height in ((0., 0.), (.065, 0.), (.065, .045), (0., .045)))
            _sweep(mesh, surface.sample(s, 0.), surface.sample(s, 1.), offsets, surface.texture)
    # A narrow folded ridge cap straddles the exact source ridge. Each wing
    # retains its own roof projection so BAR4's image orientation stays correct.
    right, left = roofs
    rings = []
    for s in (0., 1.):
        a, b = right.sample(s, .025), left.sample(s, .975)
        ridge, uv = right.sample(s, 0.)
        rings.append((a, (geometry.add(ridge, (0., 0., -.080)), uv), b))
    for i, surface in ((0, right), (1, left)):
        _polygon(mesh, (rings[0][i], rings[1][i], rings[1][i + 1], rings[0][i + 1]), surface.texture, surface.normal)
    _polygon(mesh, rings[0], right.texture, (0., -1., 0.))
    _polygon(mesh, list(reversed(rings[1])), right.texture, (0., 1., 0.))


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    """Enhance only the exact shared source; retain header, faces and materials."""
    stats = dict(donington_barns=0, barn_roof_ribs=0, barn_window_frames=0,
                 barn_panel_frames=0, barn_door_reveals=0, barn_corner_strips=0, added_triangles=0)
    if not matches(original, texture_names):
        return original, stats
    names = _names(texture_names)
    slots = {name: names.index(name) for name in MATERIALS}
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    sides = [Surface.source(original, indices, slots['DA50BAR1']) for indices in SIDES]
    front = Surface.source(original, FRONT, slots['DA50BAR3'])
    rear = Surface.source(original, REAR, slots['DA50BAR2'])
    roofs = [Surface.source(original, indices, slots['DA50BAR4']) for indices in ROOFS]
    for surface in sides:
        for rectangle in SIDE_WINDOWS:
            x0, y0, x1, y1 = rectangle
            _frame(mesh, surface, rectangle, (x0 + .7, y0 + .6, x1 - .7, y1 - .6), .065)
    for rectangle in REAR_PANELS:
        x0, y0, x1, y1 = rectangle
        _frame(mesh, rear, rectangle, (x0 + .5, y0 + .4, x1 - .5, y1 - .4), .045)
    _frame(mesh, front, DOOR_OUTER, DOOR_INNER, .12, bottom=False)
    for surface in (*sides, front, rear):
        # These source-aligned strips articulate the existing four wall
        # junctions; they introduce no invented cross-bracing or new openings.
        width = .085 / math.dist(surface.vertices[0][:3], surface.vertices[1][:3])
        for s0, s1 in ((0., width), (1 - width, 1.)):
            _patch(mesh, surface, (s0, 0., s1, 1.), .035)
    _roof_edges(mesh, roofs)
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(donington_barns=1, barn_roof_ribs=0, barn_window_frames=6,
                 barn_panel_frames=2, barn_door_reveals=1, barn_corner_strips=8, added_triangles=added)
    return mesh, stats
