"""Honda Accord components measured from canonical ALLCARS member1.

+X is forward, +Y up, Z is the axle. Mesh slots and all source damage bindings
remain untouched; only their existing local geometry is replaced or extended.
"""

from functools import partial
import math
import json
from pathlib import Path
import struct

from vehicle_model import (Mesh, add, sub, scale, dot, cross, unit, interpolate,
                           polygon, box, cylinder, decode, pack, crc32, visible_triangles)

PAINT = 1
RUBBER = (8, 12, 15, 255)
SILVER = (193, 198, 202, 255)
WHITE = (216, 220, 221, 255)
GLASS = (120, 197, 238, 255)
RED = (208, 0, 0, 255)
mul = scale

import vehicle_geometry as panels
from vehicle_geometry import edge_key, edge_controls, surface, clip, emit_source_polygon, source_boundary

shared_edge_controls = partial(panels.shared_edge_controls, paint=PAINT, excluded_meshes=(49,))
curved_mesh = partial(panels.curved_mesh, paint=PAINT)
conform_numeric_decals = partial(panels.conform_numeric_decals, paint=PAINT, decal_slots=(7, 8))
uv_vertex = partial(panels.uv_vertex, paint=PAINT)
recess_uv = partial(panels.recess_uv, paint=PAINT, wall_color=RUBBER)


def smooth_wheel_arches(meshes, definition):
    return panels.smooth_wheel_arches(
        meshes, definition, paint=PAINT, minimum_side=.55,
        atlas_coordinates=lambda vertex, side: (
            vertex[3] * 256, ((vertex[4] + 1) if side > 0 else -vertex[4]) * 256))


def radiator(mesh):
    # Canonical HONDA256 grille pixels are dark at u=.028..063,v=.34..49
    # (the narrow, vertical front-bumper chart), below the red ACCORD strip.
    # Their UVs remain untouched as the original fins become a recessed grille.
    u0, u1, v0, v1 = .028, .063, -.65, -.515
    source = Mesh(mesh.header, list(mesh.vertices), list(mesh.faces))
    recess_uv(mesh, [(u0, v0), (u1, v0), (u1, v1), (u0, v1)], .042)
    for i in range(10):
        v = v0 + (v1 - v0) * (i + .5) / 10
        a, b = uv_vertex(source, (u0 + .003, v)), uv_vertex(source, (u1 - .003, v))
        start, end = sub(a[:3], scale(unit(a[5:]), .024)), sub(b[:3], scale(unit(b[5:]), .024))
        cylinder(mesh, start, end, .002, (47, 49, 52, 255), segments=6)


