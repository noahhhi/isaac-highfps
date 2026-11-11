# Attack-work budgeting: experimental results

These are opt-in work-budget experiments, not final release acceptance.
R11/R12 use fixed limits; R13 adds pressure-triggered activation. Exact DLLs, configuration, sustained-fire intervals, first-shot
boundaries and callback cadence are recorded in [the evidence](attack-work-progress.json).

The archived base combination includes Cricket's Body, Marked, Lead Pencil and
three Twisted Pair copies. The triple-item stress adds Technology Zero,
Trisagion and Parasite simultaneously. Every run uses the same isolated save
and seven enabled mods, with Character Enhance 1.28's reticle protection enabled unless the case explicitly uses 1.29 for resource preloading.

| Case | Sustained presentation rate | p99 interval | Maximum interval |
| --- | ---: | ---: | ---: |
| Triple items, instrumented native reference | 28.84 FPS | 53.38 ms | 56.01 ms |
| Triple items, R11 larger laser/effect budgets, no fallback | 72.15 FPS | 25.95 ms | see JSON |
| Triple items, R11 reduced laser/effect budgets, 52-second firing soak | 89.97 FPS | 12.00 ms | 27.05 ms |
| Triple items, R12, 52-second soak including image capture | 89.97 FPS | 12.27 ms | 20.01 ms |
| Five split items, instrumented native reference | 52.61 FPS | 40.50 ms | 48.43 ms |
| Five split items, R12 | 89.84 FPS | 13.21 ms | 28.60 ms |
| Triple items against four controlled targets, native | 28.39 FPS | 53.04 ms | see JSON |
| Same controlled targets, R12 | 89.71 FPS | 14.65 ms | 20.77 ms |
| Triple items, R12 at 80 Hz | 79.97 FPS | 12.89 ms | 20.09 ms |

The longer triple-item soaks are not a matched-duration native comparison.
R11's first-shot boundary still took 69.61 ms. Successful swaps alone do not
prove distinct images or physical scanout. Five split items means Parasite,
Cricket's Body, Compound Fracture, Lachryphagy and Haemolacria; Cricket's Body
was already present and receives one additional copy. This is not a claim that
its second copy stacks. All three newly covered native branches admitted real
work, and their guarded adapters reported no exceptions in the R12 case.

A four-second R12 PipeWire capture contains 360 unique decoded room images at
90.02 capture timestamps per second. All 359 adjacent pairs differ, using a
central room region excluding HUD, intensity threshold 2 and at least 32 changed
pixels; the observed minimum was 53,033 changed pixels. Capture latency is
unknown, and this does not certify physical scanout or every frame of the soak.

The controlled-target test retained real tear/laser damage and unchanged player
health. Total target HP loss fell from 8,155.375 native to 2,463.1875 under the
aggressive budget (approximately 30%). This is a substantial damage tradeoff,
not damage-equivalent optimization. The four high-HP targets are anchored each
world update in both arms; this measures synthetic collision/hurt work rather
than ordinary enemy AI. A preceding freeze-only fixture drifted into a room
change and was rejected, not counted as a pass.

## What is limited

Native pool sizes do not bound allocation attempts or recursive work. The
experimental gates run before the expensive operation:

- Parasite pairs, Cricket's Body quads and Compound Fracture pairs share a child
  creation budget. Lachryphagy and plain-tear Haemolacria bursts charge one child
  per admitted iteration. Parent cleanup and native loop bookkeeping remain.
- Technology Zero neighbor searches and new Trisagion beams have separate
  per-tick limits and share conservative laser-capacity reservations. Existing
  beam update paths remain native; denying a relink can affect subsequent native
  beam maintenance.
- Known player tear poofs use a separate cosmetic budget. Target reticles,
  unknown effects and Blood Splat render-to-texture work are excluded.

Lachryphagy absorption remains native. Weapon-specific Haemolacria branches are
not covered by the plain-tear gate. Lua execution, nested updates, loading,
rendering, unsupported ownership and failed memory checks retain native work.
Population snapshots plus reservations are admission estimates: unrelated native
or Lua allocations can exceed them. They are not a replacement global pool cap.

