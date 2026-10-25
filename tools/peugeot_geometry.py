"""Peugeot406 exterior measured from canonical1.1a ALLCARS HIGH member22.

The curved source skin is clipped coherently in world space. Original mesh
slots, damage variants, UV charts and all wheel matrices remain source-bound.
"""
from functools import partial
import math
import struct
from vehicle_model import (Mesh, add, sub, scale, dot, cross, unit, interpolate,
                           polygon, cylinder, decode, pack, crc32, visible_triangles)
import vehicle_geometry as panels

PAINT = 1
RUBBER = (0, 8, 8, 255)
GLASS = (120, 197, 238, 255)
SILVER = (193, 198, 202, 255)
BRIGHT = (224, 230, 233, 255)
RECESS = (43, 46, 48, 255)
SHELL = (8, 115, 24, 255)
MOUNT = (7, 106, 22, 255)
LAMP_SLOTS = (16, 18, 45, 47)
curved_mesh = partial(panels.curved_mesh, paint=PAINT)
register_side_band = partial(panels.register_side_band, paint=PAINT)
remove_quantized_slivers = panels.remove_quantized_slivers


def wheel_mesh(header, definition):
    return panels.spoked_wheel(header, definition, rubber=RUBBER, recess=RECESS, silver=SILVER, bright=BRIGHT)


def mirror(original):
    """Close the Peugeot406's green housing around the measured rear-facing glass."""
    mesh = Mesh(original.header, [], [])
    glass = Mesh(original.header, list(original.vertices), [f for f in original.faces if tuple(f[10:14]) == GLASS])
    edges = panels.source_boundary(glass)
    if len(edges) != 4:
        raise ValueError('Peugeot mirror requires its four source glass edges')
    outline = [edges[0][0][:3], edges[0][1][:3]]
    while len(outline) < 4:
        candidates = [(a, b) for a, b in edges if a[:3] == outline[-1] and b[:3] not in outline]
        if not candidates:
            candidates = [(b, a) for a, b in edges if b[:3] == outline[-1] and a[:3] not in outline]
        if len(candidates) != 1:
            raise ValueError('Peugeot mirror glass must form one source loop')
        outline.append(candidates[0][1][:3])
    if cross(sub(outline[1], outline[0]), sub(outline[2], outline[0]))[0] > 0:
        outline.reverse()
    side = 1 if sum(p[2] for p in outline) > 0 else -1
    outer = sorted(outline, key=lambda p: side * p[2])[-2:]
    # The inboard upper corner belongs to the original Peugeot406 A-pillar mounting
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


def solid_wing(mesh, source):
    """Four-millimetre shell of the actual Peugeot406 top and two sloped endplates."""
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


def ellipse_boundary(center, radius, height):
    return [(center[0]+radius*math.cos(math.tau*i/48),
             center[1]+height*math.sin(math.tau*i/48)) for i in range(48)]


def repair_front_crown(meshes, nodes, definition):
    """Extend the one original crown notch onto the accepted elliptical edge.

    Both healthy source mouths have a corner8.6mm above the48-edge ellipse at
    83.53degrees. Move that paint corner down locally before shared curving;
    retain its UV and apply the same correction to its damaged chart copies,
    preserving their original deformation rather than flattening damage.
    """
    import vehicle_wheel_wells as wells
    _,_,centers = wells.anchors(nodes)
    boundary = ellipse_boundary(centers[0],definition['wheel_aperture_radii'][0],
                                definition['wheel_aperture_heights'][0])
    for slots in definition['mesh_roles']['front_fenders']:
        anchor = meshes[slots[0]].vertices[3]
        intersections = []
        for a,b in zip(boundary,boundary[1:]+boundary[:1]):
            if min(a[0],b[0]) <= anchor[0] <= max(a[0],b[0]) and abs(b[0]-a[0]) > 1e-12:
                t = (anchor[0]-a[0])/(b[0]-a[0])
                intersections.append(a[1]+t*(b[1]-a[1]))
        offset = max(intersections)-anchor[1]
        if not -.012 <= offset < 0:
            raise ValueError('Peugeot crown repair requires its measured local source notch')
        for index in sorted(set(slots[:-1])):
            mesh = meshes[index]
            mesh.vertices = [(*p[:1],p[1]+offset,*p[2:]) if p[3:5] == anchor[3:5] else p
                             for p in mesh.vertices]


