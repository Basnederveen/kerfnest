#include "Decoder.h"

#include "Refusal.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace kerfnest {
namespace {

/// Fills one sheet with a small beam: each partial sheet branches on its best positions for the
/// next piece, and the best partial sheets survive (more placed area, then whether the next
/// piece still fits, then shorter used length, then the order they were made in).
SheetFill Fill(const Instance& instance, NfpCache& nfps, const int sheetType, const std::vector<int>& remaining, uint64_t& work)
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
            const std::vector<Placed> positions = s.fill.BestPositions(part, kFillBranching, work);
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
                nextFits[i] = !next[i].failed.count(remaining[k + 1]) &&
                              !next[i].fill.BestPositions(remaining[k + 1], 1, work).empty();
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
SheetFill NextSheet(const Instance& instance, NfpCache& nfps, const int policy, const std::vector<int>& remaining, uint64_t& work)
{
    const int types = static_cast<int>(instance.usable.size());
    std::vector<SheetFill> fills;
    if (policy != kMixedSizes) {
        fills.push_back(Fill(instance, nfps, policy, remaining, work));
        if (fills.back().placed().empty()) // parts only another size holds
            fills.clear();
    }
    if (fills.empty() || TakesAll(fills.front(), remaining))
        for (int type = 0; type < types; ++type)
            if (type != policy)
                fills.push_back(Fill(instance, nfps, type, remaining, work));

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

} // namespace

Objective Evaluate(const Instance& instance, const std::vector<SheetFill>& sheets)
{
    Objective o;
    for (const SheetFill& s : sheets)
        o.sheetAreaMm2 += instance.sheetAreaMm2[s.sheetType()];
    const SheetFill& last = sheets.back();
    const Clipper2Lib::Rect64& u = instance.usable[last.sheetType()];
    const bool alongX = u.Width() >= u.Height();
    const double used = static_cast<double>(last.usedLength() - (alongX ? u.left : u.top));
    o.lastSheetUsedMm2 = used * static_cast<double>(alongX ? u.Height() : u.Width()) * 1e-6;
    return o;
}

std::optional<Decoded> DecodeWith(const Instance& instance, NfpCache& nfps, const std::vector<int>& pieces,
                                  const int policy, const uint64_t cap, uint64_t& work, size_t* placedAtCap)
{
    Decoded d;
    d.policy = policy;
    std::vector<int> remaining = pieces;
    while (!remaining.empty()) {
        SheetFill sheet = NextSheet(instance, nfps, policy, remaining, work);
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
        d.remainingAt.push_back(std::move(remaining));
        d.sheets.push_back(std::move(sheet));
        remaining = std::move(next);
        if (cap != 0 && work > cap && !remaining.empty()) {
            if (placedAtCap)
                *placedAtCap = pieces.size() - remaining.size();
            return std::nullopt;
        }
    }
    return d;
}

Decoded Decode(const Instance& instance, NfpCache& nfps, const std::vector<int>& order, const uint64_t cap, uint64_t& work)
{
    std::optional<Decoded> best;
    for (int policy = kMixedSizes; policy < static_cast<int>(instance.usable.size()); ++policy) {
        size_t placed = 0;
        std::optional<Decoded> d = DecodeWith(instance, nfps, order, policy, cap, work, &placed);
        if (!d) {
            if (best)
                break; // a complete nest is in hand; the other policies are out of budget
            throw RefusalError(RefusalCode::NestIncomplete,
                               "the work budget of " + std::to_string(cap) + " units ran out after placing " +
                                   std::to_string(placed) + " of " + std::to_string(order.size()) +
                                   " pieces in the first pass; raise the budget");
        }
        if (!best || Evaluate(instance, d->sheets) < Evaluate(instance, best->sheets))
            best = std::move(d);
        if (instance.usable.size() == 1)
            break; // one size: the fixed policy is the mixed one
    }
    return std::move(*best);
}

} // namespace kerfnest
