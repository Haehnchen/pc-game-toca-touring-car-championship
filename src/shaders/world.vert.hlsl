#include "source_precision.hlsl"

#ifndef TOCA_REFERENCE_RASTER
#define TOCA_REFERENCE_RASTER 0
#endif

struct Vertex {
    float3 position;
    uint color;
    float2 uv;
    float2 padding;
};
StructuredBuffer<Vertex> corners : register(t0, space0);
struct ModelAttribute {
    float3 normal;
    uint flags;
    float3 ghostPosition;
    uint padding;
};
struct ModelInstance {
    float4 objectColumns[4];
    float4 directionDiffuse;
    uint4 ambientFlags;
    float4 uvRemap;
    float4 clipColumns[4];
};
StructuredBuffer<ModelAttribute> modelAttributes : register(t1, space0);
StructuredBuffer<ModelInstance> modelInstances : register(t2, space0);
StructuredBuffer<int> lightingAngles : register(t3, space0);
cbuffer Projection : register(b0, space1) {
    float4 viewColumns[4];
    float4 scaleNearFar;
    float4 depthSize;
    float4 reflection;
    float4 viewport;
    float4 clipColumns[4];
    float4 raster; // XY: prepared viewport scale; Z: explicit reference profile.
};
cbuffer ModelDraw : register(b1, space1) {
    uint modelMode;
    uint firstInstance;
    uint attributeBase;
    uint reserved;
};
struct Output {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float fog : TEXCOORD2;
};

float4 rasterPosition(float4 tl) {
#if TOCA_REFERENCE_RASTER
    if (raster.z != 0.0) {
        // Comparison backend's TL conversion; source53 stores remain unchanged.
        precise float2 reference = (tl.xy + viewport.zw) * raster.xy + float2(-1.0, 1.0);
        reference += float2(0.9900000095367432 / viewport.x, 0.9900000095367432 / -viewport.y);
        precise float reciprocal = 1.0 / tl.w;
        return float4(reference, tl.z, 1.0) * reciprocal;
    }
#endif
    precise float2 ndc = (tl.xy + 0.5 + viewport.zw) / viewport.xy * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return float4(ndc, tl.z, 1.0) / tl.w;
}

// GPU division may differ from the CPU's correctly rounded binary32 division.
// Correct its candidate significand using an exact integer residual; no float64
// capability or approximate per-frame pixel tolerance is required. Normal scene
// coordinates have normal operands/results. Leave exceptional/subnormal values
// to the backend, as for the TL shader's final clip-coordinate conversion.
float sourceDivide(float numerator, float denominator) {
    precise float candidate = numerator / denominator;
    uint aBits = asuint(numerator), bBits = asuint(denominator), qBits = asuint(candidate);
    int aExponent = int((aBits >> 23) & 255), bExponent = int((bBits >> 23) & 255), qExponent = int((qBits >> 23) & 255);
    if (aExponent == 0 || bExponent == 0 || aExponent == 255 || bExponent == 255 || qExponent == 0 || qExponent == 255) return candidate;
    uint a = (aBits & 0x7fffff) | 0x800000, b = (bBits & 0x7fffff) | 0x800000;
    uint shift = a < b ? 24 : 23;
    int exponent = aExponent - bExponent + (a < b ? 126 : 127);
    if (exponent <= 0 || exponent >= 255) return candidate;
    uint quotient = (qBits & 0x7fffff) | 0x800000;
    if (qExponent > exponent) quotient <<= uint(qExponent - exponent);
    if (qExponent < exponent) quotient >>= uint(exponent - qExponent);
    // The normal division candidate is within a few ULP. Its true residual
    // fits int32, so subtraction of low product words gives that residual exactly.
    int residual = int((a << shift) - b * quotient);
    [loop] while (residual > 0 && uint(residual) * 2 > b) {
        ++quotient;
        residual -= int(b);
    }
    [loop] while (residual < 0 && uint(-residual) * 2 > b) {
        --quotient;
        residual += int(b);
    }
    if (uint(abs(residual)) * 2 == b && (quotient & 1) != 0) quotient = residual > 0 ? quotient + 1 : quotient - 1;
    if (quotient >= 0x1000000) {
        quotient >>= 1;
        ++exponent;
    }
    return asfloat(((aBits ^ bBits) & 0x80000000) | (uint(exponent) << 23) | (quotient & 0x7fffff));
}

