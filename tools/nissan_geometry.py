"""Nissan Primera exterior, bound to canonical ALLCARS HIGH member19.

Topology is measured from the Primera itself. Simple solid source-mounted parts
keep its original fifty mesh slots and independent damage UV charts.
"""
from functools import partial
import math
import struct

from vehicle_model import (Mesh, add, sub, scale, dot, cross, unit, interpolate,
                           polygon, cylinder, decode, pack, crc32, visible_triangles)
import vehicle_geometry as panels

PAINT = 0
RUBBER = (0, 8, 8, 255)
GLASS = (120, 197, 238, 255)
SILVER = (193, 198, 202, 255)
BRIGHT = (224, 230, 233, 255)
RECESS = (43, 46, 48, 255)
SHELL = (206, 206, 212, 255)
MOUNT = (191, 191, 197, 255)
LAMP_SLOTS = (16, 18, 45, 47)
curved_mesh = partial(panels.curved_mesh, paint=PAINT)
register_side_band = partial(panels.register_side_band, paint=PAINT)
remove_quantized_slivers = panels.remove_quantized_slivers


def wheel_mesh(header, definition):
    """Rounded350mm tire and the original atlas's six separate silver spokes."""
    mesh = Mesh(header, [], [])
    segments, profile = definition['wheel_segments'], definition['wheel_profile']
    rings = []
    for row, (z, radius) in enumerate(profile):
        a, b = profile[max(0, row - 1)], profile[min(len(profile) - 1, row + 1)]
        dz, dr = b[0] - a[0], b[1] - a[1]
        rings.append([mesh.vertex((radius * math.cos(math.tau * i / segments), radius * math.sin(math.tau * i / segments), z),
                                  (dz * math.cos(math.tau * i / segments), dz * math.sin(math.tau * i / segments), -dr))
                      for i in range(segments)])
    for a, b in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i + 1) % segments
            mesh.quad((a[i], a[j], b[j], b[i]), RUBBER)
    radius, spoke, hub = (definition[k] for k in ('wheel_rim_radius', 'wheel_spoke_radius', 'wheel_hub_radius'))
    for side in (-1, 1):
        center = mesh.vertex((0, 0, side * .092), (0, 0, side))
        disk, lip = [], []
        for i in range(segments):
            c, s = math.cos(math.tau * i / segments), math.sin(math.tau * i / segments)
            disk.append(mesh.vertex((spoke * c, spoke * s, side * .092), (0, 0, side)))
            lip.append(mesh.vertex((radius * c, radius * s, side * .125), (0, 0, side)))
        for i in range(segments):
            j = (i + 1) % segments
            mesh.triangle((center, disk[i], disk[j]), RECESS)
            mesh.quad((disk[i], disk[j], lip[j], lip[i]), SILVER)
        for i in range(definition['wheel_spokes']):
            angle = math.tau * i / definition['wheel_spokes']
            radial, tangent = (math.cos(angle), math.sin(angle)), (-math.sin(angle), math.cos(angle))
            # The source spokes taper at the barrel and widen around the hub.
            outline = [(radial[0] * r + tangent[0] * w, radial[1] * r + tangent[1] * w, side * .099)
                       for r, w in ((hub, -.025), (spoke, -.018), (spoke, .018), (hub, .025))]
            top = [(p[0], p[1], side * (.134 if j in (0, 3) else .124)) for j, p in enumerate(outline)]
            polygon(mesh, top, BRIGHT, (0, 0, side))
            polygon(mesh, list(reversed(outline)), SILVER, (0, 0, -side))
            for j, p in enumerate(outline):
                k = (j + 1) % 4
                polygon(mesh, (p, outline[k], top[k], top[j]), SILVER)
        cylinder(mesh, (0, 0, side * .097), (0, 0, side * .14), hub, SILVER, segments=16)
    return mesh


