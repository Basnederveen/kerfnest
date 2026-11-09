#include "Nfp.h"
#include "Shape.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

using namespace kerfnest;
using namespace Clipper2Lib;

namespace {

Path64 RectPath(const int64_t x, const int64_t y, const int64_t w, const int64_t h)
{
    return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
}

OrientedShape ShapeOf(Path64 outer, Paths64 holes = {})
{
    OrientedShape s;
    s.outer = std::move(outer);
    s.holes = std::move(holes);
    s.bounds = GetBounds(s.outer);
    return s;
}

/// The direct check runs 1024 times finer: at unit scale Clipper rounds a thin overlap sliver
/// away to nothing, and the check would call real overlaps free.
constexpr int64_t kFine = 1024;

Path64 Fine(const Path64& path, const Point64 t)
{
    Path64 out;
    for (const Point64& p : path)
        out.push_back({(p.x + t.x) * kFine, (p.y + t.y) * kFine});
    return out;
}

/// Material of a shape placed at t: outer minus holes, at the fine scale.
Paths64 Material(const OrientedShape& s, const Point64 t)
{
    Paths64 holes;
    for (const auto& h : s.holes)
        holes.push_back(Fine(h, t));
    return Difference(Paths64{Fine(s.outer, t)}, holes, FillRule::NonZero);
}

bool Overlaps(const OrientedShape& a, const OrientedShape& b, const Point64 t)
{
    return Area(Intersect(Material(a, {0, 0}), Material(b, t), FillRule::NonZero)) > 0;
}

enum class Where { Inside, Outside, OnEdge };

/// Point against a region given as counter-clockwise outers and clockwise holes.
Where Classify(const Point64 p, const Paths64& region)
{
    int winding = 0;
    for (const auto& path : region) {
        const auto r = PointInPolygon(p, path);
        if (r == PointInPolygonResult::IsOn)
            return Where::OnEdge;
        if (r == PointInPolygonResult::IsInside)
            winding += IsPositive(path) ? 1 : -1;
    }
    return winding > 0 ? Where::Inside : Where::Outside;
}

/// Deterministic pseudo-random star-shaped polygon around (cx, cy): always simple.
Path64 Star(uint64_t& state, const int64_t cx, const int64_t cy, const int n, const int64_t rMin, const int64_t rMax)
{
    Path64 p;
    for (int i = 0; i < n; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const int64_t r = rMin + static_cast<int64_t>((state >> 33) % static_cast<uint64_t>(rMax - rMin + 1));
        const double a = 2 * 3.14159265358979323846 * i / n;
        p.push_back({cx + static_cast<int64_t>(std::llround(r * std::cos(a))), cy + static_cast<int64_t>(std::llround(r * std::sin(a)))});
    }
    return p;
}

void CheckAgainstBruteForce(const OrientedShape& a, const OrientedShape& b, const int64_t step)
{
    const Paths64 nfp = NoFitPolygon(a, b);
    const Rect64 box(a.bounds.left - b.bounds.right - 2 * step, a.bounds.top - b.bounds.bottom - 2 * step,
                     a.bounds.right - b.bounds.left + 2 * step, a.bounds.bottom - b.bounds.top + 2 * step);
    int checked = 0;
    for (int64_t x = box.left; x <= box.right; x += step) {
        for (int64_t y = box.top; y <= box.bottom; y += step) {
            // NFP edge vertices are intersections rounded to whole units: within a unit of an
            // edge a point says nothing.
            const Where w = Classify({x, y}, nfp);
            constexpr int64_t d = 1;
            if (w == Where::OnEdge || w != Classify({x + d, y}, nfp) || w != Classify({x - d, y}, nfp) ||
                w != Classify({x, y + d}, nfp) || w != Classify({x, y - d}, nfp))
                continue;
            ++checked;
            CHECK_MESSAGE((w == Where::Inside) == Overlaps(a, b, {x, y}), "t = (", x, ", ", y, ")");
        }
    }
    CHECK(checked > 100);
}

} // namespace

TEST_CASE("NFP: two rectangles give the sum rectangle")
{
    const Paths64 nfp = NoFitPolygon(ShapeOf(RectPath(0, 0, 100, 50)), ShapeOf(RectPath(0, 0, 30, 20)));

    REQUIRE(nfp.size() == 1);
    CHECK(GetBounds(nfp) == Rect64(-30, -20, 100, 50));
    CHECK(Area(nfp) == doctest::Approx(130.0 * 70.0));
}

TEST_CASE("NFP: a pocket the part cannot enter from outside is still a free island")
{
    // C shape: 100 x 100 with a 60 x 60 pocket whose mouth is only 10 wide; the moving part is 40 x 40.
    const Path64 c{{0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 55}, {20, 55}, {20, 80}, {80, 80},
                   {80, 20}, {20, 20}, {20, 45}, {0, 45}};
    const OrientedShape a = ShapeOf(c);
    const OrientedShape b = ShapeOf(RectPath(0, 0, 40, 40));
    const Paths64 nfp = NoFitPolygon(a, b);

    CHECK(Classify({30, 30}, nfp) == Where::Outside); // inside the pocket
    CHECK(Classify({-20, 30}, nfp) == Where::Inside); // straddling the left wall
    CheckAgainstBruteForce(a, b, 3);
}

TEST_CASE("NFP: a part fits inside a placed part's hole")
{
    const OrientedShape a = ShapeOf(RectPath(0, 0, 500, 500), {RectPath(100, 100, 300, 300)});
    const OrientedShape b = ShapeOf(RectPath(0, 0, 100, 100));
    const Paths64 nfp = NoFitPolygon(a, b);

    CHECK(Classify({200, 200}, nfp) == Where::Outside);
    CHECK(Classify({50, 200}, nfp) == Where::Inside);
    CHECK(Classify({350, 200}, nfp) == Where::Inside);
    CheckAgainstBruteForce(a, b, 7);
}

TEST_CASE("NFP: a placed part fits inside the moving part's hole")
{
    const OrientedShape a = ShapeOf(RectPath(0, 0, 100, 100));
    const OrientedShape b = ShapeOf(RectPath(0, 0, 500, 500), {RectPath(100, 100, 300, 300)});
    const Paths64 nfp = NoFitPolygon(a, b);

    CHECK(Classify({-200, -200}, nfp) == Where::Outside);
    CheckAgainstBruteForce(a, b, 7);
}

TEST_CASE("NFP: random star-shaped parts with holes match a direct overlap test")
{
    uint64_t state = 42;
    for (int round = 0; round < 6; ++round) {
        CAPTURE(round);
        // One statement per draw: argument evaluation order differs between compilers.
        const Path64 aOuter = Star(state, 0, 0, 14, 60, 140);
        const Path64 aHole = Star(state, 0, 0, 9, 25, 45);
        const OrientedShape a = ShapeOf(aOuter, {aHole});
        OrientedShape b;
        if (round % 2 == 0) {
            b = ShapeOf(Star(state, 0, 0, 11, 8, 20));
        } else {
            const Path64 bOuter = Star(state, 0, 0, 12, 50, 120);
            const Path64 bHole = Star(state, 0, 0, 8, 25, 40);
            b = ShapeOf(bOuter, {bHole});
        }
        CheckAgainstBruteForce(a, b, 9);
    }
}
