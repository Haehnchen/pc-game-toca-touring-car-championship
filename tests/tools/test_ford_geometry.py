#!/usr/bin/env python3
"""Ford source bindings, fixed-point topology and actual wheel-sweep clearance."""

import collections
import json
import math
from pathlib import Path
import runpy
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import ford_geometry as geometry
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
    minimum = [[math.inf] * 3 for _ in centers]
    maximum = 398 * math.tau / 4096
    for triangle in triangles:
        centroid = tuple(sum(p[k] for p in triangle) / 3 for k in range(3))
        wheel = min(range(4), key=lambda i: abs(centroid[0] - centers[i][0]) + abs(centroid[2] - centers[i][2]))
        center = centers[wheel]
        for state, angle in enumerate((-maximum, 0., maximum)):
            if wheel >= 2:
                angle = 0.
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
            if struct.unpack_from('<h', face, 8)[0] != 0:
                continue
            triangle = [add(mesh.vertices[i][:3], offset) for i in struct.unpack_from('<3H', face, 2)]
            if min(p[2] for p in triangle) > .55 or max(p[2] for p in triangle) < -.55:
                paint.append(triangle)
    return {'liner': triangle_clearance(liners, centers, definition['wheel_profile']),
            'paint': triangle_clearance(paint, centers, definition['wheel_profile'])}


