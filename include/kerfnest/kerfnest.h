#pragma once

#include <cstdint>
#include <string>
#include <vector>

// kerfnest: deterministic 2D irregular nesting of sheet-metal parts on stock sheets.
// All lengths are millimetres, all angles whole degrees, counter-clockwise.
namespace kerfnest {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

enum class SegmentType { Line, Arc, Circle };

/// An arc runs from start to end around center, counter-clockwise unless clockwise is set.
/// A circle is a full turn starting and ending at start.
struct Segment {
    SegmentType type = SegmentType::Line;
    Point start;
    Point end;
    Point center;
    bool clockwise = false;
};

/// A closed loop: every segment's end is exactly the next segment's start.
using Loop = std::vector<Segment>;

struct Part {
    std::string id;
    std::vector<Loop> loops; ///< outer loop first, holes after
    int quantity = 0;
};

struct SheetType {
    std::string id;
    double width = 0.0;
    double height = 0.0;
};

struct Settings {
    double spacingMm = 0.0;     ///< minimum gap between two parts
    double marginMm = 0.0;      ///< minimum gap between a part and the sheet edge
    std::vector<int> anglesDeg; ///< allowed rotations; rotation only, never mirroring
    uint64_t seed = 0;
    uint64_t workBudget = 0;    ///< deterministic work units; the search never reads a clock
    int threads = 1;            ///< does not change the result
};

struct Problem {
    std::vector<Part> parts;
    std::vector<SheetType> sheetTypes;
    Settings settings;
};

/// The part's own contour, rotated about its origin by angleDeg, then translated by (x, y).
struct Placement {
    std::string partId;
    int sheetIndex = 0;
    double x = 0.0;
    double y = 0.0;
    int angleDeg = 0;
};

enum class RefusalCode {
    InvalidContour,       ///< a loop is open, crosses itself, or a hole is not inside the outer loop
    PartExceedsAllSheets, ///< a part fits no sheet at any allowed angle
    NestIncomplete,       ///< the work budget ran out before every piece was placed
};

/// Why there is no nest. The message names what failed; there is never a partial result.
struct Refusal {
    RefusalCode code = RefusalCode::InvalidContour;
    std::string message;
};

} // namespace kerfnest
