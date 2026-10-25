#!/usr/bin/env python3
"""Peugeot source bindings, fixed-point topology and actual wheel-sweep clearance."""

import collections
import json
import math
from pathlib import Path
import runpy
import struct
import sys
import unittest
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import peugeot_geometry as geometry
import vehicle_wheel_stance as stance
import vehicle_wheel_wells as wells
from git_game_data import configured_data_root
from vehicle_model import Mesh, add, crc32, cross, decode, dot, pack, read_source, sub


def clip_axial(polygon, bound, sign):
    result = []
    if not polygon:
        return result
    a, da = polygon[-1], sign * (polygon[-1][2] - bound)
    for b in polygon:
        db = sign * (b[2] - bound)
        if (da >= 0) != (db >= 0):
            result.append(tuple(a[k] + da / (da - db) * (b[k] - a[k]) for k in range(3)))
        if db >= 0:
            result.append(b)
        a, da = b, db
    return result

def nearest_radial(polygon):
    """Exact distance from the axle to a convex triangle/slab intersection."""
    minimum, turns = math.inf, []
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = dx * dx + dy * dy
        t = max(0., min(1., -(a[0] * dx + a[1] * dy) / length)) if length else 0.
        minimum = min(minimum, math.hypot(a[0] + t * dx, a[1] + t * dy))
        turns.append(a[0] * b[1] - a[1] * b[0])
    return 0. if min(turns) > 1e-12 or max(turns) < -1e-12 else minimum

STEERING_UNITS = (-398, -300, -225, -100, 0, 100, 225, 300, 398)

def triangle_clearance(triangles, centers, profile):
    """Conservative full-face radial clearance, including arbitrary wheel spin.

    Intersect each actual triangle with thin axial slabs of the continuous
    tire shoulder. The exact nearest radial point is compared with the slab's
    greatest tire radius, so interiors and edges participate, not only vertices.
    """
    slabs = []
    for (za, ra), (zb, rb) in zip(profile, profile[1:]):
        for step in range(16):
            low, high = step / 16, (step + 1) / 16
            slabs.append((za + (zb - za) * low, za + (zb - za) * high,
                          max(ra + (rb - ra) * low, ra + (rb - ra) * high)))
    minimum = [[math.inf] * len(STEERING_UNITS) for _ in centers]
    for triangle in triangles:
        centroid = tuple(sum(p[k] for p in triangle) / 3 for k in range(3))
        wheel = min(range(4), key=lambda i: abs(centroid[0] - centers[i][0]) + abs(centroid[2] - centers[i][2]))
        center = centers[wheel]
        states = enumerate(STEERING_UNITS) if wheel < 2 else ((0, 0),)
        for state, steering in states:
            angle = steering * math.tau / 4096
            c, s = math.cos(angle), math.sin(angle)
            points = [(c * (p[0] - center[0]) - s * (p[2] - center[2]), p[1] - center[1],
                       s * (p[0] - center[0]) + c * (p[2] - center[2])) for p in triangle]
            low, high = min(p[2] for p in points), max(p[2] for p in points)
            for za, zb, radius in slabs:
                if low > zb or high < za:
                    continue
                polygon = clip_axial(clip_axial(points, za, 1), zb, -1)
                if len(polygon) >= 3:
                    minimum[wheel][state] = min(minimum[wheel][state], nearest_radial(polygon) - radius)
    for wheel in range(2, len(centers)):
        minimum[wheel] = [minimum[wheel][0]] * len(STEERING_UNITS)
    return minimum

