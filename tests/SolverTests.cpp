#include "Checker.h"
#include "TestParts.h"

#include <doctest/doctest.h>

#include <set>

using namespace kerfnest;
using namespace kerfnest::test;

namespace {

Problem MakeProblem(std::vector<Part> parts, std::vector<SheetType> sheets, std::vector<int> angles = {0, 90, 180, 270})
{
    Problem p;
    p.parts = std::move(parts);
    p.sheetTypes = std::move(sheets);
    p.settings.spacingMm = 5;
    p.settings.marginMm = 10;
    p.settings.anglesDeg = std::move(angles);
    p.settings.workBudget = 0;
    return p;
}

Solution Solved(const Problem& problem)
{
    const Result r = Solve(problem);
    if (const Refusal* refusal = std::get_if<Refusal>(&r))
        FAIL("refused: ", refusal->message);
    const Solution s = std::get<Solution>(r);
    const CheckReport check = CheckNest(problem, s);
    for (const std::string& p : check.problems)
        MESSAGE(p);
    CHECK(check.problems.empty());
    return s;
}

Refusal Refused(const Problem& problem)
{
    const Result r = Solve(problem);
    REQUIRE(std::holds_alternative<Refusal>(r));
    return std::get<Refusal>(r);
}

} // namespace

TEST_CASE("Solve: two rectangles fit one sheet")
{
    const Solution s = Solved(MakeProblem({MakePart("r", {RectLoop(0, 0, 400, 300)}, 2)}, {{"s", 1000, 500}}));

    CHECK(s.sheetTypeIds.size() == 1);
    CHECK(s.placements.size() == 2);
}

TEST_CASE("Solve: quantity overflows to a second and third sheet")
{
    const Solution s = Solved(MakeProblem({MakePart("r", {RectLoop(0, 0, 600, 450)}, 3)}, {{"s", 1000, 500}}));

    CHECK(s.sheetTypeIds.size() == 3);
    CHECK(s.placements.size() == 3);
}

TEST_CASE("Solve: a small part goes inside a plate's hole; the plate fits the sheet exactly")
{
    const Part plate = MakePart("plate", {RectLoop(0, 0, 500, 500), RectLoop(100, 100, 300, 300)});
    const Part small = MakePart("small", {RectLoop(0, 0, 100, 100)});
    const Solution s = Solved(MakeProblem({plate, small}, {{"s", 520, 520}}));

    REQUIRE(s.sheetTypeIds.size() == 1);
    for (const Placement& p : s.placements) {
        if (p.partId != "small")
            continue;
        const BoundsMm b = LoopBounds(PlacedLoops(small, p));
        CHECK(b.minX >= 10 + 100);
        CHECK(b.maxX <= 10 + 400);
        CHECK(b.minY >= 10 + 100);
        CHECK(b.maxY <= 10 + 400);
    }
}

TEST_CASE("Solve: arc contours keep their arcs (four discs on one 425.05 sheet)")
{
    // Usable 405.05 = 2 x 200 + 5 + 0.05: the safe arc approximation gives away at most 0.014 mm a side.
    const Problem problem = MakeProblem({MakePart("disc", {CircleLoop(0, 0, 100)}, 4)}, {{"s", 425.05, 425.05}});
    const Solution s = Solved(problem);

    CHECK(s.sheetTypeIds.size() == 1);
    CHECK(CheckNest(problem, s).minGapMm >= 5.0 - kCheckToleranceMm);
}

TEST_CASE("Solve: a grain material only turns 180 degrees, so a strip that needs 90 is refused")
{
    const Part strip = MakePart("strip", {RectLoop(0, 0, 900, 100)});
    Solved(MakeProblem({strip}, {{"s", 150, 1000}}));

    const Refusal r = Refused(MakeProblem({strip}, {{"s", 150, 1000}}, {0, 180}));

    CHECK(r.code == RefusalCode::PartExceedsAllSheets);
    CHECK(r.message.find("strip") != std::string::npos);
    CHECK(r.message.find("900 x 100") != std::string::npos);
    CHECK(r.message.find("130 x 980") != std::string::npos);
}

TEST_CASE("Solve: a part larger than every sheet is refused, naming its size and the largest sheet")
{
    const Refusal r = Refused(MakeProblem({MakePart("beam", {RectLoop(0, 0, 4000, 100)})}, {{"a", 3000, 1500}, {"b", 2500, 1250}}));

    CHECK(r.code == RefusalCode::PartExceedsAllSheets);
    CHECK(r.message.find("beam") != std::string::npos);
    CHECK(r.message.find("4000 x 100") != std::string::npos);
    CHECK(r.message.find("2980 x 1480") != std::string::npos);
}

TEST_CASE("Solve: an invalid contour is refused")
{
    const Refusal r = Refused(MakeProblem({MakePart("bowtie", {PolygonLoop({{0, 0}, {100, 100}, {100, 0}, {0, 100}})})}, {{"s", 1000, 500}}));

    CHECK(r.code == RefusalCode::InvalidContour);
}

TEST_CASE("Solve: never mirrors; every placement is a rotation by an allowed angle")
{
    const Part l = MakePart("L", {PolygonLoop({{0, 0}, {300, 0}, {300, 60}, {60, 60}, {60, 200}, {0, 200}})}, 9);
    const Solution s = Solved(MakeProblem({l}, {{"s", 1000, 500}}));

    const std::set<int> allowed{0, 90, 180, 270};
    for (const Placement& p : s.placements)
        CHECK(allowed.count(p.angleDeg) == 1);
}

TEST_CASE("Solve: the last sheet is the smallest size that takes every remaining part")
{
    const Solution s = Solved(MakeProblem({MakePart("r", {RectLoop(0, 0, 1000, 400)})}, {{"big", 3000, 1500}, {"small", 1200, 600}}));

    REQUIRE(s.sheetTypeIds.size() == 1);
    CHECK(s.sheetTypeIds[0] == "small");
}

TEST_CASE("Solve: a well-filled small sheet beats a big sheet that takes everything")
{
    // Two 580 squares fill a 1200 x 600 sheet to 93%; one 3000 x 1500 takes all five at 37%.
    // Three small sheets are 2.16 m2, one big sheet 4.5 m2.
    const Solution s = Solved(MakeProblem({MakePart("sq", {RectLoop(0, 0, 580, 580)}, 5)}, {{"big", 3000, 1500}, {"small", 1200, 600}}));

    CHECK(s.sheetTypeIds == std::vector<std::string>{"small", "small", "small"});
}