uint modelColor(float3 normal, uint material, ModelInstance instance) {
    precise float dot = 0;
    [unroll] for (uint axis = 0; axis < 3; ++axis) dot += normal[axis] * instance.directionDiffuse[axis];
    int fixed = int(round(clamp(dot, -1.0, 1.0) * 65536.0));
    uint angleIndex = min(uint(abs(fixed)), 65535u) >> 4;
    precise float fraction = fixed < 0 ? 0.0 : float(lightingAngles[angleIndex]) / 1024.0;
    uint result = material & 0xff000000u;
    [unroll] for (uint axis = 0; axis < 3; ++axis) {
        uint shift = (2 - axis) * 8;
        float channelMaterial = float((material >> shift) & 255);
        precise float diffuse = channelMaterial * instance.directionDiffuse.w * fraction;
        precise float ambient = sourceDivide(float(instance.ambientFlags[axis]), 255.0) * channelMaterial;
        result |= uint(clamp(round(diffuse + ambient), 0.0, 255.0)) << shift;
    }
    return result;
}

#if TOCA_REFERENCE_RASTER
// Keep this comparison backend out of the ordinary renderer's driver compiler.
// The captured hardware VS (877b1596 for D3DLVERTEX) retains separate
// world/view/projection DP4s: first multiply, then three fused multiply/adds.
float4 hardwareTransform(float4 value, float4 columns[4]) {
    float4 result;
    [loop] for (uint axis = 0; axis < 4; ++axis) {
        precise float first = value.x * columns[0][axis];
        result[axis] = mad(value.w, columns[3][axis], mad(value.z, columns[2][axis], mad(value.y, columns[1][axis], first)));
    }
    return result;
}

Output hardwareOutput(uint vertexID, bool isLine) {
    uint corner = vertexID % 6;
    // Each stream triangle has six invocation slots; the unused half remains
    // degenerate. The GPU owns clipping and interpolation of this source path.
    uint index = isLine ? vertexID : (vertexID / 6) * 3 + min(corner, 2u);
    Vertex vertex = corners[index];
    float4 world = float4(vertex.position, 1.0);
    float4 color = float4(float((vertex.color >> 16) & 255), float((vertex.color >> 8) & 255), float(vertex.color & 255), float(vertex.color >> 24)) / 255.0;
    float4 view = hardwareTransform(world, viewColumns);
    // Original projection's sparse DP4s, retaining its final fused Z add.
    precise float zBase = view.z * depthSize.x;
    float4 clip = float4(view.x * scaleNearFar.x, view.y * scaleNearFar.y, mad(view.w, depthSize.y, zBase), view.z);
    if (reflection.x != 0.0) clip.x = -clip.x;
    if (reflection.y != 0.0) clip.y = -clip.y;
    // Preserve logical source coordinates when composing a differently sized
    // target. Equal source/target extents avoid another arithmetic roundtrip.
    if (depthSize.z != viewport.x || viewport.z != 0.0 || reflection.z != 0.0)
        clip.x = clip.x * (depthSize.z / viewport.x) + clip.w * ((depthSize.z + 2.0 * (viewport.z + reflection.z)) / viewport.x - 1.0);
    if (depthSize.w != viewport.y || viewport.w != 0.0 || reflection.w != 0.0)
        clip.y = clip.y * (depthSize.w / viewport.y) + clip.w * (1.0 - (depthSize.w + 2.0 * (viewport.w + reflection.w)) / viewport.y);
    // Hardware VS877b adds W/viewport, i.e. exactly half a pixel. This is
    // distinct from VS04ec's0.495 TL convention in the reference profile.
    precise float2 bias = float2(clip.w / viewport.x, clip.w / -viewport.y);
    precise float2 shifted = clip.xy + bias;
    clip.xy = shifted;
    Output output;
    output.position = clip;
    output.color = color;
    output.uv = vertex.uv;
    output.fog = 1.0;
    return output;
}

