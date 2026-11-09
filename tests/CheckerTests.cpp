#include "Checker.h"
#include "TestParts.h"

#include <doctest/doctest.h>

using namespace kerfnest;
using namespace kerfnest::test;

namespace {

Problem TwoSquares()
{
    Problem p;
    p.parts = {MakePart("sq", {RectLoop(0, 0, 100, 100)}, 2)};
    p.sheetTypes = {{"s", 1000, 500}};
    p.settings.spacingMm = 5;
    p.settings.marginMm = 10;
    p.settings.anglesDeg = {0};
    return p;
}

Solution Placed(std::vector<Placement> placements)
{
    return {{"s"}, std::move(placements)};
}

} // namespace

TEST_CASE("Checker: accepts two squares exactly the spacing apart")
{
    const CheckReport r = CheckNest(TwoSquares(), Placed({{"sq", 0, 10, 10, 0}, {"sq", 0, 115, 10, 0}}));

    CHECK(r.problems.empty());
    CHECK(r.minGapMm == doctest::Approx(5.0));
}

TEST_CASE("Checker: detects a spacing violation of 0.1 mm")
{
    const CheckReport r = CheckNest(TwoSquares(), Placed({{"sq", 0, 10, 10, 0}, {"sq", 0, 114.9, 10, 0}}));

    REQUIRE(r.problems.size() == 1);
    CHECK(r.problems[0].find("apart") != std::string::npos);
}

TEST_CASE("Checker: detects crossing parts and a part on another's material")
{
    CHECK_FALSE(CheckNest(TwoSquares(), Placed({{"sq", 0, 10, 10, 0}, {"sq", 0, 60, 60, 0}})).problems.empty());

    Problem p = TwoSquares();
    p.parts = {MakePart("big", {RectLoop(0, 0, 300, 300)}), MakePart("small", {RectLoop(0, 0, 50, 50)})};
    const CheckReport r = CheckNest(p, Placed({{"big", 0, 10, 10, 0}, {"small", 0, 100, 100, 0}}));
    REQUIRE(r.problems.size() == 1);
    CHECK(r.problems[0].find("overlap") != std::string::npos);
}

TEST_CASE("Checker: accepts a part inside another's hole, detects a part outside the margin and a missing copy")
{
    Problem p = TwoSquares();
    p.parts = {MakePart("plate", {RectLoop(0, 0, 300, 300), RectLoop(50, 50, 200, 200)}), MakePart("small", {RectLoop(0, 0, 50, 50)})};
    CHECK(CheckNest(p, Placed({{"plate", 0, 10, 10, 0}, {"small", 0, 100, 100, 0}})).problems.empty());

    CHECK_FALSE(CheckNest(TwoSquares(), Placed({{"sq", 0, 9.9, 10, 0}, {"sq", 0, 200, 10, 0}})).problems.empty());
    CHECK_FALSE(CheckNest(TwoSquares(), Placed({{"sq", 0, 10, 10, 0}})).problems.empty());
}

TEST_CASE("Checker: measures the true gap between two discs")
{
    Problem p = TwoSquares();
    p.parts = {MakePart("disc", {CircleLoop(0, 0, 100)}, 2)};
    const CheckReport r = CheckNest(p, Placed({{"disc", 0, 110, 110, 0}, {"disc", 0, 315, 110, 0}}));

    CHECK(r.problems.empty());
    CHECK(r.minGapMm == doctest::Approx(5.0));
}
