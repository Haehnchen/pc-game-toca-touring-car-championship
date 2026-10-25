"""Track prop geometry in original local coordinates; actor pivots stay intact."""

import math
import struct

import vehicle_model as geometry
import mega_vegetation
import mega_woodland
import mega_dunlop
import mega_barn
import mega_grandstand
import mega_cbo4
import mega_cbo5
import mega_cbo6
import mega_cbo1
import mega_cbo3
import mega_cbo2
import mega_spectators
import mega_sheep
import mega_static_aircraft
import mega_static_platform
import mega_static_vectra
import mega_static_fleet


WHITE = (255, 255, 255, 255)
# Placement density is independent of the simpler emitted tyre mesh. Retain
# the established selection weight when sharing per-mesh/family budgets.
TYRE_PLACEMENT_TRIANGLES = 144
TYRE_TRIANGLES = 64
BOARD_TEXTURES = frozenset(('AUTOTRAD', 'AVON', 'CASTROL', 'CHEV', 'DUNLOP', 'FERODO',
                            'HANKOOK', 'LDV', 'MCN', 'MOBIL', 'OLDSPICE', 'ORDSURV',
                            'PIRELLI', 'SNAPON', '2SNAPON'))
TYRE_TEXTURES = frozenset(('TYRES', 'WHITYRES', 'BLUTYRES', 'YTYREF', 'GTYREF', 'LTYRBLK'))
RAIL_TEXTURES = frozenset(('BCBLO', 'WCBLO', 'CB-LO', 'CBW-LO', 'CBB-LO', 'CB-LO1',
                           'CB-LO2', 'PITBARR', 'PITBARR2'))


def bounds(mesh):
    return [(min(v[i] for v in mesh.vertices), max(v[i] for v in mesh.vertices)) for i in range(3)]


def uv(quadrant, u, v):
    # Leave a four-pixel gutter inside each quadrant of the512px atlas.
    x, y = quadrant % 2, quadrant // 2
    return (x * .5 + .008 + .484 * u, y * .5 + .008 + .484 * v - 1)


def patch(mesh, positions, normal, texture, quadrant):
    geometry.polygon(mesh, positions, WHITE, normal,
                     [uv(quadrant, u, v) for u, v in ((0, 0), (1, 0), (1, 1), (0, 1))], texture)


def textured_box(mesh, low, high, texture, quadrant, hidden_faces=()):
    for axis in range(3):
        a, b = [i for i in range(3) if i != axis]
        for sign in (-1, 1):
            if (axis, sign) in hidden_faces:
                continue
            normal = tuple(sign if i == axis else 0 for i in range(3))
            points = []
            for first, second in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = list(low)
                p[axis] = high[axis] if sign > 0 else low[axis]
                p[a] = high[a] if first else low[a]
                p[b] = high[b] if second else low[b]
                points.append(tuple(p))
            patch(mesh, points, normal, texture, quadrant)


def cone(original, mesh_type, texture):
    """Twelve-sided tapered body, two reflective collars and a solid rubber foot."""
    mesh = mesh_type(original.header, [], [])
    extent = bounds(original)
    cx, cz = [(extent[i][0] + extent[i][1]) / 2 for i in (0, 2)]
    bottom, top = extent[1]
    height = top - bottom
    radius = min(extent[0][1] - extent[0][0], extent[2][1] - extent[2][0]) * .435
    foot = bottom + height * .105
    textured_box(mesh, (extent[0][0], bottom, extent[2][0]),
                 (extent[0][1], foot, extent[2][1]), texture, 2)
    levels = (0., .23, .43, .62, .80, 1.)
    segments = 12
    for band, (a, b) in enumerate(zip(levels, levels[1:])):
        quadrant = 1 if band in (1, 3) else 0
        for i in range(segments):
            indices = []
            for part, t in ((i, a), (i + 1, a), (i + 1, b), (i, b)):
                angle = math.tau * part / segments
                r = radius * (1 - t * .94)
                point = (cx + r * math.cos(angle), foot + (top - foot) * t, cz + r * math.sin(angle))
                normal = geometry.unit((math.cos(angle), radius * .94 / (top - foot), math.sin(angle)))
                indices.append(mesh.vertex(point, normal, uv(quadrant, part / segments, (t - a) / (b - a))))
            mesh.triangle(indices[:3], WHITE, texture)
            mesh.triangle((indices[0], indices[2], indices[3]), WHITE, texture)
    center = mesh.vertex((cx, top, cz), (0, 1, 0), uv(0, .5, .5))
    ring = [mesh.vertex((cx + radius * .06 * math.cos(math.tau * i / segments), top,
                         cz + radius * .06 * math.sin(math.tau * i / segments)), (0, 1, 0),
                        uv(0, .5 + .4 * math.cos(math.tau * i / segments), .5 + .4 * math.sin(math.tau * i / segments)))
            for i in range(segments)]
    for i in range(segments):
        mesh.triangle((center, ring[i], ring[(i + 1) % segments]), WHITE, texture)
    return mesh


