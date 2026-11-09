#include "Checker.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace kerfnest::test {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct V {
    double x, y;
};
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y}; }
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y}; }
V operator*(V a, double s) { return {a.x * s, a.y * s}; }
double Dot(V a, V b) { return a.x * b.x + a.y * b.y; }
double Cross(V a, V b) { return a.x * b.y - a.y * b.x; }
double Len(V a) { return std::hypot(a.x, a.y); }

/// A line (a to b) or a circular arc (centre c, radius r, from angle a0 sweeping sweep, signed).
struct Prim {
    bool arc = false;
    V a{}, b{}, c{};
    double r = 0, a0 = 0, sweep = 0;
};

double Sweep(const Segment& s)
{
    if (s.type == SegmentType::Circle)
        return s.clockwise ? -2 * kPi : 2 * kPi;
    const double a0 = std::atan2(s.start.y - s.center.y, s.start.x - s.center.x);
    const double a1 = std::atan2(s.end.y - s.center.y, s.end.x - s.center.x);
    return s.clockwise ? -std::fmod(a0 - a1 + 4 * kPi, 2 * kPi) : std::fmod(a1 - a0 + 4 * kPi, 2 * kPi);
}

Prim ToPrim(const Segment& s)
{
    Prim p;
    p.a = {s.start.x, s.start.y};
    p.b = {s.end.x, s.end.y};
    if (s.type != SegmentType::Line) {
        p.arc = true;
        p.c = {s.center.x, s.center.y};
        p.r = Len(p.a - p.c);
        p.a0 = std::atan2(p.a.y - p.c.y, p.a.x - p.c.x);
        p.sweep = Sweep(s);
    }
    return p;
}

bool OnArcSpan(const Prim& p, const V q)
{
    if (std::abs(p.sweep) >= 2 * kPi - 1e-12)
        return true;
    const double th = std::atan2(q.y - p.c.y, q.x - p.c.x);
    const double d = p.sweep > 0 ? std::fmod(th - p.a0 + 4 * kPi, 2 * kPi) : std::fmod(p.a0 - th + 4 * kPi, 2 * kPi);
    return d <= std::abs(p.sweep) + 1e-12;
}

double DistPointLine(const V q, const V a, const V b)
{
    const V ab = b - a;
    const double t = std::clamp(Dot(q - a, ab) / Dot(ab, ab), 0.0, 1.0);
    return Len(q - (a + ab * t));
}

double DistPoint(const V q, const Prim& p)
{
    if (!p.arc)
        return DistPointLine(q, p.a, p.b);
    const double d = Len(q - p.c);
    if (d > 0 && OnArcSpan(p, q))
        return std::abs(d - p.r);
    return std::min(Len(q - p.a), Len(q - p.b));
}

/// Points where the line through a, b meets the circle (c, r), with their line parameter.
std::vector<std::pair<double, V>> LineCircle(const V a, const V b, const V c, const double r)
{
    const V d = b - a;
    const V f = a - c;
    const double A = Dot(d, d), B = 2 * Dot(f, d), C = Dot(f, f) - r * r;
    const double disc = B * B - 4 * A * C;
    if (disc < 0)
        return {};
    const double s = std::sqrt(disc);
    std::vector<std::pair<double, V>> out;
    for (const double t : {(-B - s) / (2 * A), (-B + s) / (2 * A)})
        out.push_back({t, a + d * t});
    return out;
}

bool Crosses(const Prim& p, const Prim& q)
{
    if (!p.arc && !q.arc) {
        const double d1 = Cross(q.b - q.a, p.a - q.a), d2 = Cross(q.b - q.a, p.b - q.a);
        const double d3 = Cross(p.b - p.a, q.a - p.a), d4 = Cross(p.b - p.a, q.b - p.a);
        return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
    }
    if (p.arc && !q.arc)
        return Crosses(q, p);
    if (!p.arc) {
        for (const auto& [t, x] : LineCircle(p.a, p.b, q.c, q.r))
            if (t >= 0 && t <= 1 && OnArcSpan(q, x))
                return true;
        return false;
    }
    const double d = Len(q.c - p.c);
    if (d == 0 || d > p.r + q.r || d < std::abs(p.r - q.r))
        return false;
    const double a = (p.r * p.r - q.r * q.r + d * d) / (2 * d);
    const double h = std::sqrt(std::max(0.0, p.r * p.r - a * a));
    const V u = (q.c - p.c) * (1 / d);
    const V m = p.c + u * a;
    const V n{-u.y, u.x};
    for (const V x : {m + n * h, m - n * h})
        if (OnArcSpan(p, x) && OnArcSpan(q, x))
            return true;
    return false;
}

