// Compile against the production header, not a copied scheduling model:
// g++ -std=c++17 -O2 -Wall -Wextra -I/path/to/repo/src clock_lifecycle_test.cpp -o clock_lifecycle_test
// Requires FixedTimeline::{Due, Consume, ExcludeBlocking} and ::period.
// Missing lifecycle exclusion is a compile failure, not a skipped pass.
#include "render_timing.h"
#include <math.h>
#include <stdio.h>

static int failures = 0;
static void check(bool ok, const char* label, int ms, int actual, int expected) {
    printf("%s %-29s block=%3dms actual=%d expected=%d\n",
                ok ? "PASS" : "FAIL", label, ms, actual, expected);
    failures += !ok;
}

template<class Clock> unsigned consumeDue(Clock& clock, double now) {
    unsigned count = 0;
    // Enough to expose debt loss while guarding a broken Due/Consume loop.
    while (clock.Due(now) && count < 4096) { clock.Consume(now); ++count; }
    return count;
}

template<class Clock> void lifecycle(int ms) {
    Clock clock;
    clock.Consume(0); // The lifecycle-bearing wrapper has already run once.
    const double resumed = ms / 1000.0;
    {
        clock.ExcludeBlocking(resumed);
        const unsigned immediate = consumeDue(clock, resumed);
        check(immediate == 0, "lifecycle no immediate burst", ms, immediate, 0);
        const unsigned early = consumeDue(clock, resumed + Clock::period - 1e-6);
        check(early == 0, "lifecycle waits full period", ms, early, 0);
        const unsigned next = consumeDue(clock, resumed + Clock::period);
        check(next == 1, "lifecycle next tick", ms, next, 1);
        unsigned ticks = next;
        for (int frame = 2; frame <= 100; ++frame)
            ticks += consumeDue(clock, resumed + frame / 100.0);
        check(ticks == 60, "lifecycle 1s recovery", ms, ticks, 60);
    }
}

template<class Clock> void ordinaryStall(int ms) {
    Clock clock;
    clock.Consume(0);
    const double resumed = ms / 1000.0;
    const unsigned expected = unsigned(floor(resumed / Clock::period + 1e-7));
    const unsigned ticks = consumeDue(clock, resumed);
    check(ticks == expected, "ordinary stall retains debt", ms, ticks, expected);
    unsigned recovered = ticks;
    for (int frame = 1; frame <= 100; ++frame)
        recovered += consumeDue(clock, resumed + frame / 100.0);
    const unsigned expectedTotal = unsigned(floor((resumed + 1) / Clock::period + 1e-7));
    check(recovered == expectedTotal, "ordinary stall 1s recovery", ms, recovered, expectedTotal);
}

template<class Clock> void boundedCatchup(int ms) {
    Clock clock;
    clock.Consume(0);
    const double resumed = ms / 1000.0;
    unsigned ticks = 0, first = 0;
    for (int frame = 0; frame <= 100; ++frame) {
        unsigned steps = 0;
        const double now = resumed + frame / 100.0;
        while (clock.Due(now) && steps < 16) {
            clock.Consume(now); ++steps; ++ticks;
        }
        if (!frame) first = steps;
    }
    const unsigned expected = unsigned(floor((resumed + 1) / Clock::period + 1e-7));
    check(first <= 16 && ticks == expected, "bounded catchup preserves debt", ms, ticks, expected);
}

int main() {
    highfps::CanonicalPresentationEpoch presentation;
    check(!presentation.Ready(0), "no presentation before capture", 0, presentation.Ready(0), 0);
    presentation.Complete(1, 1);
    check(presentation.Ready(1), "stable canonical ready", 0, presentation.Ready(1), 1);
    check(!presentation.Ready(2), "later lifecycle invalidates", 0, presentation.Ready(2), 0);
    presentation.Complete(1, 2);
    check(!presentation.Ready(2), "render-side rewind holds", 0, presentation.Ready(2), 0);
    presentation.Complete(2, 3);
    check(!presentation.Ready(3), "nested replacement holds", 0, presentation.Ready(3), 0);
    presentation.Complete(3, 3);
    check(presentation.Ready(3), "full new capture resumes", 0, presentation.Ready(3), 1);
    // 230 reproduces the observed room-load length. 249/251 deliberately straddle
    // 250ms wall time; 267/268 also cross the old now-next > .25 threshold after
    // an initial tick, proving the test is not fooled by its one-tick phase.
    const int blocks[] = {30, 80, 200, 230, 249, 251, 267, 268, 500};
    for (int ms : blocks) {
        lifecycle<highfps::FixedTimeline>(ms);
        ordinaryStall<highfps::FixedTimeline>(ms);
        boundedCatchup<highfps::FixedTimeline>(ms);
    }
    printf("RESULT failures=%d; production clock only; no game launched\n", failures);
    return failures ? 1 : 0;
}