Output hardwareLineQuad(uint vertexID) {
    Output endpoints[2];
    uint first = (vertexID / 6) * 2;
    endpoints[0] = hardwareOutput(first, true);
    endpoints[1] = hardwareOutput(first + 1, true);
    Output rejected = endpoints[0];
    rejected.position = float4(0, 0, -1, 1);
    // Clip the original segment before expansion; otherwise a near-plane
    // crossing can divide by negative W and produce an unbounded rectangle.
    [unroll] for (uint plane = 0; plane < 2; ++plane) {
        float distances[2];
        [unroll] for (uint index = 0; index < 2; ++index)
            distances[index] = plane == 0 ? endpoints[index].position.z : endpoints[index].position.w - endpoints[index].position.z;
        bool firstOutside = distances[0] < 0.0;
        bool secondOutside = distances[1] < 0.0;
        if (firstOutside && secondOutside) return rejected;
        if (firstOutside != secondOutside) {
            uint outside = firstOutside ? 0 : 1, inside = 1 - outside;
            precise float fraction = distances[outside] / (distances[outside] - distances[inside]);
            endpoints[outside].position += (endpoints[inside].position - endpoints[outside].position) * fraction;
            endpoints[outside].color += (endpoints[inside].color - endpoints[outside].color) * fraction;
            endpoints[outside].uv += (endpoints[inside].uv - endpoints[outside].uv) * fraction;
        }
    }
    if (endpoints[0].position.w <= 0.0 || endpoints[1].position.w <= 0.0) return rejected;
    precise float2 delta = (endpoints[1].position.xy / endpoints[1].position.w
        - endpoints[0].position.xy / endpoints[0].position.w) * float2(viewport.x, -viewport.y) * 0.5;
    precise float lengthSquared = dot(delta, delta);
    if (lengthSquared == 0.0) return rejected;
    // D3D11.3/DXVK requests width 1.4f. The captured rectangular-line
    // rasterizer covers a 1.375-pixel rectangle, verified independently over
    // all 256 subpixel phases; endpoint attributes remain duplicated.
    precise float2 normal = float2(-delta.y, delta.x) * (0.6875 / sqrt(lengthSquared));
    uint corner = vertexID % 6;
    uint quad = corner < 3 ? corner : corner == 3 ? 0 : corner == 4 ? 2 : 3;
    Output output = endpoints[quad < 2 ? 0 : 1];
    float side = quad == 1 || quad == 2 ? 1.0 : -1.0;
    precise float2 offset = normal * side * float2(2.0 / viewport.x, -2.0 / viewport.y) * output.position.w;
    output.position.xy += offset;
    return output;
}
#endif

int sourceQ16(SourceDouble value) {
    // Multiplication by2^16 is exact in PC53: retain the significand and
    // signed zero, and avoid expanding a generic53x24 multiplication kernel.
    if (!sourceIsZero(value) && value.exponent != SOURCE_INVALID_EXPONENT) value.exponent += 16;
    return sourceInt(value);
}

