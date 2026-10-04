#include "color/ColorMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rawrcam::color::math {
namespace {

constexpr float kMinPositive = 1.0e-8f;

Xy planckianXy(float temperatureKelvin) {
    const double t = std::clamp(static_cast<double>(temperatureKelvin), 1667.0, 25000.0);
    double x = 0.0;
    if (t < 4000.0) {
        x = -0.2661239e9 / (t * t * t) - 0.2343580e6 / (t * t) + 0.8776956e3 / t + 0.179910;
    } else {
        x = -3.0258469e9 / (t * t * t) + 2.1070379e6 / (t * t) + 0.2226347e3 / t + 0.240390;
    }

    double y = 0.0;
    if (t < 2222.0) {
        y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
    } else if (t < 4000.0) {
        y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
    } else {
        y = 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
    }
    return Xy{static_cast<float>(x), static_cast<float>(y), true};
}

std::array<double, 2> xyToUv1960(const Xy& xy) {
    const double denominator = -2.0 * xy.x + 12.0 * xy.y + 3.0;
    if (!xy.valid || std::abs(denominator) < 1.0e-12) {
        return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    }
    return {4.0 * xy.x / denominator, 6.0 * xy.y / denominator};
}

double uvDistanceSquared(const Xy& a, const std::array<double, 2>& uvTarget) {
    const auto uv = xyToUv1960(a);
    if (!std::isfinite(uv[0]) || !std::isfinite(uv[1])) return std::numeric_limits<double>::infinity();
    const double du = uv[0] - uvTarget[0];
    const double dv = uv[1] - uvTarget[1];
    return du * du + dv * dv;
}

}  // namespace

std::array<float, 9> toColumnMajor(const Matrix3& matrix) {
    std::array<float, 9> out{};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) out[x * 3 + y] = matrix[y * 3 + x];
    }
    return out;
}

Matrix3 identity3() { return {1, 0, 0, 0, 1, 0, 0, 0, 1}; }

Matrix3 multiply(const Matrix3& a, const Matrix3& b) {
    Matrix3 o{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            for (int k = 0; k < 3; ++k) {
                o[static_cast<size_t>(r * 3 + c)] +=
                    a[static_cast<size_t>(r * 3 + k)] * b[static_cast<size_t>(k * 3 + c)];
            }
        }
    }
    return o;
}

Vec3 multiply(const Matrix3& a, const Vec3& v) {
    return {a[0] * v[0] + a[1] * v[1] + a[2] * v[2], a[3] * v[0] + a[4] * v[1] + a[5] * v[2],
            a[6] * v[0] + a[7] * v[1] + a[8] * v[2]};
}

std::optional<Matrix3> inverse(const Matrix3& m) {
    const float a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(det) || std::abs(det) < 1.0e-8f) return std::nullopt;
    const float q = 1.0f / det;
    return Matrix3{(e * i - f * h) * q, (c * h - b * i) * q, (b * f - c * e) * q,
                   (f * g - d * i) * q, (a * i - c * g) * q, (c * d - a * f) * q,
                   (d * h - e * g) * q, (b * g - a * h) * q, (a * e - b * d) * q};
}

Matrix3 interpolate(const Matrix3& a, const Matrix3& b, float weightA) {
    const float w = std::clamp(weightA, 0.0f, 1.0f);
    Matrix3 out{};
    for (size_t i = 0; i < out.size(); ++i) out[i] = w * a[i] + (1.0f - w) * b[i];
    return out;
}

Matrix3 diagonal(const Vec3& v) { return {v[0], 0, 0, 0, v[1], 0, 0, 0, v[2]}; }

std::optional<Matrix3> inverseDiagonal(const Vec3& v) {
    if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]) || std::abs(v[0]) < kMinPositive ||
        std::abs(v[1]) < kMinPositive || std::abs(v[2]) < kMinPositive) {
        return std::nullopt;
    }
    return Matrix3{1.0f / v[0], 0, 0, 0, 1.0f / v[1], 0, 0, 0, 1.0f / v[2]};
}