def post(original, mesh_type, texture):
    """Bevel the four corners and cap; retain the original side markings."""
    mesh = mesh_type(original.header, [], [])
    extent = bounds(original)
    x0, x1 = extent[0]
    z0, z1 = extent[2]
    y0, y1 = extent[1]
    bevel = min(x1 - x0, z1 - z0) * .12
    outline = ((x0 + bevel, z0), (x1 - bevel, z0), (x1, z0 + bevel), (x1, z1 - bevel),
               (x1 - bevel, z1), (x0 + bevel, z1), (x0, z1 - bevel), (x0, z0 + bevel))
    original_texture = struct.unpack_from('<h', original.faces[-1], 8)[0]
    for i, (x, z) in enumerate(outline):
        nx, nz = outline[(i + 1) % len(outline)]
        normal = geometry.unit((nz - z, 0, x - nx))
        geometry.polygon(mesh, ((x, y0, z), (nx, y0, nz), (nx, y1 - bevel, nz), (x, y1 - bevel, z)),
                         WHITE, normal, ((0, 0), (1, 0), (1, -.8666), (0, -.8666)), original_texture)
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
        top_a = (cx + (x - cx) * .86, y1, cz + (z - cz) * .86)
        top_b = (cx + (nx - cx) * .86, y1, cz + (nz - cz) * .86)
        patch(mesh, ((x, y1 - bevel, z), (nx, y1 - bevel, nz), top_b, top_a),
              geometry.unit((normal[0], 1, normal[2])), texture, 1)
    top = [(cx + (x - cx) * .86, y1, cz + (z - cz) * .86) for x, z in outline]
    geometry.polygon(mesh, top, WHITE, (0, 1, 0), [uv(1, (x - x0) / (x1 - x0), (z - z0) / (z1 - z0)) for x, _, z in top], texture)
    return mesh


def sign(original, mesh_type, texture, texture_names=()):
    """Keep every authored arrow/number and a simple raised metal edge."""
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    names = [name.decode('ascii') if isinstance(name, bytes) else name for name in texture_names]
    names = [name.rstrip('\0').upper() for name in names]
    for index, face in enumerate(original.faces):
        slot = struct.unpack_from('<h', face, 8)[0]
        if slot < 0 or slot >= len(names) or names[slot] != 'LDHL':
            continue
        indices = struct.unpack_from('<3H', face, 2)
        vertices = [original.vertices[corner] for corner in indices]
        if not all(.70 <= vertex[3] <= 1. and -1. <= vertex[4] <= -.52 for vertex in vertices):
            continue
        # The original LDHL corner doubles as blank sign paint. The authored
        # replacement has a different logo layout; use its safe white texel.
        # Private face corners preserve any logo/number vertices shared here.
        copied = []
        for vertex in vertices:
            copied.append(len(mesh.vertices))
            mesh.vertices.append((*vertex[:3], .99, -.99, *vertex[5:]))
        mesh.faces[index] = face[:2] + struct.pack('<3H', *copied) + face[8:]
    extent = bounds(original)
    # The canonical board lies in XZ; its actor matrix stands it upright.
    x0, x1 = extent[0]
    y0, _ = extent[1]
    z0, z1 = extent[2]
    if min(x1 - x0, z1 - z0) < .2:
        return mesh
    width = .012
    for lo, hi in [((x0, y0 - .006, z0), (x0 + width, y0 + .003, z1)),
                   ((x1 - width, y0 - .006, z0), (x1, y0 + .003, z1)),
                   ((x0 + width, y0 - .006, z0), (x1 - width, y0 + .003, z0 + width)),
                   ((x0 + width, y0 - .006, z1 - width), (x1 - width, y0 + .003, z1))]:
        textured_box(mesh, lo, hi, texture, 3)
    return mesh


def board_details(mesh, texture):
    """Give confirmed flat advertising boards a simple solid backing.

    These original DA09AD boards are in local XZ, with -Y toward the artwork;
    their unchanged parent transforms stand the complete board at the track.
    """
    (x0, x1), (y0, _), (z0, z1) = bounds(mesh)
    # Opposing Silverstone ORDSURV panels have less than .05m clearance; an .08m
    # rear crosses the opposite source artwork. Keep the solid inside the gap.
    back = y0 + .04
    # The original advertising surface is the ONLY full front face. A metal
    # face just millimeters behind it fights the original depth buffer even
    # though the two planes have distinct world coordinates.
    textured_box(mesh, (x0, y0, z0), (x1, back, z1), texture, 3, hidden_faces=((1, -1),))


