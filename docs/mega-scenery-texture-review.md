# Mega: Texture Review of All Track Objects

Covers all pure track objects, including technically accepted models and
generated/shared images. Excluded: vehicles, cockpits, ground and sky images.
All ten tracks are complete, including existing/shared images.

## Workflow per Package

1. Bundle related objects and shared materials. Check each original texture from
   `references/toca-1.1a/` with CRC32, UVs, alpha, motifs and usage. Compare unclear
   lettering with related originals first; document any remaining motorsport
   reconstruction briefly; no arbitrary brands.
2. Rework bad images with imagegen (no pixel blocks, pseudo-lettering or blanket
   dirt). 1:1 replacement with the same UVs, slots, alpha, motifs and repeats. Drafts
   under `build/`; WebP70, method 6, ≤ 512 px per edge.
3. Check representative consumers with existing native checks: original, previous and
   new texture on the same simple geometry from close range, matching angles, driving
   distance and track context. UV edges, alpha, filtering, shared variants; periods,
   spacing and phase after encoding. Equal edge colors do not prove a seamless join.
4. Keep source/output files, bindings and CRC32 per package in the recipes; commit the
   package. Individual reports/comparison pages/fixtures only for a concrete source
   ambiguity or integration defect.
5. Textures before geometry; keep simple bodies, thickness and back faces; extra relief
   only with visible benefit.

## Completion MT00–MT05

- [x] MT00: Complete scenery inventory including FREE8/9/10 reconciled;
  all 1983 pure mesh rows accepted, vehicles/cockpits excluded.
- [x] MT01: Donington GP/Short with local variants and shared images checked.
- [x] MT02: Near trees, tree groups and woodland strips checked on all tracks.
- [x] MT03: Barriers, cones/posts/signs, advertising and flags checked.
- [x] MT04: Buildings, figures/crew, stands, tents, containers and animals checked.
- [x] MT05: Sources, active image mapping, source masks, motifs, UVs and shared
  consumers fully reconciled. No open local texture findings.

New or visibly defective track objects continue to follow the workflow above.

- [x] R06 Croft — All 159 mesh rows/155 types/603 bindings and 83 sources
  checked: 82 image mappings and one native mast constant. Existing/shared
  materials, tire periods and original prop bodies/UVs reconciled.
  [Package and tasks](mega-tracks-plan.md#r06--croft).

Accepted track states (source materials, mesh rows, bindings, native
color constants): [R03](mega-tracks-plan.md#r03--brands-hatch),
[R04](mega-tracks-plan.md#r04--oulton-park), [R07](mega-tracks-plan.md#r07--knockhill),
[R08](mega-tracks-plan.md#r08--snetterton); Silverstone
[asset data](../assets/models/mega/silverstone-rest-generation.json) (19 direct remainder
images replace missing/unsuitable generic images); Thruxton
[remainder package](../assets/models/mega/thruxton-rest-generation.json); Brands
[buildings](../assets/models/mega/brands-buildings-generation.json); Oulton
[remainder package](../assets/models/mega/oulton-rest-generation.json); Lavaland
[remainder package](../assets/models/mega/lavaland-rest-generation.json).

## Donington Material Status

GP: 141 source materials + four foliage images; Short: 136 shared source materials
and the same foliage images (five DENSO tower materials omitted). Direct replacement
with unchanged UVs, slots, alpha, motifs and repeat phases; local Short arrow variants
and animated flags checked. The shared `L50_1` braking-marker motif fits the original
UV crop; generation/CRC32 in `texture-generation.json` under `dunlop-marker-50-repair`.

Sources/import parameters: [buildings](../assets/models/mega/donington-buildings-generation.json),
[MT01 materials](../assets/models/mega/donington-mt01-generation.json),
[signs and flags](../assets/models/mega/donington-mt03-generation.json);
[technical completion](porting-completed/track-assets.md).

## Donington: textures and simple geometry

Method: same cameras, lighting and crops; front, oblique view, back, side, roof, driving
distance, context, sign. Captures stay under `build/`;
[model check](../tests/tools/test_mega_cbo.py) binds the source via CRC32.

| Model | Original | Elaborate prototype | Adopted geometry |
| --- | ---: | ---: | ---: |
| CBO3 pit building | 32 | 1264 | 476 |
| CBO1 control tower | 54 | 1950 | Check open |

CBO3: textures provide the largest visible difference. Roof edges, ridge, window reveals and
closed sign back stay three-dimensional; roof profiles and decorative sign frame dropped
(−788 triangles, 62 %).

CBO1: 1617 of the 1896 additional triangles are in window frames. Compare a simpler variant
before further build-out; likewise the roof profiles of CBO4–6. These models were not rebuilt
in this comparison.

Working rule: prioritize textures, add sensible simple bodies. Compare several viewing angles
and driving distance before fine detail; adopt only visible benefit. Budgets are upper limits,
not targets. Numbers are geometry counts, not FPS measurements; the fixture logs draw entries
before merging, not GPU draw calls. Further close-up cameras/facade images follow the workflow above.
