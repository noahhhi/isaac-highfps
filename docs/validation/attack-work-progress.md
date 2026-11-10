# Attack-work budgeting: experimental results

These are opt-in fixed-budget experiments, not the final adaptive-pressure
release. Exact DLLs, configuration, sustained-fire intervals, first-shot
boundaries and callback cadence are recorded in [the evidence](attack-work-progress.json).

The archived base combination includes Cricket's Body, Marked, Lead Pencil and
three Twisted Pair copies. The triple-item stress adds Technology Zero,
Trisagion and Parasite simultaneously. Every run uses the same isolated save
and seven enabled mods, with Character Enhance 1.28's reticle protection enabled.

| Case | Sustained presentation rate | p99 interval | Maximum interval |
| --- | ---: | ---: | ---: |
| Triple items, instrumented native reference | 28.84 FPS | 53.38 ms | 56.01 ms |
| Triple items, R11 larger laser/effect budgets, no fallback | 72.15 FPS | 25.95 ms | see JSON |
| Triple items, R11 reduced laser/effect budgets, 52-second firing soak | 89.97 FPS | 12.00 ms | 27.05 ms |
| Triple items, R12, 52-second soak including image capture | 89.97 FPS | 12.27 ms | 20.01 ms |
| Five split items, instrumented native reference | 52.61 FPS | 40.50 ms | 48.43 ms |
| Five split items, R12 | 89.84 FPS | 13.21 ms | 28.60 ms |

The longer triple-item soaks are not a matched-duration native comparison.
R11's first-shot boundary still took 69.61 ms. Successful swaps alone do not
prove distinct images or physical scanout. Five split items means Parasite,
Cricket's Body, Compound Fracture, Lachryphagy and Haemolacria; Cricket's Body
was already present and receives one additional copy. This is not a claim that
its second copy stacks. All three newly covered native branches admitted real
work, and their guarded adapters reported no exceptions in the R12 case.

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
implementation they apply whenever enabled, including ordinary low-load bursts.
Pressure-triggered activation and recovery are still being developed.

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

Adaptive pressure activation, actual damage/collision stress against enemies,
first-shot stalls, low-load fidelity, distinct-image analysis, physical VRR and
the final combined regression run remain outstanding. One R11 launch timed out
before fixture execution; that failed attempt is retained separately and is
not counted as a pass. The completed runs saved through the native pause menu
and exited through a verified `WM_DELETE_WINDOW` request with native shutdown
evidence.