def vertical_panels(mesh, texture_names, accepted):
    """Recover actual authored rectangular panels from shared triangle edges.

    A semantic material plus four corners at two horizontal positions is
    required. Arbitrary building faces, terrain and silhouettes are untouched.
    Sloped ground is retained through separate bottom/top endpoints.
    """
    triangles, edges = {}, {}
    for index, face in enumerate(mesh.faces):
        texture = struct.unpack_from('<h', face, 8)[0]
        if texture < 0 or texture >= len(texture_names) or texture_names[texture].upper() not in accepted:
            continue
        vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        points = [v[:3] for v in vertices]
        normal = geometry.unit(tuple(sum(v[axis] for v in vertices) for axis in (5, 6, 7)))
        if abs(normal[1]) > .2:
            continue
        normal = geometry.unit((normal[0], 0., normal[2]))
        triangles[index] = (texture, points, normal)
        for a, b in zip(points, points[1:] + points[:1]):
            edges.setdefault((texture, tuple(sorted((a, b)))), []).append(index)
    used = set()
    for candidates in edges.values():
        if len(candidates) != 2:
            continue
        first, second = candidates
        if first in used or second in used:
            continue
        texture, a, normal = triangles[first]
        _, b, other_normal = triangles[second]
        if geometry.dot(normal, other_normal) < .995:
            continue
        points = sorted(set(a + b))
        if len(points) != 4:
            continue
        tangent = (-normal[2], 0., normal[0])
        points.sort(key=lambda p: geometry.dot(p, tangent))
        left, right = sorted(points[:2], key=lambda p: p[1]), sorted(points[2:], key=lambda p: p[1])
        if any(abs(geometry.dot(geometry.sub(pair[1], pair[0]), tangent)) > .002 for pair in (left, right)):
            continue
        if any(abs(geometry.dot(geometry.sub(p, points[0]), normal)) > .002 for p in points[1:]):
            continue
        width = math.dist(left[0][::2], right[0][::2])
        height = min(left[1][1] - left[0][1], right[1][1] - right[0][1])
        if not .6 <= width <= 30 or not .25 <= height <= 2.5:
            continue
        used.update((first, second))
        yield texture, left[0], right[0], right[1], left[1], normal


def tyre(mesh, center, radius, height, texture):
    """An eight-sided hollow rubber tyre with broad shoulders and an inner wall."""
    inner = radius * .46
    # Keep the original broad outer and inner rings. The two tiny shoulder
    # rings disappear; retained rings keep their original texture-V positions.
    profile = ((inner, 0., -.707, -.707, 0.), (radius, height * .2, 1., 0., .4),
               (radius, height * .8, 1., 0., .6), (inner, height, -.707, .707, 1.))
    segments, rings = 8, []
    for r, y, radial, vertical, texture_v in profile:
        ring = []
        for index in range(segments + 1):
            angle = math.tau * index / segments
            c, s = math.cos(angle), math.sin(angle)
            ring.append(mesh.vertex((center[0] + r * c, center[1] + y, center[2] + r * s),
                                    (radial * c, vertical, radial * s), (index / segments, -texture_v)))
        rings.append(ring)
    for row in range(len(rings)):
        next_ring = rings[(row + 1) % len(rings)]
        for index in range(segments):
            a, b = rings[row][index:index + 2]
            c, d = next_ring[index:index + 2]
            mesh.triangle((a, b, d), WHITE, texture)
            mesh.triangle((a, d, c), WHITE, texture)


def rail_details(mesh, panel):
    """Add a physical W-beam cross section to confirmed painted steel panels."""
    texture, left, right, top_right, top_left, normal = panel
    lower = [geometry.interpolate(left, top_left, .35), geometry.interpolate(right, top_right, .35)]
    upper = [geometry.interpolate(left, top_left, .85), geometry.interpolate(right, top_right, .85)]
    profile = ((0., .018), (.25, .09), (.5, .025), (.75, .09), (1., .018))
    strips = []
    for fraction, depth in profile:
        strips.append([geometry.add(geometry.interpolate(a, b, fraction), geometry.mul(normal, depth)) for a, b in zip(lower, upper)])
    for row in range(len(strips) - 1):
        a, b = strips[row]
        c, d = strips[row + 1]
        face_normal = geometry.unit(geometry.cross(geometry.sub(b, a), geometry.sub(c, a)))
        if geometry.dot(face_normal, normal) < 0:
            face_normal = geometry.mul(face_normal, -1)
        geometry.polygon(mesh, (a, b, d, c), WHITE, face_normal,
                         ((0., -row / 4), (1., -row / 4), (1., -(row + 1) / 4), (0., -(row + 1) / 4)), texture)
    # End closures expose the actual folded steel profile at sector boundaries.
    for side in (0, 1):
        side_normal = geometry.unit(geometry.sub(right, left)) if side else geometry.unit(geometry.sub(left, right))
        for row in range(len(strips) - 1):
            base = [geometry.add(geometry.interpolate(lower[side], upper[side], fraction), geometry.mul(normal, -.012))
                    for fraction in (profile[row][0], profile[row + 1][0])]
            geometry.polygon(mesh, (strips[row][side], strips[row + 1][side], base[1], base[0]), WHITE, side_normal,
                             ((0., 0.), (1., 0.), (1., -1.), (0., -1.)), texture)