uint sourceModelColor(float3 normal, uint material, ModelInstance instance) {
    float3 components;
    [loop] for (uint axis = 0; axis < 3; ++axis) {
        // 487c78..fc stores all nine basis/light products as binary32.
        precise float3 coefficients = float3(instance.objectColumns[0][axis], instance.objectColumns[1][axis], instance.objectColumns[2][axis])
            * instance.directionDiffuse[axis];
        SourceDouble products[3];
        // Keep one exact48-bit product kernel instead of three inline copies.
        [loop] for (uint component = 0; component < 3; ++component) products[component] = sourceProduct(normal[component], coefficients[component]);
        SourceDouble first = products[axis == 1 ? 1 : 0];
        SourceDouble second = products[axis == 1 ? 0 : 1];
        // X is Z+(X+Y); addition's operand exchange preserves every bit.
        components[axis] = sourceFloat(sourceAdd(sourceAdd(first, second), products[2]));
    }
    // 487d66/7d/98 and 487da4 store components and their sum before FISTP.
    float dot = sourceFloat(sourceAdd(sourceAdd(sourceDouble(components.x), sourceDouble(components.y)), sourceDouble(components.z)));
    int fixed = sourceQ16(sourceDouble(dot));
    uint angleIndex = min(uint(abs(fixed)), 65535u) >> 4;
    float fraction = fixed < 0 ? 0.0 : float(lightingAngles[angleIndex]) / 1024.0;
    uint result = material & 0xff000000u;
    [loop] for (uint axis = 0; axis < 3; ++axis) {
        uint shift = (2 - axis) * 8;
        float channelMaterial = float((material >> shift) & 255);
        // Original ambient=(byte/255 double)*material, then FSTP float.
        // For byte operands this equals correctly rounded (byte*material)/255:
        // the integer product is exact and the odd denominator excludes ties.
        float ambient = sourceDivide(float(instance.ambientFlags[axis]) * channelMaterial, 255.0);
        precise float diffuse = channelMaterial * instance.directionDiffuse.w;
        int channel = sourceInt(sourceAdd(sourceProduct(diffuse, fraction), sourceDouble(ambient)));
        result |= uint(clamp(channel, 0, 255)) << shift;
    }
    return result;
}

Vertex intersect(Vertex first, Vertex second) {
    precise float fraction = sourceDivide(scaleNearFar.z - first.position.z, second.position.z - first.position.z);
    Vertex result = second;
    if (first.position.z <= scaleNearFar.z) result = first;
    precise float2 crossing = first.position.xy + (second.position.xy - first.position.xy) * fraction;
    precise float2 uv = first.uv + (second.uv - first.uv) * fraction;
    result.position = float3(crossing, scaleNearFar.z);
    result.uv = uv;
    return result;
}

float4 project(Vertex vertex) {
    precise float reciprocal = sourceDivide(1.0, vertex.position.z);
    precise float x = (1.0 + vertex.position.x * scaleNearFar.x * reciprocal) * depthSize.z * 0.5;
    precise float y = (1.0 - vertex.position.y * scaleNearFar.y * reciprocal) * depthSize.w * 0.5;
    precise float z = vertex.position.z == scaleNearFar.z ? 0.0 : depthSize.x + depthSize.y * reciprocal;
    return float4(x, y, z, reciprocal);
}

struct SourceClipVertex {
    float4 clip;
    SourceDouble depth;
    uint outcode;
    uint color;
    float2 uv;
};
struct SourceProjectedVertex {
    float4 tl;
    uint color;
    float2 uv;
};

SourceClipVertex sourceClipVertex(Vertex vertex, float4 matrix[4], int nearQ16) {
    SourceClipVertex result;
    result.depth = sourceZero(false);
    int q[4] = {0, 0, 0, 0};
    // Retain one integer projection kernel across all four components.
    // 47e730's FST leaves the53-bit dot live for Q16 FISTP; only Z needs
    // that raw dot later for its multiply by the stored reciprocal.
    [loop] for (uint axis = 0; axis < 4; ++axis) {
        float4 coefficients = float4(matrix[0][axis], matrix[1][axis], matrix[2][axis], matrix[3][axis]);
        SourceDouble value = sourceClipDot4(coefficients, vertex.position, axis == 0);
        result.clip[axis] = sourceFloat(value);
        if (axis == 2) result.depth = value;
        else q[axis] = sourceQ16(value);
    }
    int qx = q[0], qy = q[1], qw = q[3];
    result.outcode = 0;
    if (qw < qx) result.outcode |= 4;
    else if (qw < -qx) result.outcode |= 2;
    if (qw < qy) result.outcode |= 16;
    else if (qw < -qy) result.outcode |= 8;
    if (qw <= nearQ16) result.outcode |= 1;
    result.color = vertex.color;
    result.uv = vertex.uv;
    return result;
}

