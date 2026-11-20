#include "../src/display_observation.h"
#include "../src/render_timing.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace highfps::refresh;

static void CheckMotion(double rate) {
    highfps::FixedTimeline simulation;
    unsigned ticks = 0;
    double lastTick = 0.0, last = 0.0;
    for (unsigned frame = 0; double(frame) / rate < 10; ++frame) {
        const double now = double(frame) / rate;
        while (simulation.Due(now)) {
            lastTick = simulation.Consume(now);
            ++ticks;
        }
        const double position = lastTick + highfps::RenderAlpha(now, lastTick, simulation.period) * simulation.period;
        if (frame) assert(std::fabs(position - last - 1.0 / rate) < 1e-8);
        last = position;
    }
    assert(ticks == 599 || ticks == 600);
}

static void CheckVariableRecovery(Policy output) {
    assert(output.vrr == VRR::ReportedActive && output.pacer == Pacer::Software);
    Deadline deadline;
    highfps::FixedTimeline simulation;
    double previous = 0.0, lastTick = 0.0, lastPose = 0.0;
    unsigned ticks = 0;
    for (unsigned frame = 0; frame < 180; ++frame) {
        const bool slow = frame >= 60 && frame < 120;
        const double start = previous;
        const double ready = start + (slow ? .020 : .002);
        const double presented = deadline.Due(ready);
        const double sample = SampleTime(output.vrr, start, previous, true);
        while (simulation.Due(sample)) {
            lastTick = simulation.Consume(sample);
            ++ticks;
        }
        const double pose = lastTick + highfps::RenderAlpha(sample, lastTick, simulation.period) * simulation.period;
        if (frame) {
            assert(std::fabs(presented - previous - (slow ? .020 : output.output.Period())) < 1e-9);
            assert(pose > lastPose);
        }
        deadline.Consume(presented, output.output, output.vrr);
        previous = presented;
        lastPose = pose;
    }
    assert(std::fabs(double(ticks - 1) - lastPose / simulation.period) < 1);
}

int main() {
    for (int hz = 30; hz <= 90; ++hz) {
        const Observation display{IntegerRate(hz)};
        const auto software = Resolve(display, {true}, false);
        const auto synchronized = Resolve(display, {true}, true);
        assert(software.pacer == Pacer::Software && software.output.Hz() == hz);
        assert(synchronized.pacer == Pacer::External && synchronized.output.Hz() == hz);
        CheckMotion(hz);
    }
    const Observation ntsc{{60000, 1001}};
    auto capped = Resolve(ntsc, {true, false, {90, 1}}, true);
    assert(Equal(capped.output, ntsc.mode) && capped.pacer == Pacer::External);
    CheckMotion(ntsc.mode.Hz());
    const Observation steam{{80, 1}, {40, 1}, Support::No};
    capped = Resolve(steam, {true, true, {60, 1}}, true);
    assert(capped.output.Hz() == 40 && capped.pacer == Pacer::External && capped.vrr == VRR::Unsupported);
    capped = Resolve(steam, {true, true, {}, {35, 1}}, true);
    assert(capped.output.Hz() == 35 && capped.pacer == Pacer::Software);
    assert(Resolve(ntsc, {}, false).pacer == Pacer::Uncapped);
    assert(Resolve({}, {true, false, {47, 1}}, true).pacer == Pacer::Software);
    assert(Resolve({}, {false, true}, true).vrr == VRR::Unknown);

    const char sample[] = "HFDS1 12800 123 60000 1001 2 2 1 0\n";
    const auto host = ParseHostSnapshot(sample, 12801);
    assert(host.valid && host.outputId == 123 && Equal(host.display.mode, ntsc.mode));
    assert(Resolve(host.display, {true, true}, true).vrr == VRR::WaitingForFeedback);
    auto active = host.display;
    active.active = Support::Yes;
    assert(Resolve(active, {true, true}, true).vrr == VRR::ReportedActive);
    active.mode = {120, 1};
    CheckVariableRecovery(Resolve(active, {true, true, {90, 1}}, true));
    assert(SampleTime(VRR::Unsupported, 1.02, 1.01, true) == 1.01);
    assert(SampleTime(VRR::Unknown, 1.02, 1.03, true) == 1.02);
    assert(SampleTime(VRR::Off, 1.4, 1.0, true) == 1.4);
    assert(SampleTime(VRR::ReportedActive, 1.02, 1.01, true) == 1.02);
    assert(FixedAnimationSample(1.006, 1, {90, 1}, .002) == 1.0);
    assert(std::fabs(FixedAnimationSample(1.016, 1, {90, 1}, .002) - (1 + 1.0 / 90)) < 1e-12);
    assert(FixedAnimationSample(1.35, 1, {90, 1}, .002) == 1.35);
    active.enabled = Support::No;
    assert(Resolve(active, {true, true}, true).vrr == VRR::SystemDisabled);
    assert(!ParseHostSnapshot(sample, 17000).valid && !ParseHostSnapshot(sample, 12799).valid);
    assert(!ParseHostSnapshot("HFDS1 12800 123 60 0 2 2 1 0", 12800).valid);
    assert(!ParseHostSnapshot("HFDS1 12800 123 60 1 2 2 1 0 o", 12800).valid);
    assert(!ParseHostSnapshot("HFDS1 18446744073709551616 1 60 1 0 0 0 0", 12800).valid);

    Deadline deadline;
    assert(deadline.Due(1) == 1);
    deadline.Consume(1, ntsc.mode);
    assert(std::fabs(deadline.Due(1) - 1 - ntsc.mode.Period()) < 1e-12);
    deadline.Consume(2, ntsc.mode);
    assert(std::fabs(deadline.Due(2) - 2 - ntsc.mode.Period()) < 1e-12);
    deadline.Reset();
    assert(deadline.Due(3) == 3);

    SwapPacingCheck swapPacing;
    for (unsigned frame = 0; frame != 100; ++frame)
        assert(!swapPacing.Observe(5 + frame * ntsc.mode.Period(), ntsc.mode));
    assert(!swapPacing.useSoftware);
    for (unsigned frame = 0; frame != 12; ++frame)
        assert(!swapPacing.Observe(10 + frame * .001, ntsc.mode));
    assert(swapPacing.Observe(10.012, ntsc.mode));
    assert(!swapPacing.Observe(10.013, ntsc.mode) && swapPacing.useSoftware);
    swapPacing.Reset();
    assert(!swapPacing.useSoftware && !swapPacing.Observe(11, ntsc.mode));
    std::puts("presentation refresh: fixed 30-90/59.94, host policy, VRR 90-50-90 recovery without bursts");
}
