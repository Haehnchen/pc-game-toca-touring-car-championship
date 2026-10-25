#ifndef TOCA_SOURCE_PRECISION_HLSL
#define TOCA_SOURCE_PRECISION_HLSL

// Source x87 PC=53 arithmetic, without float64 or 64-bit integer shader types.
// significand is little-endian, normalized with its leading bit at bit52;
// value = (-1)^sign * significand * 2^(exponent - 52). Zero retains its sign.
// Every finite binary32 input, including subnormals, is decoded exactly.
// Finite scene exponents must stay within int arithmetic; x87 exception flags
// and x87 exponent-range underflow/overflow are outside this precision adapter.
// Nonfinite float inputs are deliberately invalid, rather than silently using
// native float arithmetic. Invalid propagates and converts to canonical qNaN.
struct SourceDouble {
    uint2 significand;
    int exponent;
    bool sign;
};
static const int SOURCE_INVALID_EXPONENT = 0x7fffffff;

SourceDouble sourceZero(bool sign) {
    SourceDouble result;
    result.significand = uint2(0, 0);
    result.exponent = 0;
    result.sign = sign;
    return result;
}

SourceDouble sourceInvalid() {
    SourceDouble result = sourceZero(false);
    result.exponent = SOURCE_INVALID_EXPONENT;
    return result;
}

bool sourceIsZero(SourceDouble value) {
    return (value.significand.x | value.significand.y) == 0;
}

bool sourceWideNonzero(uint3 value) {
    return (value.x | value.y | value.z) != 0;
}

int sourceWideTop(uint3 value) {
    if (value.z != 0) return 64 + int(firstbithigh(value.z));
    if (value.y != 0) return 32 + int(firstbithigh(value.y));
    if (value.x != 0) return int(firstbithigh(value.x));
    return -1;
}

uint3 sourceWideLeft(uint3 value, uint distance) {
    if (distance == 0) return value;
    if (distance >= 96) return uint3(0, 0, 0);
    if (distance >= 64) return uint3(0, 0, value.x << (distance - 64));
    if (distance >= 32) {
        uint shift = distance - 32;
        if (shift == 0) return uint3(0, value.x, value.y);
        return uint3(0, value.x << shift, (value.y << shift) | (value.x >> (32 - shift)));
    }
    return uint3(value.x << distance, (value.y << distance) | (value.x >> (32 - distance)),
        (value.z << distance) | (value.y >> (32 - distance)));
}

uint3 sourceWideRight(uint3 value, uint distance) {
    if (distance == 0) return value;
    if (distance >= 96) return uint3(0, 0, 0);
    if (distance >= 64) return uint3(value.z >> (distance - 64), 0, 0);
    if (distance >= 32) {
        uint shift = distance - 32;
        if (shift == 0) return uint3(value.y, value.z, 0);
        return uint3((value.y >> shift) | (value.z << (32 - shift)), value.z >> shift, 0);
    }
    return uint3((value.x >> distance) | (value.y << (32 - distance)),
        (value.y >> distance) | (value.z << (32 - distance)), value.z >> distance);
}

bool sourceWideLowBits(uint3 value, uint count) {
    if (count == 0) return false;
    if (count >= 96) return sourceWideNonzero(value);
    uint3 retained = sourceWideLeft(sourceWideRight(value, count), count);
    return retained.x != value.x || retained.y != value.y || retained.z != value.z;
}

uint3 sourceWideRightJam(uint3 value, uint distance) {
    uint3 result = sourceWideRight(value, distance);
    if (sourceWideLowBits(value, distance)) result.x |= 1;
    return result;
}

uint3 sourceWideIncrement(uint3 value) {
    value.x += 1;
    if (value.x == 0) {
        value.y += 1;
        if (value.y == 0) value.z += 1;
    }
    return value;
}

uint3 sourceWideRoundRight(uint3 value, uint distance) {
    if (distance == 0) return value;
    uint3 result = sourceWideRight(value, distance);
    // Guard plus sticky, with ties to the even retained significand.
    bool guard = (sourceWideRight(value, distance - 1).x & 1) != 0;
    if (guard && (sourceWideLowBits(value, distance - 1) || (result.x & 1) != 0)) result = sourceWideIncrement(result);
    return result;
}