def mirror(original):
    """Close the Primera's silver housing around the measured rear-facing glass."""
    mesh = Mesh(original.header, [], [])
    glass = Mesh(original.header, list(original.vertices), [f for f in original.faces if tuple(f[10:14]) == GLASS])
    edges = panels.source_boundary(glass)
    if len(edges) != 4:
        raise ValueError('Nissan mirror requires its four source glass edges')
    outline = [edges[0][0][:3], edges[0][1][:3]]
    while len(outline) < 4:
        candidates = [(a, b) for a, b in edges if a[:3] == outline[-1] and b[:3] not in outline]
        if not candidates:
            candidates = [(b, a) for a, b in edges if b[:3] == outline[-1] and a[:3] not in outline]
        if len(candidates) != 1:
            raise ValueError('Nissan mirror glass must form one source loop')
        outline.append(candidates[0][1][:3])
    if cross(sub(outline[1], outline[0]), sub(outline[2], outline[0]))[0] > 0:
        outline.reverse()
    side = 1 if sum(p[2] for p in outline) > 0 else -1
    outer = sorted(outline, key=lambda p: side * p[2])[-2:]
    # The inboard upper corner belongs to the original Primera A-pillar mounting
    # wedge. Expanding it with the housing made a large slab over the door.
    top = max(p[1] for p in outer)
    inner_z = min(outline, key=lambda p: p[1])[2]
    for axis, limit, sign in ((1, top, -1), (2, inner_z, side)):
        clipped = []
        a = outline[-1]
        da = sign * (a[axis] - limit)
        for b in outline:
            db = sign * (b[axis] - limit)
            if da < 0 < db or db < 0 < da:
                clipped.append(interpolate(a, b, da / (da - db)))
            if db >= 0:
                clipped.append(b)
            a, da = b, db
        outline = clipped
    perimeter = []
    for i, b in enumerate(outline):
        a, c = outline[i - 1], outline[(i + 1) % len(outline)]
        start, end = interpolate(a, b, .86), interpolate(b, c, .14)
        for j in range(3):
            t = j / 2
            perimeter.append(add(add(scale(start, (1 - t) ** 2), scale(b, 2 * t * (1 - t))), scale(end, t * t)))
    center = tuple(sum(p[k] for p in perimeter) / len(perimeter) for k in range(3))
    # The source's far-forward vertex is the body attachment, not the shell.
    # Extrude the measured glass perimeter locally; connect its stalk below.
    front = [(p[0] + .045, center[1] + (p[1] - center[1]) * .80,
              center[2] + (p[2] - center[2]) * .86) for p in perimeter]
    inner = [(p[0] + .003, center[1] + (p[1] - center[1]) * .88, center[2] + (p[2] - center[2]) * .88) for p in perimeter]
    front_center, glass_center = add(center, (.055, 0, 0)), add(center, (.003, 0, 0))
    for i, p in enumerate(perimeter):
        j = (i + 1) % len(perimeter)
        polygon(mesh, (p, front[i], front[j], perimeter[j]), SHELL)
        polygon(mesh, (p, perimeter[j], inner[j], inner[i]), RECESS)
        polygon(mesh, (glass_center, inner[i], inner[j]), GLASS)
        polygon(mesh, (front_center, front[j], front[i]), SHELL)
    attachment = max(original.vertices, key=lambda v: v[0])[:3]
    cylinder(mesh, front_center, attachment, .014, RUBBER, segments=8)
    source_glass = [original.vertices[i] for face in glass.faces
                    for i in struct.unpack_from('<3H', face, 2)]
    mounting = [max(source_glass, key=lambda v: v[1])[:3],
                min(source_glass, key=lambda v: v[1])[:3], attachment]
    if cross(sub(mounting[1], mounting[0]), sub(mounting[2], mounting[0]))[2] * side < 0:
        mounting.reverse()
    back = [add(p, (0, 0, -side * .004)) for p in mounting]
    polygon(mesh, mounting, MOUNT)
    polygon(mesh, list(reversed(back)), MOUNT)
    for i, a in enumerate(mounting):
        j = (i + 1) % 3
        polygon(mesh, (a, back[i], back[j], mounting[j]), MOUNT)
    return mesh


def wing_faces(source):
    """The Primera's silver top and high-U painted sloping endplates only."""
    return [face for face in source.faces
            if tuple(face[10:14]) == SHELL or
            (struct.unpack_from('<h', face, 8)[0] == PAINT and
             all(source.vertices[i][3] > .9 for i in struct.unpack_from('<3H', face, 2)))]


