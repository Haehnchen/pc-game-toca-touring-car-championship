"""Ford Mondeo exterior, bound to canonical ALLCARS HIGH member16.

Topology is measured from the Mondeo itself. Simple solid source-mounted parts
keep its original fifty mesh slots and independent damage UV charts.
"""
from functools import partial
import json
import math
from pathlib import Path
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
BLUE = (40, 58, 139, 255)
LAMP_SLOTS = (16, 18, 45, 47)
uv_vertex = partial(panels.uv_vertex, paint=PAINT)
recess_uv = partial(panels.recess_uv, paint=PAINT, wall_color=RUBBER)
curved_mesh = partial(panels.curved_mesh, paint=PAINT)
remove_quantized_slivers = panels.remove_quantized_slivers


def wheel_mesh(header, definition):
    return panels.spoked_wheel(header, definition, rubber=RUBBER, recess=RECESS, silver=SILVER, bright=BRIGHT)


def mirror(original):
    """Close the outboard mirror; keep the source mounting wedge separate."""
    mesh = Mesh(original.header, [], [])
    glass = Mesh(original.header, list(original.vertices), [f for f in original.faces if tuple(f[10:14]) == GLASS])
    edges = panels.source_boundary(glass)
    if len(edges) != 4:
        raise ValueError('Ford mirror requires its four source glass edges')
    outline = [edges[0][0][:3], edges[0][1][:3]]
    while len(outline) < 4:
        candidates = [(a, b) for a, b in edges if a[:3] == outline[-1] and b[:3] not in outline]
        if not candidates:
            candidates = [(b, a) for a, b in edges if b[:3] == outline[-1] and a[:3] not in outline]
        if len(candidates) != 1:
            raise ValueError('Ford mirror glass must form one source loop')
        outline.append(candidates[0][1][:3])
    if cross(sub(outline[1], outline[0]), sub(outline[2], outline[0]))[0] > 0:
        outline.reverse()
    side = 1 if sum(p[2] for p in outline) > 0 else -1
    outer = sorted(outline, key=lambda p: side * p[2])[-2:]
    # The inboard upper corner belongs to the original A-pillar mounting
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
    front = [(p[0] + .045, center[1] + (p[1] - center[1]) * .80, center[2] + (p[2] - center[2]) * .86) for p in perimeter]
    inner = [(p[0] + .003, center[1] + (p[1] - center[1]) * .88, center[2] + (p[2] - center[2]) * .88) for p in perimeter]
    front_center, glass_center = add(center, (.055, 0, 0)), add(center, (.003, 0, 0))
    for i, p in enumerate(perimeter):
        j = (i + 1) % len(perimeter)
        polygon(mesh, (p, front[i], front[j], perimeter[j]), BLUE)
        polygon(mesh, (p, perimeter[j], inner[j], inner[i]), RECESS)
        polygon(mesh, (glass_center, inner[i], inner[j]), GLASS)
        polygon(mesh, (front_center, front[j], front[i]), BLUE)
    attachment = max(original.vertices, key=lambda v: v[0])[:3]
    cylinder(mesh, front_center, attachment, .014, RUBBER, segments=8)
    # Retain the thin inboard mounting plate at the original body points.
    # It closes the A-pillar junction without becoming part of the housing.
    mounting = [max(original.vertices, key=lambda v: v[1])[:3],
                min(original.vertices, key=lambda v: v[1])[:3], attachment]
    if cross(sub(mounting[1], mounting[0]), sub(mounting[2], mounting[0]))[2] * side < 0:
        mounting.reverse()
    back = [add(p, (0, 0, -side * .004)) for p in mounting]
    polygon(mesh, mounting, BLUE)
    polygon(mesh, list(reversed(back)), BLUE)
    for i, a in enumerate(mounting):
        j = (i + 1) % 3
        polygon(mesh, (a, back[i], back[j], mounting[j]), BLUE)
    return mesh


def wing_faces(source):
    """The Mondeo's blue top and high-U painted sloping endplates only."""
    return [face for face in source.faces
            if tuple(face[10:14]) == BLUE or
            (struct.unpack_from('<h', face, 8)[0] == PAINT and
             all(source.vertices[i][3] > .9 for i in struct.unpack_from('<3H', face, 2)))]


