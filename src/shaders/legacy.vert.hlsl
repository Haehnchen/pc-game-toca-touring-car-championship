cbuffer Viewport : register(b0, space1) {
    float2 viewportSize;
    float2 viewportOrigin;
    float4 raster; // XY: prepared viewport scale; Z: explicit reference profile.
};
struct Input {
    float4 position : TEXCOORD0; // Original TL x, y, z, reciprocal W.
    float4 diffuse : TEXCOORD1; // Packed ARGB bytes arrive as BGRA.
    float4 specular : TEXCOORD2;
    float2 uv : TEXCOORD3;
};
struct Output {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float fog : TEXCOORD2;
};
Output main(Input input) {
    Output output;
    // D3D's integer pixel centers become SDL_GPU's half-integer centers.
    float2 ndc = (input.position.xy + 0.5 + viewportOrigin) / viewportSize * 2.0 - 1.0;
    ndc.y = -ndc.y;
    output.position = float4(ndc, input.position.z, 1.0) / input.position.w;
    if (raster.z != 0.0) {
        // Observed dgVoodoo2.63 TL shader: scale/add, then0.99/viewport;
        // one reciprocal followed by multiplication of all four components.
        precise float2 reference = (input.position.xy + viewportOrigin) * raster.xy + float2(-1.0, 1.0);
        reference += float2(0.9900000095367432 / viewportSize.x, 0.9900000095367432 / -viewportSize.y);
        precise float reciprocal = 1.0 / input.position.w;
        output.position = float4(reference, input.position.z, 1.0) * reciprocal;
    }
    output.color = input.diffuse.bgra;
    output.uv = input.uv;
    output.fog = input.specular.a;
    return output;
}
