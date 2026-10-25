#pragma once

#include <array>
#include <cmath>
#include <stdexcept>

namespace toca::scene::matrix {
using Matrix = std::array<float, 16>;
inline constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// Internal linkage preserves each consumer's FP contraction policy: source
// scene arithmetic and strict TL preparation compile with different options.
static inline Matrix multiply(const Matrix& local, const Matrix& parent) {
    Matrix result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t inner = 0; inner < 4; ++inner) {
                result[row * 4 + column] += local[row * 4 + inner] * parent[inner * 4 + column];
            }
        }
    }
    return result;
}

// 47f500/48741e keep PC=53 products/sums until each binary32 coefficient
// store. Their even/odd columns use different x87 accumulation orders.
static inline Matrix source_product(const Matrix& local, const Matrix& parent) {
    Matrix result;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const auto term = [&](std::size_t inner) {
                return double(local[row * 4 + inner]) * double(parent[inner * 4 + column]);
            };
            double sum;
            if (column % 2 == 0) {
                sum = term(2) + term(1);
                sum += term(0);
            } else {
                sum = term(1) + term(0);
                sum += term(2);
            }
            sum += term(3);
            result[row * 4 + column] = float(sum);
        }
    }
    return result;
}

// 47fe20 rotates the other two basis rows about one existing local row.
// Its Rodrigues terms have explicit binary32 spills; only each final row
// product/sum stays PC=53 until its binary32 store. The axis need not be unit.
static inline Matrix source_axis_rotation(const Matrix& local, std::size_t axis, float sine, float cosine) {
    const auto x = local[axis * 4], y = local[axis * 4 + 1], z = local[axis * 4 + 2];
    const float complement = float(1.0 - double(cosine));
    const float xx = float(double(x) * x), yy = float(double(y) * y), zz = float(double(z) * z);
    const float tx = float(double(complement) * x);
    const float xy = float(double(y) * tx), xz = float(double(z) * tx), yz = float(double(complement) * z * y);
    const float sx = float(double(sine) * x), sy = float(double(sine) * y), sz = float(double(sine) * z);
    const std::array<std::array<float, 3>, 3> rotation{{{float((1.0 - double(xx)) * cosine + xx), float(double(xy) - sz), float(double(xz) + sy)},
        {float(double(xy) + sz), float((1.0 - double(yy)) * cosine + yy), float(double(yz) - sx)},
        {float(double(xz) - sy), float(double(yz) + sx), float((1.0 - double(zz)) * cosine + zz)}}};
    auto result = local;
    for (std::size_t row = 0; row < 3; ++row) {
        if (row == axis) {
            continue;
        }
        const double a = local[row * 4], b = local[row * 4 + 1], d = local[row * 4 + 2];
        result[row * 4] = float((d * rotation[2][0] + a * rotation[0][0]) + b * rotation[1][0]);
        result[row * 4 + 1] = float((b * rotation[1][1] + a * rotation[0][1]) + d * rotation[2][1]);
        result[row * 4 + 2] = float((a * rotation[0][2] + b * rotation[1][2]) + d * rotation[2][2]);
    }
    return result;
}

//47f500 publishes camera nodes after extended products and sums. Keep this
//separate from presentation matrix arithmetic and its contraction policy.
static inline Matrix camera_multiply(const Matrix& local, const Matrix& parent) {
    Matrix result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            double value = 0;
            for (std::size_t inner = 0; inner < 4; ++inner) {
                value += double(local[row * 4 + inner]) * parent[inner * 4 + column];
            }
            result[row * 4 + column] = float(value);
        }
    }
    return result;
}

static inline Matrix camera_inverse(const Matrix& input, const char* invalid_basis) {
    //47c520 keeps products extended until each cofactor's float spill, then
    //stores the determinant and reciprocal before multiplying the cofactors.
    //Float products followed by individual division change source camera bits.
    std::array<double, 16> m{};
    for (std::size_t index = 0; index < input.size(); ++index) {
        m[index] = input[index];
    }
    Matrix result{};
    result[0] = float(m[10] * m[5] - m[9] * m[6]);
    result[1] = float(m[9] * m[2] - m[10] * m[1]);
    result[2] = float(m[6] * m[1] - m[5] * m[2]);
    result[4] = float(m[8] * m[6] - m[4] * m[10]);
    result[5] = float(m[0] * m[10] - m[8] * m[2]);
    result[6] = float(m[4] * m[2] - m[0] * m[6]);
    result[8] = float(m[4] * m[9] - m[8] * m[5]);
    result[9] = float(m[8] * m[1] - m[0] * m[9]);
    result[10] = float(m[0] * m[5] - m[4] * m[1]);
    const auto determinant = float(m[0] * result[0] + m[4] * result[1] + m[8] * result[2]);
    if (!std::isfinite(determinant) || determinant == 0) {
        throw std::runtime_error(invalid_basis);
    }
    const auto reciprocal = float(1.0 / double(determinant));
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            result[row * 4 + column] *= reciprocal;
        }
    }
    for (std::size_t component = 0; component < 3; ++component) {
        result[12 + component] = float(-m[12] * result[component] - m[13] * result[4 + component] - m[14] * result[8 + component]);
    }
    result[15] = 1;
    return result;
}

//47c520 inverts the full affine3x3; source lookup bases need not be unit.
static inline Matrix inverse(const Matrix& matrix, const char* invalid_basis) {
    Matrix result{};
    result[0] = matrix[10] * matrix[5] - matrix[9] * matrix[6];
    result[1] = matrix[9] * matrix[2] - matrix[10] * matrix[1];
    result[2] = matrix[6] * matrix[1] - matrix[5] * matrix[2];
    result[4] = matrix[8] * matrix[6] - matrix[4] * matrix[10];
    result[5] = matrix[0] * matrix[10] - matrix[8] * matrix[2];
    result[6] = matrix[4] * matrix[2] - matrix[0] * matrix[6];
    result[8] = matrix[4] * matrix[9] - matrix[8] * matrix[5];
    result[9] = matrix[8] * matrix[1] - matrix[0] * matrix[9];
    result[10] = matrix[0] * matrix[5] - matrix[4] * matrix[1];
    const auto determinant = matrix[0] * result[0] + matrix[4] * result[1] + matrix[8] * result[2];
    if (!std::isfinite(determinant) || determinant == 0) {
        throw std::runtime_error(invalid_basis);
    }
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            result[row * 4 + column] /= determinant;
        }
    }
    for (std::size_t component = 0; component < 3; ++component) {
        result[12 + component] = -matrix[12] * result[component] - matrix[13] * result[4 + component] - matrix[14] * result[8 + component];
    }
    result[15] = 1;
    return result;
}
} // namespace toca::scene::matrix