def solid_wing(mesh, source):
    """Four-millimetre shell of the actual Mondeo top and two sloped endplates."""
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
    return u, v if side > 0 else 255.36328125 - v


def smooth_wheel_arches(meshes, definition):
    panels.smooth_wheel_arches(meshes, definition, paint=PAINT,
                              atlas_coordinates=arch_coordinates, minimum_side=.55)


def wheel_arch_opaque_uvs(meshes):
    return [tuple((mesh.vertices[i][3], mesh.vertices[i][4] + 1) for i in struct.unpack_from('<3H', mesh.faces[f], 2))
            for mesh in meshes for f in sorted(getattr(mesh, 'wheel_arch_faces', ()))
            if struct.unpack_from('<h', mesh.faces[f], 8)[0] == PAINT]


def lamps_opaque_uvs(meshes):
    return [tuple((meshes[index].vertices[i][3], meshes[index].vertices[i][4] + 1) for i in struct.unpack_from('<3H', face, 2))
            for index in LAMP_SLOTS for face in meshes[index].faces
            if struct.unpack_from('<h', face, 8)[0] == PAINT]


def register_lamp_folds(meshes, originals):
    """Join neutral front frames and rear clear bands at physical paint folds.

    Original front outline steps are source-supported. The redraw's charts
    retain separate reflections; only their outline and band registration
    changes. Matching bumper/quarter charts receive the same local UV field.
    """
    # Corner displacement is barycentric over source triangles, including the
    # adjoining bumper rather than ending the correction at the lens slot.
    for lamp,corner,bottom in ((45,1,132.35),(47,0,132.10)):
        anchor=originals[lamp].vertices[corner]
        delta=bottom/256-1-anchor[4]
        for index in (lamp,9,10,11):
            original=originals[index]
            charts=[]
            for face in original.faces:
                if struct.unpack_from('<h',face,8)[0] != PAINT:
                    continue
                tri=[original.vertices[i] for i in struct.unpack_from('<3H',face,2)]
                matches=[i for i,p in enumerate(tri) if math.dist(p[3:5],anchor[3:5])<1e-8]
                if matches:
                    charts.append((tri,matches[0]))
            for vertex,p in enumerate(meshes[index].vertices):
                for tri,corner_index in charts:
                    a,b,c=tri
                    denominator=(b[4]-c[4])*(a[3]-c[3])+(c[3]-b[3])*(a[4]-c[4])
                    if abs(denominator)<1e-12:
                        continue
                    wa=((b[4]-c[4])*(p[3]-c[3])+(c[3]-b[3])*(p[4]-c[4]))/denominator
                    wb=((c[4]-a[4])*(p[3]-c[3])+(a[3]-c[3])*(p[4]-c[4]))/denominator
                    weights=wa,wb,1-wa-wb
                    if min(weights)>=-1e-7:
                        meshes[index].vertices[vertex]=(*p[:4],p[4]+weights[corner_index]*delta,*p[5:])
                        break
    for selected,top,bottom,first,last,sample_first,sample_last in (
            ((16,30,31,32),55.71484375,34.421875,.82,.93,.8425,.9575),
            ((18,27,28,29),199.6484375,220.9453125,.825,.9275,.815,.9125)):
        register_side_band(meshes,selected,3.3,12,top,bottom,
                           (0.,.75,first,last,1.),(0.,.75,sample_first,sample_last,1.),fade=(3.3,6))


