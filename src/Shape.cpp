#include "Shape.h"

#include "Refusal.h"
#include "Units.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

using namespace Clipper2Lib;

namespace kerfnest {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMiterLimit = 2.0; // past it Clipper squares the corner off at the offset distance: still outside the round offset

struct Rotation {
    double c = 1.0;
    double s = 0.0;
};

// Quarter turns are exact, so 90-degree nests rotate without rounding.
Rotation RotationFor(const int angleDeg)
{
    switch (((angleDeg % 360) + 360) % 360) {
    case 0: return {1.0, 0.0};
    case 90: return {0.0, 1.0};
    case 180: return {-1.0, 0.0};
    case 270: return {0.0, -1.0};
    default: {
        const double a = angleDeg * kPi / 180.0;
        return {std::cos(a), std::sin(a)};
    }
    }
}

Point Rotated(const Point p, const Rotation r) { return {p.x * r.c - p.y * r.s, p.x * r.s + p.y * r.c}; }

Point64 ToUnits(const Point p, const Rotation r)
{
    const Point q = Rotated(p, r);
    return {RoundUnits(q.x), RoundUnits(q.y)};
}

double Radius(const Point p, const Point c) { return std::hypot(p.x - c.x, p.y - c.y); }

/// Signed sweep of an arc or circle, counter-clockwise positive.
double Sweep(const Segment& s)
{
    if (s.type == SegmentType::Circle)
        return s.clockwise ? -2 * kPi : 2 * kPi;
    const double a0 = std::atan2(s.start.y - s.center.y, s.start.x - s.center.x);
    const double a1 = std::atan2(s.end.y - s.center.y, s.end.x - s.center.x);
    if (s.clockwise)
        return -std::fmod(a0 - a1 + 4 * kPi, 2 * kPi);
    return std::fmod(a1 - a0 + 4 * kPi, 2 * kPi);
}

/// Signed area enclosed by the loop, arcs exact. Counter-clockwise positive.
double LoopArea(const Loop& loop)
{
    double area = 0.0;
    for (const Segment& s : loop) {
        area += 0.5 * (s.start.x * s.end.y - s.end.x * s.start.y);
        if (s.type != SegmentType::Line) {
            const double r = Radius(s.start, s.center);
            const double th = Sweep(s);
            area += 0.5 * r * r * (th - std::sin(th));
        }
    }
    return area;
}

Loop Reversed(const Loop& loop)
{
    Loop out(loop.rbegin(), loop.rend());
    for (Segment& s : out) {
        std::swap(s.start, s.end);
        s.clockwise = !s.clockwise;
    }
    return out;
}

enum class ArcPoints {
    Chords,        ///< vertices on the arc: the polygon edge runs on the centre side of the arc
    Circumscribed, ///< edges tangent to the arc: the polygon edge runs on the far side of the arc
    FromFit,       ///< whichever keeps the polygon on the requested side of the loop
};

/// Counter-clockwise polygon of the loop rotated by angleDeg. Arcs within kArcToleranceMm.
Path64 Polygonise(const Loop& input, const int angleDeg, const Fit fit, const ArcPoints mode)
{
    const Loop loop = LoopArea(input) < 0 ? Reversed(input) : input;
    const Rotation rot = RotationFor(angleDeg);

    Path64 path;
    for (const Segment& s : loop) {
        path.push_back(ToUnits(s.start, rot));
        if (s.type == SegmentType::Line)
            continue;

        const double th = Sweep(s);
        // In a counter-clockwise loop the interior is on the left: a counter-clockwise arc has
        // its centre on the interior side, so the arc bulges out of the region.
        const bool bulgesOut = th > 0;
        const bool circumscribe = mode == ArcPoints::FromFit ? bulgesOut == (fit == Fit::Contain)
                                                             : mode == ArcPoints::Circumscribed;
        const double rs = Radius(s.start, s.center);
        const double re = Radius(s.end, s.center);
        const double r = circumscribe ? std::max(rs, re) : std::min(rs, re);
        const double maxStep = std::min(kPi / 2, 2 * std::acos(r / (r + kArcToleranceMm)));
        const int n = std::max(1, static_cast<int>(std::ceil(std::abs(th) / maxStep)));
        const double step = th / n;
        const double a0 = std::atan2(s.start.y - s.center.y, s.start.x - s.center.x);

        if (circumscribe) {
            // Tangent lines at a and a + step meet at a + step/2, at r / cos(step/2).
            const double rv = r / std::cos(step / 2);
            for (int k = 0; k < n; ++k) {
                const double a = a0 + (k + 0.5) * step;
                path.push_back(ToUnits({s.center.x + rv * std::cos(a), s.center.y + rv * std::sin(a)}, rot));
            }
        } else {
            for (int k = 1; k < n; ++k) {
                const double a = a0 + k * step;
                path.push_back(ToUnits({s.center.x + r * std::cos(a), s.center.y + r * std::sin(a)}, rot));
            }
        }
    }
    return path;
}

[[noreturn]] void Refuse(const Part& part, const std::string& what)
{
    throw RefusalError(RefusalCode::InvalidContour, "part '" + part.id + "': " + what);
}

std::string Mm(const double v)
{
    std::ostringstream os;
    os << v;
    return os.str() + " mm";
}

bool IsSimple(const Path64& path)
{
    const Paths64 merged = Union(Paths64{path}, FillRule::NonZero);
    return merged.size() == 1 && std::abs(Area(merged[0]) - Area(path)) < 1.0;
}

Paths64 Offset(const Paths64& paths, const int64_t delta)
{
    return InflatePaths(paths, static_cast<double>(delta), JoinType::Miter, EndType::Polygon, kMiterLimit);
}

/// Exact bounds of the loop rotated by angleDeg (arcs through their extreme points), in units
/// rounded outward.
Rect64 TrueBounds(const Loop& loop, const int angleDeg)
{
    const Rotation rot = RotationFor(angleDeg);
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    const auto include = [&](const Point p) {
        minX = std::min(minX, p.x), minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x), maxY = std::max(maxY, p.y);
    };
    for (const Segment& s : loop) {
        include(Rotated(s.start, rot));
        if (s.type == SegmentType::Line)
            continue;
        const Point c = Rotated(s.center, rot);
        const Point start = Rotated(s.start, rot);
        const double r = Radius(start, c);
        const double th = Sweep(s);
        const double a0 = std::atan2(start.y - c.y, start.x - c.x);
        for (int k = 0; k < 4; ++k) {
            const double a = k * kPi / 2;
            const double along = th > 0 ? std::fmod(a - a0 + 4 * kPi, 2 * kPi) : std::fmod(a0 - a + 4 * kPi, 2 * kPi);
            if (along <= std::abs(th))
                include({c.x + r * std::cos(a), c.y + r * std::sin(a)});
        }
    }
    return Rect64(FloorUnits(minX), FloorUnits(minY), CeilUnits(maxX), CeilUnits(maxY));
}

} // namespace

