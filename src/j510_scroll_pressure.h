#pragma once
#include "camera_presentation.h"
#include <cmath>

namespace highfps::j510_scroll {
using Point = camera_presentation::Point;
constexpr bool Same(Point a, Point b) { return a.x == b.x && a.y == b.y; }
inline bool BoundedMove(Point a, Point b) {
    const auto x = a.x-b.x, y = a.y-b.y;
    return std::isfinite(x) && std::isfinite(y) && x*x+y*y<=32.f*32.f;
}
struct Frame {
    Point source{}, visual{};
    bool armed = false;
    bool completed = false;
    bool reentered = false;
    bool within = false;
    constexpr void Clear() { armed = completed = reentered = within = false; }
    bool Prepare(Point prior, Point current, float alpha) {
        Clear();
        if (!BoundedMove(prior,current) || !std::isfinite(alpha)) return false;
        source = current;
        visual = camera_presentation::Interpolate(prior,current,alpha);
        armed = BoundedMove(source,visual);
        return armed;
    }
    constexpr bool Enter(Point current) {
        if (within || completed) { reentered = true; return false; }
        if (!armed || !Same(current,source)) return false;
        within = true;
        return true;
    }
    constexpr bool Finish(bool scopeOwnsCamera, Point current) {
        within = false;
        const bool restore = scopeOwnsCamera && Same(current,visual);
        completed = restore && !reentered;
        return restore;
    }
    constexpr bool CanSwap() const { return !armed || (completed && !reentered); }
};
}
