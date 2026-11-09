#include <kerfnest/kerfnest.h>

#include "Decoder.h"
#include "Refusal.h"
#include "Shape.h"
#include "Units.h"

#include <algorithm>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace Clipper2Lib;

namespace kerfnest {
namespace {

std::string Size(const int64_t w, const int64_t h)
{
    std::ostringstream os;
    os << UnitsToMm(w) << " x " << UnitsToMm(h);
    return os.str();
}

void CheckProblem(const Problem& problem)
{
    const Settings& s = problem.settings;
    if (problem.sheetTypes.empty())
        throw std::invalid_argument("kerfnest: no sheet types");
    if (s.anglesDeg.empty())
        throw std::invalid_argument("kerfnest: no allowed angles");
    if (s.spacingMm < 0 || s.marginMm < 0)
        throw std::invalid_argument("kerfnest: negative spacing or margin");
}

Instance Prepare(const Problem& problem)
{
    Instance in;
    for (const Part& part : problem.parts) {
        ValidateLoops(part);
        std::vector<OrientedShape> shapes;
        for (const int angle : problem.settings.anglesDeg)
            shapes.push_back(BuildOrientedShape(part, angle, problem.settings.spacingMm));
        in.shapes.push_back(std::move(shapes));
        in.netAreaMm2.push_back(NetAreaMm2(part));
    }
    FillUnusableHoles(in.shapes);

    const double m = problem.settings.marginMm;
    for (const SheetType& t : problem.sheetTypes) {
        in.usable.push_back(Rect64(CeilUnits(m), CeilUnits(m), FloorUnits(t.width - m), FloorUnits(t.height - m)));
        in.sheetAreaMm2.push_back(t.width * t.height);
    }
    return in;
}

/// Refuses the first part that fits no sheet type at any allowed angle.
void CheckEveryPartFits(const Problem& problem, const Instance& in)
{
    for (size_t p = 0; p < problem.parts.size(); ++p) {
        const auto fits = [&](const OrientedShape& s) {
            return std::any_of(in.usable.begin(), in.usable.end(), [&](const Rect64& u) {
                return s.trueBounds.Width() <= u.Width() && s.trueBounds.Height() <= u.Height();
            });
        };
        if (std::any_of(in.shapes[p].begin(), in.shapes[p].end(), fits))
            continue;

        std::ostringstream os;
        os << "part '" << problem.parts[p].id << "' (";
        std::set<std::pair<int64_t, int64_t>> seen;
        for (const OrientedShape& s : in.shapes[p]) {
            if (!seen.insert({s.trueBounds.Width(), s.trueBounds.Height()}).second)
                continue;
            os << (seen.size() > 1 ? ", " : "") << Size(s.trueBounds.Width(), s.trueBounds.Height()) << " mm at " << s.angleDeg << " deg";
        }
        const auto largest = std::max_element(in.usable.begin(), in.usable.end(), [](const Rect64& a, const Rect64& b) {
            return static_cast<double>(a.Width()) * a.Height() < static_cast<double>(b.Width()) * b.Height();
        });
        os << ") fits no sheet: the largest usable area is " << Size(largest->Width(), largest->Height())
           << " mm (sheet minus the " << problem.settings.marginMm << " mm margin)";
        throw RefusalError(RefusalCode::PartExceedsAllSheets, os.str());
    }
}

/// Every copy, largest inflated area first; ties by part order.
std::vector<int> InitialOrder(const Problem& problem, const Instance& in)
{
    std::vector<int> parts(problem.parts.size());
    std::iota(parts.begin(), parts.end(), 0);
    std::stable_sort(parts.begin(), parts.end(), [&](const int a, const int b) {
        return Area(in.shapes[a].front().outer) > Area(in.shapes[b].front().outer);
    });
    std::vector<int> order;
    for (const int p : parts)
        order.insert(order.end(), problem.parts[p].quantity, p);
    return order;
}

} // namespace

Result Solve(const Problem& problem)
{
    CheckProblem(problem);
    try {
        const Instance in = Prepare(problem);
        CheckEveryPartFits(problem, in);
        NfpCache nfps(in);
        const std::vector<SheetFill> sheets = Decode(in, nfps, InitialOrder(problem, in));

        Solution solution;
        for (size_t i = 0; i < sheets.size(); ++i) {
            solution.sheetTypeIds.push_back(problem.sheetTypes[sheets[i].sheetType()].id);
            for (const Placed& p : sheets[i].placed())
                solution.placements.push_back({problem.parts[p.part].id, static_cast<int>(i), UnitsToMm(p.pos.x),
                                               UnitsToMm(p.pos.y), problem.settings.anglesDeg[p.angle]});
        }
        return solution;
    } catch (const RefusalError& e) {
        return e.refusal;
    }
}

} // namespace kerfnest
