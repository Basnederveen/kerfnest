#include "Placer.h"

#include "Nfp.h"

#include <algorithm>
#include <tuple>
#include <utility>

using namespace Clipper2Lib;

namespace kerfnest {
namespace {

/// Candidate positions closer than this (true bounds' corner) count as one option.
constexpr int64_t kSameSpotUnits = 1000;

/// The boundary turns by more than this at a corner: two contacts meet there. Vertices along a
/// polygonised arc turn far less.
constexpr double kCornerSin = 0.17364817766693; // sin(10 degrees)

struct Candidate {
    Point64 pos;
    bool corner = false;
};

bool IsCorner(const Point64 prev, const Point64 at, const Point64 next)
{
    const double ax = static_cast<double>(at.x - prev.x), ay = static_cast<double>(at.y - prev.y);
    const double bx = static_cast<double>(next.x - at.x), by = static_cast<double>(next.y - at.y);
    const double cross = ax * by - ay * bx, dot = ax * bx + ay * by;
    return dot < 0 || std::abs(cross) > kCornerSin * std::hypot(ax, ay) * std::hypot(bx, by);
}

/// Feasible positions on the boundary of: the range of positions that keep the part on the
/// sheet, minus the forbidden region. Every two-contact position is one of them. A range of
/// zero width (an exact fit) is a segment or a single point.
std::vector<Candidate> Candidates(const Rect64& range, const Paths64& forbidden)
{
    std::vector<Candidate> out;
    if (range.Width() > 0 && range.Height() > 0) {
        for (const Path64& p : forbidden.empty() ? Paths64{range.AsPath()} : Difference(Paths64{range.AsPath()}, forbidden, FillRule::NonZero))
            for (size_t i = 0; i < p.size(); ++i)
                out.push_back({p[i], IsCorner(p[(i + p.size() - 1) % p.size()], p[i], p[(i + 1) % p.size()])});
        return out;
    }
    const Point64 a(range.left, range.top);
    const Point64 b(range.right, range.bottom);
    if (a == b) {
        int winding = 0;
        for (const Path64& p : forbidden) {
            const PointInPolygonResult r = PointInPolygon(a, p);
            if (r == PointInPolygonResult::IsOn)
                return {{a, true}};
            if (r == PointInPolygonResult::IsInside)
                winding += IsPositive(p) ? 1 : -1;
        }
        return winding > 0 ? std::vector<Candidate>{} : std::vector<Candidate>{{a, true}};
    }
    Clipper64 clipper;
    clipper.AddOpenSubject({Path64{a, b}});
    clipper.AddClip(forbidden);
    Paths64 closed, open;
    clipper.Execute(ClipType::Difference, FillRule::NonZero, closed, open);
    for (const Path64& p : open) {
        out.push_back({p.front(), true});
        out.push_back({p.back(), true});
    }
    return out;
}

bool Contains(const Rect64& outer, const Rect64& inner)
{
    return inner.left >= outer.left && inner.right <= outer.right && inner.top >= outer.top && inner.bottom <= outer.bottom;
}

Rect64 Shifted(const Rect64& r, const Point64 t) { return Rect64(r.left + t.x, r.top + t.y, r.right + t.x, r.bottom + t.y); }

uint64_t Vertices(const Paths64& paths)
{
    uint64_t n = 0;
    for (const Path64& p : paths)
        n += p.size();
    return n;
}

} // namespace

const Paths64& NfpCache::Get(const int partA, const int angleA, const int partB, const int angleB)
{
    const std::array<int, 4> key{partA, angleA, partB, angleB};
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = nfps_.find(key);
        if (it != nfps_.end())
            return it->second;
    }
    Paths64 nfp = NoFitPolygon(instance_.shapes[partA][angleA], instance_.shapes[partB][angleB]);
    const std::lock_guard<std::mutex> lock(mutex_);
    return nfps_.emplace(key, std::move(nfp)).first->second; // a thread that was first keeps its copy
}

SheetFill::SheetFill(const Instance& instance, NfpCache& nfps, const int sheetType)
    : instance_(instance), nfps_(nfps), sheetType_(sheetType)
{
    const Rect64& u = instance_.usable[sheetType];
    lengthAlongX_ = u.Width() >= u.Height();
    usedLength_ = lengthAlongX_ ? u.left : u.top;
}

