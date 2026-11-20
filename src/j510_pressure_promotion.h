#pragma once
#include <cstdint>

namespace highfps::j510 {
struct PressurePromotion {
    bool active = false;
    unsigned quiet = 0;
    void Observe(bool sameWorld, unsigned tears, unsigned effects) {
        if (!sameWorld) { active = false; quiet = 0; return; }
        if (tears >= 384 || effects >= 768) { active = true; quiet = 0; return; }
        if (tears >= 256 || effects >= 512) { quiet = 0; return; }
        if (active && ++quiet >= 30) { active = false; quiet = 0; }
    }
    bool ShouldPromote(bool extra, bool stableScene, bool stationaryCamera) const {
        return active && extra && stableScene && stationaryCamera;
    }
};
// A stationary camera is common even while the player crosses a small room.
// Signal movement from consecutive native player positions instead. An
// incomplete pose sample is never treated as evidence of rest.
struct PlayerRest {
    static constexpr unsigned required = 6;
    unsigned ticks = 0;
    void Observe(bool playersKnown, bool anyMoved) {
        if (!playersKnown || anyMoved) ticks = 0;
        else if (ticks < required) ++ticks;
    }
    bool CanShowEndpoint(float lowerBoundAlpha) const {
        return ticks >= required || lowerBoundAlpha >= 1.0f;
    }
    void Reset() { ticks = 0; }
};
constexpr bool SamePoseIdentity(std::uint32_t oldKind, std::uint32_t newKind,
                                std::uint32_t oldSeed, std::uint32_t newSeed) {
    return oldKind == newKind && oldSeed == newSeed;
}
// The original render consumes the current native pose. During the same U
// tick a later extra must not rewind to an interpolation toward that endpoint.
struct PresentedEndpoint {
    std::uint32_t tick = 0;
    bool valid = false;
    void Note(std::uint32_t current, bool complete, std::uint64_t swaps) {
        if (complete && swaps == 1) { tick = current; valid = true; }
    }
    bool Hold(std::uint32_t current) const { return valid && tick == current; }
    void Invalidate() { valid = false; }
};
}
