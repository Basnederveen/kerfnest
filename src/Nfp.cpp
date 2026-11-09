#include "Nfp.h"

#include <map>
#include <optional>
#include <stdexcept>

using namespace Clipper2Lib;

namespace kerfnest {
namespace {

/// Point reflection: a half turn, so the orientation is kept.
Path64 Negated(const Path64& path)
{
    Path64 out;
    out.reserve(path.size());
    for (const Point64& p : path)
        out.push_back({-p.x, -p.y});
    return out;
}

int64_t Cross(const Point64 u, const Point64 v) { return u.x * v.y - u.y * v.x; }
int64_t Dot(const Point64 u, const Point64 v) { return u.x * v.x + u.y * v.y; }
bool SameDirection(const Point64 u, const Point64 v) { return Cross(u, v) == 0 && Dot(u, v) > 0; }

/// Whether the turn from direction `in` to direction `out` sweeps direction v. The sweep runs
/// counter-clockwise for a left turn and clockwise for a right turn. Ties between a direction of
/// A and one of B are broken by symbolic perturbation: B's directions count as turned a hair
/// counter-clockwise. For an edge of A at a vertex of B (vIsA), the sweep's ends are B's, so v
/// lies a hair clockwise of an end it equals; for an edge of B at a vertex of A, v lies a hair
/// counter-clockwise of it.
bool Sweeps(const Point64 in, const Point64 out, const Point64 v, const bool vIsA)
{
    const bool left = Cross(in, out) > 0;
    const int s = left ? 1 : -1;
    if (s * Cross(in, v) > 0 && s * Cross(v, out) > 0)
        return true;
    // A hair clockwise of the end is inside a left sweep; a hair clockwise of the start is
    // inside a right sweep. Counter-clockwise, the other way round.
    if (SameDirection(v, out))
        return vIsA == left;
    if (SameDirection(v, in))
        return vIsA != left;
    return false;
}

/// A ⊕ B for simple counter-clockwise polygons, by the convolution theorem (Guibas, Ramshaw,
/// Stolfi 1983): the convolution cycles pair every edge of one polygon with each vertex of the
/// other whose turn sweeps the edge's direction, reversed at right turns; the sum is the set of
/// points with a nonzero winding number. Pockets come out as holes. O(n m) cheap tests, but
/// typically O(n + m) segments, where the quad-based sum unions n m quads.
Paths64 ConvolutionSum(const Path64& rawA, const Path64& rawB)
{
    const Path64 a = TrimCollinear(rawA);
    const Path64 b = TrimCollinear(rawB);
    const size_t n = a.size();
    const size_t m = b.size();
    const auto edge = [](const Path64& p, const size_t i) { return p[(i + 1) % p.size()] - p[i]; };
    const auto incoming = [](const Path64& p, const size_t i) { return p[i] - p[(i + p.size() - 1) % p.size()]; };

    // Nodes are vertex pairs (i, j) = a[i] + b[j]; a segment runs between two of them.
    struct Seg {
        size_t from, to;
    };
    std::vector<Seg> segs;
    const auto node = [m](const size_t i, const size_t j) { return i * m + j; };
    for (size_t j = 0; j < m; ++j) {
        const Point64 in = incoming(b, j), out = edge(b, j);
        const bool left = Cross(in, out) > 0;
        for (size_t i = 0; i < n; ++i)
            if (Sweeps(in, out, edge(a, i), true)) {
                const size_t s = node(i, j), t = node((i + 1) % n, j);
                segs.push_back(left ? Seg{s, t} : Seg{t, s});
            }
    }
    for (size_t i = 0; i < n; ++i) {
        const Point64 in = incoming(a, i), out = edge(a, i);
        const bool left = Cross(in, out) > 0;
        for (size_t j = 0; j < m; ++j)
            if (Sweeps(in, out, edge(b, j), false)) {
                const size_t s = node(i, j), t = node(i, (j + 1) % m);
                segs.push_back(left ? Seg{s, t} : Seg{t, s});
            }
    }

    // Every node has as many segments in as out, so walking unused segments closes cycles.
    std::map<size_t, std::vector<size_t>> outgoing;
    std::map<size_t, int> balance;
    for (size_t k = 0; k < segs.size(); ++k) {
        outgoing[segs[k].from].push_back(k);
        ++balance[segs[k].from];
        --balance[segs[k].to];
    }
    for (const auto& [id, inMinusOut] : balance)
        if (inMinusOut != 0)
            throw std::logic_error("convolution: a node has unequal segments in and out");

    const auto point = [&](const size_t id) { return a[id / m] + b[id % m]; };
    Paths64 cycles;
    for (auto& [start, list] : outgoing) {
        while (!list.empty()) {
            Path64 cycle{point(start)};
            size_t at = start;
            do {
                auto& choices = outgoing[at];
                const size_t k = choices.back();
                choices.pop_back();
                at = segs[k].to;
                cycle.push_back(point(at));
            } while (at != start);
            cycle.pop_back();
            cycles.push_back(std::move(cycle));
        }
    }
    return Union(cycles, FillRule::NonZero);
}

/// Depth a probe point keeps from a piece's edges. Edge vertices are rounded intersections and
/// can sit a unit off the true edge; a probe on them could land on the wrong side.
constexpr int64_t kProbeDepthUnits = 2;

/// A point strictly inside the piece (outer with its holes), or none for a sliver thinner than
/// twice the probe depth: such a piece is dropped, which only loses positions.
std::optional<Point64> ProbePoint(const Paths64& piece)
{
    for (const Path64& p : InflatePaths(piece, -static_cast<double>(kProbeDepthUnits), JoinType::Miter, EndType::Polygon))
        if (Area(p) > 0)
            return p.front();
    return std::nullopt;
}

/// Keeps every piece of the tree (outer with its holes) whose probe point passes the test.
template <typename Test>
void CollectPieces(const PolyPath64& node, const Test& inside, Paths64& out)
{
    for (const auto& outer : node) {
        Paths64 piece{outer->Polygon()};
        for (const auto& hole : *outer)
            piece.push_back(hole->Polygon());
        const std::optional<Point64> probe = ProbePoint(piece);
        if (probe && inside(*probe))
            out.insert(out.end(), piece.begin(), piece.end());
        for (const auto& hole : *outer)
            CollectPieces(*hole, inside, out);
    }
}

} // namespace

Paths64 InnerFitInHole(const Path64& hole, const Path64& b)
{
    const Rect64 hb = GetBounds(hole);
    const Rect64 bb = GetBounds(b);
    const Rect64 range(hb.left - bb.left, hb.top - bb.top, hb.right - bb.right, hb.bottom - bb.bottom);
    if (range.left > range.right || range.top > range.bottom)
        return {};

    // Within range, B + t stays inside the hole's bounds. Its boundary meets the hole's only on
    // the ring; between the ring's edges B + t is either inside the hole or not, all the way, so
    // one point per piece decides the piece.
    const Paths64 ring = MinkowskiSum(Negated(b), hole, true);
    Clipper64 clipper;
    clipper.AddSubject({range.AsPath()});
    clipper.AddClip(ring);
    PolyTree64 tree;
    clipper.Execute(ClipType::Difference, FillRule::NonZero, tree);

    Paths64 fit;
    CollectPieces(tree, [&](const Point64 t) {
        return Area(Difference(Paths64{TranslatePath(b, t.x, t.y)}, Paths64{hole}, FillRule::NonZero)) == 0.0;
    }, fit);
    return fit;
}

Paths64 NoFitPolygon(const OrientedShape& a, const OrientedShape& b)
{
    const Paths64 nfp = ConvolutionSum(a.outer, Negated(b.outer));

    Paths64 free;
    for (const Path64& hole : a.holes)
        for (Path64& p : InnerFitInHole(hole, b.outer))
            free.push_back(std::move(p));
    // A inside B's hole at s means B's origin at t = −s.
    for (const Path64& hole : b.holes)
        for (const Path64& p : InnerFitInHole(hole, a.outer))
            free.push_back(Negated(p));

    return free.empty() ? nfp : Difference(nfp, free, FillRule::NonZero);
}

} // namespace kerfnest