def solid_wing(mesh, source):
    """Four-millimetre shell of the actual Primera top and two sloped endplates."""
    unique, normals, edges = {}, {}, {}
    for face in wing_faces(source):
        vertices = [source.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        key = tuple(sorted(p[:3] for p in vertices))
        if key in unique:
            continue
        normal = unit(cross(sub(vertices[1][:3], vertices[0][:3]), sub(vertices[2][:3], vertices[0][:3])))
        if (abs(normal[2]) > .5 and normal[2] * sum(p[2] for p in vertices) < 0) or (abs(normal[1]) > .5 and normal[1] < 0):
            vertices.reverse()
            normal = scale(normal, -1)
        unique[key] = vertices, normal, face
        for p in vertices:
            normals[p[:3]] = add(normals.get(p[:3], (0, 0, 0)), normal)
        for a, b in zip(vertices, vertices[1:] + vertices[:1]):
            edges.setdefault(panels.edge_key(a, b), []).append((a, b))
    back = {p: sub(p, scale(unit(n), .004)) for p, n in normals.items()}
    for vertices, normal, face in unique.values():
        texture, color = struct.unpack_from('<h', face, 8)[0], tuple(face[10:14])
        polygon(mesh, [p[:3] for p in vertices], color, normal, [p[3:5] for p in vertices], texture)
        polygon(mesh, [back[p[:3]] for p in reversed(vertices)], RUBBER, scale(normal, -1))
    for uses in edges.values():
        if len(uses) == 1:
            a, b = uses[0]
            polygon(mesh, (a[:3], back[a[:3]], back[b[:3]], b[:3]), RUBBER)


def closed_lens(original):
    return panels.closed_lens(original, back_color=RUBBER)


def arch_coordinates(vertex, side):
    u, v = vertex[3] * 256, (vertex[4] + 1) * 256
    return u, v if side > 0 else 255.953125 - v


def flare_front_fenders(meshes, nodes, definition):
    """Move the front paint laterally before any subdivision or aperture cuts.

    A single smooth field applies to every source copy of a physical point,
    including adjoining bonnet/door/damage panels. OriginalXY/UVs and all
    matrices stay fixed. The source windshield, lamps and mirror mounts lie
    outside the field; the underfloor and lower sill fade to zero as well.
    """
    amount = definition.get('front_fender_flare', 0.)
    if not 0 <= amount <= .03:
        raise ValueError('Nissan front fender flare must stay within30mm')
    if not amount:
        return
    import vehicle_wheel_wells as wells
    _, wheel, centers = wells.anchors(nodes)
    center_x = centers[0][0]

    def ramp(t):
        if t <= 0:
            return 0., 0.
        if t >= 1:
            return 1., 0.
        return t*t*(3-2*t), 6*t*(1-t)

    cache = {}
    def field(point):
        if point in cache:
            return cache[point]
        x, y, z = point
        side = 1 if z > 0 else -1
        dx = x-center_x
        sx, dsx = ramp((abs(dx)-.28)/.22)
        lower, dlower = ramp((y+.55)/.20)
        upper, dupper = ramp((.18-y)/.10)
        sz, dsz = ramp((abs(z)-.55)/.15)
        xgate, ygate = 1-sx, lower*upper
        gx = -dsx*(1 if dx > 0 else -1)/.22
        gy = dlower/.20*upper-lower*dupper/.10
        delta = amount*xgate*ygate*sz
        result = (x, y, z+side*delta)
        jacobian = (side*amount*gx*ygate*sz,
                    side*amount*xgate*gy*sz,
                    1+amount*xgate*ygate*dsz/.15)
        cache[point] = result, jacobian
        return cache[point]

    for index, mesh in enumerate(meshes):
        if index == wheel:
            continue
        result = []
        for p in mesh.vertices:
            position, (dx, dy, dz) = field(p[:3])
            if position == p[:3]:
                result.append(p)
                continue
            nx, ny, nz = p[5:]
            normal = unit((nx-dx/dz*nz, ny-dy/dz*nz, nz/dz))
            result.append((*position, *p[3:5], *normal))
        mesh.vertices = result


def cut_wheel_apertures(meshes, nodes, definition):
    """Cut the four physical openings directly from the unchanged curved skin.

    The canonical upper mouths lie247..309mm from their wheel centers. A
    common measured polygon removes that narrow paint from every adjoining panel;
    no existing vertex moves and shared fender/bumper edges stay continuous.
    Below the cut, the source's wider lower mouth continues to the sill.
    """
    import vehicle_wheel_wells as wells
    _, _, centers = wells.anchors(nodes)
    radii = definition['wheel_aperture_radii']
    heights = definition.get('wheel_aperture_heights', radii)
    powers = definition.get('wheel_aperture_powers', [2, 2])
    if len(powers) != 2 or any(p not in (2, 4) for p in powers):
        raise ValueError('Nissan aperture powers must be ellipse2 or rounded-shoulder4 per axle')
    if len(radii) != 2 or not .35 <= radii[0] <= .41 or not .31 <= radii[1] <= .41:
        raise ValueError('Nissan apertures require measured front350..410/rear310..410mm radii')
    if len(heights) != 2 or any(not .25 <= h <= r for h, r in zip(heights, radii)):
        raise ValueError('Nissan aperture crown heights require250mm through their axle radius')

    def clip(poly, a, b, sign):
        if not poly:
            return []
        def distance(p):
            return sign * ((b[0]-a[0])*(p[1]-a[1]) - (b[1]-a[1])*(p[0]-a[0]))
        result = []
        previous, da = poly[-1], distance(poly[-1])
        for current in poly:
            db = distance(current)
            if (da >= 0) != (db >= 0):
                result.append(interpolate(previous, current, da/(da-db)))
            if db >= 0:
                result.append(current)
            previous, da = current, db
        return result

    for index in definition['arch_meshes']:
        meshes[index].wheel_arch_faces = set()
    for wheel, center in enumerate(centers):
        radius, height, power = radii[wheel//2], heights[wheel//2], powers[wheel//2]
        side = 1 if center[2] > 0 else -1
        boundary = [(center[0]+radius*math.copysign(abs(math.cos(math.tau*i/48))**(2/power), math.cos(math.tau*i/48)),
                     center[1]+height*math.copysign(abs(math.sin(math.tau*i/48))**(2/power), math.sin(math.tau*i/48)))
                    for i in range(48)]
        for index in definition['arch_meshes']:
            mesh = meshes[index]
            old_faces, old_marks = mesh.faces, mesh.wheel_arch_faces
            mesh.faces, marks = [], set()
            for face_index, face in enumerate(old_faces):
                first = len(mesh.faces)
                tri = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                candidate = (struct.unpack_from('<h', face, 8)[0] == PAINT and
                             min(side*p[2] for p in tri) > .55 and
                             max(p[0] for p in tri) > center[0]-radius and
                             min(p[0] for p in tri) < center[0]+radius and
                             max(p[1] for p in tri) > center[1]-height and
                             min(p[1] for p in tri) < center[1]+height)
                if not candidate:
                    mesh.faces.append(face)
                else:
                    inside, outside = tri, []
                    for a, b in zip(boundary, boundary[1:]+boundary[:1]):
                        part = clip(inside, a, b, -1)
                        if len(part) >= 3:
                            outside.append(part)
                        inside = clip(inside, a, b, 1)
                        if len(inside) < 3:
                            break
                    for part in outside:
                        panels.emit_source_polygon(mesh, part, face)
                    marks.update(range(first, len(mesh.faces)))
                if face_index in old_marks:
                    marks.update(range(first, len(mesh.faces)))
            mesh.wheel_arch_faces = marks


def wheel_arch_opaque_uvs(meshes):
    return [tuple((mesh.vertices[i][3], mesh.vertices[i][4] + 1) for i in struct.unpack_from('<3H', mesh.faces[f], 2))
            for mesh in meshes for f in sorted(getattr(mesh, 'wheel_arch_faces', ()))
            if struct.unpack_from('<h', mesh.faces[f], 8)[0] == PAINT]


def lamps_opaque_uvs(meshes):
    return [tuple((meshes[index].vertices[i][3], meshes[index].vertices[i][4] + 1) for i in struct.unpack_from('<3H', face, 2))
            for index in LAMP_SLOTS for face in meshes[index].faces
            if struct.unpack_from('<h', face, 8)[0] == PAINT]


def bumper_fold_opaque_uvs(meshes):
    """Release source key bleed only on surviving solid lower bumper corners.

    These chart strips cover the actual joined paint below the two red bands.
    The grille, air openings and underbody retain their canonical masks.
    """
    result = []
    for index in (9, 10, 11):
        mesh = meshes[index]
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != PAINT:
                continue
            for left, right, top, bottom in ((246, 256, 149, 156), (163, 174, 149, 156),
                                             (248, 256, 0, 6), (248, 256, 250, 256)):
                part = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                for axis, bound, sign in ((3, left/256, 1), (3, right/256, -1),
                                          (4, top/256-1, 1), (4, bottom/256-1, -1)):
                    part = panels.clip(part, axis, bound, sign)
                for i in range(1, len(part)-1):
                    result.append(tuple((p[3], p[4]+1) for p in (part[0], part[i], part[i+1])))
    return result


def register_lamp_folds(meshes):
    """Register front dark outlines and rear red bands at their actual folds.

    The unchanged source also has visible steps here. The generated charts
    retain separate reflections; only their shared outline/band phase moves,
    fading into the adjoining paint and retaining complete source motifs.
    """
    for selected, top, bottom, offset in (
            ((0, 1, 2, 9, 10, 11), 32.54296875, 24.5703125, .17857),
            ((4, 5, 6, 9, 10, 11), 223.40625, 231.3828125, .1355)):
        register_side_band(meshes, selected, 235, 256, top, bottom,
                           (-.4, 0., 1., 1.4), (-.4, offset, 1., 1.4), (243, 256))
    for selected, top, bottom, knots, mapped in (
            ((16, 20, 21, 22, 30, 31, 32), 44.55078125, 26.9765625,
             (0., .215, .575, 1.), (0., .26, .5975, 1.)),
            ((18, 20, 21, 22, 27, 28, 29), 211.40234375, 228.9765625,
             (0., .215, .595, 1.), (0., .2125, .5601, 1.))):
        register_side_band(meshes, selected, 0, 16, top, bottom, knots, mapped, (0, 5))


def register_bumper_folds(meshes):
    """Join both front red/white wraps and the rear gray crease locally."""
    for selected, top, bottom, knots, mapped in (
            ((9, 10, 11), 0, 256,
             (0., 10.34707, 15.29294, 16.86930, 20.17965, 24.57031),
             (0., 10.09137, 14.42594, 16.47521, 19.15502, 24.57031)),
            ((9, 10, 11), 0, 256,
             (231.38281, 236.21746, 239.31391, 241.40401, 246.41579, 255.953125),
             (231.38281, 236.37228, 239.08167, 241.01695, 245.37247, 255.953125))):
        register_side_band(meshes, selected, 230, 256, top, bottom,
                           tuple(v/256 for v in knots), tuple(v/256 for v in mapped), (250, 256))
    # The positive rear gray crease already meets its main chart. On the
    # negative side, remove the measured10.6mm step below the rear lamp.
    register_side_band(meshes, (20, 21, 22), 0, 16, 0, 256,
                       tuple(v/256 for v in (232., 239.23153, 246.58594)),
                       tuple(v/256 for v in (232., 238.62158, 246.58594)), (0, 4))


def prepare(source, definition):
    if definition['source_model_crc32'] != '73b8aae9' or crc32(source) != '73b8aae9':
        raise ValueError('Nissan geometry requires canonical HIGH member19')
    header, nodes, originals, textures = decode(source)
    meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
    flare_front_fenders(meshes, nodes, definition)
    for index in (24, 25):
        selected = set(wing_faces(originals[index]))
        meshes[index].faces = [face for face in meshes[index].faces if face not in selected]
    shared = panels.shared_edge_controls(meshes, set(definition['curved_meshes']), paint=PAINT, excluded_meshes=(49,))
    for index in definition['curved_meshes']:
        meshes[index] = curved_mesh(meshes[index], definition['body_subdivisions'], definition['body_max_curve'], shared)
    meshes[49] = wheel_mesh(originals[49].header, definition)
    for index in (13, 14, 27, 28, 29, 30, 31, 32):
        panels.conform_numeric_decals(meshes[index], paint=PAINT, decal_slots=(7, 8))
    cut_wheel_apertures(meshes, nodes, definition)
    for index in (41, 43):
        meshes[index] = mirror(originals[index])
    for index in LAMP_SLOTS:
        meshes[index] = closed_lens(originals[index])
    for index in (24, 25):
        solid_wing(meshes[index], originals[index])
    remove_quantized_slivers(meshes, range(50))
    return header, nodes, meshes, textures


def generate(source, definition, texture_directory=None, *, quality=70):
    header, nodes, meshes, textures = prepare(source, definition)
    if texture_directory is not None:
        import nissan_textures
        count = struct.unpack_from('<H', header, 16)[0]
        nissan_textures.fit_uvs(meshes, textures, count, texture_directory)
        register_lamp_folds(meshes)
        register_bumper_folds(meshes)
        remove_quantized_slivers(meshes, range(50))
        textures = nissan_textures.replace_textures(textures, count, texture_directory,
                                                 wheel_arch_opaque_uvs(meshes) + lamps_opaque_uvs(meshes) + bumper_fold_opaque_uvs(meshes), quality=quality)
    result = pack(header, nodes, meshes, textures)
    return result, {'source_model_crc32': crc32(source), 'output_crc32': crc32(result), 'bytes': len(result),
                    'original_visible_triangles': visible_triangles(nodes, decode(source)[2]),
                    'visible_triangles': visible_triangles(nodes, meshes),
                    'stored_triangles': sum(len(m.faces) for m in meshes),
                    'wheel_triangles': len(meshes[49].faces), 'wheel_vertices': len(meshes[49].vertices),
                    'curved_meshes': len(definition['curved_meshes'])}