def clearance_reports(before, after, definition):
    floor, _, centers = wells.anchors(after[1])
    mesh = after[2][floor]
    liners = [[mesh.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
              for face in mesh.faces[len(before[2][floor].faces)::2]]
    records = wells.node_records(before[1])
    origin = next(r['origin'] for r in records if r['id'] == 14)
    paint = []
    for node in records:
        if not node['variants'] or node['id'] in (1, 2, 3, 4, 14):
            continue
        mesh = before[2][node['variants'][0]]
        offset = sub(node['origin'], origin)
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != 1:
                continue
            triangle = [add(mesh.vertices[i][:3], offset) for i in struct.unpack_from('<3H', face, 2)]
            if min(p[2] for p in triangle) > .55 or max(p[2] for p in triangle) < -.55:
                paint.append(triangle)
    return {'liner': triangle_clearance(liners, centers, definition['wheel_profile']),
            'paint': triangle_clearance(paint, centers, definition['wheel_profile'])}

def fold_report(source, definition, image_path, meshes=None):
    """Sample decoded artwork along all eight actual physical source folds.

    Barycentric lookup uses final quantized faces, including the curved bumper
    edge polylines. Color runs describe real motif phase; opaque minima expose
    canonical-key leaks independently of reflections or color variation.
    """
    from PIL import Image
    image = Image.open(image_path).convert('RGBA')
    width, height = image.size
    pixels = image.load()
    originals = decode(source)[2]
    meshes = meshes or decode(pack(*geometry.prepare(source, definition)))[2]

    def color(uv):
        x, y = uv[0]*width-.5, (uv[1]+1)*height-.5
        ix, iy = math.floor(x), math.floor(y)
        fx, fy = x-ix, y-iy
        return tuple(sum(pixels[max(0,min(width-1,ix+dx)),max(0,min(height-1,iy+dy))][k]*weight
                         for dx,dy,weight in ((0,0,(1-fx)*(1-fy)),(1,0,fx*(1-fy)),
                                              (0,1,(1-fx)*fy),(1,1,fx*fy))) for k in range(4))

    cache = {}
    def uv(slot, point, chart):
        key = slot, chart
        if key not in cache:
            triangles = []
            for face in meshes[slot].faces:
                if struct.unpack_from('<h', face, 8)[0] != 1:
                    continue
                tri = [meshes[slot].vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                u = sum(p[3] for p in tri)/3*256
                v = sum(p[4]+1 for p in tri)/3*256
                if chart == 'lamp-main' and u < 16:
                    continue
                if chart == 'lamp-side' and u > 16:
                    continue
                if chart == 'front-main' and u > 100:
                    continue
                if chart == 'front-side' and u < 220:
                    continue
                if chart == 'rear-main' and (v < 42 or v > 153):
                    continue
                if chart == 'rear-side' and 42 < v < 153:
                    continue
                a,b,c = tri
                ab,ac = sub(b[:3],a[:3]),sub(c[:3],a[:3])
                aa,cc,bc = dot(ab,ab),dot(ac,ac),dot(ab,ac)
                denominator = aa*cc-bc*bc
                if denominator > 1e-18:
                    triangles.append((tri,ab,ac,aa,cc,bc,denominator))
            cache[key] = triangles
        best = None
        for tri,ab,ac,aa,cc,bc,denominator in cache[key]:
            ap = sub(point,tri[0][:3])
            wb = (cc*dot(ap,ab)-bc*dot(ap,ac))/denominator
            wc = (aa*dot(ap,ac)-bc*dot(ap,ab))/denominator
            weights = tuple(max(0,w) for w in (1-wb-wc,wb,wc))
            total = sum(weights)
            weights = tuple(w/total for w in weights)
            projected = tuple(sum(w*p[k] for w,p in zip(weights,tri)) for k in range(3))
            result = tuple(sum(w*p[k] for w,p in zip(weights,tri)) for k in (3,4))
            candidate = math.dist(point,projected),result
            if best is None or candidate < best:
                best = candidate
        if best is None or best[0] > 4/65536:
            raise AssertionError((slot,chart,point,best))
        return best[1]

    def physical_path(slot, indices):
        mesh = meshes[slot]
        result = []
        for ia,ib in zip(indices,indices[1:]):
            a,b = originals[slot].vertices[ia],originals[slot].vertices[ib]
            du,dv = b[3]-a[3],b[4]-a[4]
            length = du*du+dv*dv
            points = {}
            for face in mesh.faces:
                if struct.unpack_from('<h',face,8)[0] != 1:
                    continue
                for i in struct.unpack_from('<3H',face,2):
                    p = mesh.vertices[i]
                    t = ((p[3]-a[3])*du+(p[4]-a[4])*dv)/length
                    if -.00001 <= t <= 1.00001 and abs((p[3]-a[3])*dv-(p[4]-a[4])*du) < 1e-6:
                        points[round(t,7)] = p[:3]
            result.extend(p for _,p in sorted(points.items()))
        return [p for i,p in enumerate(result) if not i or math.dist(p,result[i-1]) > 1/65536]

    def motif(rgba):
        r,g,b,_ = rgba
        if r > 80 and r > 1.5*g and r-b > 40:
            return 'red'
        if r > 70 and r > 1.04*g and r > 1.4*b:
            return 'gold'
        if g > 45 and g > 1.13*r and g > 1.2*b:
            return 'green'
        return 'dark' if (r+g+b)/3 < 90 else 'neutral'

    report = {}
    for key,slot,indices,slots,charts in (
            ('front_positive_z',45,(0,3),(45,0),(None,None)),
            ('front_negative_z',47,(1,3),(47,4),(None,None)),
            ('rear_positive_z',16,(1,2),(16,16),('lamp-main','lamp-side')),
            ('rear_negative_z',18,(0,1),(18,18),('lamp-main','lamp-side')),
            ('bumper_front_positive_z',9,(4,6),(9,9),('front-main','front-side')),
            ('bumper_front_negative_z',9,(9,11),(9,9),('front-main','front-side')),
            ('bumper_rear_positive_z',20,(2,0,6),(20,20),('rear-main','rear-side')),
            ('bumper_rear_negative_z',20,(11,10,13),(20,20),('rear-main','rear-side'))):
        path = physical_path(slot,indices)
        spans = [math.dist(a,b) for a,b in zip(path,path[1:])]
        length = sum(spans)
        samples = [[],[]]
        minima = [255.,255.]
        for step in range(401):
            t,distance = step/400,length*step/400
            for a,b,span in zip(path,path[1:],spans):
                if distance <= span+1e-10:
                    local = distance/span
                    point = tuple(x*(1-local)+y*local for x,y in zip(a,b))
                    break
                distance -= span
            for side,(selected,chart) in enumerate(zip(slots,charts)):
                rgba = color(uv(selected,point,chart))
                if .005 < t < .995:
                    minima[side] = min(minima[side],rgba[3])
                samples[side].append(motif(rgba))
        runs = []
        for row in samples:
            groups = []
            for i,name in enumerate(row):
                if groups and groups[-1][0] == name:
                    groups[-1][2] = i/400
                else:
                    groups.append([name,i/400,i/400])
            runs.append([group for group in groups if group[2]-group[1] >= .005])
        report[key] = dict(length_m=length,alpha_min=minima,motif_runs=runs)
    return report


class PeugeotGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data_root = configured_data_root()
        inventory = runpy.run_path(str(PROJECT / 'tools/inventory-vehicles.py'))['inventory'](cls.data_root)
        cls.entry = next(v for v in inventory['vehicles'] if v['key'] == 'peugeot')
        cls.definition = json.loads((PROJECT / 'assets/models/vehicles/peugeot/definition.json').read_text())
        cls.source = read_source(cls.entry['models']['high']['source'], cls.data_root)
        cls.original = decode(cls.source)
        cls.model, cls.stats = geometry.generate(cls.source, cls.definition)
        cls.generated = decode(cls.model)
        cls.adjusted, cls.stance = stance.apply(cls.model, cls.definition)
        cls.lined, cls.well_stats = wells.apply(cls.adjusted, cls.definition)

    def test_exact_source_slots_hierarchy_and_damage_bindings(self):
        self.assertEqual(crc32(self.source), 'c6af623c')
        self.assertEqual(struct.unpack_from('<3H', self.generated[0], 12), (25, 50, 9))
        self.assertEqual(self.generated[1], self.original[1])
        self.assertEqual(self.generated[3], self.original[3])
        self.assertEqual([m.header[:12] for m in self.generated[2]], [m.header[:12] for m in self.original[2]])
        for level, member, checksum in (('medium',23,'b42bdbc7'),('low',24,'be4a28c5')):
            source = self.entry['models'][level]['source']
            self.assertEqual(source['member'], member)
            self.assertEqual(crc32(read_source(source, self.data_root)), checksum)
        self.assertEqual(self.model, geometry.generate(self.source, self.definition)[0])
        with self.assertRaisesRegex(ValueError, 'canonical HIGH member22'):
            geometry.generate(self.source, dict(self.definition, source_model_crc32='c577ebac'))
        # Missing front-fender/bonnet variants participate in the same lateral
        # source-point flare, so their adjoining edges keep the same placement.
        for index in (8,17,19,42,44,46,48):
            self.assertEqual(self.generated[2][index].encode(), self.original[2][index].encode(), index)
        flared = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in self.original[2]]
        geometry.flare_front_fenders(flared, self.original[1], self.definition)
        for index in (3,7,12,15,26):
            self.assertEqual(self.generated[2][index].encode(), flared[index].encode(), index)
        for healthy, damaged in ((0,2),(4,6),(9,11),(13,14),(20,22),(24,25),(27,29),(30,32),(36,37)):
            self.assertNotEqual(self.generated[2][healthy].vertices, self.generated[2][damaged].vertices)

    def test_flare_keeps_source_xy_uv_and_shared_points_for_every_variant(self):
        source = self.original[2]
        meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in source]
        geometry.flare_front_fenders(meshes, self.original[1], self.definition)
        shared = {}
        maximum = 0.
        for slot, (before, after) in enumerate(zip(source, meshes)):
            self.assertEqual(after.faces, before.faces, slot)
            for a, b in zip(before.vertices, after.vertices):
                self.assertEqual(a[:2]+a[3:5], b[:2]+b[3:5], slot)
                displacement = b[2]-a[2]
                maximum = max(maximum, abs(displacement))
                self.assertGreaterEqual(displacement*(1 if a[2] > 0 else -1), 0.)
                self.assertLessEqual(abs(displacement), self.definition['front_fender_flare']+1e-12)
                if a[:3] in shared:
                    self.assertEqual(shared[a[:3]], b[:3], slot)
                shared[a[:3]] = b[:3]
        self.assertGreater(maximum, .020)
        self.assertEqual(meshes[49].encode(), source[49].encode())

    def test_aperture_cut_preserves_curved_skin_and_shared_panel_edges(self):
        # Clipping preserves the existing curved skin and shared edges despite
        # different fender/bumper/door subdivision patterns.
        with patch.object(geometry, 'cut_wheel_apertures', lambda *args: None):
            before = decode(pack(*geometry.prepare(self.source, self.definition)))[2]
        after = self.generated[2]
        for index in self.definition['arch_meshes']:
            self.assertEqual(after[index].vertices[:len(before[index].vertices)],
                             before[index].vertices, index)

        def edge_points(slot, indices):
            a, b = [self.original[2][slot].vertices[i] for i in indices]
            du, dv = b[3]-a[3], b[4]-a[4]
            length = du*du+dv*dv
            mesh = after[slot]
            used = {i for face in mesh.faces if struct.unpack_from('<h', face, 8)[0] == 1
                    for i in struct.unpack_from('<3H', face, 2)}
            points = {}
            for i in used:
                p = mesh.vertices[i]
                t = ((p[3]-a[3])*du+(p[4]-a[4])*dv)/length
                distance = abs((p[3]-a[3])*dv-(p[4]-a[4])*du)/math.sqrt(length)
                if -.0001 <= t <= 1.0001 and distance < 1.5/65536:
                    points[t] = p[:3]
            return [p for _, p in sorted(points.items())]

        def distance_to_path(point, path):
            minimum = math.inf
            for a, b in zip(path, path[1:]):
                delta = sub(b, a)
                length = dot(delta, delta)
                t = max(0., min(1., dot(sub(point, a), delta)/length)) if length else 0.
                minimum = min(minimum, math.dist(point, add(a, tuple(t*v for v in delta))))
            return minimum

        catalog = {}
        for slot in self.definition['arch_meshes'] + [13, 14]:
            mesh = self.original[2][slot]
            for face in mesh.faces:
                if struct.unpack_from('<h', face, 8)[0] != 1:
                    continue
                ids = struct.unpack_from('<3H', face, 2)
                for a, b in zip(ids, ids[1:]+ids[:1]):
                    key = tuple(sorted((mesh.vertices[a][:3], mesh.vertices[b][:3])))
                    catalog.setdefault(key, {})[slot] = (a, b)
        joins = 0
        for uses in catalog.values():
            uses = sorted(uses.items())
            if len(uses) < 2:
                continue
            left, la = uses[0]
            for right, ra in uses[1:]:
                a, b = edge_points(left, la), edge_points(right, ra)
                if len(a) < 2 and len(b) < 2:
                    continue
                joins += 1
                self.assertGreaterEqual(len(a), 2, (left, la, right, ra))
                self.assertGreaterEqual(len(b), 2, (left, la, right, ra))
                for point in a:
                    self.assertLess(distance_to_path(point, b), .0002, (left, right, point))
                for point in b:
                    self.assertLess(distance_to_path(point, a), .0002, (left, right, point))
        self.assertGreater(joins, 0)

    def test_front_skin_openings_follow_round_elliptical_shoulders(self):
        # Trace the actual paint silhouette. A flattened superellipse can pass
        # tire clearance while making both shoulders look like square corners.
        _,_,centers = wells.anchors(self.generated[1])
        radius = self.definition['wheel_aperture_radii'][0]
        height = self.definition['wheel_aperture_heights'][0]
        for wheel,center in enumerate(centers[:2]):
            contour = wells.aperture(self.generated[2],self.generated[1],center,self.definition)
            # Every real upper contour vertex participates: the original
            # 83.53degree crown notch fell between the representative rays.
            for point in contour:
                if point[1] <= center[1]:
                    continue
                ellipse = ((point[0]-center[0])/radius)**2+((point[1]-center[1])/height)**2
                self.assertAlmostEqual(ellipse,1.,delta=.01,msg=str((wheel,point)))
            for degrees in (30,45,60,90,120,135,150):
                dx,dy = math.cos(math.radians(degrees)),math.sin(math.radians(degrees))
                hits = []
                for a,b in zip(contour,contour[1:]):
                    ex,ey = b[0]-a[0],b[1]-a[1]
                    denominator = dx*ey-dy*ex
                    if abs(denominator) < 1e-12:
                        continue
                    ax,ay = a[0]-center[0],a[1]-center[1]
                    distance = (ax*ey-ay*ex)/denominator
                    fraction = (ax*dy-ay*dx)/denominator
                    if distance > 0 and -1e-7 <= fraction <= 1+1e-7:
                        hits.append(distance)
                self.assertTrue(hits,(wheel,degrees))
                distance = min(hits)
                ellipse = (distance*dx/radius)**2+(distance*dy/height)**2
                # The48-edge polygon lies slightly inside the ideal ellipse.
                self.assertAlmostEqual(ellipse,1.,delta=.01,msg=str((wheel,degrees)))

    def test_quantized_faces_are_finite_outward_and_nondegenerate(self):
        for index, mesh in enumerate(decode(self.lined)[2]):
            self.assertLess(max(len(mesh.vertices), len(mesh.faces)), 65536)
            self.assertTrue(all(math.isfinite(v) for p in mesh.vertices for v in p), index)
            for face in mesh.faces:
                a,b,c = [mesh.vertices[i] for i in struct.unpack_from('<3H',face,2)]
                normal = cross(sub(b[:3],a[:3]),sub(c[:3],a[:3]))
                self.assertGreater(dot(normal,normal),1e-16,index)
                self.assertGreater(dot(normal,add(add(a[5:],b[5:]),c[5:])),0,index)

    def test_wheel_materials_and_original_pivots(self):
        mesh = self.generated[2][49]
        self.assertTrue(all(struct.unpack_from('<h',f,8)[0] == -1 for f in mesh.faces))
        # Source front openings are already close to the tire. Outward shifts
        # cannot approach40mm; retain the four original matrices exactly.
        self.assertEqual(decode(self.adjusted)[1],self.original[1])

    @staticmethod
    def edge_counts(mesh):
        edges = collections.Counter()
        for face in mesh.faces:
            vertices = [tuple(round(v*65536) for v in mesh.vertices[i][:3]) for i in struct.unpack_from('<3H',face,2)]
            for a,b in zip(vertices,vertices[1:]+vertices[:1]):
                edges[tuple(sorted((a,b)))] += 1
        return edges

    def test_lamps_mirrors_and_actual_wing_are_closed(self):
        for index in geometry.LAMP_SLOTS:
            self.assertTrue(all(count == 2 for count in self.edge_counts(self.generated[2][index]).values()),index)
        for index in (41,43):
            mesh = self.generated[2][index]
            self.assertTrue(all(count == 2 for count in self.edge_counts(mesh).values()),index)
            self.assertTrue(any(tuple(f[10:14]) == geometry.GLASS for f in mesh.faces))
            self.assertTrue(any(tuple(f[10:14]) == geometry.SHELL for f in mesh.faces))
            glass_x = [p[0] for face in self.original[2][index].faces
                       if tuple(face[10:14]) == geometry.GLASS
                       for i in struct.unpack_from('<3H', face, 2)
                       for p in [self.original[2][index].vertices[i]]]
            shell_x = [mesh.vertices[i][0] for face in mesh.faces
                       if tuple(face[10:14]) == geometry.SHELL
                       for i in struct.unpack_from('<3H', face, 2)]
            # The forward source body attachment is a stalk endpoint, not a
            # housing bound. A shelf-sized extrusion is a visible regression.
            self.assertLess(max(shell_x) - max(glass_x), .070)
            self.assertLess(max(shell_x) - min(shell_x), .16)
            shell = [mesh.vertices[i] for face in mesh.faces
                     if tuple(face[10:14]) == geometry.SHELL
                     for i in struct.unpack_from('<3H', face, 2)]
            self.assertLess(max(p[1] for p in shell) - min(p[1] for p in shell), .12)
            self.assertLess(max(p[2] for p in shell) - min(p[2] for p in shell), .21)
            # A thin source mounting triangle closes the pillar-side hole
            # without extending the compact outboard glass/housing.
            mount = [mesh.vertices[i][:3] for face in mesh.faces
                     if tuple(face[10:14]) == geometry.MOUNT
                     for i in struct.unpack_from('<3H', face, 2)]
            self.assertTrue(mount)
            source_glass = [self.original[2][index].vertices[i]
                            for face in self.original[2][index].faces
                            if tuple(face[10:14]) == geometry.GLASS
                            for i in struct.unpack_from('<3H', face, 2)]
            for point in (max(source_glass, key=lambda p: p[1]),
                          min(source_glass, key=lambda p: p[1]),
                          max(self.original[2][index].vertices, key=lambda p: p[0])):
                self.assertLess(min(math.dist(p, point[:3]) for p in mount), 2 / 65536)
        for index in (24,25):
            mesh=Mesh(self.original[2][index].header,[],[])
            geometry.solid_wing(mesh,self.original[2][index])
            self.assertTrue(all(count == 2 for count in self.edge_counts(mesh).values()),index)
            self.assertTrue(any(struct.unpack_from('<h',f,8)[0] == 1 for f in mesh.faces))

    def test_lenses_keep_complete_source_corner_positions_and_uv_charts(self):
        for index in geometry.LAMP_SLOTS:
            original,mesh=self.original[2][index],self.generated[2][index]
            for point in original.vertices:
                self.assertTrue(any(math.dist(p[:3],point[:3]) < .00003 and math.dist(p[3:5],point[3:5]) < .00003 for p in mesh.vertices))
            for axis in (3,4):
                self.assertAlmostEqual(min(p[axis] for p in mesh.vertices),min(p[axis] for p in original.vertices),delta=.00002)
                self.assertAlmostEqual(max(p[axis] for p in mesh.vertices),max(p[axis] for p in original.vertices),delta=.00002)

    def test_actual_liner_and_paint_faces_clear_full_spin_tire_at_nine_steering_states(self):
        directory = PROJECT / 'assets/models/vehicles/peugeot/textures'
        model = geometry.generate(self.source,self.definition,directory)[0]
        adjusted = stance.apply(model, self.definition)[0]
        lined = wells.apply(adjusted, self.definition)[0]
        reports = clearance_reports(decode(adjusted), decode(lined), self.definition)
        for surface,wheels in reports.items():
            for wheel,states in enumerate(wheels):
                for state,clearance in enumerate(states):
                    with self.subTest(surface=surface,wheel=wheel,steering=state):
                        self.assertTrue(math.isfinite(clearance))
                        self.assertGreater(clearance,.010)

    def test_liner_mouths_trace_four_actual_skin_openings_to_sill(self):
        before,after=decode(self.adjusted),decode(self.lined)
        self.assertEqual(self.well_stats['wheel_well_mesh'],26)
        self.assertEqual(self.well_stats['wheel_well_count'],4)
        floor,_,centers=wells.anchors(before[1])
        positions={p[:3] for p in after[2][floor].vertices[len(before[2][floor].vertices):]}
        for center in centers:
            contour=wells.aperture(before[2],before[1],center,self.definition)
            self.assertGreater(len(contour),10)
            self.assertLess(min(p[1] for p in contour),-.43)
            for point in contour:
                self.assertLess(min(math.dist(p,point) for p in positions),.005)
        for index,(a,b) in enumerate(zip(before[2],after[2])):
            if index==floor:
                self.assertEqual(b.faces[:len(a.faces)],a.faces)
                self.assertEqual(b.vertices[:len(a.vertices)],a.vertices)
            else:
                self.assertEqual(a.encode(),b.encode())

    def test_local_fold_fields_preserve_surface_positions_normals_and_area(self):
        _, _, meshes, _ = geometry.prepare(self.source, self.definition)
        before = [(list(m.vertices), list(m.faces)) for m in meshes]
        geometry.register_lamp_folds(meshes)
        geometry.remove_quantized_slivers(meshes, range(50))

        def area(vertices, faces):
            total = 0.
            for face in faces:
                a, b, c = [vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
                normal = cross(sub(b, a), sub(c, a))
                total += math.sqrt(dot(normal, normal)) / 2
            return total

        for index, mesh in enumerate(meshes):
            vertices, faces = before[index]
            self.assertEqual([p[:3] + p[5:] for p in mesh.vertices[:len(vertices)]],
                             [p[:3] + p[5:] for p in vertices], index)
            self.assertAlmostEqual(area(vertices, faces), area(mesh.vertices, mesh.faces),
                                   delta=.00001, msg=str(index))

    def test_final_decoded_webp_fold_opacity_and_registered_lamp_transitions(self):
        directory = PROJECT / 'assets/models/vehicles/peugeot/textures'
        model = geometry.generate(self.source,self.definition,directory)[0]
        report = fold_report(self.source,self.definition,directory/'P406.webp',decode(model)[2])
        self.assertEqual(len(report),8)
        for key,record in report.items():
            with self.subTest(fold=key):
                self.assertGreaterEqual(min(record['alpha_min']),254.9)
                if key.startswith('front_'):
                    onsets = [next(run[1] for run in reversed(row) if run[0]=='gold' and run[2]>.99)
                              for row in record['motif_runs']]
                    self.assertLess(abs(onsets[0]-onsets[1])*record['length_m'],.003)
                elif key.startswith('rear_'):
                    # Isolated decoded compression/reflection samples can
                    # interrupt a continuous red band; compare its envelope.
                    bands = [(min(run[1] for run in row if run[0]=='red'),
                              max(run[2] for run in row if run[0]=='red'))
                             for row in record['motif_runs']]
                    for end in (0,1):
                        self.assertLess(abs(bands[0][end]-bands[1][end])*record['length_m'],.003)


if __name__ == "__main__":
    unittest.main()
