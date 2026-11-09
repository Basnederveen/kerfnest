#pragma once

#include "Decoder.h"

#include <kerfnest/kerfnest.h>

#include <cstdint>
#include <vector>

namespace kerfnest {

/// Neighbours evaluated per search iteration. Fixed, so the result does not depend on the
/// thread count: each neighbour has its own seed, and the batch's winner is the best objective,
/// ties to the lowest index.
inline constexpr size_t kSearchBatch = 8;

/// Deterministic iterated local search. A neighbour keeps sheets 0..k-1 as they are and decodes
/// the pieces of sheets k.. again in a perturbed order (swap two pieces, move one earlier, put
/// the weakest sheet's pieces first, put one part's copies first), one in four also under another
/// sheet-size policy; k is mostly one of the last
/// three sheets, where the waste is. The best neighbour is taken when it is no worse (plateaus
/// move); the best nest seen is returned. Stops when `work` reaches settings.workBudget, or after
/// a long run of batches without a better nest.
std::vector<SheetFill> Search(const Instance& instance, NfpCache& nfps, Decoded current, const Settings& settings,
                              uint64_t& work);

} // namespace kerfnest
