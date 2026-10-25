"""M01a-d vegetation in original CM3D local coordinates (-Z is up).

The original foliage slots stay in the material prefix. Append-only
MegaTreeBark and per-type foliage slots supply opaque stems and transparent
clusters. Each type has its own spatial crown profile. The tree group keeps its
whole motif; the tall oval crown wraps dense inner imagery around its volume.
"""

import math
import struct


WHITE = (255, 255, 255, 255)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, factor):
    return tuple(x * factor for x in a)


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def unit(vector):
    length = math.sqrt(sum(value * value for value in vector))
    return scale(vector, 1 / length)


def tube(mesh, points, radii, texture, segments=6, phase=0.):
    """Tapered bent bark tube, with a shared smooth ring and an explicit UV seam."""
    rings = []
    distance = [0.]
    for first, second in zip(points, points[1:]):
        distance.append(distance[-1] + math.dist(first, second))
    for row, (point, radius) in enumerate(zip(points, radii)):
        axis = unit(sub(points[min(row + 1, len(points) - 1)], points[max(0, row - 1)]))
        first = unit(cross(axis, (1., 0., 0.) if abs(axis[0]) < .8 else (0., 1., 0.)))
        second = cross(axis, first)
        ring = []
        for index in range(segments + 1):
            angle = math.tau * index / segments + phase
            normal = add(scale(first, math.cos(angle)), scale(second, math.sin(angle)))
            ring.append(mesh.vertex(add(point, scale(normal, radius)), normal,
                                    (index / segments, distance[row] / distance[-1] - 1)))
        rings.append(ring)
    for first, second in zip(rings, rings[1:]):
        for index in range(segments):
            a, b, c, d = first[index], first[index + 1], second[index + 1], second[index]
            mesh.triangle((a, b, c), WHITE, texture)
            mesh.triangle((a, c, d), WHITE, texture)
    # Only the tip cap is potentially visible. The root remains exactly on its
    # original ground plane; buried bottoms do not need duplicate disk faces.
    normal = unit(sub(points[-1], points[-2]))
    center = mesh.vertex(points[-1], normal, (.5, -.5))
    for index in range(segments):
        mesh.triangle((center, rings[-1][index], rings[-1][index + 1]), WHITE, texture)


def leaf_spray(mesh, center, angle, width, height, texture, tilt, roll=0., divisions=3):
    """A bowed quad grid enclosing one complete alpha-cut foliage sprite."""
    normal = unit((math.cos(angle), math.sin(angle), tilt))
    across = (-math.sin(angle), math.cos(angle), 0.)
    vertical = unit(cross(across, normal))  # Local -Z is the top of the texture.
    if roll:
        across, vertical = (add(scale(across, math.cos(roll)), scale(vertical, math.sin(roll))),
                            sub(scale(vertical, math.cos(roll)), scale(across, math.sin(roll))))
    grid = []
    for row in range(divisions + 1):
        v = row / divisions
        y = 1 - 2 * v
        ring = []
        for column in range(divisions + 1):
            u = column / divisions
            x = 2 * u - 1
            bow = width * .16 * (1 - x * x) * (1 - y * y)
            point = add(center, add(add(scale(across, x * width * .5), scale(vertical, y * height * .5)),
                                    scale(normal, bow)))
            slope_x = -.64 * x * (1 - y * y)
            slope_y = -.64 * width / height * y * (1 - x * x)
            vertex_normal = unit(sub(sub(normal, scale(across, slope_x)), scale(vertical, slope_y)))
            ring.append(mesh.vertex(point, vertex_normal, (u, v - 1)))
        grid.append(ring)
    for first, second in zip(grid, grid[1:]):
        for index in range(divisions):
            mesh.triangle((first[index], first[index + 1], second[index + 1]), WHITE, texture)
            mesh.triangle((first[index], second[index + 1], second[index]), WHITE, texture)


def make_simple_tree(original, mesh_type, foliage_slot, bark_slot):
    """Broad TRIS04 crown: one simple stem and fourteen overlapping bunches."""
    mesh = mesh_type(original.header, [], [])
    bounds = [(min(v[a] for v in original.vertices), max(v[a] for v in original.vertices)) for a in range(3)]
    cx, cy = [(bounds[a][0] + bounds[a][1]) * .5 for a in (0, 1)]
    bottom, top = bounds[2][1], bounds[2][0]
    height = bottom - top
    radius = min(bounds[0][1] - cx, bounds[1][1] - cy)
    tube(mesh, [(cx, cy, bottom), (cx + radius * .02, cy, bottom - height * .30),
                (cx, cy + radius * .02, bottom - height * .64)],
         (radius * .06, radius * .035, radius * .01), bark_slot, segments=4)
    layers = ((.27, .43, .98, .50, 4), (.43, .59, 1.06, .50, 4),
              (.60, .51, 1., .48, 2), (.77, .24, .80, .46, 2))
    for layer, (h, reach, width, tall, count) in enumerate(layers):
        for index in range(count):
            base_angle = math.tau * index / count + layer * .57
            angle = base_angle + .12 * math.sin(2 * base_angle + layer)
            center = (cx + radius * reach * math.cos(angle), cy + radius * reach * math.sin(angle),
                      bottom - height * (h + .05 * math.sin(angle * 2 + layer)))
            for side in (-1, 1):
                leaf_spray(mesh, center, angle + side * .52, radius * width, height * tall,
                           foliage_slot, .25 - layer * .2 + side * .12, divisions=2)
    # Two broad central bunches bridge the ring of outer sprays. Opposing
    # bowed faces retain rear coverage without fine grids or hidden branches.
    for h, tall, angle in ((.50, .56, .35), (.68, .50, .35 + math.pi * .5)):
        for side in (0., math.pi):
            leaf_spray(mesh, (cx, cy, bottom - height * h), angle + side,
                       radius * 1.45, height * tall, foliage_slot, 0., divisions=2)
    horizontal = [max(abs(v[a] - (cx, cy)[a]) for v in mesh.vertices) for a in (0, 1)]
    factors = [(bounds[a][1] - (cx, cy)[a]) / horizontal[a] for a in (0, 1)]
    vertical = height / (bottom - min(v[2] for v in mesh.vertices))
    mesh.vertices = [(cx + (x - cx) * factors[0], cy + (y - cy) * factors[1],
                      min(bottom, bottom + (z - bottom) * vertical), u, v,
                      *unit((nx / factors[0], ny / factors[1], nz / vertical)))
                     for x, y, z, u, v, nx, ny, nz in mesh.vertices]
    return mesh