def register_side_band(meshes, selected, left, right, top, bottom, knots, mapped, fade=None):
    """Apply one bounded continuous UV field to every connected paint chart."""
    levels=sorted((top+t*(bottom-top))/256-1 for t in knots)
    limits=((3,left/256,1),(3,right/256,-1),(4,levels[0],1),(4,levels[-1],-1))
    def mapping(p):
        t=((p[4]+1)*256-top)/(bottom-top)
        result=t
        for a,b,c,d in zip(knots,knots[1:],mapped,mapped[1:]):
            if a-1e-7<=t<=b+1e-7:
                result=c+(t-a)/(b-a)*(d-c)
                break
        if fade is not None:
            u=p[3]*256
            start,end=fade
            weight=1.
            if u<start and start>left:
                weight=max(0.,(u-left)/(start-left))
            elif u>end and right>end:
                weight=max(0.,(right-u)/(right-end))
            result=t+(result-t)*weight
        return (*p[:4],(top+result*(bottom-top))/256-1,*p[5:])
    for index in selected:
        mesh=meshes[index]
        old_faces,old_marks=mesh.faces,getattr(mesh,'wheel_arch_faces',())
        mesh.faces,marks=[],set()
        for face_index,face in enumerate(old_faces):
            first=len(mesh.faces)
            if struct.unpack_from('<h',face,8)[0] != PAINT:
                mesh.faces.append(face)
            else:
                inside=[mesh.vertices[i] for i in struct.unpack_from('<3H',face,2)]
                outside=[]
                for axis,bound,sign in limits:
                    outside.append(panels.clip(inside,axis,bound,-sign))
                    inside=panels.clip(inside,axis,bound,sign)
                if len(inside)<3:
                    mesh.faces.append(face)
                else:
                    for part in outside:
                        panels.emit_source_polygon(mesh,part,face)
                    for low,high in zip(levels,levels[1:]):
                        part=panels.clip(panels.clip(inside,4,low,1),4,high,-1)
                        panels.emit_source_polygon(mesh,[mapping(p) for p in part],face)
            if face_index in old_marks:
                marks.update(range(first,len(mesh.faces)))
        if hasattr(mesh,'wheel_arch_faces'):
            mesh.wheel_arch_faces=marks


def register_bumper_folds(meshes):
    """Register the three source yellow/white/yellow stripes at all four joins.

    Knots are measured on actual quantized curved folds, rather than comparing
    flat atlas rows. Full corrections occupy the real corner and fade into
    adjacent paint; source UV slots, phase, positions and complete motifs stay.
    """
    fields=(
        ((9,10,11),230,256,(254,256),
         (31.14453,28.57013,27.22437,25.95526,23.96115,22.76468,20.77058,14.70849,.53906),
         (31.14453,28.24971,26.43384,25.07785,23.48256,22.36586,20.69081,14.70849,.53906)),
        ((9,10,11),230,256,(254,256),
         (224.21875,226.99071,228.21578,229.25271,231.40632,232.44324,234.51709,240.65887,254.82422),
         (224.21875,226.99071,228.93365,230.05034,231.80513,233.00158,234.83614,240.65887,254.82422)),
        ((20,21,22),0,12,(0,4),
         (34.42188,31.04596,29.90059,28.55014,26.61253,25.55566,23.55933,18.54704,13.03125),
         (34.42188,30.54646,28.60885,27.43455,25.7318,24.49878,22.6786,18.54704,13.03125)),
        ((20,21,22),0,12,(0,4),
         (220.94531,224.03424,225.64301,226.8173,228.69616,229.81174,231.74931,236.82023,242.33594),
         (220.94531,223.90962,225.93658,226.93473,228.75488,229.92917,231.92546,236.82023,242.33594)),
    )
    for slots,left,right,fade,knots,mapped in fields:
        ordered=sorted(zip(knots,mapped))
        register_side_band(meshes,slots,left,right,0,256,
                           tuple(a/256 for a,b in ordered),tuple(b/256 for a,b in ordered),fade=fade)


def bumper_fold_opaque_uvs(meshes):
    """Release legacy key only on solid painted bumper corner joins.

    These narrow measured chart strips contain trim and paint, including the
    lower rim texels. The grille, underbody, apertures and unused atlas stay
    under their canonical masks.
    """
    result=[]
    regions={
        (9,10,11): ((250,256,133,165),(168,174,133,165),
                    (254,256,0,32),(254,256,223,256)),
        (20,21,22): ((80,85,144,167),(164,170,144,167),
                     (0,8,12,35),(0,8,220,244)),
    }
    for slots,rectangles in regions.items():
        for index in slots:
            mesh=meshes[index]
            for face in mesh.faces:
                if struct.unpack_from('<h',face,8)[0] != PAINT:
                    continue
                for left,right,top,bottom in rectangles:
                    part=[mesh.vertices[i] for i in struct.unpack_from('<3H',face,2)]
                    for axis,bound,sign in ((3,left/256,1),(3,right/256,-1),
                                            (4,top/256-1,1),(4,bottom/256-1,-1)):
                        part=panels.clip(part,axis,bound,sign)
                    for a in range(1,len(part)-1):
                        result.append(tuple((p[3],p[4]+1) for p in (part[0],part[a],part[a+1])))
    return result