/// Exact distance between two primitives: 0 when they cross, else the smallest of the endpoint
/// distances and the interior points where the connecting segment is normal to both.
double Dist(const Prim& p, const Prim& q)
{
    if (Crosses(p, q))
        return 0.0;
    double d = std::min({DistPoint(p.a, q), DistPoint(p.b, q), DistPoint(q.a, p), DistPoint(q.b, p)});
    if (p.arc && !q.arc)
        return std::min(d, Dist(q, p));
    if (!p.arc && q.arc) {
        const V ab = p.b - p.a;
        const double t = Dot(q.c - p.a, ab) / Dot(ab, ab);
        if (t > 0 && t < 1) {
            const V foot = p.a + ab * t;
            const double fd = Len(foot - q.c);
            if (fd > 0)
                for (const double s : {1.0, -1.0}) {
                    const V x = q.c + (foot - q.c) * (s * q.r / fd);
                    if (OnArcSpan(q, x))
                        d = std::min(d, DistPointLine(x, p.a, p.b));
                }
        }
    }
    if (p.arc && q.arc) {
        const double cd = Len(q.c - p.c);
        if (cd > 0) {
            const V u = (q.c - p.c) * (1 / cd);
            for (const double s1 : {1.0, -1.0})
                for (const double s2 : {1.0, -1.0}) {
                    const V x1 = p.c + u * (s1 * p.r);
                    const V x2 = q.c + u * (s2 * q.r);
                    if (OnArcSpan(p, x1) && OnArcSpan(q, x2))
                        d = std::min(d, Len(x1 - x2));
                }
        } else {
            d = std::min(d, std::abs(p.r - q.r)); // concentric: conservative (spans ignored)
        }
    }
    return d;
}

/// Even-odd point in loop, arcs as fine chords. Only used for points far from the loop.
bool InsideLoop(const V q, const Loop& loop)
{
    std::vector<V> pts;
    for (const Segment& s : loop) {
        const Prim p = ToPrim(s);
        pts.push_back(p.a);
        if (p.arc) {
            const int n = std::max(8, static_cast<int>(std::abs(p.sweep) / 0.01));
            for (int k = 1; k < n; ++k) {
                const double t = p.a0 + p.sweep * k / n;
                pts.push_back({p.c.x + p.r * std::cos(t), p.c.y + p.r * std::sin(t)});
            }
        }
    }
    bool inside = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
        if ((pts[i].y > q.y) != (pts[j].y > q.y) &&
            q.x < (pts[j].x - pts[i].x) * (q.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x)
            inside = !inside;
    return inside;
}

bool OnMaterial(const V q, const std::vector<Loop>& loops)
{
    if (!InsideLoop(q, loops.front()))
        return false;
    for (size_t i = 1; i < loops.size(); ++i)
        if (InsideLoop(q, loops[i]))
            return false;
    return true;
}

Point Rotate(const Point p, const int angleDeg)
{
    double c, s;
    switch (((angleDeg % 360) + 360) % 360) {
    case 0: c = 1, s = 0; break;
    case 90: c = 0, s = 1; break;
    case 180: c = -1, s = 0; break;
    case 270: c = 0, s = -1; break;
    default: c = std::cos(angleDeg * kPi / 180), s = std::sin(angleDeg * kPi / 180);
    }
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}

std::string Name(const Placement& p, const int copy)
{
    std::ostringstream os;
    os << "'" << p.partId << "' #" << copy << " on sheet " << p.sheetIndex;
    return os.str();
}

} // namespace

std::vector<Loop> PlacedLoops(const Part& part, const Placement& placement)
{
    std::vector<Loop> out;
    for (const Loop& loop : part.loops) {
        Loop moved;
        for (Segment s : loop) {
            for (Point* p : {&s.start, &s.end, &s.center}) {
                const Point r = Rotate(*p, placement.angleDeg);
                *p = {r.x + placement.x, r.y + placement.y};
            }
            moved.push_back(s);
        }
        out.push_back(moved);
    }
    return out;
}