def tree7_envelope(original):
    """LHRTS7's alpha silhouette, lifted through the original static UV charts.

    The 64x64 image occupies x19..41 and y15..63. Including the source UV
    seams and 0.02..0.99 inset gives a 10.936m visible height and about 2.75m
    maximum width inside the padded 6.511x14.038m source mesh. The narrow stem
    occupies only the bottom four rows. These fractions retain that source
    silhouette while allowing the original local bounds and placement to vary.
    """
    extent = [(min(v[a] for v in original.vertices), max(v[a] for v in original.vertices)) for a in range(3)]
    half_x, half_y = [(high - low) * .5 for low, high in extent[:2]]
    foot = ((extent[0][0] + extent[0][1]) * .5 - half_x * .0176,
            (extent[1][0] + extent[1][1]) * .5 - half_y * .0156, extent[2][1])
    return foot, min(half_x, half_y) * .43, (extent[2][1] - extent[2][0]) * .779


def tree2b_radius(h):
    """LHRTS2's tall, dense oval crown, measured from its original alpha rows.

    Unlike LHRTS7, this image fills its source bounds. Its broad lower/middle
    crown reaches nearly the full width, and the upper half narrows gradually.
    The bottom seven rows contain the short stem and rounded crown attachment.
    """
    profile = ((0., .13), (.05, .13), (.11, .26), (.18, .70), (.24, .98),
               (.37, 1.), (.56, 1.), (.69, .97), (.76, .91), (.82, .89),
               (.89, .76), (.95, .71), (.98, .40), (1., .03))
    for (h0, r0), (h1, r1) in zip(profile, profile[1:]):
        if h <= h1:
            return r0 + (r1 - r0) * (h - h0) / (h1 - h0)
    return profile[-1][1]


def tree2b_canopy_surfaces(mesh, bounds, texture):
    """Wrap dense inner crown imagery around a rounded, profiled tree volume."""
    cx, cy = [(bounds[a][0] + bounds[a][1]) * .5 for a in (0, 1)]
    rx, ry = [(bounds[a][1] - bounds[a][0]) * .5 for a in (0, 1)]
    top, bottom = bounds[2]
    height = bottom - top

    def point(u, h, side):
        theta = (u - .5) * math.pi
        # Only the original short stem is omitted. A tapered lower connection
        # joins the opaque bark rather than creating another billboard stem.
        profile = tree2b_radius(h)
        phi = math.atan2(side * math.cos(theta), math.sin(theta))
        radial = .83
        depth = profile * min(1., max(0., (h - .10) / .10))
        return (cx + rx * math.sin(theta) * profile * radial, cy + side * ry * math.cos(theta) * depth * radial,
                bottom - height * h)

    for side in (-1, 1):
        rings = []
        for h in (.10, .24, .56, .82, .95, 1.):
            ring = []
            for column in range(7):
                u = column / 6
                p = point(u, h, side)
                tangent_u = sub(point(min(1., u + .001), h, side), point(max(0., u - .001), h, side))
                tangent_h = sub(point(u, min(1., h + .001), side), point(u, max(.10, h - .001), side))
                normal = scale(unit(cross(tangent_u, tangent_h)), side)
                # Spread dense motif columns evenly over the curved half-ring.
                # A local-X projection collapses U at its poles and stretches
                # isolated image columns into horizontal strips.
                uv = (.5 + .70 * tree2b_radius(h) * (u - .5), .99 - .97 * h - 1)
                ring.append(mesh.vertex(p, normal, uv))
            rings.append(ring)
        for lower, upper in zip(rings, rings[1:]):
            for index in range(6):
                mesh.triangle((lower[index], lower[index + 1], upper[index + 1]), (176, 176, 176, 255), texture)
                mesh.triangle((lower[index], upper[index + 1], upper[index]), (176, 176, 176, 255), texture)


def fitted_leaf_spray(mesh, center, angle, width, texture, tilt, roll, bounds, bare_fraction=.08, divisions=3):
    """Fit one rolled whole sprite without moving its branch attachment inward."""
    start = len(mesh.vertices)
    leaf_spray(mesh, center, angle, width, width, texture, tilt, roll, divisions)
    limits = list(bounds)
    height = bounds[2][1] - bounds[2][0]
    limits[2] = (bounds[2][0], bounds[2][1] - bare_fraction * height)
    factor = 1.
    for vertex in mesh.vertices[start:]:
        for axis, c in enumerate(center):
            delta = vertex[axis] - c
            if delta:
                factor = min(factor, (limits[axis][delta > 0] - c) / delta)
    for index in range(start, len(mesh.vertices)):
        vertex = mesh.vertices[index]
        point = add(center, scale(sub(vertex[:3], center), factor))
        mesh.vertices[index] = (*point, *vertex[3:])


