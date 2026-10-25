"""Shared Mega cockpit geometry in the unchanged original animation basis.

The wheel's source is a tilted cutout quad. Its original Y/Z texture chart
locates the leather rim, three perforated spokes and six hub bolts. The wiper
uses its original hull as a control cage: open frame windows replace broad side
panels, leaving rails, transverse bridges and the original pivot housing.
"""

import json
import math
from pathlib import Path
import struct

from vehicle_model import Mesh, add, crc32, cross, decode, dot, interpolate, pack, polygon, scale, sub, unit, visible_triangles

DEFINITION = Path(__file__).resolve().parents[1] / 'assets/models/cockpits/shared/definition.json'
WHITE = (255, 255, 255, 255)


def wheel_mesh(original, definition):
    mesh = Mesh(original.header, [], [])
    top, bottom = original.vertices[0], original.vertices[3]
    center_y = (top[1] + bottom[1]) / 2
    center_x = (top[0] + bottom[0]) / 2
    slope = (top[0] - bottom[0]) / (top[1] - bottom[1])
    half_z = max(abs(v[2]) for v in original.vertices)
    vertical = (top[1] - bottom[1]) / (2 * half_z)

    def point(q, z, depth):
        y = center_y + vertical * q
        return (center_x + slope * (y - center_y) + depth, y, z)

    def uv(q, z):
        # Exact source chart, including its deliberately inset V boundaries.
        # Physical +Y maps to the raster BOTTOM: the third source spoke points
        # toward -Y. Retain this orientation instead of flipping the artwork.
        t = (vertical * q + center_y - bottom[1]) / (top[1] - bottom[1])
        return (z / (2 * half_z) + .5, bottom[4] + t * (top[4] - bottom[4]))

    def surface(points, normal=None, chart=None):
        polygon(mesh, [point(*p) for p in points], WHITE, normal,
                chart or [uv(p[0], p[1]) for p in points], texture=0)

    # A true rounded tube; the UV radial range stays in the leather band even
    # on the outer/back surfaces rather than sampling the cutout background.
    segments, sides = definition['rim_segments'], definition['rim_cross_segments']
    radius, tube = definition['rim_radius'], definition['rim_tube_radius']
    rings = []
    for i in range(segments + 1):
        angle = math.tau * i / segments
        cq, cz = math.cos(angle), math.sin(angle)
        ring = []
        for j in range(sides + 1):
            turn = math.tau * j / sides
            radial, depth = tube * math.cos(turn), tube * math.sin(turn)
            normal = unit((math.sin(turn), math.cos(turn) * cq / vertical - slope * math.sin(turn), math.cos(turn) * cz))
            chart_radius = radius + .013 * math.cos(turn)
            ring.append(mesh.vertex(point((radius + radial) * cq, (radius + radial) * cz, depth),
                                    normal, uv(chart_radius * cq, chart_radius * cz)))
        rings.append(ring)
    for i in range(segments):
        for j in range(sides):
            a, b, c, d = rings[i][j], rings[i + 1][j], rings[i + 1][j + 1], rings[i][j + 1]
            mesh.triangle((a, b, c), WHITE, 0)
            mesh.triangle((a, c, d), WHITE, 0)

    def local(s, t, angle, depth):
        return (s * math.cos(angle) - t * math.sin(angle),
                s * math.sin(angle) + t * math.cos(angle), depth)

    # Radial projection triangulates a rectangular cell around a genuine round
    # opening. Front/back plates and inner/outer walls make each cell solid.
    def spoke_cell(lo, hi, angle, hole):
        middle = (lo + hi) / 2
        half_width = .029
        outside, inside = [], []
        for i in range(16):
            a = math.tau * i / 16
            ds, dt = math.cos(a), math.sin(a)
            distance = min((hi - middle) / abs(ds) if abs(ds) > 1e-9 else math.inf,
                           half_width / abs(dt) if abs(dt) > 1e-9 else math.inf)
            outside.append((middle + ds * distance, dt * distance))
            inside.append((middle + ds * hole, dt * hole))
        for i in range(16):
            j = (i + 1) % 16
            for depth in (-.014, .009):
                points = [local(*p, angle, depth) for p in (outside[i], outside[j], inside[j], inside[i])]
                surface(list(reversed(points)) if depth < 0 else points)
            for loop in (outside, inside):
                points = [local(*loop[i], angle, -.014), local(*loop[j], angle, -.014),
                          local(*loop[j], angle, .009), local(*loop[i], angle, .009)]
                # Drilled wall surfaces sample opaque charcoal in the existing hub
                # chart; projecting their inner edges into the painted holes
                # would discard real wall geometry through texture alpha.
                surface(list(reversed(points)) if loop is inside else points, chart=[(.5, -.56)] * 4)

    for degrees in definition['spoke_directions_degrees']:
        angle = math.radians(degrees)
        # Two drilled openings in each of the source's three spokes.
        spoke_cell(.067, .139, angle, .018)
        spoke_cell(.139, .197, angle, .012)
        corners = [(s, t) for s, t in ((.197, -.029), (.275, -.029), (.275, .029), (.197, .029))]
        for depth in (-.014, .009):
            points = [local(*p, angle, depth) for p in corners]
            surface(list(reversed(points)) if depth < 0 else points)
        for i in range(4):
            j = (i + 1) % 4
            surface([local(*corners[i], angle, -.014), local(*corners[j], angle, -.014),
                     local(*corners[j], angle, .009), local(*corners[i], angle, .009)])

    def disk(q, z, radius, front, back, segments=32, chart=None):
        for i in range(segments):
            a, b = math.tau * i / segments, math.tau * (i + 1) / segments
            pa = (q + radius * math.cos(a), z + radius * math.sin(a))
            pb = (q + radius * math.cos(b), z + radius * math.sin(b))
            for depth in (front, back):
                points = [(q, z, depth), (*pa, depth), (*pb, depth)]
                surface(list(reversed(points)) if depth == front else points, chart=[chart] * 3 if chart else None)
            points = [(*pa, front), (*pb, front), (*pb, back), (*pa, back)]
            surface(points, chart=[chart] * 4 if chart else None)

    disk(0, 0, .071, -.023, .014)
    disk(0, 0, .033, -.033, -.024)
    for i in range(definition['hub_bolts']):
        angle = math.tau * i / definition['hub_bolts']
        q, z = .055 * math.cos(angle), .055 * math.sin(angle)
        # The top source bolt is a neutral silver swatch. Reusing that tiny
        # chart patch prevents a misplaced alpha hole from erasing a bolt.
        disk(q, z, .0055, -.036, -.025, segments=8, chart=uv(.055, 0))
    return mesh


