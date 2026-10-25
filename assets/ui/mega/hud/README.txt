Mega HUD artwork

Newly authored replacement sprites preserve the original TOCA 1.1a HUD layout.
New RGBA WebP exports use quality 70, method 6. Existing assets retain their
recorded quality; no re-encoding is required. The game selects primary-km.webp or
primary-miles.webp and shares the secondary font and three start-light atlases.

Font/gauge artwork: tools/build-hud-assets.py redraws the original wide italic
characters, bevels, labels and curved RPM segments from geometric contours.
No system typeface or pixel enlargement is used for these replacement assets.
The complete font/gauge set is reproducible from the saved Python geometry:

  python3 tools/build-hud-assets.py --sprites build/hud-art/sprites --preview build/hud-art/overview.png

This writes the runtime font/gauge atlases here, plus individual WebP70 cutouts and a
labelled overview below build/. Use --output DIR to generate into another folder.
vector-art.json and the sprite manifest record names, source rectangles and CRC32.
Pillow is the only image-generation dependency; original game files are not read
by normal generation. The finished start-light atlases are committed separately.

Start lights: the built-in Imagegen tool recreated the original metal housing
and red/green lenses. The aligned
lossless masters are assets/sources/hud-lights/start-{housing,red,green}.webp,
at 512x512, 256x256 and 256x256. They preserve exact RGBA before runtime encoding;
provenance.json beside them retains source identities and CRC32.
The three finished runtime WebPs retain quality 80, method 6 and exact RGBA
handling. Encoding preserves the prepared layout without crops or transforms.
lights.json retains source/output identities, the original encoder recipe CRC32
and reference provenance. The lossless sources stay outside this runtime
directory and are not copied into the game build.

Canonical references remain read-only and are not included in these files.
Runtime requires only the WebP outputs; generation tools run offline from play.