BoundsMm LoopBounds(const std::vector<Loop>& loops)
{
    BoundsMm b{1e300, 1e300, -1e300, -1e300};
    const auto include = [&](const V q) {
        b.minX = std::min(b.minX, q.x), b.minY = std::min(b.minY, q.y);
        b.maxX = std::max(b.maxX, q.x), b.maxY = std::max(b.maxY, q.y);
    };
    for (const Loop& loop : loops)
        for (const Segment& s : loop) {
            const Prim p = ToPrim(s);
            include(p.a);
            include(p.b);
            if (p.arc)
                for (const V dir : {V{1, 0}, V{0, 1}, V{-1, 0}, V{0, -1}}) {
                    const V x = p.c + dir * p.r;
                    if (OnArcSpan(p, x))
                        include(x);
                }
        }
    return b;
}

CheckReport CheckNest(const Problem& problem, const Solution& solution)
{
    CheckReport report;
    std::map<std::string, const Part*> parts;
    std::map<std::string, int> placed;
    for (const Part& p : problem.parts)
        parts[p.id] = &p;

    struct Copy {
        std::string name;
        std::vector<Loop> loops;
        BoundsMm bounds;
    };
    std::map<int, std::vector<Copy>> bySheet;
    const double margin = problem.settings.marginMm;
    for (const Placement& pl : solution.placements) {
        if (!parts.count(pl.partId) || pl.sheetIndex < 0 || pl.sheetIndex >= static_cast<int>(solution.sheetTypeIds.size())) {
            report.problems.push_back("placement of unknown part or sheet: '" + pl.partId + "'");
            continue;
        }
        const auto& allowed = problem.settings.anglesDeg;
        if (std::find(allowed.begin(), allowed.end(), pl.angleDeg) == allowed.end())
            report.problems.push_back(Name(pl, placed[pl.partId]) + " at an angle not allowed");
        Copy c{Name(pl, placed[pl.partId]++), PlacedLoops(*parts[pl.partId], pl), {}};
        c.bounds = LoopBounds(c.loops);
        const SheetType* st = nullptr;
        for (const SheetType& t : problem.sheetTypes)
            if (t.id == solution.sheetTypeIds[pl.sheetIndex])
                st = &t;
        if (c.bounds.minX < margin - kCheckToleranceMm || c.bounds.minY < margin - kCheckToleranceMm ||
            c.bounds.maxX > st->width - margin + kCheckToleranceMm || c.bounds.maxY > st->height - margin + kCheckToleranceMm)
            report.problems.push_back(c.name + " is outside the sheet margin");
        bySheet[pl.sheetIndex].push_back(std::move(c));
    }
    for (const Part& p : problem.parts)
        if (placed[p.id] != p.quantity)
            report.problems.push_back("'" + p.id + "' placed " + std::to_string(placed[p.id]) + " of " + std::to_string(p.quantity));

    const double spacing = problem.settings.spacingMm;
    for (const auto& [sheet, copies] : bySheet) {
        for (size_t i = 0; i < copies.size(); ++i) {
            for (size_t j = i + 1; j < copies.size(); ++j) {
                const Copy& a = copies[i];
                const Copy& b = copies[j];
                if (a.bounds.maxX + spacing < b.bounds.minX || b.bounds.maxX + spacing < a.bounds.minX ||
                    a.bounds.maxY + spacing < b.bounds.minY || b.bounds.maxY + spacing < a.bounds.minY)
                    continue;
                double gap = 1e300;
                for (const Loop& la : a.loops)
                    for (const Segment& sa : la)
                        for (const Loop& lb : b.loops)
                            for (const Segment& sb : lb)
                                gap = std::min(gap, Dist(ToPrim(sa), ToPrim(sb)));
                report.minGapMm = std::min(report.minGapMm, gap);
                const V pa{a.loops.front().front().start.x, a.loops.front().front().start.y};
                const V pb{b.loops.front().front().start.x, b.loops.front().front().start.y};
                if (gap > 0 && (OnMaterial(pa, b.loops) || OnMaterial(pb, a.loops)))
                    report.problems.push_back(a.name + " and " + b.name + " overlap (one inside the other's material)");
                else if (gap < spacing - kCheckToleranceMm)
                    report.problems.push_back(a.name + " and " + b.name + " are " + std::to_string(gap) + " mm apart");
            }
        }
    }
    return report;
}

} // namespace kerfnest::test