def subdivide_lens(original, steps, depth):
    """Follow every source chart/fold; shared contour positions remain exact."""
    mesh = Mesh(original.header, [], [])
    for face in original.faces:
        vertices = [original.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        grid = {}
        for i in range(steps + 1):
            for j in range(steps + 1 - i):
                weights = (i / steps, j / steps, 1 - (i + j) / steps)
                point = tuple(sum(weights[a] * vertices[a][k] for a in range(3)) for k in range(8))
                normal = unit(point[5:])
                position = add(point[:3], scale(normal, depth * 27 * math.prod(weights)))
                grid[i, j] = mesh.vertex(position, normal, point[3:5])
        for i in range(steps):
            for j in range(steps - i):
                mesh.triangle((grid[i, j], grid[i + 1, j], grid[i, j + 1]), (255, 255, 255, 255), PAINT)
                if i + j < steps - 1:
                    mesh.triangle((grid[i + 1, j], grid[i + 1, j + 1], grid[i, j + 1]), (255, 255, 255, 255), PAINT)
    # A closed3mm lens edge follows the original multi-chart perimeter. Side
    # walls face inward beneath the paint, keeping the original outline full size.
    for a, b in source_boundary(original):
        normal = unit(add(a[5:], b[5:]))
        polygon(mesh, (a[:3], b[:3], sub(b[:3], scale(normal, .003)), sub(a[:3], scale(normal, .003))), RUBBER)
    return mesh


def mirror(original, side, segments=4):
    """Close the measured Accord glass silhouette with a rounded black shell."""
    mesh = Mesh(original.header, [], [])
    outline = [original.vertices[i][:3] for i in ((0, 2, 1, 3) if side > 0 else (1, 2, 0, 3))]
    if side < 0:
        outline.reverse()
    # The four source glass corners bound the casing. Round only their local
    # transitions, retaining the broad Accord mirror instead of an oval surrogate.
    perimeter = []
    for i, b in enumerate(outline):
        a, c = outline[i - 1], outline[(i + 1) % len(outline)]
        start, end = interpolate(a, b, .84), interpolate(b, c, .16)
        for step in range(segments + 1):
            t = step / segments
            perimeter.append(add(add(scale(start, (1 - t) ** 2), scale(b, 2 * t * (1 - t))), scale(end, t * t)))
    center = tuple(sum(p[k] for p in perimeter) / len(perimeter) for k in range(3))
    inner = [(p[0] + .005, center[1] + (p[1] - center[1]) * .85, center[2] + (p[2] - center[2]) * .85) for p in perimeter]
    front = [(p[0] + .115, center[1] + (p[1] - center[1]) * .84, center[2] + (p[2] - center[2]) * .84) for p in perimeter]
    glass_center = add(center, (.005, 0, 0))
    front_center = (.77, center[1] - .01, center[2] - side * .025)
    for i, a in enumerate(perimeter):
        j = (i + 1) % len(perimeter)
        polygon(mesh, (a, front[i], front[j], perimeter[j]), RUBBER)
        polygon(mesh, (a, inner[i], inner[j], perimeter[j]), RUBBER, (-1, 0, 0))
        polygon(mesh, (glass_center, inner[j], inner[i]), GLASS, (-1, 0, 0))
        polygon(mesh, (front_center, front[j], front[i]), RUBBER)
    cylinder(mesh, (.78, .25, side * .77), (.66, .29, side * .82), .021, RUBBER, segments=10)
    return mesh


def wheel_mesh(header, definition):
    """32-sided rounded tire and source-colored, fourteen-spoke Accord rim."""
    mesh = Mesh(header, [], [])
    segments = definition['wheel_segments']
    profile = definition['wheel_profile']
    rings = []
    for row, (z, radius) in enumerate(profile):
        previous, following = profile[max(0, row - 1)], profile[min(len(profile) - 1, row + 1)]
        dz, dr = following[0] - previous[0], following[1] - previous[1]
        rings.append([mesh.vertex((radius * math.cos(math.tau * i / segments), radius * math.sin(math.tau * i / segments), z),
                                  (dz * math.cos(math.tau * i / segments), dz * math.sin(math.tau * i / segments), -dr)) for i in range(segments)])
    for a, b in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i + 1) % segments
            mesh.quad((a[i], a[j], b[j], b[i]), RUBBER)
    for side in (-1, 1):
        # Canonical Honda wheel palette (74,74,82) supplies the dark recess.
        # Painting the source's spoke image here would double the 14 real spokes.
        backplate = tuple(definition['wheel_backplate_color'])
        center = mesh.vertex((0, 0, side * .099), (0, 0, side))
        rim, disk = [], []
        for i in range(segments):
            angle = math.tau * i / segments
            c, s = math.cos(angle), math.sin(angle)
            disk.append(mesh.vertex((.239 * c, .239 * s, side * .099), (0, 0, side)))
            rim.append(mesh.vertex((.258 * c, .258 * s, side * .125), (0, 0, side)))
        for i in range(segments):
            j = (i + 1) % segments
            mesh.triangle((center, disk[i], disk[j]), backplate)
            mesh.quad((disk[i], disk[j], rim[j], rim[i]), SILVER)
        for i in range(definition['wheel_spokes']):
            angle = math.tau * i / definition['wheel_spokes']
            radial, tangent = (math.cos(angle), math.sin(angle)), (-math.sin(angle), math.cos(angle))
            # Four side walls and a raised ridge form a solid tapered spoke.
            outline = [(radial[0] * r + tangent[0] * w, radial[1] * r + tangent[1] * w, side * .105)
                       for r, w in ((.055, -.008), (.24, -.006), (.24, .006), (.055, .008))]
            top = [(p[0], p[1], side * (.129 if j in (0, 3) else .118)) for j, p in enumerate(outline)]
            polygon(mesh, top, WHITE, (0, 0, side))
            polygon(mesh, list(reversed(outline)), SILVER, (0, 0, -side))
            for j, a in enumerate(outline):
                k = (j + 1) % 4
                polygon(mesh, (a, outline[k], top[k], top[j]), SILVER)
        cylinder(mesh, (0, 0, side * .101), (0, 0, side * .139), .061, SILVER, segments=16)
    return mesh