def scenery(original, mesh_type, texture_names, atlas_index, family, triangle_budget=2048, vegetation_index=None, foliage_index=None):
    """Rebuild confirmed trees or append details within a per-mesh budget.

    Tree billboards and confirmed static aircraft wings/gear are replaced;
    other existing surfaces remain exact or gain source-aligned details.
    Headers and actor/node bindings stay intact. The caller also caps the
    aggregate family budget (normally48000).
    """
    if family == 'trees':
        return mega_woodland.enhance(original, mesh_type, texture_names, triangle_budget)
    if family == 'objects':
        detailed, counts = mega_static_fleet.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_static_vectra.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_static_platform.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_static_aircraft.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_dunlop.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_barn.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_grandstand.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_cbo4.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_cbo5.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_cbo6.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_cbo1.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_cbo3.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_cbo2.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_sheep.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
        detailed, counts = mega_spectators.enhance(original, mesh_type, texture_names, atlas_index, triangle_budget)
        if counts['added_triangles']:
            return detailed, counts
    if family == 'objects' and vegetation_index is not None:
        detailed, counts = mega_vegetation.enhance(original, mesh_type, texture_names, vegetation_index, triangle_budget, foliage_index=foliage_index)
        if counts['added_triangles']:
            return detailed, counts
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    start = len(mesh.faces)
    stats = dict(boards=0, tyre_stacks=0, tyres=0, rail_panels=0, added_triangles=0)
    names = [name.decode('ascii') if isinstance(name, bytes) else name for name in texture_names]
    names = [name.rstrip('\0').upper() for name in names]
    remaining = lambda: triangle_budget - (len(mesh.faces) - start +
                                             stats['tyres'] * (TYRE_PLACEMENT_TRIANGLES - TYRE_TRIANGLES))
    if family == 'objects':
        extent = bounds(original)
        materials = {struct.unpack_from('<h', face, 8)[0] for face in original.faces}
        # Canonical advertising boards are flat local-XZ meshes. Requiring all
        # materials to be recognizable adverts avoids modifying building skins.
        if extent[1][1] - extent[1][0] < .002 and extent[0][1] - extent[0][0] > 1 and extent[2][1] - extent[2][0] > .3 \
                and materials and all(0 <= slot < len(names) and names[slot] in BOARD_TEXTURES for slot in materials) and remaining() >= 10:
            board_details(mesh, atlas_index)
            stats['boards'] = 1
    elif family == 'fence':
        for panel in vertical_panels(original, names, TYRE_TEXTURES):
            texture, left, right, top_right, top_left, normal = panel
            width = math.dist(left[::2], right[::2])
            height = min(top_left[1] - left[1], top_right[1] - right[1])
            layers = min(3, max(1, round(height / .32)))
            radius = min(.36, height * .4, width * .2)
            tire_height = height / layers * .94
            for fraction in (radius / width, 1 - radius / width):
                if remaining() < layers * TYRE_PLACEMENT_TRIANGLES:
                    break
                bottom = geometry.add(geometry.interpolate(left, right, fraction), geometry.mul(normal, radius * .35))
                local_height = (top_left[1] - left[1]) * (1 - fraction) + (top_right[1] - right[1]) * fraction
                for layer in range(layers):
                    tyre(mesh, geometry.add(bottom, (0., layer * local_height / layers, 0.)), radius, tire_height, texture)
                stats['tyre_stacks'] += 1
                stats['tyres'] += layers
        for panel in vertical_panels(original, names, RAIL_TEXTURES):
            if remaining() < 24:
                break
            rail_details(mesh, panel)
            stats['rail_panels'] += 1
    stats['added_triangles'] = len(mesh.faces) - start
    stats['allocation_triangles'] = stats['added_triangles'] + stats['tyres'] * (TYRE_PLACEMENT_TRIANGLES - TYRE_TRIANGLES)
    return mesh, stats
