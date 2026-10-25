"""Synthetic authored front panels, kept independent of the enhancer."""

from .environment import MODEL, detail


def panel(vertical=True):
    mesh = MODEL.Mesh(b'semantic-prop'.ljust(48, b'\0'), [], [])
    positions = ((0., .1, 0.), (6., .4, 0.), (6., 1.49, 0.), (0., 1.19, 0.)) if vertical \
        else ((-5., 0., 0.), (5., 0., 0.), (5., 0., -1.5), (-5., 0., -1.5))
    normal = (0., 0., -1.) if vertical else (0., -1., 0.)
    detail.geometry.polygon(mesh, positions, (0, 0, 0, 255), normal,
                            ((0., 0.), (1., 0.), (1., -1.), (0., -1.)), 0)
    return mesh
