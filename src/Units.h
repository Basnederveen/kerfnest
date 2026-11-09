#pragma once

#include <cmath>
#include <cstdint>

// The solver works in integers: 1 unit = 1 micrometre. Doubles are converted once, at the edge.
namespace kerfnest {

inline constexpr int64_t kUnitsPerMm = 1000;

inline int64_t RoundUnits(const double mm) { return std::llround(mm * kUnitsPerMm); }
inline int64_t FloorUnits(const double mm) { return static_cast<int64_t>(std::floor(mm * kUnitsPerMm)); }
inline int64_t CeilUnits(const double mm) { return static_cast<int64_t>(std::ceil(mm * kUnitsPerMm)); }
inline double UnitsToMm(const int64_t units) { return static_cast<double>(units) / kUnitsPerMm; }

} // namespace kerfnest