SourceProjectedVertex sourceOriginalVertex(SourceClipVertex vertex) {
    // A normal binary32 reciprocal rounds identically through PC53: a
    // non-power-of-two 24-bit divisor cannot reach a binary32 midpoint.
    float reciprocal = sourceDivide(1.0, vertex.clip.w);
    SourceProjectedVertex result;
    result.tl = float4(sourceTlCoordinate(sourceDouble(vertex.clip.x), reciprocal, depthSize.z * 0.5, 0.0, false, false),
        sourceTlCoordinate(sourceDouble(vertex.clip.y), reciprocal, depthSize.w * 0.5, 0.0, true, false),
        sourceFloat(sourceScale(vertex.depth, reciprocal)), reciprocal);
    result.color = vertex.color;
    result.uv = vertex.uv;
    return result;
}

SourceProjectedVertex sourceNearVertex(SourceClipVertex inside, SourceClipVertex outside, bool anchorInside) {
    float reciprocal = sourceDivide(1.0, scaleNearFar.z);
    SourceNearParameters parameters = sourceNearParameters(inside.clip.w, outside.clip.w, scaleNearFar.z, anchorInside);
    SourceDouble x = sourceNearPreparedCoordinate(inside.clip.x, outside.clip.x, parameters);
    SourceDouble y = sourceNearPreparedCoordinate(inside.clip.y, outside.clip.y, parameters);
    SourceProjectedVertex result;
    result.tl = float4(sourceTlCoordinate(x, reciprocal, depthSize.z * 0.5, 0.0, false, true),
        sourceTlCoordinate(y, reciprocal, depthSize.w * 0.5, 0.0, true, true), 0.0, reciprocal);
    result.color = outside.color;
    result.uv = float2(sourceNearPreparedUv(inside.uv.x, outside.uv.x, parameters), sourceNearPreparedUv(inside.uv.y, outside.uv.y, parameters));
    return result;
}

