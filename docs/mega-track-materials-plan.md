# Mega Track Textures and Filtering

S00–S16 are accepted for all ten tracks; ground images and junction rendering
are complete. Object work: [track plan](mega-tracks-plan.md).
[Contract, sources and evidence](porting-completed/ground-materials.md).
Ownership: [porting plan](porting-plan.md), [scenery](mega-scenery-plan.md),
[backgrounds](mega-backgrounds-plan.md), [vehicles](mega-vehicles-plan.md).

## Completed ground inventory

134 used image slots/119 sources, ten junction checks
([acceptance](porting-completed/ground-materials.md#s16-ten-track-reconciliation--2026-10-18)),
including limited Brands/Lavaland rendering repairs and Croft's unchanged
rough-asphalt junction. No evidence for scenery/track-model upgrades.

## Contract for further work

Keep source motifs, markings, directional UVs, original profiles and contact
surfaces. Mega: fixed mipmaps, 2×AF, 2×MSAA; neutral world grain and junction
repairs only to a limited extent
([rendering contract](porting-completed/ground-materials.md#rendering-contract),
[filter rules](porting-completed/ground-materials.md#what-mipmaps-provide-here)).
Inventory: [ground TSV](mega-ground-textures.tsv). New visible findings need
their own limited task.

## Track models per detail level and shared upgrade

All profiles load the same `GROUND.C3D`; Mega adds Very High data. Object detail,
sector binding, rendering limits:
[loader contract](porting-completed/ground-materials.md#track-models-per-detail-level-and-shared-build-out).
Donington GP/Short: 15 shared ground images
([status](porting-completed/track-assets.md#r00--donington-gp)).

### Optional step: ground geometry

Optional: extend visible ground shapes such as kerbs in `apply_mega_scene_resources`
after `parse_terrain`, with their own rendering contract. There is no general
ground GLB path. Work within existing chunks first; keep or explicitly map chunk/node/variant
identities, order, `origin_xz`, `sector_centers` and light flags. Terrain uses full
node matrices; the scenery baker cannot be reused unchanged. HPC/contact surfaces
stay original.
[Source contract](porting-completed/ground-materials.md#separate-optional-step-ground-geometry).
