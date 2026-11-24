#include "nova2/field.h"

#include <cmath>   // floor, tanh
#include <cstring> // memcpy

#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

namespace {

double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
double lerp(double a, double b, double t) { return a + t * (b - a); }

// Eight gradients: the four diagonals and the four axes.
double grad(u8 h, double x, double y) {
    switch (h & 7) {
    case 0:
        return x + y;
    case 1:
        return -x + y;
    case 2:
        return x - y;
    case 3:
        return -x - y;
    case 4:
        return x;
    case 5:
        return -x;
    case 6:
        return y;
    default:
        return -y;
    }
}

const char* const kFieldNames[kFieldCount] = {"cores",     "ecores",  "devices",    "latency", "page",   "strict",
                                              "bandwidth", "density", "resistance", "cooling", "coolers"};

} // namespace

const char* field_name(Field f) {
    const u32 i = static_cast<u32>(f);
    return i < kFieldCount ? kFieldNames[i] : "?";
}

void Noise2::init(u64 seed, u32 id) {
    Rng r(seed, Rng::kWorldPlace, kPurposeNoise, id);
    for (u32 i = 0; i < 256; ++i)
        perm_[i] = static_cast<u8>(i);
    for (u32 i = 255; i > 0; --i) {
        const u32 j = static_cast<u32>(r.below(i + 1));
        const u8 t = perm_[i];
        perm_[i] = perm_[j];
        perm_[j] = t;
    }
    for (u32 i = 0; i < 256; ++i)
        perm_[256 + i] = perm_[i];
}

double Noise2::at(double x, double y) const {
    const double fx = floor(x), fy = floor(y);
    const u32 xi = static_cast<u32>(static_cast<i64>(fx) & 255);
    const u32 yi = static_cast<u32>(static_cast<i64>(fy) & 255);
    const double xf = x - fx, yf = y - fy;
    const double u = fade(xf), v = fade(yf);
    const u32 a = perm_[xi] + yi, b = perm_[xi + 1] + yi;
    const double x1 = lerp(grad(perm_[a], xf, yf), grad(perm_[b], xf - 1, yf), u);
    const double x2 = lerp(grad(perm_[a + 1], xf, yf - 1), grad(perm_[b + 1], xf - 1, yf - 1), u);
    return lerp(x1, x2, v);
}

double Noise2::fbm(double x, double y, u32 octaves, double freq) const {
    double sum = 0, amp = 1, norm = 0;
    for (u32 o = 0; o < octaves; ++o) {
        sum += amp * at(x * freq, y * freq);
        norm += amp;
        amp *= 0.5;
        freq *= 2.0;
    }
    return norm > 0 ? sum / norm : 0;
}

void FieldNoise::init(u64 seed, Field f, const FieldShape& shape, double scale) {
    const u32 id = kNoiseFields + 2 * static_cast<u32>(f);
    first_.init(seed, id);
    second_.init(seed, id + 1);
    shape_ = shape;
    scale_ = scale;
}

double FieldNoise::at(double x, double y) const {
    const double v = first_.fbm(x, y, 4, scale_);
    if (shape_.edge == 0)
        return v;
    return v + shape_.edge * tanh(shape_.sharpness * second_.fbm(x, y, 4, scale_));
}

bool FieldNoise::high(double x, double y) const { return shape_.edge != 0 && second_.fbm(x, y, 4, scale_) > 0; }

u64 order_key(double d) {
    u64 b;
    memcpy(&b, &d, sizeof b);
    return (b >> 63) ? ~b : (b | 0x8000000000000000ull);
}

} // namespace nova2