Output sourceResidentOutput(uint vertexID, Vertex input[3], float4 matrix[4], uint flags) {
    Output output;
    output.position = float4(0, 0, 0, 1);
    output.color = 0;
    output.uv = 0;
    output.fog = 1;
    SourceClipVertex vertices[3];
    // Original near=.25 gives0x4000; retain the adapter's explicit-near API.
    int nearQ16 = sourceQ16(sourceDouble(scaleNearFar.z));
    [loop] for (uint index = 0; index < 3; ++index) vertices[index] = sourceClipVertex(input[index], matrix, nearQ16);
    if ((vertices[0].outcode & vertices[1].outcode & vertices[2].outcode & 0x1e) != 0) return output;
    // 47e730 has no far outcode. The reference's quantized projection and
    // GPU clip Z retain a small band beyond the nominal native far distance.
    if (raster.z == 0.0 && (flags & 1) == 0 && vertices[0].clip.w > scaleNearFar.w && vertices[1].clip.w > scaleNearFar.w && vertices[2].clip.w > scaleNearFar.w) return output;
    uint nearCount = (vertices[0].outcode & 1) + (vertices[1].outcode & 1) + (vertices[2].outcode & 1);
    SourceProjectedVertex projected[6];
    uint count = 0;
    if (nearCount == 0) {
        [loop] for (uint index = 0; index < 3; ++index) projected[index] = sourceOriginalVertex(vertices[index]);
        count = 3;
    } else if (nearCount == 1 || nearCount == 2) {
        // The Q16 classification chooses how many corners to produce, while
        // stored W chooses the source branch, including its boundary band.
        bool anchorInside = nearCount == 2;
        uint selected;
        if (anchorInside) {
            if (vertices[0].clip.w <= scaleNearFar.z) {
                if (vertices[1].clip.w <= scaleNearFar.z) {
                    if (vertices[2].clip.w <= scaleNearFar.z) return output;
                    selected = 2;
                } else selected = 1;
            } else selected = 0;
        } else {
            if (vertices[0].clip.w > scaleNearFar.z) {
                if (vertices[1].clip.w > scaleNearFar.z) {
                    if (vertices[2].clip.w > scaleNearFar.z) return output;
                    selected = 2;
                } else selected = 1;
            } else selected = 0;
        }
        uint other0 = selected == 0 ? 1 : 0;
        uint other1 = selected == 2 ? 1 : 2;
        uint others[2] = {other0, other1};
        SourceProjectedVertex crossings[2], originals[2];
        // Keep one copy of the same53-bit edge/projection kernels. Each
        // edge still evaluates its original inside/outside pair in order.
        [loop] for (uint edge = 0; edge < 2; ++edge) {
            uint inside = anchorInside ? selected : others[edge];
            uint outside = anchorInside ? others[edge] : selected;
            crossings[edge] = sourceNearVertex(vertices[inside], vertices[outside], anchorInside);
            if (edge == 0 || !anchorInside) originals[edge] = sourceOriginalVertex(vertices[inside]);
        }
        SourceProjectedVertex first = crossings[0], second = crossings[1];
        SourceProjectedVertex original0 = originals[0];
        if (anchorInside) {
            // Source triples: (a,ab,ac), (ba,b,bc), (ca,cb,c).
            if (selected == 0) {
                projected[0] = original0; projected[1] = first; projected[2] = second;
            } else if (selected == 1) {
                projected[0] = first; projected[1] = original0; projected[2] = second;
            } else {
                projected[0] = first; projected[1] = second; projected[2] = original0;
            }
            count = 3;
        } else {
            // Preserve487ee9's diagonal and exact triangle submission order;
            // a generic four-corner fan can choose a different diagonal.
            SourceProjectedVertex original1 = originals[1];
            if (selected == 0) {
                projected[0] = first; projected[1] = original0; projected[2] = original1;
                projected[3] = first; projected[4] = original1; projected[5] = second;
            } else if (selected == 1) {
                projected[0] = original0; projected[1] = first; projected[2] = original1;
                projected[3] = first; projected[4] = second; projected[5] = original1;
            } else {
                projected[0] = original0; projected[1] = original1; projected[2] = first;
                projected[3] = second; projected[4] = first; projected[5] = original1;
            }
            count = 6;
        }
    } else {
        return output;
    }
    precise float area = (projected[1].tl.x - projected[0].tl.x) * (projected[2].tl.y - projected[0].tl.y)
        - (projected[1].tl.y - projected[0].tl.y) * (projected[2].tl.x - projected[0].tl.x);
    if (count == 6) {
        area += (projected[4].tl.x - projected[3].tl.x) * (projected[5].tl.y - projected[3].tl.y)
            - (projected[4].tl.y - projected[3].tl.y) * (projected[5].tl.x - projected[3].tl.x);
    }
    if (area <= 0) return output;
    uint corner = vertexID % 6;
    if (count == 3 && corner >= 3) corner = corner == 3 ? 0 : 2;
    SourceProjectedVertex vertex = projected[corner];
    float4 tl = vertex.tl;
    if ((flags & 1) != 0) tl.z = asfloat(0x3f7fff58u);
    if (reflection.x != 0) tl.x = depthSize.z - tl.x;
    if (reflection.y != 0) tl.y = depthSize.w - tl.y;
    tl.xy += reflection.zw;
    output.position = rasterPosition(tl);
    uint color = vertex.color;
    output.color = float4(sourceDivide(float((color >> 16) & 255), 255.0), sourceDivide(float((color >> 8) & 255), 255.0),
        sourceDivide(float(color & 255), 255.0), sourceDivide(float(color >> 24), 255.0));
    output.uv = vertex.uv;
    return output;
}