The tested reduced-effect settings are `SpawnChildrenPerTick=6`,
`TechZeroSearchesPerTick=1`, `TrisagionCreatesPerTick=1`, `PoofsPerTick=1`,
`TearSoftLimit=128`, `LaserSoftLimit=8`, `PoofSoftLimit=32`. These deliberately
reduce player attack output and damage opportunities. In this experimental
R11/R12 implementation they apply whenever enabled, including ordinary low-load
bursts. R13's additional `AdaptiveWorkBudget=1` applies these limits only when
its world-tick decision indicates pressure.

R13 combines bounded fixed/presentation work samples with existing populations
and the previous world's attempted work. Denied requests still count as demand;
otherwise successful limiting would immediately disable itself and provoke
another flood. Search attempts are distinct from predicted laser creation.
The estimate scales presentation work to the configured cap/display rate and
does not label renderer wall time as GPU utilization. Loading, invalid samples,
room/run changes and backwards world stamps invalidate the appropriate evidence.
The first unobserved burst can precede activation by one world tick.

A detailed 52-second R13 triple-item soak measured 89.81 FPS with p99 13.01 ms.
A separately matched light-counter fixture measured native 30.01 FPS versus
R13 89.85 FPS, with p99 45.80 versus 11.46 ms. This profile removes repeated
entity censuses and detailed effect/removal observers, and explicitly reports
those metrics as unmeasured; it still contains minimal validation hooks.
An explicit native DROP test verified target removal and zero aim, followed by
pressure levels 3 → 2 → 1 → 0 without repeated activation. A separate functional
diagnostic removed only the three Twisted Pair copies before one Haemolacria
volley: all 45 observed Haemolacria child requests passed, all attack/poof denial
counts were zero, and pressure remained off. That altered inventory is not used
as a performance comparison for the original combination.

## Final R14 and Character Enhance 1.29 check

The final 52-second light-counter triple-item soak measured **89.96 FPS**, p99
**12.13 ms** and maximum **22.35 ms**, with two intervals above 16.7 ms and none
above 33 ms. The first-shot boundary was **22.20 ms**. World and main callback
rates remained **30.002 / 60.003 Hz**. This is one final-candidate soak, not a
matched native/CE1.29 comparison or a guarantee for every scene.

Character Enhance 1.29 optionally retains ten attack sprites at actual new/continued
run start when related items are carried. A controlled Continue pair measured
82.25 ms with preloading off and 22.08 ms on at the first-shot boundary. Fresh
runs with no related items skipped it; a controlled Marked grant loaded ten
resources. Both language menus, saved toggles and normal save/exit passed.
R14 only changes failed shader-hook rollback ownership relative to R13; it does
not add parallel native gameplay. All final tests used silent isolated profiles.

## CPU, GPU and threading

The protected native triple-item reference used approximately 98% of one CPU
core on its main thread. R12's measured window used approximately 51%, while its
DRM graphics client averaged approximately 33% engine utilization. Device-wide
GPU busy averaged 63% in that window and includes other applications; it is not
interchangeable with the client counter. Short samples and shared DRM clients
also limit attribution.

The native game already reuses player aim and weapon configuration for Incubus
and Twisted Pair. The direction calculator is a possible immutable numeric
worker task, but existing sampling did not show it as a meaningful hotspot.
Full shot-parameter evaluation accesses shared RNG and source-specific state;
moving independent firing directions to threads would not make entity pools,
Lua callbacks or world state independent. No native gameplay work is moved to
workers by these gates.

## Remaining validation

Broader natural-combat behavior, additional low-load coverage and physical VRR remain unverified. Resource preloading reduces the observed first-shot stall but does not eliminate all long frames. Two R11/R13 launches timed out after 90 seconds before fixture execution; those attempts are retained separately and are not counted as passes. Ten later startup/menu-exit checks passed without establishing the cause or a fix for the intermittent stalls. The completed runs saved through the native pause menu
and exited through a verified `WM_DELETE_WINDOW` request with native shutdown
evidence.
