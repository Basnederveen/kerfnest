#pragma once

#include "Shape.h"

#include <clipper2/clipper.h>

// No-fit polygons, exact on the integer polygons. A position t means: B's origin at t, with A
// at the origin. Touching is never an overlap.
namespace kerfnest {

/// Positions where B's material overlaps A's material: counter-clockwise outers, clockwise
/// holes. The holes are the free islands, from B inside one of A's holes, A inside one of B's
/// holes, or B in a pocket of A.
Clipper2Lib::Paths64 NoFitPolygon(const OrientedShape& a, const OrientedShape& b);

/// Positions where the outer path b lies inside the hole (closed: touching allowed).
Clipper2Lib::Paths64 InnerFitInHole(const Clipper2Lib::Path64& hole, const Clipper2Lib::Path64& b);

} // namespace kerfnest
