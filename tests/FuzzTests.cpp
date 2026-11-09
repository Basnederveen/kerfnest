#include "Checker.h"
#include "Rng.h"
#include "TestParts.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace kerfnest;
using namespace kerfnest::test;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Star-shaped outline (always simple) with a collinear extra vertex on every other edge.
Loop StarLoop(Rng& rng, const double rMin, const double rMax, const int n)
{
    std::vector<std::pair<double, double>> pts;
    for (int i = 0; i < n; ++i) {
        const double r = rMin + (rMax - rMin) * static_cast<double>(rng.Below(1000)) / 1000.0;
        const double a = 2 * kPi * i / n;
        pts.push_back({r * std::cos(a), r * std::sin(a)});
    }
    std::vector<std::pair<double, double>> withCollinear;
    for (size_t i = 0; i < pts.size(); ++i) {
        withCollinear.push_back(pts[i]);
        if (i % 2 == 0) {
            const auto& b = pts[(i + 1) % pts.size()];
            withCollinear.push_back({(pts[i].first + b.first) / 2, (pts[i].second + b.second) / 2});
        }
    }
    return PolygonLoop(withCollinear);
}

/// Rectangle w x h with corner radius r (r may be tiny).
Loop RoundedRect(const double w, const double h, const double r)
{
    return {Line(r, 0, w - r, 0), Arc({w - r, 0}, {w, r}, {w - r, r}, false),
            Line(w, r, w, h - r), Arc({w, h - r}, {w - r, h}, {w - r, h - r}, false),
            Line(w - r, h, r, h), Arc({r, h}, {0, h - r}, {r, h - r}, false),
            Line(0, h - r, 0, r), Arc({0, r}, {r, 0}, {r, r}, false)};
}

Part RandomPart(Rng& rng, const int index, const int quantity)
{
    const std::string id = "p" + std::to_string(index);
    switch (index % 4) {
    case 0: return MakePart(id, {StarLoop(rng, 40, 220, 7 + static_cast<int>(rng.Below(9)))}, quantity);
    case 1: {
        const double w = 100 + static_cast<double>(rng.Below(700)), h = 60 + static_cast<double>(rng.Below(400));
        return MakePart(id, {RoundedRect(w, h, 0.05 + static_cast<double>(rng.Below(20)))}, quantity);
    }
    case 2: {
        // Plate with a large cutout (room for small parts) and a ring of small holes, one of them
        // touching the cutout's bounding box corner region.
        const double w = 500 + static_cast<double>(rng.Below(500));
        std::vector<Loop> loops{RectLoop(0, 0, w, w), RectLoop(w * 0.2, w * 0.2, w * 0.6, w * 0.6)};
        for (int k = 0; k < 6; ++k)
            loops.push_back(CircleLoop(w * 0.1 + k * w * 0.13, w * 0.1, 5 + static_cast<double>(rng.Below(10))));
        return MakePart(id, loops, quantity);
    }
    default:
        return MakePart(id, {CircleLoop(0, 0, 20 + static_cast<double>(rng.Below(200))), CircleLoop(0, 0, 8)}, quantity);
    }
}

} // namespace

TEST_CASE("Fuzz: 700 generated pieces with awkward geometry nest without a crash, every copy checked")
{
    Rng rng(2026);
    Problem p;
    int pieces = 0;
    for (int i = 0; i < 20; ++i) {
        const int quantity = i == 19 ? 700 - pieces : 20 + static_cast<int>(rng.Below(30));
        p.parts.push_back(RandomPart(rng, i, quantity));
        pieces += quantity;
    }
    REQUIRE(pieces == 700);
    p.sheetTypes = {{"3000x1500", 3000, 1500}, {"2500x1250", 2500, 1250}};
    p.settings.spacingMm = 5;
    p.settings.marginMm = 10;
    p.settings.anglesDeg = {0, 90, 180, 270};

    const Result r = Solve(p);
    if (const Refusal* refusal = std::get_if<Refusal>(&r))
        FAIL("refused: ", refusal->message);
    const CheckReport check = CheckNest(p, std::get<Solution>(r));
    for (size_t i = 0; i < std::min<size_t>(check.problems.size(), 10); ++i)
        MESSAGE(check.problems[i]);
    CHECK(check.problems.empty());
}
