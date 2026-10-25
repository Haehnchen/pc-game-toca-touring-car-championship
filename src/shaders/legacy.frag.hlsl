Texture2D image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);
cbuffer Material : register(b0, space3) {
    float4 fogColor;
    float4 controls; // x: color key, y: fog, z: cutout threshold, w: P8 key mask.
    float4 raster; // x: reference profile; y: linear; z: clamp; w: reconstructed P8.
};
struct Input {
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float fog : TEXCOORD2;
};

float4 referenceIndexedTexel(int2 at, int2 size) {
    if (raster.z != 0.0) at = clamp(at, int2(0, 0), size - 1);
    else at = ((at % size) + size) % size;
    // The wrapper's format LUT yields byte-valued floats before filtering.
    // Recover those exact bytes from our lossless expanded RGBA8 upload.
    return round(image.Load(int3(at, 0)) * 255.0);
}

float4 referenceIndexedSample(float2 uv) {
    uint width, height;
    image.GetDimensions(width, height);
    int2 size = int2(width, height);
    // Observed packed/P8 decoder phase, shared by point and bilinear modes.
    float2 position = uv * float2(size) + asfloat(0xbefff2e5u); // -.4999000132
    int2 base = int2(floor(position));
    float2 fraction = frac(position);
    if (raster.y == 0.0) {
        // DXBC round_ne / SPIR-V RoundEven chooses the neighboring texel.
        return referenceIndexedTexel(base + int2(round(fraction)), size) * asfloat(0x3b808081u);
    }
    float4 a = referenceIndexedTexel(base, size);
    float4 b = referenceIndexedTexel(base + int2(1, 0), size);
    float4 c = referenceIndexedTexel(base + int2(0, 1), size);
    float4 d = referenceIndexedTexel(base + int2(1, 1), size);
    // Preserve the wrapper's byte-domain X/X/Y lerps, then normalize once.
    float4 first = fraction.x * (b - a) + a;
    float4 second = fraction.x * (d - c) + c;
    return (fraction.y * (second - first) + first) * asfloat(0x3b808081u);
}

float4 main(Input input) : SV_Target0 {
    bool referenceIndexed = raster.x != 0.0 && (controls.w != 0.0 || raster.w != 0.0);
    float4 texel = referenceIndexed ? referenceIndexedSample(input.uv) : image.Sample(imageSampler, input.uv);
    if (controls.x != 0.0 && (texel.a == 0.0 || texel.a < controls.z)) discard;
    // The P8 key mask controls rejection, not material opacity. This keeps
    // the source's opaque filtered key fringe and vertex-alpha particle fade.
    if (controls.w != 0.0 || referenceIndexed) texel.a = 1.0;
    float4 color = texel * input.color;
    if (controls.y != 0.0) color.rgb = lerp(fogColor.rgb, color.rgb, input.fog);
    return color;
}
