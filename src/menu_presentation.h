#pragma once
#include <cmath>
#include <cstdint>

namespace highfps { namespace menu_presentation {
// J374 menu viewport only. The host reads these fields inside its memory-fault
// guard. current_menu is deliberately absent: changing pages starts a slide.
struct Context {
    uintptr_t menu = 0;
    uint64_t epoch = 0;
    int managerState = 0;
    float width = 0, height = 0;
};
struct Point { float x = 0, y = 0; };
inline bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
inline bool Equal(Point a, Point b) { return a.x == b.x && a.y == b.y; }
inline bool Active(const Context& c) {
    return c.menu >= 0x10000 && c.managerState == 1 &&
        std::isfinite(c.width) && std::isfinite(c.height) && c.width > 0 && c.height > 0;
}
inline bool Same(const Context& a, const Context& b) {
    return a.menu == b.menu && a.epoch == b.epoch && a.managerState == b.managerState &&
        a.width == b.width && a.height == b.height;
}
inline float Alpha60(double now, double lastMainTick) {
    if (!std::isfinite(now) || !std::isfinite(lastMainTick)) return 0;
    const double a = (now - lastMainTick) * 60.0;
    return a <= 0 ? 0.0f : (a >= 1 ? 1.0f : float(a));
}
inline Point Interpolate(Point previous, Point current, float alpha) {
    if (!std::isfinite(alpha)) alpha = 0;
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    // Double intermediates avoid overflow for otherwise finite endpoint floats.
    return {float(double(previous.x) + (double(current.x) - previous.x) * alpha),
            float(double(previous.y) + (double(current.y) - previous.y) * alpha)};
}
struct History {
    Context context{};
    Point previous{}, current{};
    bool valid = false;
    void Invalidate() { valid = false; }
    bool Matches(const Context& c) const { return valid && Active(c) && Same(context, c); }
    // Call once AFTER each hidden canonical render, with authoritative coordinates.
    // Restore any outstanding presentation before Update/canonical Render. A page
    // ID change is not a hardSnap. Pass hardSnap only for known native teleport/init.
    bool Sample(const Context& c, Point authoritative, bool hardSnap = false) {
        if (!Active(c) || !Finite(authoritative)) { Invalidate(); return false; }
        const bool fresh = !Matches(c) || hardSnap;
        previous = fresh ? authoritative : current;
        current = authoritative;
        context = c;
        valid = true;
        return true;
    }
};

// Explicit scope for a __try/__finally host (no destructor/SEH unwinding reliance).
// The host supplies live Context again at Restore, before touching old memory.
// This scope must not cross Update, hidden canonical Render, or object lifetime.
// Host must call Restore from __finally around only the actual native pure Render.
class Scope {
    History* history_ = nullptr;
    Context context_{};
    float* viewport_ = nullptr;
    Point saved_{}, displayed_{};
    bool applied_ = false;
public:
    Scope() = default;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    bool Applied() const { return applied_; }
    bool Begin(History& history, const Context& live, float* viewport,
               double now, double lastMainTick, bool purePresentation) {
        if (applied_) return false; // no nested overwrite of the restoration backup
        if (!purePresentation) return false;
        if (!history.Matches(live)) { history.Invalidate(); return false; }
        if (!viewport || reinterpret_cast<uintptr_t>(viewport) != live.menu + 0x44)
            return false;
        const Point current{viewport[0], viewport[1]};
        // Never display a stale history after an unsampled native write.
        if (!Finite(current) || !Equal(current, history.current)) {
            history.Invalidate(); return false;
        }
        context_ = live;
        viewport_ = viewport;
        saved_ = current;
        displayed_ = Interpolate(history.previous, history.current, Alpha60(now, lastMainTick));
        history_ = &history;
        applied_ = true;
        viewport[0] = displayed_.x;
        viewport[1] = displayed_.y;
        return true;
    }
    bool Restore(const Context& live) {
        if (!applied_) return true;
        applied_ = false;
        if (!Active(live) || !Same(context_, live)) {
            history_->Invalidate(); return false; // do not dereference an expired owner
        }
        // Preserve a meaningful native write if a render callback changed the field.
        // In the intended pure-render path no update writes these coordinates.
        if (!Equal({viewport_[0], viewport_[1]}, displayed_)) {
            history_->Invalidate(); return false;
        }
        viewport_[0] = saved_.x;
        viewport_[1] = saved_.y;
        return true;
    }
    // Host memory-exception fallback: no further dereference of suspect memory.
    void Abandon() {
        if (history_) history_->Invalidate();
        applied_ = false;
    }
};
}}