SourceDouble sourceNormalize(uint3 magnitude, int exponentAtBitZero, bool sign) {
    int top = sourceWideTop(magnitude);
    if (top < 0) return sourceZero(sign);
    int shift = top - 52;
    uint3 rounded = shift > 0 ? sourceWideRoundRight(magnitude, uint(shift)) : sourceWideLeft(magnitude, uint(-shift));
    SourceDouble result;
    result.exponent = exponentAtBitZero + top;
    if ((rounded.y & 0x200000) != 0) {
        rounded = sourceWideRight(rounded, 1);
        result.exponent += 1;
    }
    result.significand = uint2(rounded.x, rounded.y);
    result.sign = sign;
    return result;
}

SourceDouble sourceDouble(float value) {
    uint bits = asuint(value);
    bool sign = (bits & 0x80000000) != 0;
    uint exponent = (bits >> 23) & 255;
    uint fraction = bits & 0x7fffff;
    if (exponent == 255) return sourceInvalid();
    if (exponent == 0) return sourceNormalize(uint3(fraction, 0, 0), -149, sign);
    // The normal binary32 leading bit is exactly 23. Promotion to PC53 is
    // an exact 29-bit shift, without the general 96-bit normalization kernel.
    uint mantissa = fraction | 0x800000;
    SourceDouble result;
    result.significand = uint2(mantissa << 29, mantissa >> 3);
    result.exponent = int(exponent) - 127;
    result.sign = sign;
    return result;
}

bool sourceMagnitudeLess(SourceDouble a, SourceDouble b) {
    if (a.exponent != b.exponent) return a.exponent < b.exponent;
    if (a.significand.y != b.significand.y) return a.significand.y < b.significand.y;
    return a.significand.x < b.significand.x;
}

uint3 sourceWideAdd(uint3 a, uint3 b) {
    uint3 result;
    result.x = a.x + b.x;
    uint carry = result.x < a.x ? 1 : 0;
    uint middle = a.y + b.y;
    uint carryHigh = middle < a.y ? 1 : 0;
    result.y = middle + carry;
    if (result.y < middle) carryHigh += 1;
    result.z = a.z + b.z + carryHigh;
    return result;
}

// The caller supplies a >= b.
uint3 sourceWideSubtract(uint3 a, uint3 b) {
    uint3 result;
    result.x = a.x - b.x;
    uint borrow = a.x < b.x ? 1 : 0;
    uint middle = a.y - b.y;
    uint borrowHigh = a.y < b.y ? 1 : 0;
    result.y = middle - borrow;
    if (middle < borrow) borrowHigh += 1;
    result.z = a.z - b.z - borrowHigh;
    return result;
}

SourceDouble sourceAdd(SourceDouble a, SourceDouble b) {
    if (a.exponent == SOURCE_INVALID_EXPONENT || b.exponent == SOURCE_INVALID_EXPONENT) return sourceInvalid();
    if (sourceIsZero(a) && sourceIsZero(b)) return sourceZero(a.sign && b.sign);
    if (sourceIsZero(a)) return b;
    if (sourceIsZero(b)) return a;
    if (sourceMagnitudeLess(a, b)) {
        SourceDouble swapped = a;
        a = b;
        b = swapped;
    }
    uint3 large = sourceWideLeft(uint3(a.significand.x, a.significand.y, 0), 3);
    uint3 small = sourceWideLeft(uint3(b.significand.x, b.significand.y, 0), 3);
    small = sourceWideRightJam(small, uint(a.exponent - b.exponent));
    uint3 sum = a.sign == b.sign ? sourceWideAdd(large, small) : sourceWideSubtract(large, small);
    // Exact cancellation under round-to-nearest is positive zero. For a
    // subtracting alignment, three guard bits retain every cancellation bit;
    // alignment with a sticky tail can cancel at most one leading bit.
    if (!sourceWideNonzero(sum)) return sourceZero(false);
    return sourceNormalize(sum, a.exponent - 55, a.sign);
}

// Extract the normalized24-bit significand of a decoded finite binary32.
uint sourceFloatSignificand(SourceDouble value) {
    return (value.significand.y << 3) | (value.significand.x >> 29);
}

