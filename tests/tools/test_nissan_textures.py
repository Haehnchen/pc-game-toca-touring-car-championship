#!/usr/bin/env python3
"""Nissan V05 source identity, decal/glass alpha and reproducible artwork."""

from pathlib import Path
import json
import shutil
import struct
import subprocess
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import nissan_textures as nissan
import vehicle_textures as images
from vehicle_model import decode, read_source

PACKAGE = PROJECT / 'assets/models/vehicles/nissan/textures'
CONVERTER = shutil.which('magick') or shutil.which('convert')


def rgba(name):
    data = (PACKAGE / (name + '.webp')).read_bytes()
    width, height = images.webp_dimensions(data)
    pixels = subprocess.run([CONVERTER, str(PACKAGE / (name + '.webp')), '-depth', '8', 'rgba:-'],
                            check=True, stdout=subprocess.PIPE).stdout
    return width, height, pixels


class NissanTextureTests(unittest.TestCase):

    def test_arch_paint_continuation_preserves_actual_opening_and_other_black_details(self):
        width = height = 256
        pixels = bytearray(width * height * 4)
        blue = (205, 211, 218, 255)
        # A continuous painted annulus surrounds a genuinely empty source
        # opening. Registration must bring that paint into the released band
        # without changing the unreleased adjacent opening or remote detail.
        import math
        for y in range(height):
            for x in range(width):
                if 24 <= math.hypot(x + .5 - 207.5, y + .5 - 8) <= 27:
                    pixels[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes(blue)
        # The defective atlas had a dark antialias rim above the old32 cutoff.
        pixels[(29 * width + 215) * 4:(29 * width + 215) * 4 + 4] = bytes((60,60,60,255))
        released = bytearray(width * height)
        released[29 * width + 215] = 1
        released[100 * width + 100] = 1
        output = nissan.continue_arch_paint(width, height, pixels, released)
        self.assertEqual(tuple(output[(29 * width + 215) * 4:(29 * width + 215) * 4 + 3]), blue[:3])
        self.assertEqual(output[(29 * width + 214) * 4:(29 * width + 214) * 4 + 4], bytes(4))
        self.assertEqual(output[(100 * width + 100) * 4:(100 * width + 100) * 4 + 4], bytes(4))

    def test_solid_bumper_corners_continue_paint_without_opening_other_keys(self):
        header, _, _, section = decode(read_source(nissan.SOURCE))
        original = images.texture_records(section, struct.unpack_from('<H',header,16)[0])[0][2]
        width = height = 256
        pixels, released = bytearray(width*height*4), bytearray(width*height)
        paint = bytes((220,226,232,255))
        for x in (170,250):
            pixels[(153*width+x)*4:(153*width+x)*4+4] = paint
        for x in (164,252):
            released[153*width+x] = 1
        released[140*width+220] = 1
        result = nissan.continue_bumper_paint(original,width,height,pixels,released)
        for x in (164,252):
            self.assertEqual(result[(153*width+x)*4:(153*width+x)*4+3],paint[:3])
            self.assertEqual(result[(153*width+x)*4+3],0) # RGB helper cannot release alpha.
        self.assertEqual(result[(153*width+253)*4:(153*width+253)*4+4],bytes(4))
        self.assertEqual(result[(140*width+220)*4:(140*width+220)*4+4],bytes(4))


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_source_word_orientation_bounds_and_local_artwork_scope(self):
        original = images.texture_records(decode(read_source(nissan.SOURCE))[3],9)[0][2]
        metadata = json.loads((PACKAGE/'metadata.json').read_text())
        width=height=1024
        raw = subprocess.check_output([CONVERTER,str(PACKAGE/'work/Nissan.webp'),'-resize','1024x1024!',
                                       '-depth','8','rgba:-'])
        result = nissan.register_body_words(original,width,height,raw,metadata)
        allowed = bytearray(width*height)
        for entry in metadata['body_words']:
            x,y,w,h = entry['search_master']
            for yy in range(round(y*height/1254),round((y+h)*height/1254)):
                for xx in range(round(x*width/1254),round((x+w)*width/1254)):
                    allowed[yy*width+xx]=1
        self.assertTrue(all(allowed[i] or raw[i*4:i*4+4]==result[i*4:i*4+4] for i in range(width*height)))
        entry = metadata['body_words'][0]
        x,y,w,h = entry['search_master']
        points = [(xx,yy) for yy in range(round(y*height/1254),round((y+h)*height/1254))
                  for xx in range(round(x*width/1254),round((x+w)*width/1254))
                  if max(raw[(yy*width+xx)*4:(yy*width+xx)*4+3])<100]
        x0,y0,x1,y1 = min(x for x,y in points),min(y for x,y in points),max(x for x,y in points),max(y for x,y in points)
        for yy in range(y0,y1+1):
            for xx in range(x0,x1+1):
                old = ((y0+y1-yy)*width+x0+x1-xx)*4
                new = (yy*width+xx)*4
                self.assertEqual(max(result[new:new+3])<100,max(raw[old:old+3])<100)
        # These source guard rows are blank beside the red word's first half.
        # The original generated baselines spilled into them toward the hole.
        for y0,y1 in ((136,140),(160,164),(878,884)):
            for yy in range(y0,y1):
                for xx in range(704,800):
                    r,g,b = result[(yy*width+xx)*4:(yy*width+xx)*4+3]
                    self.assertFalse(r>90 and r>1.5*g and r>1.5*b,(xx,yy))

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_decoded_actual_arch_paint_is_opaque_without_cutout_black_residue(self):
        import math
        import nissan_geometry as geometry
        import vehicle_wheel_wells as wells
        from vehicle_manifest import load_definition,load_manifest
        entry = next(e for e in load_manifest()['vehicles'] if e['key']=='nissan')
        definition = load_definition(entry)
        model = geometry.generate(read_source(nissan.SOURCE),definition,PACKAGE)[0]
        header,nodes,meshes,textures = decode(model)
        _,_,centers = wells.anchors(nodes)
        original = images.texture_records(decode(read_source(nissan.SOURCE))[3],9)[0][2]
        ow,oh,old = images.bmp_pixels(original)
        width,height,pixels = rgba('Nissan')
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
                if struct.unpack_from('<h',face,8)[0] != 0:
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
                        power = definition['wheel_aperture_powers'][wheel//2]
                        dx,dy = abs(p[0]-centers[wheel][0])/rx,abs(p[1]-centers[wheel][1])/ry
                        level = (dx**power+dy**power)**(1/power)
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
        for u in (54/256,207.5/256):
            for v in (8/256-1,(255.953125-8)/256-1):
                self.assertEqual(color(u,v)[3],0,(u,v))


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_original_slots_and_exact_glass_key_masks(self):
        header, _, _, section = decode(read_source(nissan.SOURCE))
        records = images.texture_records(section, struct.unpack_from('<H', header, 16)[0])
        self.assertEqual(tuple(n for n, _, _ in records), nissan.SLOTS)
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

        for name in ('Num4', 'Num23'):
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

    @unittest.skipUnless(CONVERTER, 'ImageMagick required')
    def test_number_ink_explicit_alpha_and_both_driver_name_bands(self):
        for name, components in (('Num4', 1), ('Num23', 2)):
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
        self.assertEqual(metadata['name_source_uv_range'], [.625, .75])
        for name in ('ListA', 'ListB'):
            width, height, pixels = rgba(name)
            low, high = metadata['name_source_uv_range']
            self.assertGreater(sum(pixels[(y * width + x) * 4 + 3] == 255
                                   for y in range(int(low*height), int(high*height)) for x in range(width)), 1000)
            self.assertTrue(all(pixels[(y*width+x)*4+3] == 0
                                for y in (int(low*height), int(high*height)-1) for x in range(width)))

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_finalization_reproduces_every_embedded_webp_byte(self):
        self.assertTrue(nissan.finalize(True)['verified'])


if __name__ == '__main__':
    unittest.main()