def make_tree2b(original, mesh_type, source_slot, foliage_slot, bark_slot):
    """A tall oval crown with staggered outer lobes and a connected leafy core."""
    mesh = mesh_type(original.header, [], [])
    bounds = [(min(v[a] for v in original.vertices), max(v[a] for v in original.vertices)) for a in range(3)]
    cx, cy = [(bounds[a][0] + bounds[a][1]) * .5 for a in (0, 1)]
    bottom, top = bounds[2][1], bounds[2][0]
    height = bottom - top
    radius = min(bounds[0][1] - cx, bounds[1][1] - cy)

    def stem(h):
        return (cx + radius * .025 * math.sin(h * 4.7),
                cy + radius * .019 * math.sin(h * 7.1), bottom - h * height)

    tube(mesh, [stem(h) for h in (0., .30, .80)],
         [radius * r for r in (.075, .045, .008)], bark_slot, 4)
    outer_count = 10
    clusters = []
    for index in range(outer_count):
        t = index / (outer_count - 1)
        # Spread overlapping lobes over the entire body, rather than spending
        # most of the foliage on a crowded, horn-like top.
        h = .20 + .72 * t + .018 * math.sin(index * 2.11) * (1 - t)
        angle = index * 2.399963 + .33 * math.sin(index * 1.73)
        reach = radius * tree2b_radius(h) * (.78 + .025 * math.sin(index * 2.13))
        width = radius * (1.30 + .08 * math.cos(index * 1.37))
        center = add(stem(h), (reach * math.cos(angle), reach * math.sin(angle), 0.))
        roll = (.70 + .45 * math.sin(index * 1.7)) if h < .80 else (1.35 + .12 * math.sin(index * 1.7))
        clusters.append((h, center, angle, width, roll))
    for h, center, angle, width, roll in clusters:
        for side in range(2):
            card_angle = angle + (side * 2 - 1) * .28
            tangent = (-math.sin(card_angle), math.cos(card_angle), 0.)
            shift = side - .5
            offset = add(scale(tangent, width * .035 * shift), (0., 0., width * .025 * shift))
            fitted_leaf_spray(mesh, add(center, offset), card_angle, width, foliage_slot,
                                .10 - .18 * h + .09 * shift, roll + .12 * shift, bounds, divisions=2)
    # The tilted first tube ring must still sit on the exact source foot plane.
    mesh.vertices = [(*vertex[:2], min(bottom, vertex[2]), *vertex[3:]) for vertex in mesh.vertices]
    tree2b_canopy_surfaces(mesh, bounds, source_slot)
    return mesh


def tree1_envelope(original):
    """Lift Croft LHRTS1's padded alpha chart into its actual local envelope.

    The original 64-square image occupies x16..46 and y6..63. Clipping its
    opaque texels through the original inset UV triangles gives the fractions
    below: a 3.39 m wide, 12.97 m tall tree inside its 6.51 by 14.04 m carrier.
    """
    source = [(min(v[a] for v in original.vertices), max(v[a] for v in original.vertices)) for a in range(3)]
    foot = ((source[0][0] + source[0][1]) * .5 - (source[0][1] - source[0][0]) * .5 * .01764487763894284,
            (source[1][0] + source[1][1]) * .5 - (source[1][1] - source[1][0]) * .5 * .015583107898948036,
            source[2][1])
    fractions = ((.23751004235232487, .7557574843909737), (.23572623706731355, .7562833355050848))
    extent = [tuple(source[a][0] + f * (source[a][1] - source[a][0]) for f in fractions[a]) for a in (0, 1)]
    extent.append((foot[2] - (source[2][1] - source[2][0]) * .9239690721649485, foot[2]))
    return foot, extent


def tree1_radius(h):
    """Stepped LHRTS1 crown, with its widest upper-middle lobe and long stem."""
    profile = ((0., .06), (.19, .185), (.26, .186), (.33, .75), (.40, .813),
               (.434, .923), (.451, .75), (.47, .75), (.54, .75), (.61, .875), (.68, 1.), (.75, .625),
               (.82, .625), (.89, .491), (.96, .245), (1., .185))
    for (h0, r0), (h1, r1) in zip(profile, profile[1:]):
        if h <= h1:
            return r0 + (r1 - r0) * (h - h0) / (h1 - h0)
    return profile[-1][1]


def make_tree1(original, mesh_type, foliage_slot, bark_slot):
    """A long free stem supports a narrow, irregularly stepped branch crown."""
    mesh = mesh_type(original.header, [], [])
    foot, extent = tree1_envelope(original)
    radius = min(high - low for low, high in extent[:2]) * .5
    height = extent[2][1] - extent[2][0]

    def stem(h):
        return (foot[0] + radius * .045 * math.sin(h * 2.7) * h,
                foot[1] + radius * .025 * math.sin(h * 4.1) * h, foot[2] - h * height)

    tube(mesh, [stem(h) for h in (0., .30, .60, .87)],
         [radius * r for r in (.071, .055, .025, .006)], bark_slot, 4)
    # Narrow bare forks reproduce the source's upper-stem side strokes without
    # filling the long free trunk with new foliage.
    # Its 31-pixel opaque width spans 3.39 m: about 0.109 m per source pixel.
    # A middle radius of .032 * 1.687 m gives a matching 0.108 m diameter.
    for angle in (.35, 2.1, .35 + math.pi, 2.1 + math.pi):
        middle = add(stem(.245), (radius * .10 * math.cos(angle), radius * .10 * math.sin(angle), 0.))
        tip = add(stem(.315), (radius * .18 * math.cos(angle), radius * .18 * math.sin(angle), 0.))
        tube(mesh, [stem(.185), middle, tip], [radius * r for r in (.040, .032, .008)], bark_slot, 4)
    clusters = 8
    for index in range(clusters):
        t = index / (clusters - 1)
        h = .33 + .65 * (1 - (1 - t) ** 1.5)
        h += .010 * math.sin(index * 1.93) * (1 - t)
        profile = tree1_radius(h)
        # The lower-middle source crown has a substantial second lobe. Widen
        # and overlap that branch mass, fading before the already broad upper
        # middle rather than inflating the complete tree.
        lower_fill = max(0., min(1., (h - .28) / .06, (.68 - h) / .10))
        angle = index * 2.399963 + .30 * math.sin(index * 1.71)
        reach = radius * profile * (.48 + .12 * lower_fill + .075 * math.sin(index * 2.1))
        center = add(stem(h), (reach * math.cos(angle), reach * math.sin(angle), 0.))
        width = radius * profile * (1.22 + .08 * math.cos(index * 1.37)) * (1 + .32 * lower_fill)
        top_fill = max(0., min(1., (h - .90) / .08))
        width *= 1 + .10 * top_fill
        for side in range(2):
            card_angle = angle + side * math.pi
            tangent = (-math.sin(card_angle), math.cos(card_angle), 0.)
            shift = side - .5
            offset = add(scale(tangent, width * .025 * shift), (0., 0., width * .018 * shift))
            roll = .24 * math.sin(index * 1.4 + side) + .13 * shift + .45 * top_fill
            fitted_leaf_spray(mesh, add(center, offset), card_angle, width, foliage_slot,
                              .12 - .24 * h, roll, extent, bare_fraction=.27, divisions=2)
    # Canonical LHRTS1 foliage reaches row47; row48 onward is the bare stem.
    # Lift that edge through the original .02/.99 UV clamp and row6 crown top.
    lower_crown_floor = (.99 - 48 / 64) / (.99 - 6 / 64)
    for index, h in enumerate((.36, .434, .56, .68, .78, .88)):
        width = radius * tree1_radius(h) * 3.4
        # These four core cards are upright, so alpha edge447/512 maps linearly
        # to height. Fit that visible edge, allowing the transparent bottom
        # margin below it; fitting the whole square to.27 shortened the lobe.
        bare_fraction = h - (h - lower_crown_floor) / (2 * (447 / 512) - 1) if index == 0 else .27
        for side in (0., math.pi * .5, math.pi, math.pi * 1.5):
            angle = 0. if index == 1 else math.pi * .5 if index == 3 else .37 + index * 1.09
            fitted_leaf_spray(mesh, stem(h), angle + side, width, foliage_slot,
                              0., 0., extent, bare_fraction=bare_fraction, divisions=2)
    mesh.vertices = [(*vertex[:2], min(foot[2], vertex[2]), *vertex[3:]) for vertex in mesh.vertices]
    return mesh