def seals(mesh, source, textures):
    for a, b in source_boundary(source, textures):
        if math.dist(a[:3], b[:3]) < .02:
            continue
        normal = unit(add(a[5:], b[5:]))
        cylinder(mesh, add(a[:3], scale(normal, .001)), add(b[:3], scale(normal, .001)), .0026, RUBBER, segments=6)


def solid_wing(mesh, source):
    """Thicken the actual two-sided Accord wing without duplicate inner walls."""
    mesh.faces = [face for face in mesh.faces if struct.unpack_from('<h', face, 8)[0] >= 0]
    triangles, seen, normals, edges = [], set(), {}, {}
    for face in source.faces:
        if struct.unpack_from('<h', face, 8)[0] >= 0:
            continue
        points = [source.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
        key = tuple(sorted(points))
        if key in seen:
            continue  # Canonical top/end plates repeat their reverse face.
        seen.add(key)
        normal = unit(cross(sub(points[1], points[0]), sub(points[2], points[0])))
        triangles.append((points, normal))
        for point in points:
            normals[point] = add(normals.get(point, (0, 0, 0)), normal)
        for a, b in zip(points, points[1:] + points[:1]):
            edges.setdefault(tuple(sorted((a, b))), []).append((a, b))
    back = {point: sub(point, scale(unit(normal), .003)) for point, normal in normals.items()}
    for points, normal in triangles:
        polygon(mesh, points, RED, normal)
        polygon(mesh, [back[p] for p in reversed(points)], RED, scale(normal, -1))
    for uses in edges.values():
        if len(uses) == 1:
            a, b = uses[0]
            polygon(mesh, (a, back[a], back[b], b), RED)


def detail_body(meshes, originals, definition):
    for mesh in meshes:
        conform_numeric_decals(mesh)
    for index in (9, 10, 11):
        radiator(meshes[index])
    # Record arch-face opacity only after operations that replace face arrays.
    smooth_wheel_arches(meshes, definition)
    for index, side in ((35, 1), (37, -1)):
        meshes[index] = mirror(originals[index], side)
    for index in (39, 41, 43, 45):
        meshes[index] = subdivide_lens(originals[index], 6, .0018)
    for index in (21, 22, 23, 24, 25, 26):
        mesh = meshes[index]
        seals(mesh, originals[index], {2})
        side = -1 if index < 24 else 1
        for x in (-.26, -1.22):
            point, normal = surface(originals[index], (x, .14, side * 1.), 2, side)
            if abs(point[2]) < .7:
                continue
            p = add(point, scale(normal, .005))
            cylinder(mesh, add(p, (-.053, 0, 0)), add(p, (.053, 0, 0)), .006, RUBBER, segments=8)
    for index in (27, 28, 29, 32, 33, 34):
        seals(meshes[index], originals[index], {3})
    for index in (13, 14):
        mesh = meshes[index]
        for z in (-.54, .54):
            p, normal = surface(originals[index], (1.88, 1., z), 1, 1)
            cylinder(mesh, p, add(p, scale(normal, .005)), .009, SILVER, segments=12)
    for index in (27, 28, 29):
        mesh = meshes[index]
        for z in (-.27, .27):
            a, na = surface(originals[index], (.9, .35, z), 1, 1)
            b, nb = surface(originals[index], (.52, .58, z - .12), 1, 1)
            cylinder(mesh, add(a, scale(na, .004)), add(b, scale(nb, .004)), .004, RUBBER, segments=6)
    for index in (47, 48):
        solid_wing(meshes[index], originals[index])


def wheel_arch_opaque_uvs(meshes):
    """Only the rebuilt narrow Honda arch bands may restore original key pixels."""
    return [tuple((mesh.vertices[i][3], mesh.vertices[i][4] + 1) for i in struct.unpack_from('<3H', mesh.faces[f], 2))
            for mesh in meshes for f in sorted(getattr(mesh, 'wheel_arch_faces', ()))
            if struct.unpack_from('<h', mesh.faces[f], 8)[0] == PAINT]


def lamps_opaque_uvs(meshes):
    """Only the four independently bound glass panels may release key pixels."""
    return [tuple((mesh.vertices[i][3], mesh.vertices[i][4] + 1) for i in struct.unpack_from('<3H', face, 2))
            for index in (39, 41, 43, 45) for mesh in (meshes[index],) for face in mesh.faces
            if struct.unpack_from('<h', face, 8)[0] == PAINT]


def detail_optics(meshes, directory):
    """Register reflector depth after the artwork's dedicated glass UV fit."""
    metadata = json.loads((Path(directory) / 'metadata.json').read_text(encoding='utf-8'))
    for index, regions in metadata.get('optics_recess_uvs', {}).items():
        if index not in ('39', '41', '43', '45') or not isinstance(regions, list):
            raise ValueError('Honda optics must address their existing healthy glass panels')
        for region in regions:
            outline, depth = region.get('outline'), region.get('depth')
            if (not isinstance(outline, list) or len(outline) < 3 or len(outline) > 64 or
                    type(depth) not in (int, float) or not math.isfinite(depth) or not 0 < depth <= .018 or
                    any(not isinstance(point, list) or len(point) != 2 or
                        any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1 for v in point)
                        for point in outline)):
                raise ValueError('Honda reflector contours/depth must remain bounded')
            turns = [((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]))
                     for a, b, c in zip(outline, outline[1:] + outline[:1], outline[2:] + outline[:2])]
            if min(turns) < -1e-12 or max(turns) <= 1e-12:
                raise ValueError('Honda reflector UV contours must be convex and counterclockwise')
            target = [(u, v - 1) for u, v in outline]
            for point in target:
                uv_vertex(meshes[int(index)], point)
            recess_uv(meshes[int(index)], target, depth, textured_walls=True)


