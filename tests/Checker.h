#pragma once

#include <kerfnest/kerfnest.h>

#include <string>
#include <vector>

// Independent check of a nest on the true geometry: exact line and arc distances in double,
// no Clipper. Every placed copy must be inside the sheet margin, at least the spacing from
// every other copy, and never on another copy's material.
namespace kerfnest::test {

struct CheckReport {
    std::vector<std::string> problems; ///< empty when the nest is valid
    double minGapMm = 1e300;           ///< smallest boundary distance between two copies on one sheet
};

inline constexpr double kCheckToleranceMm = 1e-6;

CheckReport CheckNest(const Problem& problem, const Solution& solution);

/// The part's loops rotated by angleDeg about the origin, then moved by (x, y).
std::vector<Loop> PlacedLoops(const Part& part, const Placement& placement);

/// Exact bounds of loops, arcs included.
struct BoundsMm {
    double minX, minY, maxX, maxY;
};
BoundsMm LoopBounds(const std::vector<Loop>& loops);

} // namespace kerfnest::test