def tree7_radius(h):
    """Lower rounded mass and a long irregular taper, measured from LHRTS7."""
    profile = ((0., .07), (.06, .10), (.12, .55), (.20, .84), (.30, 1.),
               (.42, .93), (.55, .89), (.67, .73), (.80, .56), (.90, .48), (.96, .25), (1., .035))
    for (h0, r0), (h1, r1) in zip(profile, profile[1:]):
        if h <= h1:
            return r0 + (r1 - r0) * (h - h0) / (h1 - h0)
    return profile[-1][1]


def make_tree7(original, mesh_type, foliage_slot, bark_slot):
    """A slender, ascending branch system with its own spaced foliage lobes."""
    mesh = mesh_type(original.header, [], [])
    foot, radius, height = tree7_envelope(original)

    def spine(h):
        return (foot[0] + radius * (.020 * math.sin(h * 5) + .015 * h),
                foot[1] - radius * .018 * math.sin(h * 7), foot[2] - h * height)

    heights = (0., .18, .52, .90)
    tube(mesh, [spine(h) for h in heights],
         [radius * r for r in (.075, .058, .031, .008)], bark_slot, 4)
    clusters = 14
    for index in range(clusters):
        t = index / (clusters - 1)
        h = .10 + .87 * (1 - (1 - t) ** 1.7) + .008 * math.sin(index * 2.1) * math.sin(math.pi * t)
        # Successive branch ends rise through different azimuths. There are no
        # repeated height rings. Small upper sprays need closer vertical and
        # radial spacing to join the pointed crown instead of floating above it.
        angle = index * math.pi * (3 - math.sqrt(5)) + .39 * math.sin(index * 1.7) + .24 * math.cos(index * 2.7)
        reach = radius * tree7_radius(h)
        radial = .32 - .22 * max(0., (h - .58) / .42) + .025 * math.sin(index * 1.3)
        center = add(spine(h), (reach * radial * math.cos(angle), reach * radial * math.sin(angle), 0.))
        width = reach * (1.78 + .055 * math.sin(index * 2.6)) * (1.15 if h < .55 else 1.)
        # Both Leaf7 canvases are square; their alpha silhouettes are already
        # elongated. Square cards preserve the actual leaf pixel proportions.
        card_height = width
        for side in range(2):
            tangent = (-math.sin(angle), math.cos(angle), 0.)
            offset = add(scale(tangent, width * (-.045 if side == 0 else .045)),
                         (0., 0., card_height * (-.018 if side == 0 else .018)))
            leaf_spray(mesh, add(center, offset), angle + (-.54 if side == 0 else .54),
                       width, card_height, foliage_slot, .18 - .40 * t + (.10 if side else -.10), divisions=2)
    # Broad central sprays overlap the lower crown's ascending outer branches.
    # Their near-axis placement fills the interior from oblique views without
    # widening the measured silhouette or changing the connected upper crown.
    inner_count = 3
    inner_heights = (.22, .37, .51)
    for index, h in enumerate(inner_heights):
        angle = .37 + index * 1.09
        reach = radius * tree7_radius(h)
        center = add(spine(h), (radius * .045 * math.cos(angle), radius * .045 * math.sin(angle), 0.))
        width = reach * 1.85
        for side in range(2):
            tangent = (-math.sin(angle), math.cos(angle), 0.)
            offset = scale(tangent, width * (-.025 if side == 0 else .025))
            leaf_spray(mesh, add(center, offset), angle + (-.78 if side == 0 else .78),
                       width, width, foliage_slot, -.02 if side == 0 else .06, divisions=2)
    # The source top quarter is transparent padding. Only the measured visible
    # height anchors this crown; its raw mesh bounds must never inflate it.
    z_scale = height / (foot[2] - min(v[2] for v in mesh.vertices))
    mesh.vertices = [(x, y, min(foot[2], foot[2] + (z - foot[2]) * z_scale), u, v,
                      *unit((nx, ny, nz / z_scale))) for x, y, z, u, v, nx, ny, nz in mesh.vertices]
    return mesh, inner_count


def grove_frame(original):
    """Retain LHRTWS3's two original UV halves and slightly sloped X/Y plane."""
    columns = {}
    for vertex in original.vertices:
        columns[vertex[3]] = vertex[:2]
    columns = sorted(columns.items())
    bottom = max(v[2] for v in original.vertices)
    height = bottom - min(v[2] for v in original.vertices)

    def xy(u):
        for (u0, p0), (u1, p1) in zip(columns, columns[1:]):
            if u <= u1:
                t = (u - u0) / (u1 - u0)
                return tuple(a + (b - a) * t for a, b in zip(p0, p1))
        return columns[-1][1]

    def material_uv(u, h):
        # Reproduce interpolation of the source static loader's inset UVs.
        # Insetting only new vertices would move the original narrow stem gaps.
        for (u0, _), (u1, _) in zip(columns, columns[1:]):
            if u <= u1:
                a, b = min(.99, max(.02, u0)), min(.99, max(.02, u1))
                return (a + (b - a) * (u - u0) / (u1 - u0), -.01 - .97 * h)
        return (.99, -.01 - .97 * h)

    return xy, material_uv, bottom, height, columns


