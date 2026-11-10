# Performance work: interim evidence

Status: **in progress**, not a stable-90-FPS release acceptance. The exact test builds are identified in [the measurement summary](performance-progress.json). Later source changes are not covered merely by sharing the version string.

## Indexed draw cache, threading and native render cost

The former 512-entry linear/FIFO callback cache could repeatedly search, evict and move large records under load. The new cache uses exact-key and lineage indexes, stable records, compact no-op records, bounded retained memory, and current-canonical-frame admission protection. The default is 16,384 entries and 64 MiB; the process is still 32-bit, so total machine RAM is not its address-space budget. See [cache unit evidence](cache-performance-unit.json). Native entity-pool warnings are a separate condition and also occurred in the user's stable native run.

The main thread can be pinned without pinning every engine thread. A bounded copied-text queue moves log I/O to a lower-priority worker outside the selected logical CPU. Renderer, GL and Lua ownership stay on the game thread. Shader-location caching also avoids repeated synchronous driver queries; the existing Mesa driver already used a worker, so this does not claim to have newly enabled Mesa threading.

Empty entity-render callback lists avoid unnecessary cache capture and lookup. Ordinary fixed Update → canonical Render → GC remains necessary for game/mod side effects. Directly displaying these canonical poses during normal interpolation would mix current poses with delayed interpolated poses and cause backwards motion. Under sustained overload, the explicit fallback temporarily uses native presentation and abandons only overloaded scheduling debt; it does not run extra Lua at display frequency. Recovery waits for sustained spare capacity and resets visual history.

## Measured results

The archived user item combination, save and seven enabled mods are copied into an isolated, silent game for every run. The single player's start position/velocity are normalized once because native Continue chooses different entry doors. The real game/configuration remains under the user's control.

A roughly 60-second comparison of the original item combination measured native 59.99 FPS versus prototype R2 89.37 FPS during sustained fire. Native p99/max frame intervals were 27.51/33.81 ms; R2 was 17.13/25.89 ms. Both observed 26,927 natural shot births. Whole active-run measurements also retain the first-shot hitch: 62.83 ms native versus 47.73 ms R2. These results establish a substantial improvement over the old fork's reproduced approximately 3-FPS failure, not strictly constant 90 FPS.

A fresh matched-fixture comparison of prototype R7 measured native 60.00 FPS versus 89.62 FPS, with p99/max intervals of 26.80/32.67 ms versus 16.15/22.81 ms. Natural shot totals were 26,931 versus 26,935; these are recorded as a difference, not asserted identical. The older native fixture was rejected by the manifest comparator, so a fresh native run was required for this comparison.

Additional individual stress items, measured separately from that baseline:

| Extra item | Instrumented native FPS | Prototype FPS | Prototype |
| --- | ---: | ---: | --- |
| Technology Zero | 36.66 | 47.78 | R6, main CPU 6 |
| Trisagion | 30.55 | 38.28 | R6, main CPU 6 |
| Parasite | 56.15 | 55.26 | R5, main CPU 6 |
| Parasite | 56.15 | 56.91 | R6, unpinned experiment |

These heavy cases mostly use native overload fallback and **do not meet the 90-FPS target**. The unpinned Parasite trial changes two factors relative to R5 and cannot isolate pinning's effect. Shot-birth totals differ across the extra-item cases, particularly that trial; repeat-native and per-phase analysis are pending before claiming gameplay equivalence from these stress tests.

Separate R3 runs measured 60.07 and 79.90 FPS with world updates near 30 Hz and native callbacks near 60 Hz. Physical scanout, VRR, runtime refresh switching, the newest build's complete regression suite, and a final long-run acceptance remain outstanding.

## Current profiling boundary

An isolated main-thread CPU profile of the native Technology Zero case found repeated shader-location calls forcing Mesa synchronization. A positive-result cache with exact context/program/kind/name identity, frame resets, live builtin validation, dispatch ownership checks, mutation tracking and deferred-delete tombstones reduced this path. A later R6 profile attributed 467 of 2,179 samples to this fork, including 226 in location-cache lookup/hash; 246 were in Gallium and 684 in game text. These are CPU sample counts, not GPU time or frame-time percentages. Further lookup optimization is being tested.

Technology Zero's sampled lasers had two source points. Auditing the sampled subtype-4 rendering path found one body quad per adjacent pair, not length-dependent tessellation. Those source points are also used for damage. No collision samples, damaging lasers or projectiles have been removed, and no safe useful beam-density reduction has yet been established.
