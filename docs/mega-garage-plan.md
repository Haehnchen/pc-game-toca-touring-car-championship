# Mega Garage Plan

Vehicle selection; [porting plan](porting-plan.md). GR00–GR04 are integrated; GR05
remains the acceptance check for model changes.

## Binding Workflow

- AI generates empty garage backgrounds only. Vehicles are rendered manually with the
  game renderer from Mega models; if a Mega GLB is missing, the exporter uses original
  HIGH. Rotation, size, position and paint follow the original garage. Neighbor cut-ins
  and interior cars belong to the vehicle layer; damaged doors, debris, bottles and the
  barrel stay in the background. Original archives unchanged.
- New layers/composites: WebP70/method 6 with layer transparency preserved; existing
  WebP80 layers and provenance unchanged. Backgrounds, vehicle layers and the shadow
  template are versioned separately; 19 opaque 1146×714 composites under
  `assets/ui/mega/garage/` (`garN-carN.webp`); the build copies finished images only.
- 19 original views 573×357 WebP70/method 6 under `assets/ui/original/garage/` (same
  names). Loader: original as base; at detail 4 the same-named Mega file if present;
  missing Mega → original; corrupt selected file → load error. No strip loader/BFP
  fallback at runtime. Target rectangle `(34,63,573,357)`, text/icons afterwards.

## GR00 — Original Inventory and Variants

[Inventory tool](../tools/inventory-garages.py), [import mappings](../assets/ui/mega/garage/inventory.json)
(archive, vehicle, strips per view). Local previews under the ignored `build/`.

```sh
python3 tools/inventory-garages.py --check
python3 tools/inventory-garages.py --preview-dir build/garage-reference/canonical
```

| Archive | Members | Use |
| --- | ---: | --- |
| `GAR1.BFP` | 68 | IDs 0–9; both bonus slots closed |
| `GAR2.BFP` | 22 | IDs 7–9; Tank unlocked |
| `GAR3.BFP` | 22 | IDs 7–9; Flexmobile unlocked |
| `GAR4.BFP` | 22 | IDs 7–9; both unlocked |

- Nine consecutive 64×360 INDEX8 strips = 576×360, visible `(1,0,573,357)`.
  Original consumers: `00475b3f`/`00475bb5` load strips, `00475dcb` selects bonus
  variants, `00476363` draws the crop. Displayed with the active Audi menu palette;
  deviating BMP palettes and two GAR1 index-255 pixels are in the inventory.
  Images are opaque, including original shadows.
- IDs 0–6 one variant, IDs 7–9 four variants: 19 views. GAR1 = 4352×360 panorama;
  GAR2–4 replace the last 1408×360 range from global strip 46. Neighboring views share
  128/192 pixels; a bonus unlock also affects Renault and neighbor cut-ins.
- 13 empty backgrounds cover all views. Renault has two variants by Tank state; Tank by
  its own lock state; Flexmobile by Tank state. The Flexmobile nose on the right at the
  Tank and the Tank cut-in on the left at the Flexmobile belong to the vehicle layer.
  Only GAR1/Car8 and GAR1/Car9 are fully vehicle-free. No garage per driver/team paint.
  Brickwork, advertising band, daylight and perspective stay continuous across the pit
  lane.

## GR01 — Manual Rendering and Compositing

Exporter independent of `BUILD_TESTING`; required Mega GLBs must already be in the
model directory:

```sh
make build BUILD_TARGETS=toca-garage-car-export
python3 tools/render-garage-vehicles.py \
  --exporter build/dev/toca-garage-car-export \
  --data-root references/toca-1.1a \
  --model-dir build/dev/generated/mega \
  --output-dir build/garage-vehicles \
  --publish-dir assets/ui/mega/garage/cars \
  --manifest assets/ui/mega/garage/generation.json
python3 tools/build-garage-assets.py
```

The render script starts a private muted Xvfb, renders main/interior cars, assembles
neighbor cut-ins from panorama starts, records loaded models and reuses unchanged
sources via a CRC32 cache. `--publish-dir`: vehicle layers and `rendering.json`;
`--manifest`: foreground paths/empty states. Render 1152×720 is cropped to
`(2,0,1146,714)`. The compositor stacks background, contact shadow and vehicle layer
and publishes the 19 composites (committed; unchanged export writes nothing).
Background/vehicle layers are already 1146×714. Manual original import:
`python3 tools/import-garage-originals.py` (active frontend palette, original crop).

### Shared Contact Shadow

`shadows/contact.webp`: 512×256 black, softly fading footprint, max. 112/255 alpha
(original views confirm the shadow shape, not an alpha value). Scaled by the render
bounds from `cars/rendering.json`, neighbor offsets/crop applied, placed between
background and vehicles. Interior cars use a shorter footprint; fully empty bonus
views have no shadow. `generation.json` names the template; `cars/rendering.json`
holds crop, neighbor offsets and render bounds.

## GR02–GR04 — Vehicle Inventory

All ten main vehicles and interior cars use Mega models; placements in the
[render recipe](../assets/ui/mega/garage/cars/rendering.json).

| Vehicle | Paint |
| --- | --- |
| Honda Accord | Tarquini20, second |
| Audi A4 | Biela1, first |
| Vauxhall Vectra | Cleland8, first |
| Volvo S40 | Rydell3, first |
| Ford Mondeo | Hoy9, first |
| Nissan Primera | Leslie4, first |
| [Peugeot406](../assets/models/vehicles/peugeot/definition.json) | Harvey12, first |
| Renault Laguna | Menu2, first |
| [Tank](../assets/models/vehicles/tank/definition.json) | first |
| [Flexmobile](../assets/models/vehicles/flexmobile/definition.json) | first |

The Audi's italic 1 can look like a 7 on the sloped hood; definition and render confirm
Biela1. Vehicle details: [vehicle plan](mega-vehicles-plan.md).

## GR05 — Acceptance and Remaining Maintenance

- [ ] When visible models change, check the
  [wheel/body clearance](mega-vehicles-plan.md#wheel-and-body-clearance): both
  axles/sides, fixed wheel-arch openings, free interior surfaces and both maximum
  steering angles; special-vehicle dimensions need a source reference.
  Re-render affected main, neighbor, interior-car and bonus views, apply the stored
  placements, recompose and check changed states in the native garage flow.

Checks: `garage-assets`, `native-garage-artwork`.
[Composition recipe](../assets/ui/mega/garage/generation.json),
[render recipe](../assets/ui/mega/garage/cars/rendering.json).