def grove_canopy_surfaces(mesh, xy, material_uv, bottom, height, columns, texture):
    """Three height rows over five crown peaks; retain the whole source motif."""
    knots = ((0., 0.), (.13, 1.1), (.22, .5), (.31, 1.65), (.44, .6),
             (.57, 1.8), (.68, .6), (.78, 1.4), (.85, .6), (.91, 1.05), (1., 0.))
    divisions = sorted({u for u, _ in knots} | {u for u, _ in columns})
    def point(u, h, side):
        for (a, da), (b, db) in zip(knots, knots[1:]):
            if u <= b:
                depth = da + (db - da) * (u - a) / (b - a)
                break
        x, y = xy(u)
        return (x, y + side * depth * (1 - abs(2 * h - 1)), bottom - height * h)
    for side in (-1, 1):
        rings = []
        for h in (0., .5, 1.):
            ring = []
            for u in divisions:
                tangent_u = sub(point(min(1., u + .001), h, side), point(max(0., u - .001), h, side))
                tangent_h = sub(point(u, min(1., h + .001), side), point(u, max(0., h - .001), side))
                normal = scale(unit(cross(tangent_u, tangent_h)), side)
                ring.append(mesh.vertex(point(u, h, side), normal, material_uv(u, h)))
            rings.append(ring)
        for lower, upper in zip(rings, rings[1:]):
            for index in range(len(divisions) - 1):
                mesh.triangle((lower[index], lower[index + 1], upper[index + 1]), WHITE, texture)
                mesh.triangle((lower[index], upper[index + 1], upper[index]), WHITE, texture)


def grove_root_parameters(columns):
    """Place complete bark feet inside opaque source runs after the UV inset."""
    roots = []
    for pixel in (14.5, 44.5, 77.5, 97.5, 114.5):
        native_u = pixel / 128
        for (u0, _), (u1, _) in zip(columns, columns[1:]):
            a, b = min(.99, max(.02, u0)), min(.99, max(.02, u1))
            if native_u <= b:
                roots.append(u0 + (u1 - u0) * (native_u - a) / (b - a))
                break
    return roots


def make_grove(original, mesh_type, source_slot, foliage_slot, bark_slot):
    """Five source-fitted feet, coarse crown volume and ten curved edge sprays."""
    mesh = mesh_type(original.header, [], [])
    xy, material_uv, bottom, height, columns = grove_frame(original)
    grove_canopy_surfaces(mesh, xy, material_uv, bottom, height, columns, source_slot)
    lobes = ((.13, .87, .23), (.31, 1., .31), (.57, .98, .32), (.78, 1., .22), (.91, .94, .16))
    roots = grove_root_parameters(columns)
    source_min, source_max = columns[0][1][0], columns[-1][1][0]
    width = source_max - source_min
    for lobe, ((u, top, breadth), root_u) in enumerate(zip(lobes, roots)):
        root_x, root_y = xy(root_u)
        crown_x, crown_y = xy(u)
        tube(mesh, [(root_x, root_y, bottom),
                    ((root_x + crown_x) * .5, (root_y + crown_y) * .5, bottom - height * top * .35),
                    (crown_x, crown_y, bottom - height * top * .7)],
             (.14, .075, .022), bark_slot, segments=4)
        card_width = min(width * breadth * .7, (crown_x - source_min) / .64, (source_max - crown_x) / .64)
        for side in (-1, 1):
            leaf_spray(mesh, (crown_x, crown_y + side * .8, bottom - height * top * .58),
                       side * math.pi * .5 + .2 * math.sin(lobe), card_width, card_width,
                       foliage_slot, -.03, divisions=2)
    slope = (columns[-1][1][1] - columns[0][1][1]) / width
    def plane_y(x):
        return columns[0][1][1] + slope * (x - source_min)
    depth = max(abs(v[1] - plane_y(v[0])) for v in mesh.vertices)
    depth_scale = 1.8 / depth
    mesh.vertices = [(x, plane_y(x) + (y - plane_y(x)) * depth_scale, min(bottom, z), u, v,
                      *unit((nx - (1 - depth_scale) * slope * ny / depth_scale, ny / depth_scale, nz)))
                     for x, y, z, u, v, nx, ny, nz in mesh.vertices]
    return mesh


def grove4_depth(u, h):
    """LHRTWS4: two broad crowns joined by a lower, dense central bridge."""
    masses = ((.23, .98, .30), (.51, .86, .22), (.79, 1., .30))
    volume = sum(math.exp(-((u - center) / breadth) ** 2) *
                 max(0., math.sin(math.pi * min(1., h / top))) ** .55
                 for center, top, breadth in masses)
    # A broad edge bulge moves the original silhouette sideways in oblique
    # views. Let the three-dimensional body return more gently to its original
    # perimeter plane, while keeping the complete motif and its central depth.
    border = max(0., math.sin(math.pi * u) * math.sin(math.pi * h)) ** .45
    return 1.65 * (1 - math.exp(-1.6 * volume)) * border