Output lineOutput(uint vertexID) {
    Output output;
    // A rejected hardware line must be outside the clip volume: unlike a
    // degenerate triangle, a zero-length visible line can rasterize a pixel.
    output.position = float4(0, 0, -1, 1);
    output.color = 0;
    output.uv = 0;
    output.fog = 1;
    Vertex endpoints[2];
    uint first = (vertexID / 2) * 2;
    [unroll] for (uint index = 0; index < 2; ++index) {
        endpoints[index] = corners[first + index];
        precise float3 position = viewColumns[0].xyz * endpoints[index].position.x
            + viewColumns[1].xyz * endpoints[index].position.y
            + viewColumns[2].xyz * endpoints[index].position.z + viewColumns[3].xyz;
        endpoints[index].position = position;
    }
    bool firstOutside = endpoints[0].position.z < scaleNearFar.z;
    bool secondOutside = endpoints[1].position.z < scaleNearFar.z;
    if (firstOutside && secondOutside) return output;
    if (firstOutside != secondOutside) {
        uint outside = firstOutside ? 0 : 1, inside = 1 - outside;
        precise float fraction = sourceDivide(scaleNearFar.z - endpoints[outside].position.z,
            endpoints[inside].position.z - endpoints[outside].position.z);
        precise float2 crossing = endpoints[outside].position.xy
            + (endpoints[inside].position.xy - endpoints[outside].position.xy) * fraction;
        precise float2 uv = endpoints[outside].uv + (endpoints[inside].uv - endpoints[outside].uv) * fraction;
        endpoints[outside].position = float3(crossing, scaleNearFar.z);
        endpoints[outside].uv = uv;
        uint color = 0;
        [unroll] for (uint shift = 0; shift < 32; shift += 8) {
            float a = float((endpoints[outside].color >> shift) & 255);
            float b = float((endpoints[inside].color >> shift) & 255);
            precise float channel = a + (b - a) * fraction;
            color |= uint(clamp(channel, 0.0, 255.0)) << shift;
        }
        endpoints[outside].color = color;
    }
    Vertex vertex = endpoints[vertexID % 2];
    float4 tl = project(vertex);
    if (reflection.x != 0) tl.x = depthSize.z - tl.x;
    if (reflection.y != 0) tl.y = depthSize.w - tl.y;
    tl.xy += reflection.zw;
    output.position = rasterPosition(tl);
    uint color = vertex.color;
    output.color = float4(sourceDivide(float((color >> 16) & 255), 255.0), sourceDivide(float((color >> 8) & 255), 255.0),
        sourceDivide(float(color & 255), 255.0), sourceDivide(float(color >> 24), 255.0));
    output.uv = vertex.uv;
    return output;
}

Output main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    if (modelMode == 2) {
#if TOCA_REFERENCE_RASTER
        return hardwareLineQuad(vertexID);
#else
        return lineOutput(vertexID);
#endif
    }
    Output output;
    output.position = float4(0, 0, 0, 1);
    output.color = 0;
    output.uv = 0;
    output.fog = 1;
    Vertex input[3];
    uint first = (vertexID / 6) * 3;
    uint flags = 0;
    ModelInstance instance = (ModelInstance)0;
    if (modelMode == 1) {
        instance = modelInstances[firstInstance + instanceID];
        flags = instance.ambientFlags.w;
    }
#if TOCA_REFERENCE_RASTER
    if (modelMode == 3) return hardwareOutput(vertexID, false);
