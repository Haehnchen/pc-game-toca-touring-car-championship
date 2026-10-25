"""M02b identified woodland pilots in original local +Y-up coordinates.

The Croft SAP1 body joins croffree.bfp member2/meshes23/24, nodes127/128.
Its shared A/B panel retains both source owners across the UV diagonal t=u.
The Snetterton LLRTS2 pilot selects member2/mesh19 TREES_20, node60;
LLRTSX selects member2/mesh0 TREES_1, node6, a simple six-triangle wedge.
Oulton's LLRTS1 quads in meshes100/101 share one body across nodes260/261;
the separate LLRTS2 triangles in mesh100 remain exact source records.
The separate three-panel body in meshes14/15 retains neighboring source
end triangles in meshes5/6 and leaves the unrelated LLRTS2 mesh16 untouched.
All retain their node translation and replace only the identified carrier
triangles with a closed foliage shell. Other woodland remains source scenery.
Snetterton's separate LLRTS2 end triangle retains its rising lower edge.

All bodies use simple source-sized sections with texture-supplied foliage
detail. No alpha profile, fine longitudinal grid or image decoder is required.
"""

from dataclasses import dataclass
import math
import struct
import zlib


CROFT_PANELS = {"bcf75547": (b"TREES_24", 13, 5, 0), "83b52010": (b"TREES_25", 7, 3, 1)}
PILOT_ARTWORK_CRC32 = "a3457d54"
PILOT_DEPTH = .9
SNET_MESH_CRC32 = "51b727dc"
SNET_ARTWORK_CRC32 = "a7eca032"
SNET_DEPTH = 1.4
LLRTSX_MESH_CRC32 = "226ec6c5"
LLRTSX_ARTWORK_CRC32 = "6ad9bec4"
LLRTSX_DEPTH = 1.4
OULTON_ARTWORK_CRC32 = "853b596e"
OULTON_DEPTH = 1.4
OULTON_PANELS = {"6850f562": (b"TREES_101", 13, 5, 0), "27f1ff22": (b"TREES_102", 4, 2, 1)}
OULTON_NORMALS_Q16 = ((-32774, 0, -56752), (34026, 0, -56010))
OULTON_WEDGE_PANELS = {"25813020": (b"TREES_15", 10, 4, 0), "3fb18e44": (b"TREES_16", 4, 2, 1)}
OULTON_WEDGE_NORMALS_Q16 = ((-17377, 0, -63190), (4006, 0, -65413), (33840, 0, -56123))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def scale(a, factor):
    return tuple(value * factor for value in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def unit(vector):
    length = math.sqrt(dot(vector, vector))
    if length <= 1e-12:
        raise ValueError("Woodland direction must not collapse")
    return scale(vector, 1 / length)


def interpolate(a, b, fraction):
    return add(a, scale(sub(b, a), fraction))


@dataclass(frozen=True)
class Panel:
    left: tuple
    right: tuple
    up_left: tuple
    up_right: tuple
    normal: tuple
    triangular: bool
    color: tuple
    texture: int


def _triangle(mesh, indices, color, texture):
    a, b, c = (mesh.vertices[index][:3] for index in indices)
    if dot(cross(sub(b, a), sub(c, a)), cross(sub(b, a), sub(c, a))) > 1e-16:
        mesh.triangle(indices, color, texture)


def _smooth_normals(mesh):
    """Share the ridge/base normals across duplicate UV vertices, not positions."""
    accumulated = {}
    for face in mesh.faces:
        indices = struct.unpack_from("<3H", face, 2)
        a, b, c = (mesh.vertices[index][:3] for index in indices)
        normal = cross(sub(b, a), sub(c, a))
        for index in indices:
            vertex = mesh.vertices[index]
            key = tuple(round(value, 8) for value in vertex[:3])
            accumulated[key] = add(accumulated.get(key, (0., 0., 0.)), normal)
    for index, vertex in enumerate(mesh.vertices):
        normal = accumulated.get(tuple(round(value, 8) for value in vertex[:3]))
        if normal and dot(normal, normal) > 1e-20:
            mesh.vertices[index] = (*vertex[:5], *unit(normal))


def _croft_panels(pair):
    """Use mesh23's frame; mesh24's root is exactly +64m in local X.

    The canonical diagonal endpoints differ by one q16 X unit across roots.
    Use mesh23 A-bottom/B-top for both owners, a <=1/65536m seam repair.
    Top/bottom/source UV extrema otherwise remain the exact canonical points.
    """
    lower, upper = pair
    def upper_point(index):
        return add(upper.vertices[index][:3], (64., 0., 0.))
    bottoms = (upper_point(2), lower.vertices[0][:3], lower.vertices[1][:3],
               lower.vertices[4][:3], lower.vertices[8][:3])
    tops = (upper_point(1), upper_point(0), lower.vertices[2][:3],
            lower.vertices[5][:3], lower.vertices[9][:3])
    normals = (unit(upper.vertices[2][5:]), *(unit(lower.vertices[i][5:]) for i in (0, 3, 7)))
    return bottoms, tops, normals


def _clip_croft_diagonal(vertices, lower):
    """Clip an oriented AB skin triangle, retaining t<=u or t>=u.

    Both owners evaluate intersections in the same edge order, so the cut is
    bit-identical before their separate root translations. No diagonal cap is
    emitted: this is a partition of the original surface, not two volumes.
    """
    result = []
    for a, b in zip(vertices, vertices[1:] + vertices[:1]):
        ga, gb = -a[4] - a[3], -b[4] - b[3]
        inside_a = ga <= 1e-12 if lower else ga >= -1e-12
        inside_b = gb <= 1e-12 if lower else gb >= -1e-12
        if inside_a:
            result.append(a)
        if inside_a != inside_b and abs(ga) > 1e-12 and abs(gb) > 1e-12:
            # Canonical edge order also handles adjacent triangles visiting
            # their common edge in opposite directions.
            first, second = sorted((a, b))
            g0, g1 = -first[4] - first[3], -second[4] - second[3]
            point = interpolate(first, second, g0 / (g0 - g1))
            result.append((*point[:4], -point[3], *point[5:]))
    return result


def _rebuild_croft(pair, mesh_type):
    """Closed D-A-B-C-E body, partitioned into the two canonical roots."""
    bottoms, tops, normals = _croft_panels(pair)
    frames = (normals[0], *(unit(add(a, b)) for a, b in zip(normals, normals[1:])), normals[-1])
    shell = mesh_type(pair[0].header, [], [])
    panel_faces = []
    color = tuple(pair[0].faces[0][10:14])
    for panel in range(4):
        first_face = len(shell.faces)
        grids = []
        for sign in (1, -1):
            grid = []
            for u in (0., 1.):
                base = interpolate(bottoms[panel], bottoms[panel + 1], u)
                top = interpolate(tops[panel], tops[panel + 1], u)
                direction = unit(interpolate(frames[panel], frames[panel + 1], u))
                outer = (panel == 0 and u == 0.) or (panel == 3 and u == 1.)
                half_width = .05 if outer else PILOT_DEPTH * .5
                offsets = (0., half_width, 0.)
                rows = []
                for row, height in enumerate((0., .5, 1.)):
                    point = add(interpolate(base, top, height), scale(direction, sign * offsets[row]))
                    rows.append(shell.vertex(point, scale(direction, sign), (u, -height)))
                grid.append(rows)
            for a, b in zip(grid, grid[1:]):
                for row in range(2):
                    _triangle(shell, (a[row], b[row], b[row + 1]), color, 1)
                    _triangle(shell, (a[row], b[row + 1], a[row + 1]), color, 1)
            grids.append(grid)
        if panel in (0, 3):
            first = panel == 0
            front, back = (grid[0 if first else -1] for grid in grids)
            tangent = unit(sub(bottoms[panel], bottoms[panel + 1]) if first else sub(bottoms[panel + 1], bottoms[panel]))
            cap = []
            for index, u in zip((front[0], front[1], front[2], back[1]),
                                (0., .0625, 0., 0.) if first else (1., 1., 1., .9375)):
                vertex = shell.vertices[index]
                cap.append(shell.vertex(vertex[:3], tangent, (u, vertex[4])))
            for index in range(1, len(cap) - 1):
                _triangle(shell, (cap[0], cap[index], cap[index + 1]), color, 1)
        panel_faces.extend([panel] * (len(shell.faces) - first_face))
    _smooth_normals(shell)
    results = [mesh_type(original.header, [], []) for original in pair]
    lookups = [{}, {}]
    for face, panel in zip(shell.faces, panel_faces):
        vertices = [shell.vertices[index] for index in struct.unpack_from("<3H", face, 2)]
        owners = (0, 1) if panel == 1 else (1,) if panel == 0 else (0,)
        for owner in owners:
            polygon = _clip_croft_diagonal(vertices, owner == 0) if panel == 1 else vertices
            indices = []
            for vertex in polygon:
                local = (*sub(vertex[:3], (64. * owner, 0., 0.)), *vertex[3:])
                if local not in lookups[owner]:
                    lookups[owner][local] = results[owner].vertex(local[:3], local[5:], local[3:5])
                indices.append(lookups[owner][local])
            for i in range(1, len(indices) - 1):
                corner = (indices[0], indices[i], indices[i + 1])
                a, b, c = (results[owner].vertices[index][:3] for index in corner)
                if dot(cross(sub(b, a), sub(c, a)), cross(sub(b, a), sub(c, a))) > 1e-16:
                    # Clipping preserves the oriented shell polygon. Do not
                    # reorient its small pieces using averaged shading normals.
                    results[owner].faces.append(struct.pack("<H3Hh4B10x", 0, *corner, 1, *color))
    return results


@dataclass(frozen=True)
class SnetPanel:
    left: tuple
    right: tuple
    up_left: tuple
    up_right: tuple
    normal: tuple
    rising_base: bool
    color: tuple
    texture: int


def _snet_panels(original):
    """Read the nine-vertex source quad and its bottom-clipped end triangle."""
    # Whole panel: C(bottom5/top4) -> B(bottom7/top8), U0 -> U1.
    left, right = (original.vertices[index][:3] for index in (5, 7))
    up_left = sub(original.vertices[4][:3], left)
    up_right = sub(original.vertices[8][:3], right)
    full = SnetPanel(left, right, up_left, up_right, unit(original.vertices[5][5:]), False,
                     tuple(original.faces[1][10:14]), struct.unpack_from("<h", original.faces[1], 8)[0])
    # End triangle: B(bottom2/top1), U0 -> A(top0), U1. Its virtual lower
    # baseline is used only for UV evaluation; no vertices are emitted below
    # the actual rising B-bottom -> A-top boundary (texture height t>=u).
    left = original.vertices[2][:3]
    up = sub(original.vertices[1][:3], left)
    right = sub(original.vertices[0][:3], up)
    taper = SnetPanel(left, right, up, up, unit(original.vertices[2][5:]), True,
                      tuple(original.faces[0][10:14]), struct.unpack_from("<h", original.faces[0], 8)[0])
    return (full, taper)


def _rebuild_snet(original, mesh_type):
    """Loft the full broadleaf panel into a body that tapers to the upper tip."""
    panels = _snet_panels(original)
    normals = (panels[0].normal, unit(add(panels[0].normal, panels[1].normal)), panels[1].normal)
    result = mesh_type(original.header, [], [])
    ends = []
    for panel_index, panel in enumerate(panels):
        grids = []
        for sign in (1, -1):
            grid = []
            for u in (0., 1.):
                baseline = interpolate(panel.left, panel.right, u)
                up = interpolate(panel.up_left, panel.up_right, u)
                lower = u if panel.rising_base else 0.
                remaining = 1 - lower
                direction = unit(interpolate(normals[panel_index], normals[panel_index + 1], u))
                offsets = (0., SNET_DEPTH * .5 * remaining, 0.)
                rows = []
                for row, fraction in enumerate((lower, (lower + 1) * .5, 1.)):
                    center = add(baseline, scale(up, fraction))
                    point = add(center, scale(direction, sign * offsets[row]))
                    normal = unit(add(scale(direction, sign), (0., (-.35, 0., .7)[row], 0.)))
                    rows.append(result.vertex(point, normal, (u, -fraction)))
                grid.append(rows)
            for left, right in zip(grid, grid[1:]):
                for row in range(2):
                    _triangle(result, (left[row], right[row], right[row + 1]), panel.color, panel.texture)
                    _triangle(result, (left[row], right[row + 1], left[row + 1]), panel.color, panel.texture)
            grids.append(grid)
        ends.append(grids)
    # C is the full-height end. The other end converges to source A-top,
    # rather than inventing a full forest face under the partial triangle.
    panel = panels[0]
    front, back = (grid[0] for grid in ends[0])
    tangent = unit(sub(panel.left, panel.right))
    cap = []
    for index, u in zip((front[0], front[1], front[2], back[1]), (.0625, .125, .0625, 0.)):
        vertex = result.vertices[index]
        cap.append(result.vertex(vertex[:3], tangent, (u, vertex[4])))
    for index in range(1, len(cap) - 1):
        _triangle(result, (cap[0], cap[index], cap[index + 1]), panel.color, panel.texture)
    _smooth_normals(result)
    return result


def _rebuild_llrtsx(original, mesh_type):
    """One six-triangle wedge; the source image supplies all foliage detail.

    Both skins meet at the exact source ground line and rising upper edge.
    Their only extra position is the right mid-height section, +/-0.7m from
    the source plane. No sampled crown profile or longitudinal grid is needed.
    """
    left, right, top = (vertex[:3] for vertex in original.vertices)
    direction = unit(original.vertices[0][5:])
    center = interpolate(right, top, .5)
    color = tuple(original.faces[0][10:14])
    texture = struct.unpack_from("<h", original.faces[0], 8)[0]
    result = mesh_type(original.header, [], [])
    ends = []
    for sign in (1, -1):
        normal = scale(direction, sign)
        middle = add(center, scale(normal, LLRTSX_DEPTH * .5))
        indices = [result.vertex(point, normal, uv) for point, uv in
                   ((left, (0., 0.)), (right, (1., 0.)), (top, (1., -1.)), (middle, (1., -.5)))]
        _triangle(result, (indices[0], indices[1], indices[3]), color, texture)
        _triangle(result, (indices[0], indices[3], indices[2]), color, texture)
        ends.append(middle)
    # Interior U values give the narrow end real foliage despite the empty
    # U1 image margin. Even its UVs stay inside the source triangle t<=U;
    # the unchanged top point alone uses the exact original U1/V-1 corner.
    tangent = unit(sub(right, left))
    cap = [result.vertex(point, tangent, uv) for point, uv in
           ((right, (.9375, 0.)), (ends[0], (.9375, -.5)),
            (top, (1., -1.)), (ends[1], (.875, -.5)))]
    _triangle(result, (cap[0], cap[1], cap[2]), color, texture)
    _triangle(result, (cap[0], cap[2], cap[3]), color, texture)
    _smooth_normals(result)
    return result


def _oulton_panel(original, side):
    indices = (3, 0, 2, 1) if side == 0 else (0, 1, 3, 2)
    left, right, top_left, top_right = (original.vertices[i][:3] for i in indices)
    return Panel(left, right, sub(top_left, left), sub(top_right, right),
                 unit(original.vertices[0][5:]), False, tuple(original.faces[0][10:14]), 0)


def _rebuild_oulton(original, mesh_type, side):
    """One half of the joined body; no cap on its common B cross-section."""
    panel = _oulton_panel(original, side)
    joint = unit(add(*(tuple(value / 65536 for value in normal) for normal in OULTON_NORMALS_Q16)))
    normals = (panel.normal, joint) if side == 0 else (joint, panel.normal)
    shell = mesh_type(original.header, [], [])
    joins = []
    for sign in (1, -1):
        grid = []
        for u in (0., 1.):
            base = interpolate(panel.left, panel.right, u)
            up = interpolate(panel.up_left, panel.up_right, u)
            direction = unit(interpolate(*normals, u))
            outer = (side == 0 and u == 0.) or (side == 1 and u == 1.)
            offsets = (0., .05 if outer else OULTON_DEPTH * .5, 0.)
            rows = []
            for row, fraction in enumerate((0., .5, 1.)):
                point = add(add(base, scale(up, fraction)), scale(direction, sign * offsets[row]))
                normal = scale(direction, sign)
                rows.append(shell.vertex(point, normal, (u, -fraction)))
            grid.append(rows)
        for a, b in zip(grid, grid[1:]):
            for row in range(2):
                _triangle(shell, (a[row], b[row], b[row + 1]), panel.color, panel.texture)
                _triangle(shell, (a[row], b[row + 1], a[row + 1]), panel.color, panel.texture)
        joins.append(grid[-1 if side == 0 else 0])
        if sign == 1:
            front_end = grid[0 if side == 0 else -1]
        else:
            back_end = grid[0 if side == 0 else -1]
    tangent = unit(sub(panel.left, panel.right) if side == 0 else sub(panel.right, panel.left))
    cap = []
    for index, u in zip((front_end[0], front_end[1], front_end[2], back_end[1]),
                        (0., .0625, 0., 0.) if side == 0 else (1., 1., 1., .9375)):
        vertex = shell.vertices[index]
        cap.append(shell.vertex(vertex[:3], tangent, (u, vertex[4])))
    for index in range(1, len(cap) - 1):
        _triangle(shell, (cap[0], cap[index], cap[index + 1]), panel.color, panel.texture)
    _smooth_normals(shell)
    # Normals on this open half must not acquire a separate one-sided average
    # at B. Both meshes use the same explicit shared frame across their UV wrap.
    for sign, indices in zip((1, -1), joins):
        for row, index in enumerate(indices):
            vertex = shell.vertices[index]
            normal = unit(add(scale(joint, sign), (0., (-.35, 0., .7)[row], 0.)))
            shell.vertices[index] = (*vertex[:5], *normal)
    # Keep every LLRTS2 byte and its original vertex attributes in mesh100.
    # Original unused LLRTS1 vertices are harmless and compacted by the GLB tool.
    result = mesh_type(original.header, list(original.vertices), list(original.faces[2:]))
    offset = len(result.vertices)
    result.vertices.extend(shell.vertices)
    for face in shell.faces:
        record = bytearray(face)
        indices = struct.unpack_from("<3H", record, 2)
        struct.pack_into("<3H", record, 2, *(index + offset for index in indices))
        result.faces.append(bytes(record))
    return result


def _rebuild_oulton_wedge(original, mesh_type, part):
    """A->B->C belongs to mesh14; C->D belongs to its exact mesh15 peer."""
    normals = [unit(tuple(value / 65536 for value in normal)) for normal in OULTON_WEDGE_NORMALS_Q16]
    frames = (normals[0], unit(add(normals[0], normals[1])), unit(add(normals[1], normals[2])), normals[2])
    specs = ((0, 1, 3, 2, 0), (4, 5, 8, 6, 2)) if part == 0 else ((0, 1, 3, 2, 0),)
    shell = mesh_type(original.header, [], [])
    shared = []
    for local_panel, (li, ri, lti, rti, fi) in enumerate(specs):
        panel_index = local_panel if part == 0 else 2
        left, right, left_top, right_top = (original.vertices[i][:3] for i in (li, ri, lti, rti))
        color = tuple(original.faces[fi][10:14])
        grids = []
        for sign in (1, -1):
            grid = []
            for u in (0., 1.):
                base = interpolate(left, right, u)
                up = interpolate(sub(left_top, left), sub(right_top, right), u)
                direction = unit(interpolate(frames[panel_index], frames[panel_index + 1], u))
                outer = (panel_index == 0 and u == 0.) or (panel_index == 2 and u == 1.)
                offsets = (0., .05 if outer else OULTON_DEPTH * .5, 0.)
                rows = []
                for row, height in enumerate((0., .5, 1.)):
                    point = add(add(base, scale(up, height)), scale(direction, sign * offsets[row]))
                    rows.append(shell.vertex(point, scale(direction, sign), (u, -height)))
                grid.append(rows)
            for a, b in zip(grid, grid[1:]):
                for row in range(2):
                    _triangle(shell, (a[row], b[row], b[row + 1]), color, 0)
                    _triangle(shell, (a[row], b[row + 1], a[row + 1]), color, 0)
            if panel_index > 0:
                shared.append((sign, grid[0], frames[panel_index]))
            if panel_index < 2:
                shared.append((sign, grid[-1], frames[panel_index + 1]))
            grids.append(grid)
        if panel_index in (0, 2):
            first = panel_index == 0
            front, back = (grid[0 if first else -1] for grid in grids)
            tangent = unit(sub(left, right) if first else sub(right, left))
            cap = []
            for index, u in zip((front[0], front[1], front[2], back[1]),
                                (0., .0625, 0., 0.) if first else (1., 1., 1., .9375)):
                vertex = shell.vertices[index]
                cap.append(shell.vertex(vertex[:3], tangent, (u, vertex[4])))
            for index in range(1, len(cap) - 1):
                _triangle(shell, (cap[0], cap[index], cap[index + 1]), color, 0)
    _smooth_normals(shell)
    # Share both UV-wrap frames, including C across the two source meshes.
    # Neither B nor C receives any independent internal cap.
    for sign, indices, frame in shared:
        for row, index in enumerate(indices):
            vertex = shell.vertices[index]
            normal = unit(add(scale(frame, sign), (0., (-.35, 0., .7)[row], 0.)))
            shell.vertices[index] = (*vertex[:5], *normal)
    return shell


def _connected_selection(meshes, fingerprints):
    selected = {}
    for index, original in enumerate(meshes):
        record = fingerprints.get(f"{zlib.crc32(original.encode()):08x}")
        if record and (original.header[:12].split(b"\0", 1)[0], len(original.vertices), len(original.faces)) == record[:3]:
            if record[3] in selected:
                return {}
            selected[record[3]] = index
    return selected if set(selected) == {0, 1} else {}


def enhance_connected(meshes, mesh_type, texture_names, triangle_budget=48000, per_mesh_budget=2048):
    """Reserve complete source pairs atomically; retain100/101 budget priority."""
    names = [(name.decode("ascii") if isinstance(name, bytes) else name).split("\0", 1)[0] for name in texture_names]
    if names[:2] == ["LLRTS1", "SAP1"]:
        selected = _connected_selection(meshes, CROFT_PANELS)
        if selected:
            sources = [meshes[selected[side]] for side in (0, 1)]
            pair = _rebuild_croft(sources, mesh_type)
            additions = [len(mesh.faces) - len(original.faces) for mesh, original in zip(pair, sources)]
            if max(additions) <= per_mesh_budget and sum(additions) <= triangle_budget:
                result = list(meshes)
                for side, mesh in enumerate(pair):
                    result[selected[side]] = mesh
                return result, dict(woodland_strips=1, woodland_panels=4, removed_woodland_triangles=8, added_triangles=sum(additions))
        return meshes, {"added_triangles": 0}
    if names[:2] != ["LLRTS1", "LLRTS2"]:
        return meshes, {"added_triangles": 0}
    result = meshes
    stats = {"added_triangles": 0}
    for fingerprints, rebuild, panels, removed in ((OULTON_PANELS, _rebuild_oulton, 2, 4), (OULTON_WEDGE_PANELS, _rebuild_oulton_wedge, 3, 6)):
        selected = _connected_selection(meshes, fingerprints)
        if not selected:
            continue
        pair = {index: rebuild(meshes[index], mesh_type, side) for side, index in selected.items()}
        additions = [len(mesh.faces) - len(meshes[index].faces) for index, mesh in pair.items()]
        if max(additions) > per_mesh_budget or stats["added_triangles"] + sum(additions) > triangle_budget:
            continue
        if result is meshes:
            result = list(meshes)
        for index, mesh in pair.items():
            result[index] = mesh
        for key, value in dict(woodland_strips=1, woodland_panels=panels, removed_woodland_triangles=removed, added_triangles=sum(additions)).items():
            stats[key] = stats.get(key, 0) + value
    return result, stats


def enhance(original, mesh_type, texture_names, triangle_budget=2048):
    """Replace identified pilots within their additional-face cap."""
    names = [(name.decode("ascii") if isinstance(name, bytes) else name).split("\0", 1)[0] for name in texture_names]
    if (len(names) > 2 and names[2] == "LLRTSX" and original.header[:12].split(b"\0", 1)[0] == b"TREES_1"
            and len(original.vertices) == 3 and len(original.faces) == 1
            and f"{zlib.crc32(original.encode()):08x}" == LLRTSX_MESH_CRC32):
        mesh = _rebuild_llrtsx(original, mesh_type)
        added = len(mesh.faces) - len(original.faces)
        if added <= triangle_budget:
            return mesh, dict(woodland_strips=1, woodland_panels=1, removed_woodland_triangles=1, added_triangles=added)
        return original, {"added_triangles": 0}
    if (len(names) > 1 and names[1] == "LLRTS2" and original.header[:12].split(b"\0", 1)[0] == b"TREES_20"
            and len(original.vertices) == 9 and len(original.faces) == 3
            and f"{zlib.crc32(original.encode()):08x}" == SNET_MESH_CRC32):
        mesh = _rebuild_snet(original, mesh_type)
        added = len(mesh.faces) - len(original.faces)
        if added <= triangle_budget:
            return mesh, dict(woodland_strips=1, woodland_panels=2, removed_woodland_triangles=len(original.faces), added_triangles=added)
        return original, {"added_triangles": 0}
    return original, {"added_triangles": 0}