def make_grove4(original, mesh_type, source_slot, foliage_slot, bark_slot):
    """A closed ground-reaching group, with its own three connected masses.

    The complete source motif controls the outer silhouette and small openings.
    Its original inset interpolation is retained on both curved sides; whole
    leaf sprites break up the surface without inventing isolated tree trunks.
    """
    counts = (2, 2, 2)
    mesh = mesh_type(original.header, [], [])
    xy, material_uv, bottom, height, columns = grove_frame(original)
    divisions = sorted({0., .23, .51, .79, 1.} | {u for u, _ in columns})
    width = columns[-1][1][0] - columns[0][1][0]

    def point(u, h, side):
        x, y = xy(u)
        return (x, y + side * grove4_depth(u, h), bottom - height * h)

    for side in (-1, 1):
        rings = []
        for h in (0., .5, 1.):
            ring = []
            for u in divisions:
                tangent_u = sub(point(min(1., u + .001), h, side), point(max(0., u - .001), h, side))
                tangent_h = sub(point(u, min(1., h + .001), side), point(u, max(0., h - .001), side))
                normal = scale(unit(cross(tangent_u, tangent_h)), side)
                ring.append(mesh.vertex(point(u, h, side), normal, material_uv(u, h)))
            rings.append(ring)
        for lower, upper in zip(rings, rings[1:]):
            for index in range(len(divisions) - 1):
                a, b, c, d = lower[index], lower[index + 1], upper[index + 1], upper[index]
                mesh.triangle((a, b, c), WHITE, source_slot)
                mesh.triangle((a, c, d), WHITE, source_slot)

    left, right = columns[0][1][0], columns[-1][1][0]
    mid_y = (columns[0][1][1] + columns[-1][1][1]) * .5
    envelope = ((left, right), (mid_y - 1.8, mid_y + 1.8), (bottom - height, bottom))
    masses = ((.23, .98, .45), (.51, .86, .26), (.79, 1., .43))
    for lobe, ((u, top, breadth), count) in enumerate(zip(masses, counts)):
        root_x, root_y = xy(u)

        def stem(h):
            return (root_x + .10 * math.sin(h * 5 + lobe), root_y, bottom - height * h)

        # The original has dense foliage at ground level. These structural
        # stems begin inside that mass; no new row of exposed feet is inferred.
        tube(mesh, [stem(h * top) for h in (.08, .40, .77)], (.14, .075, .018), bark_slot, 4)
        for index in range(count):
            t = index / (count - 1)
            h = top * (.15 + .70 * t) + .022 * math.sin(index * 2.1 + lobe)
            crown_u = u + breadth * .22 * math.sin(index * 2.399963 + lobe)
            x, y = xy(crown_u)
            card_width = width * breadth * (.37 + .025 * math.cos(index * 1.7 + lobe))
            center = (x, y, bottom - height * h)
            for side in (-1, 1):
                angle = side * math.pi * .5 + .30 * math.sin(index * 1.7 + lobe)
                surface = (x, y + side * grove4_depth(crown_u, h) * .87, center[2])
                fitted_leaf_spray(mesh, surface, angle, card_width, foliage_slot,
                                  .10 - .18 * t, .22 * math.sin(index * 1.3 + lobe),
                                  envelope, bare_fraction=0., divisions=2)
    return mesh


def grove_lm4_depth(u, h):
    """LMRTS4's two broad side crowns and smaller connecting middle mass."""
    masses = ((.21, .96, .29), (.49, .88, .19), (.74, 1., .29))
    volume = sum(math.exp(-((u - center) / breadth) ** 2) *
                 max(0., math.sin(math.pi * min(1., h / top))) ** .55
                 for center, top, breadth in masses)
    # The reversed LMRTS4 chart needs additional lateral reserve in grazing
    # views. Fade only the side bulge faster; the validated central volume and
    # its vertical profile remain unchanged at every height.
    border = max(0., math.sin(math.pi * u)) ** 1.45 * max(0., math.sin(math.pi * h)) ** .85
    # The bottom three source rows contain separate narrow feet. Keeping their
    # original plane prevents a curved mask from filling the gaps obliquely.
    t = min(1., max(0., (h - .085) / .16))
    return 1.65 * (1 - math.exp(-1.6 * volume)) * border * t * t * (3 - 2 * t)


def grove_lm4_roots(columns):
    """Invert the original inset at the centers of nine two-pixel bark runs."""
    roots = []
    for pixel in (14., 20., 26., 29., 34., 41., 46., 51., 55.):
        native_u = pixel / 64
        for (u0, _), (u1, _) in zip(columns, columns[1:]):
            a, b = min(.99, max(.02, u0)), min(.99, max(.02, u1))
            if native_u <= b:
                roots.append(u0 + (u1 - u0) * (native_u - a) / (b - a))
                break
    return roots


def grove_lm4_foot_radius(columns, root):
    """97% of one mapped source texel is half a two-texel foot's width."""
    for (u0, p0), (u1, p1) in zip(columns, columns[1:]):
        if root <= u1:
            a, b = min(.99, max(.02, u0)), min(.99, max(.02, u1))
            return .97 * abs(p1[0] - p0[0]) / (b - a) / 64


