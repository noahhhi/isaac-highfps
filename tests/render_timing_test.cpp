#include "../src/render_timing.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned ConsumeDue(highfps::FixedTimeline& clock, double now, unsigned limit = 4096) {
    unsigned count = 0;
    while (clock.Due(now) && count < limit) { clock.Consume(now); ++count; }
    return count;
}

static void TestLifecycleAndRenderStalls() {
    const int blocks[] = {30, 80, 200, 230, 249, 251, 267, 268, 500};
    for (int ms : blocks) {
        const double duration = ms / 1000.0;
        highfps::FixedTimeline load;
        load.Consume(0); // wrapper containing the blocking lifecycle call
        load.ExcludeBlocking(duration);
        assert(ConsumeDue(load, duration) == 0);
        assert(ConsumeDue(load, duration + load.period - 1e-6) == 0);
        assert(ConsumeDue(load, duration + load.period) == 1);
        unsigned resumed = 1;
        for (int i = 2; i <= 100; ++i) resumed += ConsumeDue(load, duration + i / 100.0);
        assert(resumed == 60);

        highfps::FixedTimeline render;
        render.Consume(0);
        const unsigned expected = unsigned(floor(duration / render.period + 1e-7));
        unsigned ticks = ConsumeDue(render, duration);
        assert(ticks == expected); // same duration, different semantic event
        for (int i = 1; i <= 100; ++i) ticks += ConsumeDue(render, duration + i / 100.0);
        assert(ticks == expected + 60);

        // Production limits a catch-up batch to 16. The rest must survive to
        // future presents rather than silently disappearing at a 250ms cutoff.
        highfps::FixedTimeline bounded;
        bounded.Consume(0);
        unsigned boundedTicks = 0;
        for (int i = 0; i <= 100; ++i)
            boundedTicks += ConsumeDue(bounded, duration + i / 100.0, 16);
        assert(boundedTicks == expected + 60);

        // A lifecycle call begins while 80ms of ordinary render delay exists.
        // Its exclusion must not erase those pre-existing due simulation ticks.
        highfps::FixedTimeline delayed;
        delayed.Consume(0);
        delayed.Consume(.08); // consume first overdue tick, not wall time .08
        const double before = delayed.next;
        delayed.ExcludeBlocking(duration);
        assert(fabs(delayed.next - before - duration) < 1e-10);
        assert(ConsumeDue(delayed, .08 + duration) == 3);
        printf("%dms lifecycle: no burst, 60Hz recovery; render stall: %u ticks preserved; prior debt retained\n", ms, expected);
    }
    highfps::FixedTimeline unchanged;
    unchanged.Consume(0);
    const double before = unchanged.next;
    unchanged.ExcludeBlocking(0);
    unchanged.ExcludeBlocking(-1);
    assert(unchanged.next == before);
}

int main() {
    TestLifecycleAndRenderStalls();
    const int rates[] = {24, 30, 48, 60, 80, 90, 100, 120, 144, 165, 240};
    for (int fps : rates) {
        highfps::FixedTimeline clock;
        int ticks = 0; double last = 0, previous = 0, error = 0;
        for (int frame = 0; frame < fps * 10; ++frame) {
            const double now = double(frame) / fps;
            unsigned steps = 0;
            while (clock.Due(now) && steps++ < 16) { last = clock.Consume(now); ++ticks; }
            const double pos = (ticks - 1) * 3.0 + highfps::RenderAlpha(now, last, clock.period) * 3.0;
            if (frame) error = fmax(error, fabs(pos - previous - 180.0 / fps));
            previous = pos;
        }
        assert(abs(ticks - 600) <= 2);
        assert(error < 0.00001);
        printf("%d FPS: %d fixed ticks, max uniform-motion error %.9f\n", fps, ticks, error);
    }
    // Variable frame intervals model a VRR presentation stream. The simulation
    // schedule and motion trajectory must not depend on the interval sequence.
    highfps::FixedTimeline variable;
    const double intervals[] = {1.0/48, 1.0/80, 1.0/144, 1.0/100, 1.0/61};
    double now = 0, last = 0; int ticks = 0;
    for (int frame = 0; frame < 1000; ++frame) {
        now += intervals[frame % 5];
        while (variable.Due(now)) { last = variable.Consume(now); ++ticks; }
        const double rendered = (ticks - 1) * 3.0 + highfps::RenderAlpha(now,last,variable.period)*3.0;
        assert(fabs(rendered - now*180.0) < 0.00001);
    }
    assert(abs(ticks-int(now*60)) <= 1);
    puts("variable 48-144 Hz: uniform motion and fixed 60 Hz logic passed");
    // An actual render frame consumes all fixed ticks due since the previous
    // frame. The Lua-layer budget follows that count, not the number of images.
    highfps::FixedTimeline cadence;
    unsigned images = 0, callbacks = 0, multiPassFrames = 0;
    for (int i = 0; i < 480; ++i) {
        unsigned due = 0;
        while (cadence.Due(double(i)/48)) { cadence.Consume(double(i)/48); ++due; }
        callbacks += due;
        multiPassFrames += due > 1;
        ++images;
    }
    assert(images == 480 && callbacks == 599 && multiPassFrames == 119);
    highfps::FixedTimeline c; c.Consume(0);
    unsigned count = 0; while (c.Due(.2)) { c.Consume(.2); ++count; }
    assert(count == 12);
    c.ExcludeBlocking(4.8); // explicitly measured lifecycle span [.2, 5.0]
    assert(!c.Due(5.0));
    assert(c.Due(5.0 + c.period));
    assert(highfps::FullTick19715(0)); assert(!highfps::FullTick19715(1));
    assert(highfps::RenderAlpha(0, 1, 1) == 0);
    assert(highfps::RenderAlpha(3, 1, 1) == 1);
}
