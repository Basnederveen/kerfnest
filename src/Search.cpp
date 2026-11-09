#include "Search.h"

#include "Rng.h"

#include <algorithm>
#include <optional>
#include <thread>

namespace kerfnest {
namespace {

/// Neighbours start in one of the last kTailSheets sheets, except one in kFullRestart.
constexpr size_t kTailSheets = 3;
constexpr size_t kFullRestart = 4;

/// The search also stops after this many batches without a better nest: small groups are done
/// long before a budget sized for large ones runs out.
constexpr uint64_t kStallBatches = 60;

struct Neighbour {
    size_t from = 0; ///< sheets before this index are kept
    Decoded tail;
    uint64_t work = 0;
};

void Swap(std::vector<int>& seq, Rng& rng)
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        const size_t i = rng.Below(seq.size()), j = rng.Below(seq.size());
        if (seq[i] != seq[j]) {
            std::swap(seq[i], seq[j]);
            return;
        }
    }
}

void MoveEarlier(std::vector<int>& seq, Rng& rng)
{
    const size_t from = rng.Below(seq.size());
    const size_t to = rng.Below(from + 1);
    const int piece = seq[from];
    seq.erase(seq.begin() + static_cast<std::ptrdiff_t>(from));
    seq.insert(seq.begin() + static_cast<std::ptrdiff_t>(to), piece);
}

/// The pieces in `first` (a multiset of parts) go to the front, in their sequence order.
void PutFirst(std::vector<int>& seq, std::vector<int> first)
{
    std::vector<int> front, back;
    for (const int part : seq) {
        const auto it = std::find(first.begin(), first.end(), part);
        if (it != first.end()) {
            first.erase(it);
            front.push_back(part);
        } else {
            back.push_back(part);
        }
    }
    front.insert(front.end(), back.begin(), back.end());
    seq = std::move(front);
}

std::vector<int> PartsOf(const SheetFill& sheet)
{
    std::vector<int> parts;
    for (const Placed& p : sheet.placed())
        parts.push_back(p.part);
    return parts;
}

Neighbour MakeNeighbour(const Instance& instance, NfpCache& nfps, const Decoded& current, Rng rng)
{
    const size_t n = current.sheets.size();
    Neighbour nb;
    nb.from = rng.Below(kFullRestart) == 0 ? rng.Below(n) : n - 1 - rng.Below(std::min(n, kTailSheets));
    std::vector<int> seq = current.remainingAt[nb.from];
    if (seq.size() > 1) {
        switch (rng.Below(4)) {
        case 0: Swap(seq, rng); break;
        case 1: MoveEarlier(seq, rng); break;
        case 2: {
            // The least-filled sheet from `from` on: its pieces lead, so other sheets absorb them.
            size_t weakest = nb.from;
            for (size_t k = nb.from; k < n; ++k)
                if (current.sheets[k].netAreaMm2() * instance.sheetAreaMm2[current.sheets[weakest].sheetType()] <
                    current.sheets[weakest].netAreaMm2() * instance.sheetAreaMm2[current.sheets[k].sheetType()])
                    weakest = k;
            PutFirst(seq, PartsOf(current.sheets[weakest]));
            break;
        }
        default: {
            const int part = seq[rng.Below(seq.size())];
            PutFirst(seq, std::vector<int>(static_cast<size_t>(std::count(seq.begin(), seq.end(), part)), part));
            break;
        }
        }
    }
    nb.tail = DecodeWith(instance, nfps, seq, current.policy, 0, nb.work);
    return nb;
}

/// Sheets 0..from-1 of the current nest followed by the neighbour's tail.
Decoded Joined(const Decoded& current, Neighbour&& nb)
{
    Decoded d;
    d.policy = current.policy;
    for (size_t k = 0; k < nb.from; ++k) {
        d.sheets.push_back(current.sheets[k]);
        d.remainingAt.push_back(current.remainingAt[k]);
    }
    for (size_t k = 0; k < nb.tail.sheets.size(); ++k) {
        d.sheets.push_back(std::move(nb.tail.sheets[k]));
        d.remainingAt.push_back(std::move(nb.tail.remainingAt[k]));
    }
    return d;
}

} // namespace

std::vector<SheetFill> Search(const Instance& instance, NfpCache& nfps, Decoded current, const Settings& settings, uint64_t& work)
{
    Objective currentScore = Evaluate(instance, current.sheets);
    std::vector<SheetFill> best = current.sheets;
    Objective bestScore = currentScore;
    const size_t threads = std::clamp<size_t>(static_cast<size_t>(std::max(settings.threads, 1)), 1, kSearchBatch);

    uint64_t lastImprovement = 0;
    for (uint64_t iteration = 0; work < settings.workBudget && iteration - lastImprovement < kStallBatches; ++iteration) {
        std::vector<std::optional<Neighbour>> batch(kSearchBatch);
        const auto run = [&](const size_t first) {
            for (size_t i = first; i < kSearchBatch; i += threads)
                batch[i] = MakeNeighbour(instance, nfps, current, Rng(MixSeed(MixSeed(settings.seed, iteration), i)));
        };
        std::vector<std::thread> pool;
        for (size_t t = 1; t < threads; ++t)
            pool.emplace_back(run, t);
        run(0);
        for (std::thread& t : pool)
            t.join();

        std::optional<size_t> winner;
        std::vector<Decoded> joined(kSearchBatch);
        std::vector<Objective> scores(kSearchBatch);
        for (size_t i = 0; i < kSearchBatch; ++i) {
            work += batch[i]->work;
            joined[i] = Joined(current, std::move(*batch[i]));
            scores[i] = Evaluate(instance, joined[i].sheets);
            if (!winner || scores[i] < scores[*winner])
                winner = i;
        }
        if (scores[*winner] <= currentScore) {
            current = std::move(joined[*winner]);
            currentScore = scores[*winner];
            if (currentScore < bestScore) {
                best = std::vector<SheetFill>(current.sheets); // SheetFill holds references: not assignable
                bestScore = currentScore;
                lastImprovement = iteration;
            }
        }
    }
    return best;
}

} // namespace kerfnest