const Paths64& SheetFill::ForbiddenFor(const int part, const int angle, uint64_t& work)
{
    Forbidden& f = forbidden_[{part, angle}];
    if (f.covered < placed_.size()) {
        Paths64 all = std::move(f.region);
        for (; f.covered < placed_.size(); ++f.covered) {
            const Placed& p = placed_[f.covered];
            for (const Path64& path : nfps_.Get(p.part, p.angle, part, angle))
                all.push_back(TranslatePath(path, p.pos.x, p.pos.y));
        }
        work += Vertices(all);
        f.region = Union(all, FillRule::NonZero);
    }
    return f.region;
}

std::vector<Placed> SheetFill::BestPositions(const int part, const size_t count, uint64_t& work)
{
    using Score = std::tuple<bool, int64_t, int64_t, int64_t, int>;
    struct Option {
        Score score;
        Placed placed;
        bool corner;
    };
    std::vector<Option> options;
    const Rect64& u = instance_.usable[sheetType_];

    for (int angle = 0; angle < static_cast<int>(instance_.shapes[part].size()); ++angle) {
        const OrientedShape& shape = instance_.shapes[part][angle];
        const Rect64& tb = shape.trueBounds;
        const Rect64 range(u.left - tb.left, u.top - tb.top, u.right - tb.right, u.bottom - tb.bottom);
        if (range.left > range.right || range.top > range.bottom)
            continue;

        const Paths64& forbidden = ForbiddenFor(part, angle, work);
        work += Vertices(forbidden) + 4;
        for (const Candidate& c : Candidates(range, forbidden)) {
            const Rect64 placedTrue = Shifted(tb, c.pos);
            const Rect64 placedInflated = Shifted(shape.bounds, c.pos);
            const bool inHole = std::any_of(holeBounds_.begin(), holeBounds_.end(),
                                            [&](const Rect64& h) { return Contains(h, placedInflated); });
            const int64_t length = std::max(usedLength_, lengthAlongX_ ? placedTrue.right : placedTrue.bottom);
            const int64_t across = lengthAlongX_ ? placedTrue.top : placedTrue.left;
            const int64_t along = lengthAlongX_ ? placedTrue.left : placedTrue.top;
            options.push_back({Score{!inHole, length, across, along, angle}, Placed{part, angle, c.pos}, c.corner});
        }
    }
    std::stable_sort(options.begin(), options.end(), [](const Option& a, const Option& b) { return a.score < b.score; });

    // The best position first; the alternatives are corners only (points along a polygonised
    // arc are the same contact a hair further on), and positions whose true bounds start within
    // kSameSpotUnits are one option (a disc at 0 and 90 degrees, a corner found from two sides).
    std::vector<Placed> out;
    std::vector<Point64> taken;
    for (const auto& [score, p, corner] : options) {
        if (out.size() == count)
            break;
        if (!out.empty() && !corner)
            continue;
        const Rect64 bounds = Shifted(instance_.shapes[part][p.angle].trueBounds, p.pos);
        const bool same = std::any_of(taken.begin(), taken.end(), [&](const Point64 q) {
            return std::abs(q.x - bounds.left) < kSameSpotUnits && std::abs(q.y - bounds.top) < kSameSpotUnits;
        });
        if (same)
            continue;
        taken.push_back({bounds.left, bounds.top});
        out.push_back(p);
    }
    return out;
}

void SheetFill::Place(const Placed& p)
{
    const OrientedShape& shape = instance_.shapes[p.part][p.angle];
    const Rect64 placedTrue = Shifted(shape.trueBounds, p.pos);
    placed_.push_back(p);
    for (const Path64& h : shape.holes)
        holeBounds_.push_back(Shifted(GetBounds(h), p.pos));
    usedLength_ = std::max(usedLength_, lengthAlongX_ ? placedTrue.right : placedTrue.bottom);
    netAreaMm2_ += instance_.netAreaMm2[p.part];
}

} // namespace kerfnest