def wiper_mesh(original):
    mesh = Mesh(original.header, [], [])
    # Exact source section stations: pointed tip -> root housing.
    upper = [original.vertices[i] for i in (5, 6, 9, 2)]
    front = [original.vertices[i] for i in (8, 7, 10, 11)]
    back = [original.vertices[i] for i in (19, 18, 17, 16)]

    def sample(a, b, s, t):
        high, low = interpolate(a[0], b[0], s), interpolate(a[1], b[1], s)
        return interpolate(high, low, t)

    def emit(records):
        # Upper front/back edges share a source vertex; collapse duplicate
        # corners before triangulation so a closed bridge never emits zero area.
        cleaned = []
        for record in records:
            if not cleaned or record[:3] != cleaned[-1][:3]:
                cleaned.append(record)
        if len(cleaned) > 1 and cleaned[0][:3] == cleaned[-1][:3]:
            cleaned.pop()
        if len(cleaned) < 3:
            return
        records = cleaned
        points = [v[:3] for v in records]
        if dot(cross(sub(points[1], points[0]), sub(points[2], points[0])),
               cross(sub(points[1], points[0]), sub(points[2], points[0]))) < 1e-16:
            return
        # The generated strip has a transparent ten-percent lower margin.
        # Keep the original longitudinal U chart and fit only that edge into
        # its opaque rubber band, so the blade/root cannot vanish at V=1.
        polygon(mesh, points, WHITE, uvs=[(v[3], min(v[4], -.10)) for v in records], texture=0)

    def cell(a, b, s0, s1, t0, t1):
        # Front/back are the original paired tapered surfaces. Every new
        # position and UV is an interpolation inside that source control cage.
        surfaces = []
        for side in (front, back):
            aa, bb = (upper[a], side[a]), (upper[b], side[b])
            surfaces.append([sample(aa, bb, s, t) for s, t in ((s0, t0), (s1, t0), (s1, t1), (s0, t1))])
        for side, records in enumerate(surfaces):
            emit(list(reversed(records)) if side == 0 else records)
        # Bridges close the rails/windows in their existing source basis.
        for edge in range(4):
            nxt = (edge + 1) % 4
            emit([surfaces[0][edge], surfaces[0][nxt], surfaces[1][nxt], surfaces[1][edge]])

    for a in range(3):
        b = a + 1
        cell(a, b, 0, 1, 0, .18)  # uninterrupted blade-side rail
        cell(a, b, 0, 1, .80, 1)  # formed lower support rail
        # Two windows per station; the end and central bridges are real solid
        # silhouette features rather than dark marks on an intact panel.
        for s0, s1 in ((0, .045), (.4775, .5225), (.955, 1)):
            cell(a, b, s0, s1, .18, .80)

    # Keep the original root housing and its pivot silhouette in the same
    # longitudinal texture chart, without a separate new node.
    for face in original.faces:
        indices = struct.unpack_from('<3H', face, 2)
        if min(original.vertices[i][0] for i in indices) >= upper[-1][0] - .00005:
            emit([original.vertices[i] for i in indices])
    return mesh


def generate(source_bytes, role, texture_directory=None):
    """Return CM3D bytes and compact metrics; no source or runtime state edits."""
    role = {3: 'wiper', 4: 'wheel'}.get(role, role)
    definition = json.loads(DEFINITION.read_text(encoding='utf-8'))
    if role not in ('wheel', 'wiper'):
        raise ValueError('Shared cockpit geometry role must be wheel or wiper')
    if crc32(source_bytes) != definition[role]['source_crc32']:
        raise ValueError('Shared cockpit geometry requires its canonical1.1a member')
    header, nodes, originals, textures = decode(source_bytes)
    meshes = [wheel_mesh(originals[0], definition[role]) if role == 'wheel' else wiper_mesh(originals[0])]
    if texture_directory is not None:
        import cockpit_textures
        textures = cockpit_textures.replace_textures(textures, struct.unpack_from('<H', header, 16)[0], texture_directory)
    output = pack(header, nodes, meshes, textures)
    return output, {'role': role, 'source_model_crc32': crc32(source_bytes), 'output_crc32': crc32(output),
                    'bytes': len(output), 'vertices': len(meshes[0].vertices), 'stored_triangles': len(meshes[0].faces),
                    'original_visible_triangles': visible_triangles(nodes, originals),
                    'visible_triangles': visible_triangles(nodes, meshes)}
