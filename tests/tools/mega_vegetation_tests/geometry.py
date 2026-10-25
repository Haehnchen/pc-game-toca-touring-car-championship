"""CM3D synthetic mesh layout and source-bound comparisons."""

from dataclasses import dataclass
import struct

from .environment import vegetation


@dataclass
class Mesh:
    header: bytes
    vertices: list
    faces: list

    def vertex(self, point, normal, uv):
        # Store the CM3D public vertex order used by authoring and exporters.
        self.vertices.append((*point, *uv, *vegetation.unit(normal)))
        return len(self.vertices) - 1

    def triangle(self, indices, color, texture):
        a, b, c = [self.vertices[i] for i in indices]
        n = vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))
        summed = vegetation.add(vegetation.add(a[5:], b[5:]), c[5:])
        if sum(x * y for x, y in zip(n, summed)) < 0:
            indices = (indices[0], indices[2], indices[1])
        self.faces.append(struct.pack("<H3Hh4B10x", 0, *indices, texture, *color))


def bounds(mesh):
    return [(min(v[a] for v in mesh.vertices), max(v[a] for v in mesh.vertices)) for a in range(3)]


def assert_contained_tall_tree(test, source, result):
    old, new = bounds(source), bounds(result)
    for axis in (0, 1):
        test.assertGreaterEqual(new[axis][0], old[axis][0] - 1e-8)
        test.assertLessEqual(new[axis][1], old[axis][1] + 1e-8)
        test.assertGreaterEqual(new[axis][1] - new[axis][0], .90 * (old[axis][1] - old[axis][0]))
    for first, second in zip(old[2], new[2]):
        test.assertAlmostEqual(first, second, places=8)



def canonical_mesh(source):
    """Decode the public fixed16 vertex layout without altering faces or slots."""
    return Mesh(source["raw"][:48],
                [tuple(component / 65536 for component in vertex) for vertex in source["vertices"]],
                source["faces"])
