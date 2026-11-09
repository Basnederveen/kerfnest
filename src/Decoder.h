#pragma once

#include "Placer.h"

#include <vector>

namespace kerfnest {

/// Width of the beam that fills one sheet, and the positions each partial sheet branches on.
/// A greedy fill (1 x 1) takes the locally tightest spot even when it blocks the next part.
inline constexpr size_t kFillBeamWidth = 2;
inline constexpr size_t kFillBranching = 2;

/// Turns an order of pieces (part indices, one entry per copy) into sheets, sheet by sheet: a
/// sheet is filled by walking the remaining pieces in order and placing each one that fits. Its
/// stock size comes from the sheet-size policy (one fixed size, or per sheet the highest
/// utilisation); whenever a size takes every remaining piece, the smallest such size is used.
/// Every policy is decoded and the least total sheet area wins (then the shorter last sheet).
/// Every piece must fit an empty sheet of some type (the caller checks this first).
std::vector<SheetFill> Decode(const Instance& instance, NfpCache& nfps, const std::vector<int>& order);

} // namespace kerfnest
