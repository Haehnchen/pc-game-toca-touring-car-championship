Selection garage artwork

backgrounds/: AI-generated empty garages, opaque WebP quality80.
cars/: locally rendered vehicle layers, transparent WebP quality80.
New renders/imports use WebP quality70, method6; the existing quality80 layers
and their recorded provenance remain unchanged.
Cars use available project Mega models, otherwise canonical original HIGH.
The actual native model render is used; no AI-generated cars are included.

tools/render-garage-vehicles.py is invoked MANUALLY. It renders on a private,
muted Xvfb, fits the original garage pose, combines neighbors/interior cars,
and optionally publishes finished WebPs plus rendering.json and manifest CRCs.
See docs/mega-garage-plan.md for the command. Finished car images are committed.

tools/build-garage-assets.py is invoked MANUALLY after changing source layers:
  python3 tools/build-garage-assets.py
It combines the cropped background, shared translucent contact shadow and vehicle layers into19 opaque1146x714
WebP quality70/method6 files directly in this directory. These finished images and their
composition.json CRC32 provenance are committed. The normal build copies only
the19 finished WebPs beside the game; it does not encode or compose images.
The source layers remain separate for editing. shadows/contact.webp is an authored
soft black footprint (maximum alpha112/255), positioned from cars/rendering.json
for main, neighboring and interior cars; fully empty slots receive no shadow.
Detail4 selects these images;
other details select the converted original/garage/ image with the same filename.
Missing optional Mega images fall back to that original image. Runtime reads no
GAR strips. The19 opaque573x357 originals are imported manually with
tools/import-garage-originals.py from unchanged local1.1a GAR1–4 and their active
Audi frontend palette; original/garage/import.json records source/output CRC32.

generation.json records all19 mappings and exact background Imagegen prompts.
cars/rendering.json records native model/renderer/vehicle-layer CRC32 provenance.
inventory.json records canonical garage metadata without original image pixels.
Versioned source layers are 1146x714, WebP80. Native vehicle captures and raw
background generators use their recorded larger canvases. Local reference
captures stay in build/.