def prepare(source, definition):
    """Prepare unquantized source-derived meshes before registered artwork UVs."""
    header, nodes, originals, textures = decode(source)
    meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
    shared = shared_edge_controls(originals, set(definition['curved_meshes']))
    for index in definition['curved_meshes']:
        meshes[index] = curved_mesh(originals[index], definition['body_subdivisions'], definition['body_max_curve'], shared)
    meshes[49] = wheel_mesh(originals[49].header, definition)
    detail_body(meshes, originals, definition)
    return header, nodes, meshes, textures


def generate(source, definition, texture_directory=None, *, quality=70):
    header, nodes, meshes, textures = prepare(source, definition)
    original_triangles = visible_triangles(nodes, decode(source)[2])
    if texture_directory is not None:
        import honda_textures
        count = struct.unpack_from('<H', header, 16)[0]
        honda_textures.fit_uvs(meshes, textures, count, texture_directory)
        detail_optics(meshes, texture_directory)
        textures = honda_textures.replace_textures(textures, count, texture_directory,
                                                  wheel_arch_opaque_uvs(meshes) + lamps_opaque_uvs(meshes), quality=quality)
    result = pack(header, nodes, meshes, textures)
    return result, {'source_model_crc32': crc32(source), 'output_crc32': crc32(result), 'bytes': len(result),
                    'original_visible_triangles': original_triangles, 'visible_triangles': visible_triangles(nodes, meshes),
                    'wheel_triangles': len(meshes[49].faces), 'wheel_vertices': len(meshes[49].vertices),
                    'stored_triangles': sum(len(m.faces) for m in meshes), 'curved_meshes': len(definition['curved_meshes'])}