// Explicit16-bit limbs keep the include portable across HLSL translators;
// the low/high product needs neither a uint64 type nor a backend intrinsic.
uint2 sourceMultiply32(uint a, uint b) {
    uint aLow = a & 0xffff, aHigh = a >> 16;
    uint bLow = b & 0xffff, bHigh = b >> 16;
    uint low = aLow * bLow;
    uint cross0 = aLow * bHigh, cross1 = aHigh * bLow;
    uint middle = (low >> 16) + (cross0 & 0xffff) + (cross1 & 0xffff);
    return uint2((low & 0xffff) | (middle << 16), aHigh * bHigh + (cross0 >> 16) + (cross1 >> 16) + (middle >> 16));
}

SourceDouble sourceProduct(float first, float second) {
    SourceDouble a = sourceDouble(first), b = sourceDouble(second);
    if (a.exponent == SOURCE_INVALID_EXPONENT || b.exponent == SOURCE_INVALID_EXPONENT) return sourceInvalid();
    if (sourceIsZero(a) || sourceIsZero(b)) return sourceZero(a.sign != b.sign);
    uint2 product = sourceMultiply32(sourceFloatSignificand(a), sourceFloatSignificand(b));
    return sourceNormalize(uint3(product.x, product.y, 0), a.exponent + b.exponent - 46, a.sign != b.sign);
}

SourceDouble sourceScale(SourceDouble a, float factor) {
    SourceDouble b = sourceDouble(factor);
    if (a.exponent == SOURCE_INVALID_EXPONENT || b.exponent == SOURCE_INVALID_EXPONENT) return sourceInvalid();
    if (sourceIsZero(a) || sourceIsZero(b)) return sourceZero(a.sign != b.sign);
    uint multiplier = sourceFloatSignificand(b);
    uint2 lowProduct = sourceMultiply32(a.significand.x, multiplier);
    uint2 highProduct = sourceMultiply32(a.significand.y, multiplier);
    uint middle = lowProduct.y + highProduct.x;
    uint high = highProduct.y + (middle < lowProduct.y ? 1u : 0u);
    return sourceNormalize(uint3(lowProduct.x, middle, high), a.exponent + b.exponent - 75, a.sign != b.sign);
}

SourceDouble sourceNegate(SourceDouble value) {
    value.sign = !value.sign;
    return value;
}

SourceDouble sourceSubtract(SourceDouble a, SourceDouble b) {
    return sourceAdd(a, sourceNegate(b));
}

uint4 sourceQuadAdd(uint4 a, uint4 b) {
    uint4 result;
    result.x = a.x + b.x;
    uint carry = result.x < a.x ? 1u : 0u;
    uint middle = a.y + b.y;
    result.y = middle + carry;
    carry = middle < a.y || result.y < middle ? 1u : 0u;
    middle = a.z + b.z;
    result.z = middle + carry;
    carry = middle < a.z || result.z < middle ? 1u : 0u;
    result.w = a.w + b.w + carry;
    return result;
}

SourceDouble sourceDoubleMultiply(SourceDouble a, SourceDouble b) {
    if (a.exponent == SOURCE_INVALID_EXPONENT || b.exponent == SOURCE_INVALID_EXPONENT) return sourceInvalid();
    if (sourceIsZero(a) || sourceIsZero(b)) return sourceZero(a.sign != b.sign);
    uint2 low = sourceMultiply32(a.significand.x, b.significand.x);
    uint2 cross0 = sourceMultiply32(a.significand.x, b.significand.y);
    uint2 cross1 = sourceMultiply32(a.significand.y, b.significand.x);
    uint2 high = sourceMultiply32(a.significand.y, b.significand.y);
    uint4 product = sourceQuadAdd(uint4(low.x, low.y, 0, 0), uint4(0, cross0.x, cross0.y, 0));
    product = sourceQuadAdd(product, uint4(0, cross1.x, cross1.y, 0));
    product = sourceQuadAdd(product, uint4(0, 0, high.x, high.y));
    // A normalized53x53 product has105/106 significant bits. Removing its
    // low32 bits with jam retains at least20 rounding bits for the53-bit RNE.
    uint3 magnitude = uint3(product.y, product.z, product.w);
    if (product.x != 0) magnitude.x |= 1;
    return sourceNormalize(magnitude, a.exponent + b.exponent - 72, a.sign != b.sign);
}