def final_frame_report(source, definition, directory, image_path=None, strict=True):
    """Decoded final-WebP motifs queried on the actual quantized fold triangles.

    Neutral headlamp trim/reflections are separated from blue body paint.
    Rear clear bands are separated from red lenses. All tests sample the
    physical fold, so chart rotation and perspective cannot hide a seam step.
    """
    from PIL import Image
    texture = Path(image_path) if image_path else Path(directory) / 'MON.webp'
    image = Image.open(texture).convert('RGBA')
    width,height=image.size
    pixels=image.load()
    originals=decode(source)[2]
    before=decode(pack(*geometry.prepare(source,definition)))[2]
    after=decode(geometry.generate(source,definition,directory)[0])[2]
    if image_path is not None:
        before=after=originals
    def color(uv):
        px,py=uv[0]*width-.5,(uv[1]+1)*height-.5
        x,y=math.floor(px),math.floor(py)
        fx,fy=px-x,py-y
        return tuple(sum(pixels[max(0,min(width-1,x+dx)),max(0,min(height-1,y+dy))][k]*w
                         for dx,dy,w in ((0,0,(1-fx)*(1-fy)),(1,0,fx*(1-fy)),(0,1,(1-fx)*fy),(1,1,fx*fy)))
                     for k in range(4))
    panel_cache = {}
    def panel_uv(mesh, point, chart=None):
        key = id(mesh), chart
        best = None
        if key not in panel_cache:
            panel_cache[key] = []
            for face in mesh.faces:
                if struct.unpack_from('<h', face, 8)[0] != 0:
                    continue
                triangle = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                if chart == 'unused-bumper-front' and any(v[3] > .15 for v in triangle):
                    continue
                if chart == 'unused-bumper-side' and any(v[3] < .85 for v in triangle):
                    continue
                if chart == 'front-main' and any(v[3] < .65 for v in triangle):
                    continue
                if chart == 'front-side' and any(v[3] < .90 for v in triangle):
                    continue
                if chart == 'front-main' and any(not -.55 <= v[4] <= -.3 for v in triangle):
                    continue
                if chart == 'front-side' and any(-.55 <= v[4] <= -.3 for v in triangle):
                    continue
                if chart == 'rear-side' and any(v[3] > .10 for v in triangle):
                    continue
                if chart == 'rear-main' and any(v[3] < .3 for v in triangle):
                    continue
                a, b, c = triangle
                ab, ac = sub(b[:3], a[:3]), sub(c[:3], a[:3])
                aa, cc, bc = dot(ab, ab), dot(ac, ac), dot(ab, ac)
                denominator = aa * cc - bc * bc
                if denominator >= 1e-18:
                    panel_cache[key].append((triangle, ab, ac, aa, cc, bc, denominator))
        for triangle, ab, ac, aa, cc, bc, denominator in panel_cache[key]:
            ap = sub(point, triangle[0][:3])
            wb = (cc * dot(ap, ab) - bc * dot(ap, ac)) / denominator
            wc = (aa * dot(ap, ac) - bc * dot(ap, ab)) / denominator
            weights = 1 - wb - wc, wb, wc
            # Clamp only the query to the physical triangle. Quantized split
            # edges can lie a few micrometres away from the analytic source
            # line; the strict four-fixed-unit distance check below still
            # rejects points outside the actual surface.
            if min(weights) < 0:
                weights = tuple(max(0., w) for w in weights)
                total = sum(weights)
                weights = tuple(w / total for w in weights)
            projected = tuple(sum(w * v[k] for w, v in zip(weights, triangle)) for k in range(3))
            uv = tuple(sum(w * v[k] for w, v in zip(weights, triangle)) for k in (3, 4))
            candidate = math.dist(point, projected), -min(weights), uv
            if best is None or candidate < best:
                best = candidate
        if best is None or best[0] > 4 / 65536:
            raise AssertionError(f'Fold point {point} escaped its actual {chart} panel: {best}')
        return best[2]

    def measure(meshes,a,b,slots,charts,rear,path=None,kind=None):
        path=path or [a,b]
        spans=[math.dist(p,q) for p,q in zip(path,path[1:])]
        length=sum(spans)
        hits=[[],[]]
        for step in range(401):
            t=step/400
            distance=length*t
            for start,end,span in zip(path,path[1:],spans):
                if distance <= span+1e-10:
                    local=distance/span
                    point=tuple(x*(1-local)+y*local for x,y in zip(start,end))
                    break
                distance-=span
            for side,(slot,chart) in enumerate(zip(slots,charts)):
                rgba=color(panel_uv(meshes[slot],point,chart))
                if strict and rgba[3] < 254.9 and (kind is None or .025 < t < .95):
                    raise AssertionError(f'Physical lamp fold must remain opaque: {slot}, {chart}, {t}, {rgba}, {panel_uv(meshes[slot],point,chart)}')
                red,green,blue,_=rgba
                selected=(min(red,green,blue)>120 and abs(red-green)<70 and abs(green-blue)<70) if rear else not (blue-max(red,green)>15 and blue>1.3*max(red,green))
                if kind == 'yellow':
                    selected=red>120 and green>120 and blue<100
                elif kind == 'white':
                    selected=min(red,green,blue)>105 and max(red,green,blue)-min(red,green,blue)<95
                if selected:
                    hits[side].append(t)
        if not all(hits):
            raise AssertionError(f'Missing fold motif: {slots}, {charts}')
        runs=[]
        for values in hits:
            groups=[]
            for t in values:
                if groups and t-groups[-1][-1] <= 1.5/400:
                    groups[-1].append(t)
                else:
                    groups.append([t])
            runs.append([(min(g),max(g)) for g in groups if len(g)>=3])
        return dict(length_m=length,runs=runs,first_fraction=[min(h) for h in hits],last_fraction=[max(h) for h in hits],
                    first_gap_m=abs(min(hits[0])-min(hits[1]))*length,last_gap_m=abs(max(hits[0])-max(hits[1]))*length)
    report=dict(texture_crc32=crc32(texture.read_bytes()),sample_intervals=400,front={},rear={})
    for label,side,lamp,top,bottom in (('positive_z',0,45,3,1),('negative_z',4,47,3,0)):
        a,b=[originals[lamp].vertices[i][:3] for i in (top,bottom)]
        report['front'][label]=dict(before=measure(before,a,b,(side,lamp),(None,None),False),after=measure(after,a,b,(side,lamp),(None,None),False))
    for label,lamp,top,bottom in (('positive_z',16,1,3),('negative_z',18,0,3)):
        a,b=[originals[lamp].vertices[i][:3] for i in (top,bottom)]
        report['rear'][label]=dict(before=measure(before,a,b,(lamp,lamp),('rear-side','rear-main'),True),after=measure(after,a,b,(lamp,lamp),('rear-side','rear-main'),True))
    def physical_path(mesh,source_indices):
        result=[]
        for ia,ib in zip(source_indices,source_indices[1:]):
            a,b=originals[9 if mesh is before[9] else 20].vertices[ia],originals[9 if mesh is before[9] else 20].vertices[ib]
            du,dv=b[3]-a[3],b[4]-a[4]
            length=du*du+dv*dv
            selected={}
            used={i for face in mesh.faces if struct.unpack_from('<h',face,8)[0] == 0
                  for i in struct.unpack_from('<3H',face,2)}
            for index in used:
                p=mesh.vertices[index]
                t=((p[3]-a[3])*du+(p[4]-a[4])*dv)/length
                if -.00001<=t<=1.00001 and abs((p[3]-a[3])*dv-(p[4]-a[4])*du)<1e-6:
                    selected[round(t,7)]=p[:3]
            result.extend(p for _,p in sorted(selected.items()))
        return [p for i,p in enumerate(result) if not i or math.dist(p,result[i-1])>1/65536]
    report['bumper']={}
    for label,slot,indices,charts in (
            ('front_positive_z',9,(7,5,4),('front-main','front-side')),
            ('front_negative_z',9,(16,14,13),('front-main','front-side')),
            ('rear_positive_z',20,(2,0,4),('rear-main','rear-side')),
            ('rear_negative_z',20,(8,7,10),('rear-main','rear-side'))):
        path=physical_path(before[slot],indices)
        a,b=path[0],path[-1]
        report['bumper'][label]={}
        for kind in ('yellow','white'):
            report['bumper'][label][kind]=dict(before=measure(before,a,b,(slot,slot),charts,False,path,kind),after=measure(after,a,b,(slot,slot),charts,False,path,kind))
    return report


class FordGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data_root = configured_data_root()
        inventory = runpy.run_path(str(PROJECT / 'tools/inventory-vehicles.py'))['inventory'](cls.data_root)
        cls.entry = next(v for v in inventory['vehicles'] if v['key'] == 'ford')
        cls.definition = json.loads((PROJECT / 'assets/models/vehicles/ford/definition.json').read_text())
        cls.source = read_source(cls.entry['models']['high']['source'], cls.data_root)
        cls.original = decode(cls.source)
        cls.model, cls.stats = geometry.generate(cls.source, cls.definition)
        cls.generated = decode(cls.model)
        cls.adjusted, cls.stance = stance.apply(cls.model, cls.definition)
        cls.lined, cls.well_stats = wells.apply(cls.adjusted, cls.definition)

    def test_exact_source_slots_hierarchy_and_damage_bindings(self):
        self.assertEqual(crc32(self.source), 'eda6480d')
        self.assertEqual(struct.unpack_from('<3H', self.generated[0], 12), (25, 50, 9))
        self.assertEqual(self.generated[1], self.original[1])
        self.assertEqual(self.generated[3], self.original[3])
        self.assertEqual([m.header[:12] for m in self.generated[2]], [m.header[:12] for m in self.original[2]])
        for level, member, checksum in (('medium',17,'b978b063'),('low',18,'28df9b48')):
            source = self.entry['models'][level]['source']
            self.assertEqual(source['member'], member)
            self.assertEqual(crc32(read_source(source, self.data_root)), checksum)
        self.assertEqual(self.model, geometry.generate(self.source, self.definition)[0])
        with self.assertRaisesRegex(ValueError, 'canonical HIGH member16'):
            geometry.generate(self.source, dict(self.definition, source_model_crc32='c577ebac'))
        for index in (3,7,8,12,15,17,19,26,42,44,46,48):
            self.assertEqual(self.generated[2][index].encode(), self.original[2][index].encode(), index)
        for healthy, damaged in ((0,2),(4,6),(9,11),(13,14),(20,22),(24,25),(27,29),(30,32),(36,37)):
            self.assertNotEqual(self.generated[2][healthy].vertices, self.generated[2][damaged].vertices)

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
            self.assertTrue(any(tuple(f[10:14]) == geometry.BLUE for f in mesh.faces))
            original = self.original[2][index]
            glass = [original.vertices[i] for f in original.faces if tuple(f[10:14]) == geometry.GLASS
                     for i in struct.unpack_from('<3H', f, 2)]
            housing = [mesh.vertices[i] for f in mesh.faces if tuple(f[10:14]) == geometry.GLASS
                       for i in struct.unpack_from('<3H', f, 2)]
            side = 1 if sum(p[2] for p in glass) > 0 else -1
            outer = sorted(set(p[:3] for p in glass), key=lambda p: side * p[2])[-2:]
            # A source mounting vertex must not expand the visible housing
            # into the A-pillar or an oversized forward shelf.
            self.assertLessEqual(max(p[0] for p in housing), max(p[0] for p in glass) + .06)
            self.assertLessEqual(max(p[1] for p in housing), max(p[1] for p in outer) + .00003)
            self.assertGreaterEqual(min(side * p[2] for p in housing), side * min(glass, key=lambda p: p[1])[2] - .00003)
            # The blue housing is limited outboard of the mounting plate.
            blue = [mesh.vertices[i] for f in mesh.faces if tuple(f[10:14]) == geometry.BLUE
                    for i in struct.unpack_from('<3H', f, 2)
                    if side * mesh.vertices[i][2] > side * min(glass, key=lambda p: p[1])[2] + .00003]
            self.assertLessEqual(max(p[0] for p in blue), max(p[0] for p in glass) + .06)
            self.assertLessEqual(max(p[1] for p in blue), max(p[1] for p in outer) + .00003)
        for index in (24,25):
            mesh=Mesh(self.original[2][index].header,[],[])
            geometry.solid_wing(mesh,self.original[2][index])
            self.assertTrue(all(count == 2 for count in self.edge_counts(mesh).values()),index)
            self.assertTrue(any(struct.unpack_from('<h',f,8)[0] == 0 for f in mesh.faces))

    def test_lenses_keep_complete_source_corner_positions_and_uv_charts(self):
        for index in geometry.LAMP_SLOTS:
            original,mesh=self.original[2][index],self.generated[2][index]
            for point in original.vertices:
                self.assertTrue(any(math.dist(p[:3],point[:3]) < .00003 and math.dist(p[3:5],point[3:5]) < .00003 for p in mesh.vertices))
            for axis in (3,4):
                self.assertAlmostEqual(min(p[axis] for p in mesh.vertices),min(p[axis] for p in original.vertices),delta=.00002)
                self.assertAlmostEqual(max(p[axis] for p in mesh.vertices),max(p[axis] for p in original.vertices),delta=.00002)

    def test_actual_liner_and_paint_faces_clear_tire_at_both_maximum_turns(self):
        reports=clearance_reports(decode(self.adjusted),decode(self.lined),self.definition)
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

    def test_local_fold_fields_keep_positions_normals_and_surface_area(self):
        header,nodes,meshes,textures=geometry.prepare(self.source,self.definition)
        old=[(list(m.vertices),list(m.faces)) for m in meshes]
        geometry.register_lamp_folds(meshes,self.original[2])
        geometry.register_bumper_folds(meshes)
        geometry.remove_quantized_slivers(meshes,range(50))
        def area(vertices,faces):
            result=0.
            for face in faces:
                a,b,c=[vertices[i][:3] for i in struct.unpack_from('<3H',face,2)]
                normal=cross(sub(b,a),sub(c,a))
                result+=math.sqrt(dot(normal,normal))/2
            return result
        for index,mesh in enumerate(meshes):
            vertices,faces=old[index]
            self.assertEqual([p[:3]+p[5:] for p in mesh.vertices[:len(vertices)]],[p[:3]+p[5:] for p in vertices],index)
            self.assertAlmostEqual(area(vertices,faces),area(mesh.vertices,mesh.faces),delta=.00001,msg=str(index))

    def test_final_webp_outlines_trim_and_three_bumper_stripes_join_on_physical_folds(self):
        directory=PROJECT/'assets/models/vehicles/ford/textures'
        report=final_frame_report(self.source,self.definition,directory)
        for kind in ('front','rear'):
            for side,record in report[kind].items():
                with self.subTest(surface=kind,side=side):
                    self.assertLess(record['after']['first_gap_m'],.003)
                    self.assertLess(record['after']['last_gap_m'],.003)
        for side,record in report['bumper'].items():
            with self.subTest(surface='bumper',side=side):
                yellow=record['yellow']['after']
                runs=[[r for r in chart if r[0]<.6][:2] for chart in yellow['runs']]
                self.assertEqual([len(chart) for chart in runs],[2,2])
                for a,b in zip(runs[0],runs[1]):
                    for first,second in zip(a,b):
                        self.assertLess(abs(first-second)*yellow['length_m'],.003)
                whites=[]
                for chart,yellows in zip(record['white']['after']['runs'],runs):
                    window=(.18,.29) if side.startswith('front') else (.30,.42)
                    selected=[r for r in chart if window[0] < (r[0]+r[1])/2 < window[1]]
                    self.assertTrue(selected)
                    whites.append((selected[0][0],selected[-1][1]))
                for first,second in zip(*whites):
                    self.assertLess(abs(first-second)*yellow['length_m'],.003)
        final=decode(geometry.generate(self.source,self.definition,directory)[0])
        for index,mesh in enumerate(final[2]):
            for face in mesh.faces:
                a,b,c=[mesh.vertices[i] for i in struct.unpack_from('<3H',face,2)]
                normal=cross(sub(b[:3],a[:3]),sub(c[:3],a[:3]))
                self.assertGreater(dot(normal,normal),1e-16,index)
                self.assertGreater(dot(normal,add(add(a[5:],b[5:]),c[5:])),0,index)


if __name__ == '__main__':
    unittest.main()
