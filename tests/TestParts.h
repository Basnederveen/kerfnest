#pragma once

#include <kerfnest/kerfnest.h>

#include <string>
#include <utility>
#include <vector>

// Hand-made contours for tests. Loops are counter-clockwise unless built otherwise.
namespace kerfnest::test {

inline Segment Line(const double x0, const double y0, const double x1, const double y1)
{
    Segment s;
    s.type = SegmentType::Line;
    s.start = {x0, y0};
    s.end = {x1, y1};
    return s;
}

inline Segment Arc(const Point start, const Point end, const Point center, const bool clockwise)
{
    Segment s;
    s.type = SegmentType::Arc;
    s.start = start;
    s.end = end;
    s.center = center;
    s.clockwise = clockwise;
    return s;
}

inline Loop CircleLoop(const double cx, const double cy, const double r)
{
    Segment s;
    s.type = SegmentType::Circle;
    s.start = {cx + r, cy};
    s.end = s.start;
    s.center = {cx, cy};
    return {s};
}

inline Loop PolygonLoop(const std::vector<std::pair<double, double>>& pts)
{
    Loop loop;
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        loop.push_back(Line(a.first, a.second, b.first, b.second));
    }
    return loop;
}

inline Loop RectLoop(const double x, const double y, const double w, const double h)
{
    return PolygonLoop({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}});
}

inline Part MakePart(std::string id, std::vector<Loop> loops, const int quantity = 1)
{
    Part p;
    p.id = std::move(id);
    p.loops = std::move(loops);
    p.quantity = quantity;
    return p;
}

} // namespace kerfnest::test