def make_grove_lm4(original, mesh_type, source_slot, foliage_slot, bark_slot):
    """A mirrored, already two-sided source group with nine real short feet."""
    counts = (2, 2, 2)
    mesh = mesh_type(original.header, [], [])
    xy, material_uv, bottom, height, columns = grove_frame(original)
    divisions = sorted({0., .21, .49, .74, 1.} | {u for u, _ in columns})
    # U increases from right to left, opposite to the other accepted groves.
    direction = 1 if columns[-1][1][0] > columns[0][1][0] else -1
    left, right = sorted((columns[0][1][0], columns[-1][1][0]))
    width = right - left

    def point(u, h, side):
        x, y = xy(u)
        return (x, y + side * grove_lm4_depth(u, h), bottom - height * h)

    for side in (-1, 1):
        rings = []
        for row in range(3):
            # Original rows 29..31 are pure painted feet. Real bark supplies
            # those strokes, so they must not be drawn again on the canopy.
            h = (.99 - 29 / 32) / .97 if row == 0 else row / 2
            ring = []
            for u in divisions:
                tangent_u = sub(point(min(1., u + .001), h, side), point(max(0., u - .001), h, side))
                tangent_h = sub(point(u, min(1., h + .001), side), point(u, max(0., h - .001), side))
                normal = scale(unit(cross(tangent_u, tangent_h)), side * direction)
                ring.append(mesh.vertex(point(u, h, side), normal, material_uv(u, h)))
            rings.append(ring)
        for lower, upper in zip(rings, rings[1:]):
            for index in range(len(divisions) - 1):
                a, b, c, d = lower[index], lower[index + 1], upper[index + 1], upper[index]
                mesh.triangle((a, b, c), WHITE, source_slot)
                mesh.triangle((a, c, d), WHITE, source_slot)

    roots = grove_lm4_roots(columns)
    masses = ((.21, .96, .45), (.49, .88, .28), (.74, 1., .43))
    stems = []
    for root in roots:
        x, y = xy(root)
        crown_u = min((m[0] for m in masses), key=lambda u: abs(u - root))
        crown_x, crown_y = xy(crown_u)
        points = [(x + (crown_x - x) * lean, y + (crown_y - y) * lean, bottom - height * h)
                  for h, lean in ((0., 0.), (.07, 0.), (.20, .15))]
        # The original feet span two texels, about .68-.72 m after the inset.
        # Stay just inside that actual span, rather than halving its width by
        # requiring the outer filter neighbor to be completely opaque too.
        radius = grove_lm4_foot_radius(columns, root)
        tube(mesh, points, (radius, radius * .90, radius * .42), bark_slot, 4, phase=math.pi / 2)
        stems.append(points)

    mid_y = (min(p[1] for _, p in columns) + max(p[1] for _, p in columns)) * .5
    envelope = ((left, right), (mid_y - 1.8, mid_y + 1.8),
                (bottom - height * .9884020618556705, bottom))
    for lobe, ((u, top, breadth), count) in enumerate(zip(masses, counts)):
        for index in range(count):
            t = index / (count - 1)
            h = top * (.19 + .64 * t) + .020 * math.sin(index * 2.1 + lobe)
            crown_u = u + breadth * .20 * math.sin(index * 2.399963 + lobe)
            x, y = xy(crown_u)
            card_width = width * breadth * (.38 + .025 * math.cos(index * 1.7 + lobe))
            center = (x, y, bottom - height * h)
            root_index = min(range(len(roots)), key=lambda i: abs(roots[i] - crown_u))
            for side in (-1, 1):
                angle = side * math.pi * .5 + .30 * math.sin(index * 1.7 + lobe)
                surface = (x, y + side * grove_lm4_depth(crown_u, h) * .87, center[2])
                fitted_leaf_spray(mesh, surface, angle, card_width, foliage_slot,
                                  .10 - .18 * t, .22 * math.sin(index * 1.3 + lobe),
                                  envelope, bare_fraction=.10, divisions=2)
    return mesh


def grove_source_parameter(columns, native_u):
    """Invert the original static inset without confusing image U and X."""
    for (u0, _), (u1, _) in zip(columns, columns[1:]):
        a, b = min(.99, max(.02, u0)), min(.99, max(.02, u1))
        if native_u <= b:
            return u0 + (u1 - u0) * (native_u - a) / (b - a)
    return columns[-1][0]


def grove_lm2_depth(t, h, island):
    """Separate left double crown and lower right crown, never a gap bridge."""
    masses = (((.28, .93, .36), (.71, 1., .36)), ((.55, .956, .47),))[island]
    volume = sum(math.exp(-((t - center) / breadth) ** 2) *
                 max(0., math.sin(math.pi * min(1., h / top))) ** .55
                 for center, top, breadth in masses)
    border = max(0., math.sin(math.pi * t)) ** 1.45 * max(0., math.sin(math.pi * h)) ** .85
    fade = min(1., max(0., (h - .10) / .16))
    return 1.8 * (1 - math.exp(-1.8 * volume)) * border * fade * fade * (3 - 2 * fade)


def grove_lm2_islands(columns):
    # Source columns33..36 are empty throughout all32 image rows. Bounds are
    # mapped through the original UV inset, not approximated as linear X cuts.
    return ((columns[0][0], grove_source_parameter(columns, 33 / 64)),
            (grove_source_parameter(columns, 37 / 64), columns[-1][0]))


def make_grove_lm2(original, mesh_type, source_slot, foliage_slot, bark_slot):
    """Three grounded stems support two rigorously separate foliage islands."""
    counts = (2, 2, 2)
    mesh = mesh_type(original.header, [], [])
    xy, material_uv, bottom, height, columns = grove_frame(original)
    islands = grove_lm2_islands(columns)
    cut = (.99 - 29 / 32) / .97

    def point(u, h, side, island):
        a, b = islands[island]
        x, y = xy(u)
        return (x, y + side * grove_lm2_depth((u - a) / (b - a), h, island), bottom - height * h)

    for island, (start, end) in enumerate(islands):
        fractions = (0., .28, .5, .71, 1.) if island == 0 else (0., .55, 1.)
        divisions = sorted({start + (end - start) * t for t in fractions} |
                           {u for u, _ in columns if start < u < end})
        for side in (-1, 1):
            rings = []
            for row in range(3):
                h = cut if row == 0 else row / 2
                ring = []
                for u in divisions:
                    tangent_u = sub(point(min(end, u + .001), h, side, island),
                                    point(max(start, u - .001), h, side, island))
                    tangent_h = sub(point(u, min(1., h + .001), side, island),
                                    point(u, max(cut, h - .001), side, island))
                    normal = scale(unit(cross(tangent_u, tangent_h)), side)
                    ring.append(mesh.vertex(point(u, h, side, island), normal, material_uv(u, h)))
                rings.append(ring)
            for lower, upper in zip(rings, rings[1:]):
                for i in range(len(divisions) - 1):
                    mesh.triangle((lower[i], lower[i + 1], upper[i + 1]), WHITE, source_slot)
                    mesh.triangle((lower[i], upper[i + 1], upper[i]), WHITE, source_slot)

    stems = []
    masses = ((11 / 64, .925, .25, 0), (24 / 64, 1., .24, 0), (51 / 64, .956, .36, 1))
    width = xy(1.)[0] - xy(0.)[0]
    for index, (pixel, upper_pixel) in enumerate(((11., 10.5), (24., 23.5), (51., 51.))):
        root = grove_source_parameter(columns, pixel / 64)
        upper = grove_source_parameter(columns, upper_pixel / 64)
        root_x, root_y = xy(root)
        upper_x, upper_y = xy(upper)
        pixel_radius = abs(xy(grove_source_parameter(columns, (pixel + 1) / 64))[0] - root_x) * .97
        flare = 1.5 if index < 2 else 1.
        # Two-texel feet widen/lean into the three-texel row29 strokes on the
        # left pair. This joins the original low branch arches without keeping
        # a second set of painted feet on the newly curved foliage surfaces.
        points = [(root_x, root_y, bottom), (root_x, root_y, bottom - height * .045),
                  (upper_x, upper_y, bottom - height * .09),
                  (upper_x, upper_y, bottom - height * .22)]
        tube(mesh, points, (pixel_radius, pixel_radius, pixel_radius * flare, pixel_radius * .42),
             bark_slot, 4, phase=math.pi / 2)
        stems.append(points)

    mid_y = (min(p[1] for _, p in columns) + max(p[1] for _, p in columns)) * .5
    for lobe, ((native_u, top, breadth, island), count) in enumerate(zip(masses, counts)):
        u = grove_source_parameter(columns, native_u)
        start, end = islands[island]
        x0, y0 = xy(start)
        x1, y1 = xy(end)
        envelope = ((x0, x1), (mid_y - 1.8, mid_y + 1.8), (bottom - height, bottom))
        for index in range(count):
            t = index / (count - 1)
            h = top * (.20 + .63 * t) + .018 * math.sin(index * 2.1 + lobe)
            crown_u = u + breadth * .16 * math.sin(index * 2.399963 + lobe)
            x, y = xy(crown_u)
            center = (x, y, bottom - height * h)
            card_width = width * breadth * (.50 + .025 * math.cos(index * 1.7 + lobe))
            depth = grove_lm2_depth((crown_u - start) / (end - start), h, island)
            for side in (-1, 1):
                angle = side * math.pi * .5 + .30 * math.sin(index * 1.7 + lobe)
                surface = (x, y + side * depth * .87, center[2])
                fitted_leaf_spray(mesh, surface, angle, card_width, foliage_slot,
                                  .10 - .18 * t, .22 * math.sin(index * 1.3 + lobe),
                                  envelope, bare_fraction=.12, divisions=2)
    return mesh


