#pragma once

#include <cstddef>
#include <cstdint>

namespace kerfnest {

/// SplitMix64. Own generator and own range mapping: std distributions differ between standard
/// libraries, and the search must give the same result on every platform's build of the same
/// input.
class Rng {
public:
    explicit Rng(const uint64_t seed) : state_(seed) {}

    uint64_t Next()
    {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    /// In [0, n); n > 0.
    size_t Below(const size_t n) { return static_cast<size_t>(Next() % n); }

private:
    uint64_t state_;
};

/// A seed for one stream out of two numbers (run seed, iteration, neighbour index, ...).
inline uint64_t MixSeed(const uint64_t a, const uint64_t b) { return Rng(a ^ (b * 0xD1B54A32D192ED03ULL)).Next(); }

} // namespace kerfnest
