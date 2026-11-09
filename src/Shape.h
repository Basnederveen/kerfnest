#pragma once

#include <kerfnest/kerfnest.h>

#include <clipper2/clipper.h>

#include <cstdint>
#include <vector>

// Turning exact contours (lines, arcs, circles in mm) into the solver's integer polygons.
// Every approximation errs to the safe side: a part's polygon contains the part, a hole's
// polygon lies inside the hole. Two inflated parts that do not overlap are therefore at least
// the spacing apart in the true geometry.
namespace kerfnest {

/// Largest distance between an arc and its polygon edges.
inline constexpr double kArcToleranceMm = 0.01;

/// Extra growth on every offset: covers rounding polygon and offset vertices to whole units.
inline constexpr int64_t kRoundingGuardUnits = 2;

enum class Fit {
    Contain, ///< the polygon contains the region the loop encloses
    Inside,  ///< the polygon lies inside the region the loop encloses
};

/// The loop rotated about the origin by angleDeg, as a counter-clockwise polygon.
Clipper2Lib::Path64 PolygoniseLoop(const Loop& loop, int angleDeg, Fit fit);

/// Every loop closes exactly, no loop crosses itself, every hole lies inside the outer loop
/// and apart from the other holes.
/// @throws RefusalError (InvalidContour) naming the part, and the loop and segment when known.
void ValidateLoops(const Part& part);

/// A part at one angle, as the solver sees it: inflated by half the spacing, holes shrunk by
/// the same amount. All paths counter-clockwise.
struct OrientedShape {
    int angleDeg = 0;
    Clipper2Lib::Path64 outer;
    Clipper2Lib::Paths64 holes;
    Clipper2Lib::Rect64 bounds;     ///< of the inflated outer
    Clipper2Lib::Rect64 trueBounds; ///< of the true contour, rounded outward: sheet-edge fits stay exact
};

/// Material area of the true contour: outer minus holes, arcs exact.
double NetAreaMm2(const Part& part);

/// Expects a part that passed ValidateLoops.
OrientedShape BuildOrientedShape(const Part& part, int angleDeg, double spacingMm);

/// Fills every hole that cannot hold any part at any of its angles (bounds or area too small).
/// Conservative, and removes vertices nothing can use. shapes[part][angle].
void FillUnusableHoles(std::vector<std::vector<OrientedShape>>& shapes);

} // namespace kerfnest