def enhance(original, mesh_type, texture_names, bark_index, triangle_budget=2048, foliage_index=None):
    """Replace each confirmed near-tree type within its additional-face budget."""
    name = original.header[:12].split(b"\0", 1)[0]
    names = [(n.decode("ascii") if isinstance(n, bytes) else n).split("\0", 1)[0] for n in texture_names]
    slots = {struct.unpack_from("<h", face, 8)[0] for face in original.faces}
    expected = {b"TRIS04_1": "LHRTS4", b"TRIS04B_1": "LHRTS4", b"TRIS02B_1": "LHRTS2", b"TRIE01_1": "LHRTS1",
                b"TRIS07_1": "LHRTS7", b"TRGE17_1": "LHRTWS3", b"TRGS18_1": "LHRTWS4",
                b"TRGS22_1": "LMRTS4", b"TRGS20_1": "LMRTS2"}.get(name)
    if (expected is None or foliage_index is None or not slots or
            not all(0 <= slot < len(names) and names[slot] == expected for slot in slots)):
        return original, {"added_triangles": 0}
    group_builders = {b"TRGS20_1": (make_grove_lm2, "grove_groups_lm2", 3),
                      b"TRGS22_1": (make_grove_lm4, "grove_groups_lm4", 9),
                      b"TRGS18_1": (make_grove4, "grove_groups4", 3)}
    if name in group_builders:
        make, label, trunks = group_builders[name]
        mesh = make(original, mesh_type, min(slots), foliage_index, bark_index)
        added = len(mesh.faces) - len(original.faces)
        if added <= triangle_budget:
            stats = dict(grove_groups=1, grove_lobes=3, trunks=trunks, branches=0, crown_clusters=6, leaf_cards=12,
                         source_mask_triangles=sum(struct.unpack_from("<h", f, 8)[0] == min(slots) for f in mesh.faces),
                         removed_tree_triangles=len(original.faces), added_triangles=added)
            stats[label] = 1
            if name == b"TRGS20_1":
                stats["grove_islands"] = 2
            return mesh, stats
        return original, {"added_triangles": 0}
    if name == b"TRGE17_1":
        mesh = make_grove(original, mesh_type, min(slots), foliage_index, bark_index)
        added = len(mesh.faces) - len(original.faces)
        if added <= triangle_budget:
            return mesh, dict(grove_groups=1, grove_lobes=5, trunks=5, branches=0, crown_clusters=5, leaf_cards=10,
                              source_mask_triangles=sum(struct.unpack_from("<h", f, 8)[0] == min(slots) for f in mesh.faces),
                              removed_tree_triangles=len(original.faces), added_triangles=added)
        return original, {"added_triangles": 0}
    if name == b"TRIS04_1":
        mesh = make_simple_tree(original, mesh_type, foliage_index, bark_index)
        added = len(mesh.faces) - len(original.faces)
        if added <= triangle_budget:
            return mesh, dict(near_trees=1, trunks=1, branches=0, crown_clusters=14, leaf_cards=28,
                              removed_tree_triangles=len(original.faces), added_triangles=added)
        return original, {"added_triangles": 0}
    if name == b"TRIE01_1":
        mesh = make_tree1(original, mesh_type, foliage_index, bark_index)
        stats = dict(near_trees=1, near_trees1=1, trunks=1, branches=4, crown_clusters=14, leaf_cards=40)
    elif name == b"TRIS02B_1":
        mesh = make_tree2b(original, mesh_type, min(slots), foliage_index, bark_index)
        stats = dict(near_trees=1, near_trees2b=1, trunks=1, branches=0, crown_clusters=10, interior_clusters=0, leaf_cards=20,
                     source_mask_triangles=120)
    elif name == b"TRIS07_1":
        mesh, inner_count = make_tree7(original, mesh_type, foliage_index, bark_index)
        stats = dict(near_trees=1, near_trees7=1, trunks=1, branches=0, crown_clusters=17, interior_clusters=inner_count, leaf_cards=34)
    elif name == b"TRIS04B_1":
        mesh = make_simple_tree(original, mesh_type, foliage_index, bark_index)
        stats = dict(near_trees=1, near_trees4b=1, trunks=1, branches=0, crown_clusters=14, leaf_cards=28)
    else:
        return original, {"added_triangles": 0}
    added = len(mesh.faces) - len(original.faces)
    if added <= triangle_budget:
        return mesh, dict(stats, removed_tree_triangles=len(original.faces), added_triangles=added)
    return original, {"added_triangles": 0}