Xy xyzToXy(const Vec3& xyz) {
    const float sum = xyz[0] + xyz[1] + xyz[2];
    if (!std::isfinite(sum) || sum <= kMinPositive) return {};
    const float x = xyz[0] / sum;
    const float y = xyz[1] / sum;
    if (!std::isfinite(x) || !std::isfinite(y) || x <= 0.0f || y <= 0.0f || x + y >= 1.0f) return {};
    return {x, y, true};
}

float correlatedColorTemperatureKelvin(const Xy& xy) {
    const auto uvTarget = xyToUv1960(xy);
    if (!std::isfinite(uvTarget[0]) || !std::isfinite(uvTarget[1])) return 0.0f;

    // Find the nearest point on an analytic Planckian locus in reciprocal
    // temperature space. This preserves tint/off-locus displacement while
    // giving the temperature coordinate needed for DNG-style interpolation.
    double lo = 1.0 / 25000.0;
    double hi = 1.0 / 1667.0;
    for (int iteration = 0; iteration < 48; ++iteration) {
        const double m1 = lo + (hi - lo) / 3.0;
        const double m2 = hi - (hi - lo) / 3.0;
        const double d1 = uvDistanceSquared(planckianXy(static_cast<float>(1.0 / m1)), uvTarget);
        const double d2 = uvDistanceSquared(planckianXy(static_cast<float>(1.0 / m2)), uvTarget);
        if (d1 < d2)
            hi = m2;
        else
            lo = m1;
    }
    const double reciprocal = 0.5 * (lo + hi);
    if (!(reciprocal > 0.0) || !std::isfinite(reciprocal)) return 0.0f;
    return static_cast<float>(1.0 / reciprocal);
}

float deltaUvFromPlanckian(const Xy& xy, float cctKelvin) {
    if (!xy.valid || !(cctKelvin > 0.0f) || !std::isfinite(cctKelvin)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto uv = xyToUv1960(xy);
    const auto uvPlanck = xyToUv1960(planckianXy(cctKelvin));
    if (!std::isfinite(uv[0]) || !std::isfinite(uv[1]) || !std::isfinite(uvPlanck[0]) ||
        !std::isfinite(uvPlanck[1])) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return static_cast<float>(uv[1] - uvPlanck[1]);
}

std::optional<float> referenceIlluminantCctKelvin(int32_t illuminant) {
    switch (illuminant) {
        case 17:
            return 2856.0f;  // Standard Light A
        case 18:
            return 4874.0f;  // Standard Light B
        case 19:
            return 6774.0f;  // Standard Light C
        case 20:
            return 5503.0f;  // D55
        case 21:
            return 6504.0f;  // D65
        case 22:
            return 7504.0f;  // D75
        case 23:
            return 5003.0f;  // D50
        case 24:
            return 3200.0f;  // ISO studio tungsten
        default:
            return std::nullopt;
    }
}

float reciprocalTemperatureWeight1(float temperatureKelvin, float temperature1Kelvin, float temperature2Kelvin) {
    if (!(temperatureKelvin > 0.0f) || !(temperature1Kelvin > 0.0f) || !(temperature2Kelvin > 0.0f) ||
        std::abs(temperature1Kelvin - temperature2Kelvin) < 1.0e-4f) {
        return 1.0f;
    }
    const float invT = 1.0f / temperatureKelvin;
    const float inv1 = 1.0f / temperature1Kelvin;
    const float inv2 = 1.0f / temperature2Kelvin;
    const float denominator = inv1 - inv2;
    if (std::abs(denominator) < 1.0e-12f) return 1.0f;
    return std::clamp((invT - inv2) / denominator, 0.0f, 1.0f);
}

}  // namespace rawrcam::color::math
