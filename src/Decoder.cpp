#include "Decoder.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>

namespace kerfnest {
namespace {

/// A per-sheet choice by utilisation instead of one fixed sheet type.
constexpr int kMixedSizes = -1;

/// Fills one sheet with a small beam: each partial sheet branches on its best positions for the
/// next piece, and the best partial sheets survive (more placed area, then shorter used length,
/// then the order they were made in).
SheetFill Fill(const Instance& instance, NfpCache& nfps, const int sheetType, const std::vector<int>& remaining)
{
    struct State {
        SheetFill fill;
        std::set<int> failed; ///< free space only shrinks: a part that did not fit never will
    };
    std::vector<State> beam{{SheetFill(instance, nfps, sheetType), {}}};
    for (size_t k = 0; k < remaining.size(); ++k) {
        const int part = remaining[k];
        std::vector<State> next;
        for (State& s : beam) {
            if (s.failed.count(part)) {
                next.push_back(std::move(s));
                continue;
            }
            const std::vector<Placed> positions = s.fill.BestPositions(part, kFillBranching);
            if (positions.empty()) {
                s.failed.insert(part);
                next.push_back(std::move(s));
                continue;
            }
            for (size_t c = 0; c + 1 < positions.size(); ++c) {
                State child = s;
                child.fill.Place(positions[c]);
                next.push_back(std::move(child));
            }
            s.fill.Place(positions.back());
            next.push_back(std::move(s));
        }
        // One step of lookahead: a partial sheet on which the next piece still fits beats one where
        // the locally tightest spot (a disc in the valley between two others) blocked it.
        std::vector<bool> nextFits(next.size(), false);
        if (next.size() > 1 && k + 1 < remaining.size())
            for (size_t i = 0; i < next.size(); ++i)
                nextFits[i] = !next[i].failed.count(remaining[k + 1]) && !next[i].fill.BestPositions(remaining[k + 1], 1).empty();
        std::vector<size_t> rank(next.size());
        std::iota(rank.begin(), rank.end(), 0);
        std::stable_sort(rank.begin(), rank.end(), [&](const size_t a, const size_t b) {
            if (next[a].fill.netAreaMm2() != next[b].fill.netAreaMm2())
                return next[a].fill.netAreaMm2() > next[b].fill.netAreaMm2();
            if (nextFits[a] != nextFits[b])
                return static_cast<bool>(nextFits[a]);
            return next[a].fill.usedLength() < next[b].fill.usedLength();
        });
        beam.clear();
        for (size_t i = 0; i < std::min(kFillBeamWidth, rank.size()); ++i)
            beam.push_back(std::move(next[rank[i]]));
    }
    return std::move(beam.front().fill);
}

bool TakesAll(const SheetFill& fill, const std::vector<int>& remaining) { return fill.placed().size() == remaining.size(); }

/// The next sheet under a sheet-size policy.
SheetFill NextSheet(const Instance& instance, NfpCache& nfps, const int policy, const std::vector<int>& remaining)
{
    const int types = static_cast<int>(instance.usable.size());
    std::vector<SheetFill> fills;
    if (policy != kMixedSizes) {
        fills.push_back(Fill(instance, nfps, policy, remaining));
        if (fills.back().placed().empty()) // parts only another size holds
            fills.clear();
    }
    if (fills.empty() || TakesAll(fills.front(), remaining))
        for (int type = 0; type < types; ++type)
            if (type != policy)
                fills.push_back(Fill(instance, nfps, type, remaining));

    const auto area = [&](const SheetFill& f) { return instance.sheetAreaMm2[f.sheetType()]; };
    std::optional<size_t> best;
    for (size_t i = 0; i < fills.size(); ++i) {
        const SheetFill& f = fills[i];
        if (f.placed().empty())
            continue;
        if (!best) {
            best = i;
            continue;
        }
        const SheetFill& b = fills[*best];
        const bool all = TakesAll(f, remaining), bestAll = TakesAll(b, remaining);
        if (all && bestAll) {
            if (area(f) < area(b))
                best = i;
        } else if (all != bestAll) {
            // A size that finishes the nest beats one that does not, unless the other is the fixed
            // policy's own size (its sheet is chosen already; only a smaller finishing size replaces it).
            if (all && (policy == kMixedSizes || area(f) <= area(b)))
                best = i;
        } else if (policy == kMixedSizes) {
            // Utilisation as products: a / A > b / B  <=>  a * B > b * A; ties to the smaller sheet.
            const double mine = f.netAreaMm2() * area(b), theirs = b.netAreaMm2() * area(f);
            if (mine > theirs || (mine == theirs && area(f) < area(b)))
                best = i;
        }
    }
    if (!best)
        throw std::logic_error("decoder: no sheet type takes any remaining piece");
    return std::move(fills[*best]);
}

std::vector<SheetFill> DecodeWith(const Instance& instance, NfpCache& nfps, const std::vector<int>& order, const int policy)
{
    std::vector<SheetFill> sheets;
    std::vector<int> remaining = order;
    while (!remaining.empty()) {
        SheetFill sheet = NextSheet(instance, nfps, policy, remaining);
        // Copies of a part are interchangeable: consume the first remaining ones.
        std::vector<int> placedCount(instance.shapes.size(), 0);
        for (const Placed& p : sheet.placed())
            ++placedCount[p.part];
        std::vector<int> next;
        for (const int part : remaining) {
            if (placedCount[part] > 0)
                --placedCount[part];
            else
                next.push_back(part);
        }
        remaining = std::move(next);
        sheets.push_back(std::move(sheet));
    }
    return sheets;
}

} // namespace

std::vector<SheetFill> Decode(const Instance& instance, NfpCache& nfps, const std::vector<int>& order)
{
    const auto totalArea = [&](const std::vector<SheetFill>& sheets) {
        double a = 0.0;
        for (const SheetFill& s : sheets)
            a += instance.sheetAreaMm2[s.sheetType()];
        return a;
    };
    std::optional<std::vector<SheetFill>> best;
    for (int policy = kMixedSizes; policy < static_cast<int>(instance.usable.size()); ++policy) {
        std::vector<SheetFill> sheets = DecodeWith(instance, nfps, order, policy);
        if (!best || totalArea(sheets) < totalArea(*best) ||
            (totalArea(sheets) == totalArea(*best) && sheets.back().usedLength() < best->back().usedLength()))
            best.emplace(std::move(sheets));
        if (instance.usable.size() == 1)
            break; // one size: the fixed policy is the mixed one
    }
    return std::move(*best);
}

} // namespace kerfnest
