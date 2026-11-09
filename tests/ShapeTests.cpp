#include "Refusal.h"
#include "Shape.h"
#include "TestParts.h"
#include "Units.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace kerfnest;
using namespace kerfnest::test;
using namespace Clipper2Lib;

namespace {

constexpr double kPi = 3.14159265358979323846;

Point64 At(const double xMm, const double yMm) { return {RoundUnits(xMm), RoundUnits(yMm)}; }

double MaxRadiusUnits(const Path64& path)
{
    double r = 0.0;
    for (const auto& v : path)
        r = std::max(r, std::hypot(static_cast<double>(v.x), static_cast<double>(v.y)));
    return r;
}

Refusal RefusalOf(const Part& part)
{
    try {
        ValidateLoops(part);
    } catch (const RefusalError& e) {
        return e.refusal;
    }
    FAIL("expected a refusal");
    return {};
}

} // namespace

TEST_CASE("Polygonise: a disc's polygon contains the true circle, within the arc tolerance")
{
    const Path64 poly = PolygoniseLoop(CircleLoop(0, 0, 100), 0, Fit::Contain);

    for (int i = 0; i < 3600; ++i) {
        const double a = i * 2 * kPi / 3600;
        CHECK(PointInPolygon(At(99.999 * std::cos(a), 99.999 * std::sin(a)), poly) != PointInPolygonResult::IsOutside);
    }
    CHECK(MaxRadiusUnits(poly) <= RoundUnits(100 + kArcToleranceMm) + 1);
}

TEST_CASE("Polygonise: a hole's polygon lies inside the true hole, within the arc tolerance")
{
    const Path64 poly = PolygoniseLoop(CircleLoop(0, 0, 25), 0, Fit::Inside);

    CHECK(MaxRadiusUnits(poly) <= RoundUnits(25) + 1);
    for (int i = 0; i < 3600; ++i) {
        const double a = i * 2 * kPi / 3600;
        const double r = 25 - kArcToleranceMm - 0.002;
        CHECK(PointInPolygon(At(r * std::cos(a), r * std::sin(a)), poly) == PointInPolygonResult::IsInside);
    }
}

TEST_CASE("Polygonise: a notch arc keeps the material inside and the notch open")
{
    // 100 x 50 plate with a R20 half-round notch in the top edge, bulging down into the material.
    Loop loop{Line(0, 0, 100, 0), Line(100, 0, 100, 50), Line(100, 50, 70, 50),
              Arc({70, 50}, {30, 50}, {50, 50}, /*clockwise=*/true), Line(30, 50, 0, 50), Line(0, 50, 0, 0)};
    const Path64 poly = PolygoniseLoop(loop, 0, Fit::Contain);

    CHECK(PointInPolygon(At(50, 29.999), poly) != PointInPolygonResult::IsOutside);
    CHECK(PointInPolygon(At(50, 30 + kArcToleranceMm + 0.002), poly) == PointInPolygonResult::IsOutside);
}

TEST_CASE("Polygonise: a clockwise loop comes out counter-clockwise")
{
    const Path64 poly = PolygoniseLoop(PolygonLoop({{0, 0}, {0, 50}, {100, 50}, {100, 0}}), 0, Fit::Contain);

    CHECK(Area(poly) == doctest::Approx(100000.0 * 50000.0));
}

TEST_CASE("Polygonise: quarter turns are exact")
{
    const Path64 poly = PolygoniseLoop(RectLoop(0, 0, 100, 50), 90, Fit::Contain);
    const Rect64 b = GetBounds(poly); // Clipper names y-down: top is the smallest y

    CHECK(b.left == -50000);
    CHECK(b.right == 0);
    CHECK(b.top == 0);
    CHECK(b.bottom == 100000);
}

