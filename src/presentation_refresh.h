#pragma once

#include <cmath>
#include <cstdint>

namespace highfps::refresh {

struct Rate {
    std::uint32_t numerator = 0;
    std::uint32_t denominator = 1;
    constexpr bool Valid() const {
        return denominator && numerator >= denominator &&
               numerator <= std::uint64_t(denominator) * 1000;
    }
    constexpr double Hz() const { return Valid() ? double(numerator) / denominator : 0.0; }
    constexpr double Period() const { return Valid() ? double(denominator) / numerator : 0.0; }
};

constexpr Rate IntegerRate(int value) {
    return value > 0 && value <= 1000 ? Rate{std::uint32_t(value), 1} : Rate{};
}

constexpr bool Less(Rate left, Rate right) {
    return left.Valid() && right.Valid() &&
           std::uint64_t(left.numerator) * right.denominator <
               std::uint64_t(right.numerator) * left.denominator;
}

constexpr bool Equal(Rate left, Rate right) {
    return left.Valid() == right.Valid() && !Less(left, right) && !Less(right, left);
}

constexpr Rate LowerKnown(Rate left, Rate right) {
    if (!left.Valid()) return right;
    if (!right.Valid()) return left;
    return Less(left, right) ? left : right;
}

enum class Support { Unknown, No, Yes };
enum class VRR { Off, Unknown, Unsupported, SystemDisabled, WaitingForFeedback, ReportedActive };
enum class Pacer { Uncapped, External, Software };

struct Observation {
    Rate mode{};
    Rate externalCap{};
    Support capable = Support::Unknown;
    Support enabled = Support::Unknown;
    Support active = Support::Unknown;
    bool compositorPaced = false;
};

struct Request {
    bool followSystem = false;
    bool vrr = false;
    Rate maxRefresh{};
    Rate maxFps{};
    Rate idleFps{};
};

struct Policy {
    Rate output{};
    Pacer pacer = Pacer::Uncapped;
    VRR vrr = VRR::Off;
};

inline const char* Name(VRR value) {
    switch (value) {
    case VRR::Off: return "off";
    case VRR::Unknown: return "unknown";
    case VRR::Unsupported: return "unsupported";
    case VRR::SystemDisabled: return "system-disabled";
    case VRR::WaitingForFeedback: return "waiting-feedback";
    case VRR::ReportedActive: return "reported-active";
    }
    return "unknown";
}

inline const char* Name(Pacer value) {
    switch (value) {
    case Pacer::Uncapped: return "uncapped";
    case Pacer::External: return "external";
    case Pacer::Software: return "software";
    }
    return "uncapped";
}

constexpr Policy Resolve(Observation display, Request request, bool vsync) {
    Policy result;
    if (request.vrr) {
        if (display.capable == Support::No) result.vrr = VRR::Unsupported;
        else if (display.capable != Support::Yes) result.vrr = VRR::Unknown;
        else if (display.enabled == Support::No) result.vrr = VRR::SystemDisabled;
        else if (display.enabled != Support::Yes) result.vrr = VRR::Unknown;
        else result.vrr = display.active == Support::Yes ? VRR::ReportedActive : VRR::WaitingForFeedback;
    }
    const Rate appLimit = LowerKnown(LowerKnown(request.maxRefresh, request.maxFps), request.idleFps);
    const bool followsDisplay = request.followSystem || request.vrr;
    const bool externalPacer = vsync || (followsDisplay && display.compositorPaced);
    const Rate externalLimit = followsDisplay ? LowerKnown(display.mode, display.externalCap) : display.mode;
    result.output = followsDisplay || vsync ? LowerKnown(appLimit, externalLimit) : appLimit;
    const bool needsCap = appLimit.Valid() && (!externalLimit.Valid() || Less(appLimit, externalLimit));
    if (externalPacer && !needsCap) result.pacer = Pacer::External;
    else if (result.output.Valid()) result.pacer = Pacer::Software;
    return result;
}

constexpr double SampleTime(VRR mode, double wallNow, double lastPresent, bool anchorValid) {
    return mode != VRR::ReportedActive && anchorValid && lastPresent > 0 &&
           lastPresent <= wallNow && wallNow - lastPresent < 0.25 ? lastPresent : wallNow;
}

inline double FixedAnimationSample(double now, double lastPresent, Rate mode, double drawLead) {
    if (!mode.Valid() || lastPresent <= 0 || now < lastPresent || now - lastPresent >= 0.25) return now;
    const double period = mode.Period();
    const double estimated = now + drawLead - lastPresent;
    const double elapsedPeriods = std::ceil(estimated / period) - 1;
    const double sample = lastPresent + (elapsedPeriods > 0 ? elapsedPeriods * period : 0);
    return sample < now ? sample : now;
}
struct Deadline {
    double next = 0.0;
    void Reset() { next = 0.0; }
    double Due(double now) const { return next > now ? next : now; }
    void Consume(double now, Rate rate, VRR mode = VRR::Off) {
        const double period = rate.Period();
        if (mode == VRR::ReportedActive) {
            next = now + period;
            return;
        }
        if (!(next > 0.0)) next = now;
        next += period;
        if (next <= now) next = now + period;
    }
};

struct SwapPacingCheck {
    double previous = 0.0;
    unsigned early = 0;
    bool useSoftware = false;

    void Reset() { *this = {}; }
    bool Observe(double completion, Rate limit) {
        if (useSoftware || !limit.Valid()) return false;
        const double elapsed = completion - previous;
        if (previous > 0 && elapsed > 0 && elapsed < limit.Period() * 0.75) ++early;
        else early = 0;
        previous = completion;
        if (early < 12) return false;
        useSoftware = true;
        return true;
    }
};

}