def cut_wheel_apertures(meshes, nodes, definition):
    """Clip four physical openings coherently from the source curved skin.

    Smooth front and shallow rear ellipses follow the circular tire profile.
    Shared source edges retain one exact cut in adjoining paint and damage
    copies. The wider lower source mouth continues
    to the sill; no existing vertex is moved radially.
    """
    import vehicle_wheel_wells as wells
    _, _, centers = wells.anchors(nodes)
    radii = definition['wheel_aperture_radii']
    heights = definition.get('wheel_aperture_heights', radii)
    if len(radii) != 2 or not .35 <= radii[0] <= .41 or not .31 <= radii[1] <= .41:
        raise ValueError('Peugeot apertures require measured front350..410/rear310..410mm radii')
    if len(heights) != 2 or any(not .25 <= h <= r for h, r in zip(heights, radii)):
        raise ValueError('Peugeot aperture crown heights require250mm through their axle radius')

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
        radius, height = radii[wheel//2], heights[wheel//2]
        side = 1 if center[2] > 0 else -1
        boundary = ellipse_boundary(center,radius,height)
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
                nearby = (struct.unpack_from('<h', face, 8)[0] == PAINT and
                          min(side*p[2] for p in tri) > .55 and
                          max(p[0] for p in tri) > center[0]-radius-.08 and
                          min(p[0] for p in tri) < center[0]+radius+.08 and
                          max(p[1] for p in tri) > center[1]-height-.08 and
                          min(p[1] for p in tri) < center[1]+height+.08)
                if not candidate:
                    mesh.faces.append(face)
                    if nearby:
                        marks.update(range(first, len(mesh.faces)))
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


def wing_faces(source):
    """Source green horizontal blade and its two independently charted fins."""
    return [face for face in source.faces
            if tuple(face[10:14]) == SHELL or
            (struct.unpack_from('<h', face, 8)[0] == PAINT and
             all(p[0] < -1.9 and p[1] > .32 for p in
                 (source.vertices[i] for i in struct.unpack_from('<3H', face, 2))))]


def flare_front_fenders(meshes, nodes, definition):
    """Move the front paint laterally before any subdivision or aperture cuts.

    A single smooth field applies to every source copy of a physical point,
    including adjoining bonnet/door/damage panels. OriginalXY/UVs and all
    matrices stay fixed. The source windshield, lamps and mirror mounts lie
    outside the field; the underfloor and lower sill fade to zero as well.
    """
    amount = definition.get('front_fender_flare', 0.)
    if not 0 <= amount <= .045:
        raise ValueError('Peugeot front fender flare must stay within45mm')
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
        sx, dsx = ramp((abs(dx)-.32)/.22)
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

def prepare(source, definition):
    if definition['source_model_crc32'] != 'c6af623c' or crc32(source) != 'c6af623c':
        raise ValueError('Peugeot geometry requires canonical HIGH member22')
    header, nodes, originals, textures = decode(source)
    meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
    repair_front_crown(meshes, nodes, definition)
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
        import peugeot_textures
        count = struct.unpack_from('<H', header, 16)[0]
        peugeot_textures.fit_uvs(meshes, textures, count, texture_directory)
        register_lamp_folds(meshes)
        remove_quantized_slivers(meshes, range(50))
        textures = peugeot_textures.replace_textures(textures, count, texture_directory,
                                                   wheel_arch_opaque_uvs(meshes) + lamps_opaque_uvs(meshes) + bumper_fold_opaque_uvs(meshes), quality=quality)
    result = pack(header, nodes, meshes, textures)
    return result, {'source_model_crc32': crc32(source), 'output_crc32': crc32(result), 'bytes': len(result),
                    'original_visible_triangles': visible_triangles(nodes, decode(source)[2]),
                    'visible_triangles': visible_triangles(nodes, meshes),
                    'stored_triangles': sum(len(m.faces) for m in meshes),
                    'wheel_triangles': len(meshes[49].faces), 'wheel_vertices': len(meshes[49].vertices),
                    'curved_meshes': len(definition['curved_meshes'])}


def register_lamp_folds(meshes):
    """Register only source-supported paint and red-band onsets at real folds.

    Reflections retain their individual chart details. Bounded UV fields move
    the gold/lamp boundary and rear red band phase, fading into adjacent paint;
    positions, normals, source material identities and complete motifs remain.
    """
    register_side_band(meshes, (0,1,2,9,10,11), 242,256,32.52734375,22.30078125,
                       (0.,.6,.8275,1.), (0.,.6,.8075,1.), (248,256))
    register_side_band(meshes, (4,5,6,9,10,11), 242,256,223.52734375,233.75390625,
                       (0.,.6,.8325,1.), (0.,.6,.7725,1.), (248,256))
    register_side_band(meshes, (16,20,21,22,30,31,32), 0,16,37.4140625,25.265625,
                       (0.,.325,.6675,1.), (0.,.3825,.7425,1.), (0,5))


def bumper_fold_opaque_uvs(meshes):
    """Actual solid front bumper corner footprints, clipped to chart edges."""
    result = []
    for index in (9,10,11):
        mesh = meshes[index]
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != PAINT:
                continue
            for left,right,top,bottom in ((81,86,158,184),(0,4,158,184),
                                           (254,256,0,24),(254,256,232,256)):
                part = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                for axis,bound,sign in ((3,left/256,1),(3,right/256,-1),
                                        (4,top/256-1,1),(4,bottom/256-1,-1)):
                    part = panels.clip(part,axis,bound,sign)
                for i in range(1,len(part)-1):
                    result.append(tuple((p[3],p[4]+1) for p in (part[0],part[i],part[i+1])))
    return result
