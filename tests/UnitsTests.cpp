#include "Units.h"

#include <doctest/doctest.h>

using namespace kerfnest;

TEST_CASE("Units: one unit is one micrometre")
{
    CHECK(RoundUnits(1.0) == 1000);
    CHECK(RoundUnits(3000.0) == 3000000);
    CHECK(UnitsToMm(2500) == 2.5);
}

TEST_CASE("Units: rounding directions, also below zero")
{
    CHECK(RoundUnits(0.0004) == 0);
    CHECK(RoundUnits(-0.0006) == -1);
    CHECK(FloorUnits(1.0009) == 1000);
    CHECK(FloorUnits(-1.0001) == -1001);
    CHECK(CeilUnits(1.0001) == 1001);
    CHECK(CeilUnits(-1.0009) == -1000);
}
