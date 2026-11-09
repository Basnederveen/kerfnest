#pragma once

#include "Placer.h"

#include <cstdint>
#include <vector>

namespace kerfnest {

/// Width of the beam that fills one sheet, and the positions each partial sheet branches on.
/// A greedy fill (1 x 1) takes the locally tightest spot even when it blocks the next part.
inline constexpr size_t kFillBeamWidth = 4;
inline constexpr size_t kFillBranching = 3;

/// Sheet-size policy: one fixed sheet type for every sheet, or kMixedSizes (per sheet the
/// highest utilisation). Whenever a size takes every remaining piece, the smallest such size is
/// used, so the last sheet is as small as it can be.
inline constexpr int kMixedSizes = -1;

struct Decoded {
    std::vector<SheetFill> sheets;
    std::vector<std::vector<int>> remainingAt; ///< [k]: the pieces left, in walk order, when sheet k opens
    int policy = kMixedSizes;
};

/// What the search minimises: total sheet area, then the used part of the last sheet.
struct Objective {
    double sheetAreaMm2 = 0.0;
    double lastSheetUsedMm2 = 0.0;
    bool operator<(const Objective& o) const
    {
        return sheetAreaMm2 != o.sheetAreaMm2 ? sheetAreaMm2 < o.sheetAreaMm2 : lastSheetUsedMm2 < o.lastSheetUsedMm2;
    }
    bool operator<=(const Objective& o) const { return !(o < *this); }
};

Objective Evaluate(const Instance& instance, const std::vector<SheetFill>& sheets);

/// Turns pieces (part indices, one entry per copy) into sheets with one sheet-size policy, sheet
/// by sheet: a sheet is filled by walking the remaining pieces in order and placing each one
/// that fits. Every piece must fit an empty sheet of some type (the caller checks this first).
/// Adds the work done to `work`; with a cap (0: none), refuses with NestIncomplete as soon as the
/// work passes it after a sheet, naming how many pieces were placed.
Decoded DecodeWith(const Instance& instance, NfpCache& nfps, const std::vector<int>& pieces, int policy, uint64_t cap,
                   uint64_t& work);

/// Every policy, the best objective wins; the cap counts the work of all of them.
Decoded Decode(const Instance& instance, NfpCache& nfps, const std::vector<int>& order, uint64_t cap, uint64_t& work);

} // namespace kerfnest
