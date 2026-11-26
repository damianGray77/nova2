#pragma once

// The map's noise and fields (design.md 6.2, 7.3; build-plan-gate3.md step 2).
//
// Noise is Nova 1's: 2D gradient noise (Perlin's, with 8 gradients) summed
// over octaves (fBm), each noise with its own permutation, shuffled from the
// world seed and the noise's id. So each field's edges fall in different
// places, and a place can be good for one thing and poor for another.
//
// A field is two layers of the same fractal noise (6.2): the first used as
// it is, for gradients; the second through a steep step, tanh(sharpness x
// it), which makes cliffs that wind like coastlines. Its value is
//
//     first + edge x tanh(sharpness x second)
//
// so an edge strength of 0 gives gradients only. Only a field's order
// matters: values are dealt out to places by it (7.3), and the density and
// bandwidth fields are used by rank too (map.h).
#include "nova2/types.h"

namespace nova2 {

class Noise2 {
  public:
    // Its permutation, shuffled for noise `id` (kPurposeNoise).
    void init(u64 seed, u32 id);
    // One octave, roughly -1..1 (0 on the integer lattice).
    double at(double x, double y) const;
    // `octaves` octaves from frequency `freq`, each half the amplitude of the
    // last, normalised by the amplitudes' sum.
    double fbm(double x, double y, u32 octaves, double freq) const;

  private:
    u8 perm_[512] = {};
};

// The map's noises, each its own id: the terrain and its two warps, then
// two for each field (kNoiseFields + 2 x field, and + 1 for its step).
inline constexpr u32 kNoiseTerrain = 0;
inline constexpr u32 kNoiseWarpX = 1;
inline constexpr u32 kNoiseWarpY = 2;
inline constexpr u32 kNoiseFields = 16;

// The fields, in the order of their settings (map.<field>.edge).
// The last three are gate 4's (build-plan-gate4.md step 2): a link's
// resistance a length, a place's cooling, and where cooling devices are.
// They come last, so the earlier fields' noises and scatter stay as gate 3
// drew them.
enum class Field : u32 {
    cores,
    e_cores,
    devices,
    latency,
    page,
    strict,
    bandwidth,
    density,
    resistance,
    cooling,
    coolers,
    threads // the places' limits on a program's threads (9.6, 2026-11-25), last for the same reason
};
inline constexpr u32 kFieldCount = 12;
// Its settings' name: "cores", "ecores", ...
const char* field_name(Field f);

// A field's shape (6.2): its edge strength and sharpness.
struct FieldShape {
    double edge = 1;
    double sharpness = 8;
};

class FieldNoise {
  public:
    // `scale` is its frequency across the map's height.
    void init(u64 seed, Field f, const FieldShape& shape, double scale);
    double at(double x, double y) const;
    // Which side of its step a point is on (6.2): the step's contours are
    // where this changes. A field with no edge strength has no step, and
    // every point is on the one side.
    bool high(double x, double y) const;

  private:
    Noise2 first_, second_;
    FieldShape shape_;
    double scale_ = 3;
};

// A u64 whose unsigned order is the double's numeric order.
u64 order_key(double d);

} // namespace nova2
