#!/usr/bin/env python3
"""Peugeot V06 source identity, decal/glass alpha and reproducible artwork."""

from pathlib import Path
import json
import shutil
import struct
import subprocess
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import peugeot_textures as peugeot
import vehicle_textures as images
from vehicle_model import decode, read_source

PACKAGE = PROJECT / 'assets/models/vehicles/peugeot/textures'
CONVERTER = shutil.which('magick') or shutil.which('convert')


def rgba(name):
    data = (PACKAGE / (name + '.webp')).read_bytes()
    width, height = images.webp_dimensions(data)
    pixels = subprocess.run([CONVERTER, str(PACKAGE / (name + '.webp')), '-depth', '8', 'rgba:-'],
                            check=True, stdout=subprocess.PIPE).stdout
    return width, height, pixels


class PeugeotTextureTests(unittest.TestCase):


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_decoded_actual_arch_paint_is_opaque_without_cutout_black_residue(self):
        import math
        import peugeot_geometry as geometry
        import vehicle_wheel_wells as wells
        from vehicle_manifest import load_definition,load_manifest
        entry = next(e for e in load_manifest()['vehicles'] if e['key']=='peugeot')
        definition = load_definition(entry)
        model = geometry.generate(read_source(peugeot.SOURCE),definition,PACKAGE)[0]
        header,nodes,meshes,textures = decode(model)
        _,_,centers = wells.anchors(nodes)
        original = images.texture_records(decode(read_source(peugeot.SOURCE))[3],9)[1][2]
        ow,oh,old = images.bmp_pixels(original)
        width,height,pixels = rgba('P406')
        def color(u,v):
            x,y = u*width-.5,(v+1)*height-.5
            ix,iy = math.floor(x),math.floor(y)
            fx,fy = x-ix,y-iy
            return tuple(sum(pixels[(max(0,min(height-1,iy+dy))*width+max(0,min(width-1,ix+dx)))*4+k]*weight
                             for dx,dy,weight in ((0,0,(1-fx)*(1-fy)),(1,0,fx*(1-fy)),(0,1,(1-fx)*fy),(1,1,fx*fy)))
                         for k in range(4))
        samples = [0]*4
        for slot in definition['arch_meshes']:
            mesh = meshes[slot]
            for face_index,face in enumerate(mesh.faces):
                if struct.unpack_from('<h',face,8)[0] != 1:
                    continue
                tri = [mesh.vertices[i] for i in struct.unpack_from('<3H',face,2)]
                if min(abs(p[2]) for p in tri) < .55:
                    continue
                for i in range(9):
                    for j in range(9-i):
                        weights = (i/8,j/8,1-(i+j)/8)
                        p = tuple(sum(weight*vertex[k] for weight,vertex in zip(weights,tri)) for k in range(5))
                        wheel = min((k for k in range(4) if centers[k][2]*p[2]>0),
                                    key=lambda k:math.hypot(p[0]-centers[k][0],p[1]-centers[k][1]))
                        distance = math.hypot(p[0]-centers[wheel][0],p[1]-centers[wheel][1])
                        rx = definition['wheel_aperture_radii'][wheel//2]
                        ry = definition['wheel_aperture_heights'][wheel//2]
                        dx,dy = abs(p[0]-centers[wheel][0])/rx,abs(p[1]-centers[wheel][1])/ry
                        level = math.hypot(dx,dy)
                        if not .99 <= level <= 1+.08/min(rx,ry):
                            continue
                        c = color(p[3],p[4])
                        self.assertGreaterEqual(c[3],254.9,(slot,face_index,wheel,p,c))
                        ox = max(0,min(ow-1,int(p[3]*ow)))
                        oy = max(0,min(oh-1,int((p[4]+1)*oh)))
                        if old[oy*ow+ox] == (0,0,0):
                            self.assertGreater(max(c[:3]),32,(slot,face_index,wheel,p,c))
                        samples[wheel] += 1
        self.assertTrue(all(n>100 for n in samples),samples)
        for u in (50/256,210/256):
            for v in (8/256-1,(256.05859375-8)/256-1):
                self.assertEqual(color(u,v)[3],0,(u,v))


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_original_slots_and_exact_glass_key_masks(self):
        header, _, _, section = decode(read_source(peugeot.SOURCE))
        records = images.texture_records(section, struct.unpack_from('<H', header, 16)[0])
        self.assertEqual(tuple(n for n, _, _ in records), peugeot.SLOTS)
        for name in ('WSCRN', 'SIDEG'):
            original = next(payload for n, _, payload in records if n == name)
            old_width, old_height, pixels = images.bmp_pixels(original)
            width, height, output = rgba(name)
            for y in range(height):
                for x in range(width):
                    keyed = pixels[(y * old_height // height) * old_width + x * old_width // width] == (0, 0, 0)
                    self.assertEqual(output[(y * width + x) * 4 + 3], 0 if keyed else 255, (name, x, y))

        for name in ('ListA', 'ListB'):
            original = next(payload for n, _, payload in records if n == name)
            old_width, old_height, old = images.bmp_pixels(original)
            width, height, output = rgba(name)
            for row in range(8):
                source = [(i % old_width, i // old_width) for i in range(row*16*old_width,(row+1)*16*old_width)
                          if old[i] != (0,0,0)]
                actual = [(i % width, i // width) for i in range(row*height//8*width,(row+1)*height//8*width)
                          if output[i*4+3]]
                expected = (round(min(x for x,y in source)*width/old_width),
                            round(min(y for x,y in source)*height/old_height),
                            round((max(x for x,y in source)+1)*width/old_width),
                            round((max(y for x,y in source)+1)*height/old_height))
                bounds = (min(x for x,y in actual),min(y for x,y in actual),
                          max(x for x,y in actual)+1,max(y for x,y in actual)+1)
                self.assertEqual(bounds,expected,(name,row))

        for name in ('Num12', 'Num14'):
            original = next(payload for n, _, payload in records if n == name)
            old_width, old_height, pixels = images.bmp_pixels(original)
            points = [(i % old_width, i // old_width) for i, c in enumerate(pixels) if c != (0, 0, 0)]
            width, height, output = rgba(name)
            actual = [(i % width, i // width) for i, a in enumerate(output[3::4]) if a]
            expected = (round(min(x for x,y in points)*width/old_width),
                        round(min(y for x,y in points)*height/old_height),
                        round((max(x for x,y in points)+1)*width/old_width),
                        round((max(y for x,y in points)+1)*height/old_height))
            bounds = (min(x for x,y in actual), min(y for x,y in actual),
                      max(x for x,y in actual)+1, max(y for x,y in actual)+1)
            self.assertEqual(bounds, expected, name)


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_positive_z_michelin_words_and_rear_mascot_read_upright(self):
        metadata=json.loads((PACKAGE/'metadata.json').read_text())
        self.assertEqual([entry['kind'] for entry in metadata['michelin_ink_rotations']],
                         ['word','mascot','word'])
        width=height=1024
        raw=subprocess.check_output([CONVERTER,str(PACKAGE/'work/P406.webp'),'-resize','1024x1024!',
                                     '-depth','8','rgba:-'])
        registrations=[]
        corrected=peugeot.rotate_michelin_ink(width,height,raw,metadata,registrations)
        self.assertEqual(raw[3::4],corrected[3::4])
        allowed=set().union(*(r['selected']|r['targets'] for r in registrations))
        self.assertTrue(all(i in allowed or raw[i*4:i*4+4]==corrected[i*4:i*4+4]
                            for i in range(width*height)))
        _,_,encoded=rgba('P406')
        source_meshes=decode(read_source(peugeot.SOURCE))[2]
        for registration,entry in zip(registrations,metadata['michelin_ink_rotations']):
            left,top,right,bottom=registration['bounds']
            # Every selected generated pixel must rotate180, including the
            # asymmetric mascot silhouette. No font substitution or mirror.
            for i in registration['selected']:
                xx,yy=i%width,i//width
                target=(top+bottom-1-yy)*width+left+right-1-xx
                self.assertEqual(corrected[target*4:target*4+3],raw[i*4:i*4+3])
            if entry['kind']!='word':
                continue
            dark=[i for i in range(top*width,bottom*width)
                  if left<=i%width<right and max(raw[i*4:i*4+3])<80]
            self.assertGreater(len(dark),200)
            good=0
            for i in dark:
                xx,yy=i%width,i//width
                target=(top+bottom-1-yy)*width+left+right-1-xx
                good+=max(encoded[target*4:target*4+3])<140 and encoded[target*4+3]==255
            self.assertGreater(good/len(dark),.98,entry['label'])
            # The real positive-Z source chart maps atlas right to world +X
            # and atlas down to world +Y. A camera on +Z sees right as -X:
            # rotating both glyph axes180 is therefore upright, not mirrored.
            u,v=(left+right)/2/width,(top+bottom)/2/height
            matched=[]
            for mesh in source_meshes:
                for face in mesh.faces:
                    if struct.unpack_from('<h',face,8)[0]!=1:
                        continue
                    tri=[mesh.vertices[i] for i in struct.unpack_from('<3H',face,2)]
                    if min(p[2] for p in tri)<.55:
                        continue
                    a,b,c=tri;du1,dv1=b[3]-a[3],b[4]-a[4]
                    du2,dv2=c[3]-a[3],c[4]-a[4]
                    determinant=du1*dv2-du2*dv1
                    if abs(determinant)<1e-10:
                        continue
                    du,dv=u-a[3],v-1-a[4]
                    wb,wc=(du*dv2-du2*dv)/determinant,(du1*dv-du*dv1)/determinant
                    if min(wb,wc,1-wb-wc)<-1e-5:
                        continue
                    dxdu=((b[0]-a[0])*dv2-(c[0]-a[0])*dv1)/determinant
                    dydv=(du1*(c[1]-a[1])-du2*(b[1]-a[1]))/determinant
                    matched.append((dxdu,dydv))
            self.assertTrue(matched,entry['label'])
            self.assertTrue(all(dxdu>0 and dydv>0 for dxdu,dydv in matched),matched)


    @unittest.skipUnless(CONVERTER, 'ImageMagick required')
    def test_number_ink_explicit_alpha_and_both_driver_name_bands(self):
        for name, components in (('Num12', 2), ('Num14', 2)):
            width, height, pixels = rgba(name)
            self.assertEqual((width, height), (256, 256))
            self.assertGreater(sum(a == 255 for a in pixels[3::4]), 4000)
            for x, y in ((0, 0), (255, 0), (0, 255), (255, 255)):
                self.assertEqual(pixels[(y * width + x) * 4 + 3], 0)
            # Every retained alpha island belongs to one real digit; stray
            # generator residues must not survive on transparent number cards.
            visited, bodies = set(), []
            for i, alpha in enumerate(pixels[3::4]):
                if alpha < 128 or i in visited:
                    continue
                todo, size = [i], 0
                visited.add(i)
                while todo:
                    current = todo.pop()
                    size += 1
                    x, y = current % width, current // width
                    for xx, yy in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
                        j = yy * width + xx
                        if 0 <= xx < width and 0 <= yy < height and j not in visited and pixels[j*4+3] >= 128:
                            visited.add(j)
                            todo.append(j)
                if size > 100:
                    bodies.append(size)
            self.assertEqual(len(bodies), components, (name, bodies))
        metadata = json.loads((PACKAGE / 'metadata.json').read_text())
        self.assertEqual(metadata['name_source_uv_range'], [.75, .875])
        for name in ('ListA', 'ListB'):
            width, height, pixels = rgba(name)
            low, high = metadata['name_source_uv_range']
            self.assertGreater(sum(pixels[(y * width + x) * 4 + 3] == 255
                                   for y in range(int(low*height), int(high*height)) for x in range(width)), 1000)
            self.assertTrue(all(pixels[(y*width+x)*4+3] == 0
                                for y in (int(low*height), int(high*height)-1) for x in range(width)))


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_finalization_reproduces_every_embedded_webp_byte(self):
        self.assertTrue(peugeot.finalize(True)['verified'])


if __name__ == '__main__':
    unittest.main()
