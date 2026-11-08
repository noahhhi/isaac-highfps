#pragma once
namespace highfps {
// The simulation clock is an absolute schedule, never the time a render loop
// happened to dispatch an update. This matters for noninteger FPS/60 ratios.
struct FixedTimeline {
    static constexpr double period = 1.0 / 60.0;
    double next = 0.0;
    bool Due(double now) const { return now + 1e-9 >= next; }
    // Exclude only explicitly observed lifecycle work (room/floor loading,
    // suspend), preserving phase and all ordinary render debt from before it.
    // Nested lifecycle calls must be measured as a union by the caller.
    void ExcludeBlocking(double duration) {
        if (duration > 0.0) next += duration;
    }
    double Consume(double /*now*/) {
        // A late presentation is still elapsed gameplay. Duration alone cannot
        // distinguish a load from a slow renderer, so never discard its debt.
        const double scheduled = next;
        next += period;
        return scheduled;
    }
};
inline float RenderAlpha(double now, double scheduled, double interval) {
    const double a = (now - scheduled) / interval;
    return float(a < 0 ? 0 : (a > 1 ? 1 : a));
}
inline bool FullTick19715(unsigned counter) { return (counter & 1u) == 0; }
}