def relieve_wheel_arches(meshes, nodes, definition):
    """Relieve the Mondeo front folds locally without moving their UV charts.

    The source arch is narrower than the swept tire at full steering. Move
    its edge radially in the side plane, keeping the lateral paint position,
    then fade the displacement over the adjacent panel. Shared physical
    points receive the same displacement on every damage mesh and UV seam.
    """
    clearance = definition.get('arch_tire_clearance')
    if clearance is None:
        return
    if (definition['source_model_crc32'] != 'eda6480d' or
            type(clearance) not in (int, float) or not .015 <= clearance <= .030):
        raise ValueError('Ford arch relief requires its canonical source and 15..30mm clearance')
    import vehicle_wheel_wells as wells
    _, _, centers = wells.anchors(nodes)
    centers = [(x, y, z + (1 if z > 0 else -1) * definition.get('wheel_outset', [0, 0])[i // 2])
               for i, (x, y, z) in enumerate(centers)]
    contours = [wells.aperture(meshes, nodes, center, definition) for center in centers]
    profile = definition['wheel_profile']
    # A sphere bounds the entire spun front tire at every steering angle.
    # Rear wheels do not steer and retain their actual shoulder profile.
    sphere = max(math.hypot(z, r) for z, r in profile)

    def tire_radius(depth, front):
        if front:
            return math.sqrt(max(0., sphere * sphere - depth * depth)) if abs(depth) < sphere else 0.
        for (za, ra), (zb, rb) in zip(profile, profile[1:]):
            if za <= depth <= zb:
                return ra + (rb - ra) * (depth - za) / (zb - za)
        return 0.

    def edge(contour, center, dx, dy):
        hits = []
        for a, b in zip(contour, contour[1:]):
            ex, ey = b[0] - a[0], b[1] - a[1]
            denominator = dx * ey - dy * ex
            if abs(denominator) < 1e-12:
                continue
            ax, ay = a[0] - center[0], a[1] - center[1]
            radius = (ax * ey - ay * ex) / denominator
            t = (ax * dy - ay * dx) / denominator
            if radius > 0 and -1e-7 <= t <= 1 + 1e-7:
                hits.append((radius, a[2] + t * (b[2] - a[2])))
        return min(hits) if hits else None

    cache = {}
    def relief(position):
        if position in cache:
            return cache[position]
        x, y, z = position
        wheel = min(range(4), key=lambda i: abs(x - centers[i][0]) + abs(z - centers[i][2]))
        center, contour = centers[wheel], contours[wheel]
        dx, dy = x - center[0], y - center[1]
        distance = math.hypot(dx, dy)
        if abs(z) < .55 or not .18 < distance < .75:
            return position
        dx, dy = dx / distance, dy / distance
        lip = edge(contour, center, dx, dy)
        if lip is None:
            return position
        radius, lip_z = lip
        side = 1 if center[2] > 0 else -1
        # The return sits below the paint. Include the entire straight flare
        # to the barrel so safe endpoints cannot hide an intersecting triangle.
        depth = side * (lip_z - center[2]) - .004
        required = radius
        for step in range(40):
            t = step / 40
            target = tire_radius(depth + t * (wells.THROAT_DEPTH - depth), wheel < 2) + clearance
            required = max(required, (target - t * (.35 + wells.RADIAL_CLEARANCE)) / (1 - t))
        delta = max(0., required - radius)
        # A broad smooth fade keeps the mapping monotonic even on the tight
        # upper source opening, while changing no vertices beyond the fender.
        weight = max(0., 1 - abs(distance - radius) / .20)
        weight = weight * weight * (3 - 2 * weight)
        result = (x + dx * delta * weight, y + dy * delta * weight, z)
        cache[position] = result
        return result

    for index in definition['arch_meshes']:
        mesh = meshes[index]
        vertices = []
        for p in mesh.vertices:
            position = relief(p[:3])
            normal = p[5:]
            if position != p[:3]:
                # Carry the source smooth normal through the same deformation.
                # Cofactors implement inverse-transpose without an external
                # matrix dependency, including the varying lateral shoulder.
                columns, epsilon = [], .0001
                for axis in range(3):
                    low, high = list(p[:3]), list(p[:3])
                    low[axis] -= epsilon
                    high[axis] += epsilon
                    columns.append(scale(sub(relief(tuple(high)), relief(tuple(low))), .5 / epsilon))
                a, b, c = columns
                normal = unit(add(add(scale(cross(b, c), normal[0]), scale(cross(c, a), normal[1])),
                                  scale(cross(a, b), normal[2])))
            vertices.append((*position, *p[3:5], *normal))
        mesh.vertices = vertices
        for face_index, original in enumerate(mesh.faces):
            indices = struct.unpack_from('<3H', original, 2)
            points = [mesh.vertices[i] for i in indices]
            a, b, c = [tuple(round(value * 65536) / 65536 for value in p[:3]) for p in points]
            facing = cross(sub(b, a), sub(c, a))
            if dot(facing, add(add(points[0][5:], points[1][5:]), points[2][5:])) < 0:
                # The atlas clips include very narrow triangles. As with
                # Mesh.triangle, select their actual stored outward winding
                # after deformation rather than carrying the old winding.
                face = bytearray(original)
                struct.pack_into('<3H', face, 2, indices[0], indices[2], indices[1])
                mesh.faces[face_index] = bytes(face)


def prepare(source, definition):
    if definition['source_model_crc32'] != 'eda6480d' or crc32(source) != 'eda6480d':
        raise ValueError('Ford geometry requires canonical HIGH member16')
    header, nodes, originals, textures = decode(source)
    meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
    for index in (24, 25):
        selected = set(wing_faces(originals[index]))
        meshes[index].faces = [face for face in meshes[index].faces if face not in selected]
    shared = panels.shared_edge_controls(meshes, set(definition['curved_meshes']), paint=PAINT, excluded_meshes=(49,))
    for index in definition['curved_meshes']:
        meshes[index] = curved_mesh(meshes[index], definition['body_subdivisions'], definition['body_max_curve'], shared)
    meshes[49] = wheel_mesh(originals[49].header, definition)
    for index in (13, 14, 27, 28, 29, 30, 31, 32):
        panels.conform_numeric_decals(meshes[index], paint=PAINT, decal_slots=(7, 8))
    smooth_wheel_arches(meshes, definition)
    for index in (41, 43):
        meshes[index] = mirror(originals[index])
    for index in LAMP_SLOTS:
        meshes[index] = closed_lens(originals[index])
    for index in (24, 25):
        solid_wing(meshes[index], originals[index])
    relieve_wheel_arches(meshes, nodes, definition)
    remove_quantized_slivers(meshes, range(50))
    return header, nodes, meshes, textures


def generate(source, definition, texture_directory=None, *, quality=70):
    header, nodes, meshes, textures = prepare(source, definition)
    if texture_directory is not None:
        import ford_textures
        count = struct.unpack_from('<H', header, 16)[0]
        ford_textures.fit_uvs(meshes, textures, count, texture_directory)
        register_lamp_folds(meshes, decode(source)[2])
        register_bumper_folds(meshes)
        remove_quantized_slivers(meshes, range(50))
        textures = ford_textures.replace_textures(textures, count, texture_directory,
                                                 wheel_arch_opaque_uvs(meshes) + lamps_opaque_uvs(meshes) + bumper_fold_opaque_uvs(meshes), quality=quality)
    result = pack(header, nodes, meshes, textures)
    return result, {'source_model_crc32': crc32(source), 'output_crc32': crc32(result), 'bytes': len(result),
                    'original_visible_triangles': visible_triangles(nodes, decode(source)[2]),
                    'visible_triangles': visible_triangles(nodes, meshes),
                    'stored_triangles': sum(len(m.faces) for m in meshes),
                    'wheel_triangles': len(meshes[49].faces), 'wheel_vertices': len(meshes[49].vertices),
                    'curved_meshes': len(definition['curved_meshes'])}