bool sourceWideLess(uint3 a, uint3 b) {
    if (a.z != b.z) return a.z < b.z;
    if (a.y != b.y) return a.y < b.y;
    return a.x < b.x;
}

// Rounded53-bit division for finite source intermediates. A zero divisor is
// explicitly invalid; the scene clipping denominator must be nonzero.
SourceDouble sourceDoubleDivide(SourceDouble a, SourceDouble b) {
    if (a.exponent == SOURCE_INVALID_EXPONENT || b.exponent == SOURCE_INVALID_EXPONENT || sourceIsZero(b)) return sourceInvalid();
    if (sourceIsZero(a)) return sourceZero(a.sign != b.sign);
    int exponent = a.exponent - b.exponent;
    uint3 remainder = uint3(a.significand.x, a.significand.y, 0);
    uint3 divisor = uint3(b.significand.x, b.significand.y, 0);
    if (sourceWideLess(remainder, divisor)) {
        remainder = sourceWideLeft(remainder, 1);
        exponent -= 1;
    }
    remainder = sourceWideSubtract(remainder, divisor);
    uint2 quotient = uint2(1, 0);
#ifndef __cplusplus
    [loop]
#endif
    for (uint bit = 0; bit < 52; ++bit) {
        remainder = sourceWideLeft(remainder, 1);
        uint next = sourceWideLess(remainder, divisor) ? 0u : 1u;
        if (next != 0) remainder = sourceWideSubtract(remainder, divisor);
        quotient = uint2((quotient.x << 1) | next, (quotient.y << 1) | (quotient.x >> 31));
    }
    uint3 twiceRemainder = sourceWideLeft(remainder, 1);
    bool greater = sourceWideLess(divisor, twiceRemainder);
    bool equal = !greater && !sourceWideLess(twiceRemainder, divisor);
    uint3 rounded = uint3(quotient.x, quotient.y, 0);
    if (greater || (equal && (quotient.x & 1) != 0)) rounded = sourceWideIncrement(rounded);
    if ((rounded.y & 0x200000) != 0) {
        rounded = sourceWideRight(rounded, 1);
        exponent += 1;
    }
    SourceDouble result;
    result.significand = uint2(rounded.x, rounded.y);
    result.exponent = exponent;
    result.sign = a.sign != b.sign;
    return result;
}

// x87 FISTP under round-to-nearest-even. Invalid/out-of-range conversions
// produce its integer-indefinite bit pattern, rather than a float fallback.
int sourceInt(SourceDouble value) {
    if (value.exponent == SOURCE_INVALID_EXPONENT) return asint(0x80000000u);
    if (sourceIsZero(value)) return 0;
    if (value.exponent >= 31) return asint(0x80000000u);
    uint3 rounded = sourceWideRoundRight(uint3(value.significand.x, value.significand.y, 0), uint(52 - value.exponent));
    if (!value.sign && rounded.x > 0x7fffffffu) return asint(0x80000000u);
    return asint(value.sign ? 0u - rounded.x : rounded.x);
}

float sourceFloat(SourceDouble value) {
    if (value.exponent == SOURCE_INVALID_EXPONENT) return asfloat(0x7fc00000u);
    uint sign = value.sign ? 0x80000000u : 0;
    if (sourceIsZero(value)) return asfloat(sign);
    uint3 magnitude = uint3(value.significand.x, value.significand.y, 0);
    if (value.exponent < -126) {
        // Subnormal output uses units of2^-149. The same RNE operation also
        // covers underflow to signed zero and carry into the smallest normal.
        if (value.exponent < -150) return asfloat(sign);
        uint3 rounded = sourceWideRoundRight(magnitude, uint(-value.exponent - 97));
        return asfloat(sign | rounded.x);
    }
    uint3 rounded = sourceWideRoundRight(magnitude, 29);
    int exponent = value.exponent;
    if ((rounded.x & 0x1000000) != 0) {
        rounded.x >>= 1;
        exponent += 1;
    }
    if (exponent > 127) return asfloat(sign | 0x7f800000u);
    return asfloat(sign | (uint(exponent + 127) << 23) | (rounded.x & 0x7fffff));
}

