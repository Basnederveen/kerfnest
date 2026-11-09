#include "Checker.h"
#include "TestParts.h"

#include <doctest/doctest.h>

#include <map>

using namespace kerfnest;
using namespace kerfnest::test;

namespace {

/// A mixed bag of rectangles, L shapes and discs: enough types for the order to matter.
Problem Mixed(const uint64_t budget, const int threads = 1, const uint64_t seed = 7)
{
    Problem p;
    p.parts = {
        MakePart("plate", {RectLoop(0, 0, 620, 410)}, 3),
        MakePart("strip", {RectLoop(0, 0, 930, 120)}, 4),
        MakePart("L", {PolygonLoop({{0, 0}, {400, 0}, {400, 90}, {90, 90}, {90, 330}, {0, 330}})}, 5),
        MakePart("disc", {CircleLoop(0, 0, 110)}, 4),
        MakePart("tab", {RectLoop(0, 0, 210, 160)}, 7),
    };
    p.sheetTypes = {{"big", 1500, 1000}, {"small", 1000, 750}};
    p.settings.spacingMm = 5;
    p.settings.marginMm = 10;
    p.settings.anglesDeg = {0, 90, 180, 270};
    p.settings.seed = seed;
    p.settings.workBudget = budget;
    p.settings.threads = threads;
    return p;
}

double SheetArea(const Problem& p, const Solution& s)
{
    std::map<std::string, double> area;
    for (const SheetType& t : p.sheetTypes)
        area[t.id] = t.width * t.height;
    double total = 0;
    for (const std::string& id : s.sheetTypeIds)
        total += area[id];
    return total;
}

Solution Solved(const Problem& p)
{
    const Result r = Solve(p);
    if (const Refusal* refusal = std::get_if<Refusal>(&r))
        FAIL("refused: ", refusal->message);
    const Solution s = std::get<Solution>(r);
    const CheckReport check = CheckNest(p, s);
    for (const std::string& problem : check.problems)
        MESSAGE(problem);
    CHECK(check.problems.empty());
    return s;
}

bool Same(const Solution& a, const Solution& b)
{
    if (a.sheetTypeIds != b.sheetTypeIds || a.placements.size() != b.placements.size())
        return false;
    for (size_t i = 0; i < a.placements.size(); ++i) {
        const Placement& x = a.placements[i];
        const Placement& y = b.placements[i];
        if (x.partId != y.partId || x.sheetIndex != y.sheetIndex || x.x != y.x || x.y != y.y || x.angleDeg != y.angleDeg)
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("Search: never worse than the constructive pass")
{
    const Problem constructive = Mixed(0);
    const Problem searched = Mixed(50'000'000);

    CHECK(SheetArea(searched, Solved(searched)) <= SheetArea(constructive, Solved(constructive)));
}

TEST_CASE("Search: the same input gives the same nest, also on four threads")
{
    const Solution once = Solved(Mixed(20'000'000));
    const Solution again = Solved(Mixed(20'000'000));
    const Solution threaded = Solved(Mixed(20'000'000, 4));

    CHECK(Same(once, again));
    CHECK(Same(once, threaded));
}

TEST_CASE("Search: stops after a long run without improvement, whatever the budget")
{
    Solved(Mixed(1'000'000'000'000'000));
}

TEST_CASE("Search: a complete first pass is never refused; a refusal always has pieces left")
{
    // The smallest budget that gives a nest...
    uint64_t lo = 1, hi = 50'000'000;
    while (lo < hi) {
        const uint64_t mid = lo + (hi - lo) / 2;
        if (std::holds_alternative<Refusal>(Solve(Mixed(mid))))
            lo = mid + 1;
        else
            hi = mid;
    }
    // ...and just below it the refusal names pieces that were not placed: a pass that placed
    // everything is a nest, even when later sheet-size policies run out of budget.
    const Result r = Solve(Mixed(lo - 1));
    REQUIRE(std::holds_alternative<Refusal>(r));
    CHECK(std::get<Refusal>(r).message.find("placing 23 of 23") == std::string::npos);
}

TEST_CASE("Search: a constructive pass larger than the work budget is refused as incomplete")
{
    const Result r = Solve(Mixed(1));

    REQUIRE(std::holds_alternative<Refusal>(r));
    const Refusal& refusal = std::get<Refusal>(r);
    CHECK(refusal.code == RefusalCode::NestIncomplete);
    CHECK(refusal.message.find("work budget") != std::string::npos);
    CHECK(refusal.message.find("of 23 pieces") != std::string::npos);
}
