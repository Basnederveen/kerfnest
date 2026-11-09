#pragma once

#include "Shape.h"

#include <clipper2/clipper.h>

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace kerfnest {

/// Everything about a problem the decoder needs, prepared once.
struct Instance {
    std::vector<std::vector<OrientedShape>> shapes; ///< [part][angle index]
    std::vector<double> netAreaMm2;                 ///< [part]
    std::vector<Clipper2Lib::Rect64> usable;        ///< [sheet type]: sheet minus margin, for true bounds
    std::vector<double> sheetAreaMm2;               ///< [sheet type]
};

/// No-fit polygons per (part A, angle A, part B, angle B), computed on first use. Shared by the
/// search threads; a no-fit polygon is a pure function of its key, so who computes it first
/// never changes a result.
class NfpCache {
public:
    explicit NfpCache(const Instance& instance) : instance_(instance) {}
    const Clipper2Lib::Paths64& Get(int partA, int angleA, int partB, int angleB);

private:
    const Instance& instance_;
    std::mutex mutex_;
    std::map<std::array<int, 4>, Clipper2Lib::Paths64> nfps_; ///< map nodes never move: references stay valid
};

struct Placed {
    int part = 0;
    int angle = 0; ///< index into the part's shapes
    Clipper2Lib::Point64 pos;
};

/// One sheet being filled. Each part goes to its best position over all angles: inside a
/// placed part's hole first, then the smallest used length along the sheet's long side, then
/// the lowest across and along it, then the earlier angle.
class SheetFill {
public:
    SheetFill(const Instance& instance, NfpCache& nfps, int sheetType);

    /// Up to `count` positions for the part, best first; empty when it fits nowhere. Adds the
    /// vertices it feeds to Clipper to `work`: deterministic, unlike time, and independent of
    /// which no-fit polygons happen to be cached already.
    std::vector<Placed> BestPositions(int part, size_t count, uint64_t& work);

    void Place(const Placed& p);

    int sheetType() const { return sheetType_; }
    const std::vector<Placed>& placed() const { return placed_; }
    double netAreaMm2() const { return netAreaMm2_; }
    int64_t usedLength() const { return usedLength_; }

private:
    struct Forbidden {
        Clipper2Lib::Paths64 region; ///< union of the NFPs of placed_[0 .. covered)
        size_t covered = 0;
    };
    const Clipper2Lib::Paths64& ForbiddenFor(int part, int angle, uint64_t& work);

    const Instance& instance_;
    NfpCache& nfps_;
    int sheetType_;
    bool lengthAlongX_;
    int64_t usedLength_;
    std::vector<Placed> placed_;
    std::vector<Clipper2Lib::Rect64> holeBounds_; ///< of the placed parts' holes, on the sheet
    std::map<std::pair<int, int>, Forbidden> forbidden_;
    double netAreaMm2_ = 0.0;
};

} // namespace kerfnest