// 47e730: X accumulates Y+X+Z+translation; Y/Z/W accumulate Z+Y+X+translation.
SourceDouble sourceClipDot4(float4 coefficients, float3 position, bool xOrder) {
    SourceDouble x = sourceProduct(coefficients.x, position.x);
    SourceDouble y = sourceProduct(coefficients.y, position.y);
    SourceDouble z = sourceProduct(coefficients.z, position.z);
    SourceDouble first, second, last;
    if (xOrder) {
        first = y; second = x; last = z;
    } else {
        first = z; second = y; last = x;
    }
    return sourceAdd(sourceAdd(sourceAdd(first, second), last), sourceDouble(coefficients.w));
}

// 487ee9 stores denominator, anchor distance and UV fraction as binary32.
struct SourceNearParameters {
    float denominator;
    float distance;
    float fraction;
    bool anchorInside;
};

SourceNearParameters sourceNearParameters(float insideW, float outsideW, float nearPlane, bool anchorInside) {
    SourceNearParameters result;
    result.denominator = sourceFloat(sourceSubtract(sourceDouble(insideW), sourceDouble(outsideW)));
    result.anchorInside = anchorInside;
    float anchorW = anchorInside ? insideW : outsideW;
    result.distance = sourceFloat(sourceSubtract(sourceDouble(anchorW), sourceDouble(nearPlane)));
    SourceDouble numerator = sourceSubtract(sourceDouble(insideW), sourceDouble(nearPlane));
    if (anchorInside) numerator = sourceDouble(sourceFloat(numerator));
    result.fraction = sourceFloat(sourceDoubleDivide(numerator, sourceDouble(result.denominator)));
    return result;
}

// Two outside corners use the inside anchor; one outside uses the outside
// anchor. The crossing remains53-bit until its final TL store.
SourceDouble sourceNearPreparedCoordinate(float inside, float outside, SourceNearParameters parameters) {
    float anchor = parameters.anchorInside ? inside : outside;
    SourceDouble slope = sourceDoubleDivide(sourceSubtract(sourceDouble(inside), sourceDouble(outside)), sourceDouble(parameters.denominator));
    return sourceSubtract(sourceDouble(anchor), sourceScale(slope, parameters.distance));
}

float sourceNearPreparedUv(float inside, float outside, SourceNearParameters parameters) {
    return sourceFloat(sourceAdd(sourceScale(sourceSubtract(sourceDouble(outside), sourceDouble(inside)), parameters.fraction), sourceDouble(inside)));
}

SourceDouble sourceNearCoordinate(float inside, float outside, float insideW, float outsideW, float nearPlane, bool anchorInside) {
    return sourceNearPreparedCoordinate(inside, outside, sourceNearParameters(insideW, outsideW, nearPlane, anchorInside));
}

float sourceNearUv(float inside, float outside, float insideW, float outsideW, float nearPlane, bool anchorInside) {
    return sourceNearPreparedUv(inside, outside, sourceNearParameters(insideW, outsideW, nearPlane, anchorInside));
}

// 47e730 stores TL X/Y only after these53-bit multiply/adds. 487ee9 has
// a different center/origin addition order for its newly clipped corners.
float sourceTlCoordinate(SourceDouble coordinate, float reciprocal, float halfExtent, float origin, bool vertical, bool clipped) {
    SourceDouble offset = sourceScale(sourceScale(coordinate, reciprocal), halfExtent);
    SourceDouble result;
    if (clipped) {
        if (vertical) result = sourceSubtract(sourceDouble(origin), offset);
        else result = sourceAdd(offset, sourceDouble(origin));
        result = sourceAdd(result, sourceDouble(halfExtent));
    } else {
        if (vertical) result = sourceSubtract(sourceDouble(halfExtent), offset);
        else result = sourceAdd(offset, sourceDouble(halfExtent));
        result = sourceAdd(result, sourceDouble(origin));
    }
    return sourceFloat(result);
}

#endif