TEST_CASE("Shape: a rectangle grows by half the spacing plus the rounding guard, corners kept square")
{
    const OrientedShape s = BuildOrientedShape(MakePart("r", {RectLoop(0, 0, 100, 50)}), 0, 5.0);
    const int64_t grow = RoundUnits(2.5) + kRoundingGuardUnits;

    CHECK(s.bounds.Width() == RoundUnits(100) + 2 * grow);
    CHECK(s.bounds.Height() == RoundUnits(50) + 2 * grow);
    CHECK(Area(s.outer) == doctest::Approx(static_cast<double>(s.bounds.Width()) * s.bounds.Height()));
}

TEST_CASE("Shape: two inflated discs separate only at the full spacing, and not much later")
{
    const OrientedShape a = BuildOrientedShape(MakePart("d", {CircleLoop(0, 0, 50)}), 0, 5.0);

    // Smallest centre distance (in units) at which the inflated discs no longer overlap.
    int64_t lo = RoundUnits(100), hi = RoundUnits(110);
    while (lo < hi) {
        const int64_t d = (lo + hi) / 2;
        const Paths64 common = Intersect(Paths64{a.outer}, Paths64{TranslatePath(a.outer, d, 0)}, FillRule::NonZero);
        if (Area(common) > 0)
            lo = d + 1;
        else
            hi = d;
    }
    const double gapMm = UnitsToMm(lo) - 100.0;

    CHECK(gapMm >= 5.0);
    CHECK(gapMm <= 5.0 + 2 * (kArcToleranceMm + UnitsToMm(kRoundingGuardUnits)) + 0.002);
}

TEST_CASE("Shape: a hole shrinks by half the spacing plus the rounding guard")
{
    const OrientedShape s = BuildOrientedShape(MakePart("p", {RectLoop(-200, -200, 400, 400), CircleLoop(0, 0, 100)}), 0, 5.0);

    REQUIRE(s.holes.size() == 1);
    CHECK(MaxRadiusUnits(s.holes[0]) <= RoundUnits(100 - 2.5) - kRoundingGuardUnits + 1);
}

TEST_CASE("Validate: an open loop is refused, naming part, loop and segment")
{
    Loop loop = RectLoop(0, 0, 100, 50);
    loop[1].start.x += 0.0005;
    const Refusal r = RefusalOf(MakePart("bracket-7", {loop}));

    CHECK(r.code == RefusalCode::InvalidContour);
    CHECK(r.message.find("bracket-7") != std::string::npos);
    CHECK(r.message.find("loop 0") != std::string::npos);
    CHECK(r.message.find("segment 0") != std::string::npos);
}

TEST_CASE("Validate: a loop that crosses itself is refused")
{
    const Refusal r = RefusalOf(MakePart("bowtie", {PolygonLoop({{0, 0}, {100, 100}, {100, 0}, {0, 100}})}));

    CHECK(r.code == RefusalCode::InvalidContour);
    CHECK(r.message.find("bowtie") != std::string::npos);
}

TEST_CASE("Validate: a hole that crosses the outer loop is refused")
{
    const Refusal r = RefusalOf(MakePart("plate", {RectLoop(0, 0, 100, 100), RectLoop(80, 40, 40, 20)}));

    CHECK(r.code == RefusalCode::InvalidContour);
    CHECK(r.message.find("plate") != std::string::npos);
}

TEST_CASE("Shape: a hole too small for any part is filled; one that fits a part stays")
{
    const Part small = MakePart("small", {RectLoop(0, 0, 50, 50)});
    for (const auto& [holeMm, expectedHoles] : {std::pair{20.0, 0u}, std::pair{300.0, 1u}}) {
        const Part plate = MakePart("plate", {RectLoop(0, 0, 500, 500), RectLoop(100, 100, holeMm, holeMm)});
        std::vector<std::vector<OrientedShape>> shapes{{BuildOrientedShape(plate, 0, 5.0)}, {BuildOrientedShape(small, 0, 5.0)}};

        FillUnusableHoles(shapes);

        CHECK(shapes[0][0].holes.size() == expectedHoles);
    }
}