#endif
    bool resident = modelMode == 0 || (modelMode == 1 && (flags & 16) == 0);
    [loop] for (uint index = 0; index < 3; ++index) {
        input[index] = corners[first + index];
        if (modelMode == 1) {
            ModelAttribute attribute = modelAttributes[attributeBase + first + index];
            bool ghostInterior = (flags & 2) != 0 && (attribute.flags & 1) != 0;
            float3 local = ghostInterior ? attribute.ghostPosition : input[index].position;
            precise float3 world = local;
            if ((flags & 16) != 0) {
                // The separate finale consumer starts from translation; precise
                // preserves each multiply/add's binary32 rounding (NoContraction).
                world = instance.objectColumns[3].xyz;
                world += local.x * instance.objectColumns[0].xyz;
                world += local.y * instance.objectColumns[1].xyz;
                world += local.z * instance.objectColumns[2].xyz;
            }
            precise float3 normal = 0;
            [unroll] for (uint component = 0; component < 3; ++component) {
                normal += attribute.normal[component] * instance.objectColumns[component].xyz;
            }
            input[index].position = world;
            // Race setup488f49(1) selects487afe for ghosts as well. Its
            // ambient term uses the face diffuse(150,150,255), not the
            // separate100/100/180 ambient of the inactive D3D material path.
            // 495912..91e retain face RGB and force opaque alpha for an
            // original untextured run; only textured ghosts take the
            // registered150/150/255/150 material diffuse (4956ef..5743).
            uint material = (flags & 32) != 0 ? input[index].color | 0xff000000u
                : (flags & 2) != 0 ? 0x969696ffu : input[index].color;
            input[index].color = (flags & 1) != 0 ? 0xffffffffu
                : resident ? sourceModelColor(attribute.normal, material, instance)
                           : modelColor(normal, material, instance);
            if ((flags & 2) != 0 && (ghostInterior || (flags & 4) != 0)) input[index].color = (input[index].color & 0xffffffu) | 0x60000000u;
            // The ghost interior's replacement vertex restores its original UV.
            if ((flags & 8) != 0 && !ghostInterior) input[index].uv.y = sourceDivide(input[index].uv.y - instance.uvRemap.x, instance.uvRemap.y);
        }
        if (!resident) {
            precise float3 position = viewColumns[0].xyz * input[index].position.x
                + viewColumns[1].xyz * input[index].position.y
                + viewColumns[2].xyz * input[index].position.z + viewColumns[3].xyz;
            input[index].position = position;
        }
    }
    if (resident) {
        float4 matrix[4];
        [unroll] for (uint column = 0; column < 4; ++column) matrix[column] = modelMode == 1 ? instance.clipColumns[column] : clipColumns[column];
        return sourceResidentOutput(vertexID, input, matrix, flags);
    }
    Vertex clipped[4];
    uint count = 0;
    Vertex previous = input[2];
    [unroll] for (uint index = 0; index < 3; ++index) {
        Vertex current = input[index];
        bool inside = current.position.z > scaleNearFar.z;
        if ((previous.position.z > scaleNearFar.z) != inside) {
            clipped[count++] = intersect(previous, current);
        }
        if (inside) {
            clipped[count++] = current;
        }
        previous = current;
    }
    if (count < 3) {
        return output;
    }
    bool outside[5] = {true, true, true, true, true};
    float4 projected[4];
    [loop] for (uint index = 0; index < count; ++index) {
        precise float x = clipped[index].position.x * scaleNearFar.x;
        precise float y = clipped[index].position.y * scaleNearFar.y;
        float z = clipped[index].position.z;
        outside[0] = outside[0] && x < -z;
        outside[1] = outside[1] && x > z;
        outside[2] = outside[2] && y < -z;
        outside[3] = outside[3] && y > z;
        outside[4] = outside[4] && z > scaleNearFar.w && (flags & 1) == 0;
        projected[index] = project(clipped[index]);
        if ((flags & 1) != 0) projected[index].z = asfloat(0x3f7fff58u);
    }
    if (outside[0] || outside[1] || outside[2] || outside[3] || outside[4]) {
        return output;
    }
    precise float area = 0;
    [loop] for (uint index = 1; index + 1 < count; ++index) {
        area += (projected[index].x - projected[0].x) * (projected[index + 1].y - projected[0].y)
            - (projected[index].y - projected[0].y) * (projected[index + 1].x - projected[0].x);
    }
    if (area <= 0) {
        return output;
    }
    uint corner = vertexID % 6;
    // The second triangle degenerates when the source polygon has three corners.
    uint index = corner < 3 ? corner : corner == 3 ? 0 : corner == 4 ? 2 : count - 1;
    float4 tl = projected[index];
    if (reflection.x != 0) tl.x = depthSize.z - tl.x;
    if (reflection.y != 0) tl.y = depthSize.w - tl.y;
    tl.xy += reflection.zw;
    output.position = rasterPosition(tl);
    uint color = clipped[index].color;
    output.color = float4(sourceDivide(float((color >> 16) & 255), 255.0), sourceDivide(float((color >> 8) & 255), 255.0),
        sourceDivide(float(color & 255), 255.0), sourceDivide(float(color >> 24), 255.0));
    output.uv = clipped[index].uv;
    return output;
}
