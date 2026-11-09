#include "Nfp.h"

#include <optional>

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

/// A ⊕ (−B) for simple polygons A and B. With ∂A ⊕ ∂(−B) removed, the boundaries of A and B + t
/// do not meet, so one of them contains the other: B + t contains a's first vertex, or A
/// contains B + t and with it B's first vertex. The two translated copies cover exactly those
/// positions, so the union is the whole sum, pockets included.
Paths64 OuterSum(const Path64& a, const Path64& negB)
{
    Paths64 parts = MinkowskiSum(negB, a, true);
    parts.push_back(TranslatePath(a, negB.front().x, negB.front().y));
    parts.push_back(TranslatePath(negB, a.front().x, a.front().y));
    return Union(parts, FillRule::NonZero);
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
    const Paths64 nfp = OuterSum(a.outer, Negated(b.outer));

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