double NetAreaMm2(const Part& part)
{
    double area = std::abs(LoopArea(part.loops.front()));
    for (size_t i = 1; i < part.loops.size(); ++i)
        area -= std::abs(LoopArea(part.loops[i]));
    return area;
}

Path64 PolygoniseLoop(const Loop& loop, const int angleDeg, const Fit fit)
{
    return Polygonise(loop, angleDeg, fit, ArcPoints::FromFit);
}

void ValidateLoops(const Part& part)
{
    if (part.loops.empty())
        Refuse(part, "has no loops");

    std::vector<Path64> onCurve;
    for (size_t li = 0; li < part.loops.size(); ++li) {
        const Loop& loop = part.loops[li];
        if (loop.empty())
            Refuse(part, "loop " + std::to_string(li) + " is empty");
        for (size_t i = 0; i < loop.size(); ++i) {
            const size_t next = (i + 1) % loop.size();
            const Point a = loop[i].end;
            const Point b = loop[next].start;
            if (a.x != b.x || a.y != b.y)
                Refuse(part, "loop " + std::to_string(li) + " is open between segment " + std::to_string(i) +
                                 " and segment " + std::to_string(next) + " (gap " + Mm(std::hypot(a.x - b.x, a.y - b.y)) + ")");
        }
        // Vertices on the true curve: crossings found here are crossings of the contour itself.
        onCurve.push_back(Polygonise(loop, 0, Fit::Contain, ArcPoints::Chords));
        if (!IsSimple(onCurve.back()))
            Refuse(part, "loop " + std::to_string(li) + " crosses itself");
    }

    const Path64& outer = onCurve.front();
    double holesArea = 0.0;
    for (size_t li = 1; li < onCurve.size(); ++li) {
        if (Area(Difference(Paths64{onCurve[li]}, Paths64{outer}, FillRule::NonZero)) > 0.0)
            Refuse(part, "hole loop " + std::to_string(li) + " is not inside the outer loop");
        holesArea += Area(onCurve[li]);
    }
    const Paths64 holes(onCurve.begin() + 1, onCurve.end());
    if (std::abs(Area(Union(holes, FillRule::NonZero)) - holesArea) >= 1.0)
        Refuse(part, "two holes overlap");
}

OrientedShape BuildOrientedShape(const Part& part, const int angleDeg, const double spacingMm)
{
    const int64_t delta = CeilUnits(spacingMm / 2) + kRoundingGuardUnits;

    OrientedShape shape;
    shape.angleDeg = angleDeg;

    // Inflating can close a narrow inlet into an enclosed pocket: that pocket is usable like a hole.
    for (Path64& p : Offset(Paths64{PolygoniseLoop(part.loops.front(), angleDeg, Fit::Contain)}, delta)) {
        if (Area(p) > 0) {
            if (!shape.outer.empty())
                throw std::logic_error("inflating part '" + part.id + "' gave two outer paths");
            shape.outer = std::move(p);
        } else {
            std::reverse(p.begin(), p.end());
            shape.holes.push_back(std::move(p));
        }
    }

    Paths64 holes;
    for (size_t li = 1; li < part.loops.size(); ++li)
        holes.push_back(PolygoniseLoop(part.loops[li], angleDeg, Fit::Inside));
    for (Path64& p : Offset(holes, -delta))
        shape.holes.push_back(std::move(p));

    shape.bounds = GetBounds(shape.outer);
    shape.trueBounds = TrueBounds(part.loops.front(), angleDeg);
    return shape;
}

void FillUnusableHoles(std::vector<std::vector<OrientedShape>>& shapes)
{
    struct Size {
        int64_t w, h;
        double area;
    };
    std::vector<Size> sizes;
    for (const auto& part : shapes)
        for (const OrientedShape& s : part)
            sizes.push_back({s.bounds.Width(), s.bounds.Height(), Area(s.outer)});

    for (auto& part : shapes) {
        for (OrientedShape& s : part) {
            const auto unusable = [&](const Path64& hole) {
                const Rect64 b = GetBounds(hole);
                const double area = Area(hole);
                return std::none_of(sizes.begin(), sizes.end(), [&](const Size& z) {
                    return z.w <= b.Width() && z.h <= b.Height() && z.area <= area;
                });
            };
            s.holes.erase(std::remove_if(s.holes.begin(), s.holes.end(), unusable), s.holes.end());
        }
    }
}

} // namespace kerfnest
