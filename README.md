# isaac-highfps — Repentance+ 1.9.7.15.J374

This fork of [hunchcldev/isaac-highfps](https://github.com/hunchcldev/isaac-highfps) separates Isaac's display output from its native update clocks. It targets the **32-bit Repentance+ 1.9.7.15.J374 executable** and is being tested on Steam Deck under Proton.

The main update stays at 60 Hz, full world updates stay at 30 Hz, and presentation follows the configured display or frame cap. Native entity and room-scroll previews provide intermediate positions. Audited native render-side animation updates and supported Lua drawing run on the fixed timeline, with their results reused for additional presentations.

**0.13.0-j374.7 — performance work in progress.** The draw cache now uses indexed lookups and a bounded 16,384-entry / 64 MiB default budget. The main game thread can stay on one logical CPU, while copied log records are written by a separate worker. Empty entity callbacks avoid capture work; positive built-in shader locations are reused within guarded update/render scopes. Sustained overload shows complete native frames instead of repeatedly executing expensive hidden catch-up renders. This fallback can run below 60 FPS when native work is overloaded; it is not a claim of stable 90 FPS. See the [performance findings and remaining work](docs/validation/performance-progress.md).

**Experimental attack-work limits are now available, disabled by default.** Separate pre-allocation budgets cover split children, laser searches/creation and known tear poofs. A reduced-effect triple-item test sustained approximately 89.97 presentations per second over 52 seconds, with fixed world/mod clocks; it deliberately changes attack output and still has first-shot stalls. See [scope, measurements and remaining validation](docs/validation/attack-work-progress.md).

**0.13.0-j374.6 — character-selection carousel.** The wheel now interpolates the native per-character positions and scales during extra display frames, then restores them before the next update. It preserves selection, held-key repeat and native rotation timing, including wrap and reversal; it does not call the rotation updater at the display rate. See the [carousel evidence](docs/validation/character-carousel.json). This uses up to one 60 Hz interval of display history; input processing is unchanged.

**0.13.0-j374.5 — Lua mod drawing and render-triggered continuation.** Ordinary ANM2 sprites now use their verified native shader/layout contracts, with resource lifetime tracking for image reloads and first texture uploads. This restores missing mod overlays without running their Lua callbacks at the display rate. A synchronous start/continue accounts for its complete blocking work; a render that changes the room/run cannot expose the new scene until a complete canonical frame is available. See the [mod image regressions](docs/validation/mod-image-regressions.md) and the individually scoped reports below.

The earlier menu-page interpolation, large-room camera correction and fixed-clock changes remain in place. The reports retain their tested DLL hashes; this is not a claim that every mod, animation or graphics path is compatible. The upstream 1.9.7.17/J460 DLL cannot be installed directly on J374: bypassing its version check does not make its addresses or calling conventions compatible.

## Install on Steam Deck

1. Exit the game. Back up any existing `winmm.dll`, `isaac-highfps.ini`, game options and saves.
2. Copy the **J374 build** of `winmm.dll` beside `isaac-ng.exe`.
3. Add this Steam launch option, preserving any other options you use:

   ```text
   WINEDLLOVERRIDES="winmm=n,b" %command%
   ```

4. Start with the defaults below. Restart the game after changing the INI.

```ini
[highfps]
Enabled=1
Interpolate=1
MaxFps=0
VSync=1
IdleFps=0
LuaVanillaCadence=1
LuaOverlay=1
Log=1
Diag=0
```

`MaxFps=0` follows the available presentation pacing; it does not force 90 or 100 FPS. `VSync=1` requests actual OpenGL swap synchronization. A display mode limited to 80 Hz can therefore use the same configuration. To request a 100 FPS cap, set `MaxFps=100`; presenting 100 distinct frames also requires a suitable display mode and enough rendering performance.

`IdleFps=0` keeps the normal display pacing in menus and while paused. There is no default forced 60 FPS idle cap.

Variable presentation intervals are supported by the scheduler. VRR still depends on the display, driver, compositor and game session enabling it. **Physical VRR and runtime refresh-change validation are pending.** An attempted runtime compositor-limit change did not change measured presentation cadence, so it is not counted as a passing variable-rate test. The offline variable-interval scheduler tests and separate 80/100 Hz runs cover different scopes.

To disable the mod, set `Enabled=0` and restart. To remove it, exit the game, remove this fork's DLL and restore any previous DLL/configuration and launch option from your backup. The mod does not patch the executable on disk.

For the experimental triple-item stress settings, use [the FPS-priority preset](isaac-highfps.fps-priority.ini.example). Back up the active INI, copy the preset beside `winmm.dll` as `isaac-highfps.ini`, then set `Enabled=1` and restart when ready. The preset ships with `Enabled=0`; the ordinary defaults above leave attack limits disabled. Its `MainThreadCpu=6` matches the tested Deck; use `-2` on other machines to choose the main thread's current allowed CPU.

This preset trades attack output for frame rate: the controlled-target test retained about **30% of native total damage**. Split-child and laser-work limits reduce CPU creation/search work; a separate poof budget reduces cosmetic work. These settings do not diagnose GPU saturation or independently control all GPU costs. Native game, RNG, Lua and GL work remain on the game thread; the worker handles copied log records. See the [measured CPU/GPU scope and attack tradeoffs](docs/validation/attack-work-progress.md).

## Configuration

| Setting | Default | Meaning |
| --- | --- | --- |
| `Enabled` | `1` | Enable the native component. |
| `Interpolate` | `1` | Preview intermediate native entity/camera positions for presentation. |
| `MaxFps` | `0` | Follow presentation pacing, or request a positive FPS cap. |
| `VSync` | `1` | `1`: request swap synchronization; `0`: disable it; `-1`: follow the engine's interval. |
| `IdleFps` | `0` | No separate idle cap; a positive value requests one. |
| `LuaVanillaCadence` | `1` | Enable the fixed native-render/callback pipeline. |
| `LuaOverlay` | `1` | Legacy configuration name that enables supported draw capture/replay in this pipeline. |
| `Log` | `1` | Write `%TEMP%\isaac-highfps.log`, with a game-directory fallback. |
| `Diag` | `0` | Detailed timing/cache diagnostics; enable for controlled tests or bug reports. |
| `MainThreadCpu` | `-2` | Pin the main game thread to its current allowed logical CPU; `-1` disables pinning, `0..31` selects an allowed group-0 CPU. Other engine threads retain their affinities. |
| `AsyncLog` | `1` | Write copied log records on a lower-priority worker; no game/Lua/GL work is moved to it. |
| `CacheEntries` | `16384` | Draw-cache entry budget, clamped to 1024–32768. |
| `CacheMiB` | `64` | Retained CPU draw-cache budget, clamped to 16–128 MiB; native image/GPU storage and transient capture are additional. |
| `CacheShaderLocations` | `1` | Reuse positive built-in shader locations within guarded native update/render scopes. |
| `OverloadFallback` | `1` | Temporarily show native frames when sustained work exceeds the fixed budget; recover without fast-forwarding overload debt. |
| `OverloadRollingRecovery` | `0` | Experimental recovery using a bounded cost window and retry backoff; avoids requiring every recovery sample to be below 8 ms. |
| `SpawnWorkBudget` | `0` | Experimental player Parasite/Cricket's Body, Technology Zero and Trisagion admission limits; changes attack output. |
| `ExtraSplitWorkBudget` | `0` | Experimental Compound Fracture, Lachryphagy and plain-tear Haemolacria child limits; shares the child budget. |
| `PoofWorkBudget` | `0` | Experimental admission limit for known player tear poofs; separate from attack work. |
| `AdaptiveWorkBudget` | `0` | With the budget options enabled, apply their limits only while measured work or forecast entity demand indicates pressure; restore unrestricted work after sustained quiet. |
| `SpawnChildrenPerTick` | `6` | Shared admitted child count per full world tick; pairs cost 2 and quads cost 4. |
| `TechZeroSearchesPerTick` / `TrisagionCreatesPerTick` / `PoofsPerTick` | `4` each | Separate work quotas per full world tick. |
| `TearSoftLimit` / `LaserSoftLimit` / `PoofSoftLimit` | `128` / `32` / `96` | Conservative population admission thresholds, not global hard caps. |
| `MinimumPlayerLasers` | `4` | Estimated player-laser attempt allowance when other lasers occupy the soft limit; does not guarantee actual successful creation. |
| `FrameTiming` | `0` | Log successful presentation timestamps for isolated measurements. |
| `NativeTimingProbe` | `0` | With `Enabled=0` only, instrument native SwapBuffers for comparison; leaves native update/render/limiter and thread affinity unchanged. |

Keep both Lua options enabled for the tested pipeline. In this fork, `LuaOverlay=1` no longer describes the old per-callback FBO overlay approach: the active pipeline captures supported native draw commands and replays them. Turning either option off leaves the tested compatibility path and is not a recommended fix for a rendering fault.

## Mod drawing and compatibility

A fixed tick completes native **Update → Render → GC** before the next tick. Supported main and entity render callbacks execute in that native render, so state changes made by callbacks are not repeated for every display refresh. Additional presentations replay supported drawing without executing those Lua callbacks again.

**Update-owned render-to-texture is a separate category.** Some effects render and invoke Lua while the engine is updating, then are removed. Those calls must execute natively even if the previous display pass was pure; they must not be suppressed as extra presentations. Blood Splat (effect variant 7) exposed this boundary. The dedicated direct-spawn regression restored all 30 native render callbacks across ten instances; the previous build produced only 20. Baked pixels and natural damage producers need separate evidence. [Report](docs/validation/blood-callback-comparison.json).

The replay keeps the drawing coordinates sampled at the canonical 60 Hz render. It does **not** guess how to translate arbitrary mod graphics to an interpolated entity position. A native sprite can move smoothly between ticks while a mod-drawn label or marker attached to it still changes position at 60 Hz.

The native coloroffset, champion, dogma and gold Sprite shaders are supported with validated layouts and uniform contracts. Image format changes seal pending vertex data first. Verified native texture initialization can complete before the first captured draw; deletion or mutation of a real captured dependency remains invalidating, including numeric texture-name reuse. Unknown native extensions and arbitrary GPU sampler changes are not covered by this initialization path.

Capture is bounded and checks draw layout, resource ownership and render state. Custom shaders, direct GL operations and other unsupported drawing can prevent a scope from being replayed on extra presentations. Shader-parameter callback caching is a separate path; broad custom-shader compatibility remains under validation. Report missing or flickering mod graphics with the log and mod list rather than assuming all render callbacks are safe to resample.

Input filtering preserves every native update/canonical query, including nested updates. Extra display frames reuse the last scalar MC_INPUT_ACTION result for that entity, hook and action in the current fixed tick. Handled `false` differs from a nil result; nil and unsampled queries leave the caller's output untouched and use its native raw-input branch. Unsampled cases are logged. Input queries are not limited to 60 calls per second. See the [input design/tests](docs/validation/input-cache-unit.json) and [engine controller exercise](docs/validation/controller-input-compatibility.json).

The Lua globals `HIGH_FPS_NATIVE`, `HIGH_FPS_VERSION` and `HIGH_FPS_RATE` remain available. `HIGH_FPS_RATE` reports successful presentation cadence; counting `MC_POST_RENDER` is not an output-FPS measurement in the fixed pipeline.

## Validation status

Tests use a separate game copy, Proton prefix and saves. Native 60 Hz on **the same J374 executable** is the baseline. Reports identify the DLL, fixture and executable hashes so results can be tied to the revision that was tested.

| Check | Observed result | Scope |
| --- | --- | --- |
| Character wheel, 80/90 Hz | Native per-transition poses and held-repeat timing are preserved. In declared short-run pixel blocks, repeated adjacent ROI pairs fell from old90 109/301 to final90 0/352 and final80 0/297. Four actual mid-motion reversals were exercised. | Conditional movement statistic, not all-frame stutter or latency. Two ~25 ms intervals occurred while settled at 80 Hz; external input phase differs at some long-hold/reversal boundaries. [Report](docs/validation/character-carousel.json). |
| Mod image compatibility | GoodTrip's missing map is restored at 80/90 Hz; the R5 80 Hz repeat shows the map in 162/162 confirmed-Tab frames and its Sprite control in 320/320 captures. Regret's separate/two-icon resource reloads preserve the native fade states. Blind option acquisition also uses the native pickup/removal path. | Captured windows and candidate hashes are explicit, including OS key-boundary and capture sampling differences. [Image reports](docs/validation/mod-image-regressions.md). |
| Character/UI paths | Bethany shield charge, damage and exhaustion state; Eden's native continuation, choice placement and black-to-visible fade; MCM browse/repeat/settings retention. | Specific enabled features, not every Character Enhance module or MCM editing path. [Bethany](docs/validation/ce-bethany-compatibility.json), [Eden](docs/validation/ce-eden-compatibility.json), [MCM](docs/validation/mcm-browse-compatibility.json). |
| Input callback replay | Controller directions/edges, native laser creation, Schoolbag and MCM interaction are compared with native at 90/45 FPS. Extra presentations no longer execute MC_INPUT_ACTION Lua. | Process-local synthetic XInput reaches the real engine controller API; physical hardware, multiplayer and unsampled custom filters are separate scopes. [Report](docs/validation/controller-input-compatibility.json). |
| Gameplay mods | Real Forgotten/Sword/Bag weapon creation and hits; native machines' resource/acceleration, leaving a live machine and low-health acceleration refusal. | Fixed-seed recorded states; TimeMachine's deliberate extra updates are preserved. [Range](docs/validation/range-mod-compatibility.json), [TimeMachine](docs/validation/timemachine-mod-compatibility.json). |
| Main-menu ↔ character-page slides, final 80/90 Hz | Identical adjacent captured-frame pairs in declared movement windows: old90 109/316 (34.49%), new90 5/316 (1.58%), new80 3/280 (1.07%). Each new case's 488 canonical viewport samples matched native60 to log precision; restore and endpoint checks passed. | Four round trips per case. Capture/input timestamps have startup uncertainty; this is not an exact input-latency measurement or coverage of every menu. [Report](docs/validation/menu-presentation.json). |
| Large-room camera, final 80/90 Hz | Old 90 Hz camera: 22 out-of-bounds samples and 20 reverse steps against a consistently directed native camera. New 80/90 Hz cases: zero for both checks, with interpolation staying within each pair of native samples. All 2,175 normalized canonical records matched native60. | One fixed-seed Greed tall room, long travel/stops/reversal; numerical presentation checks and canonical-state checks have separate scopes. [Camera](docs/validation/camera-presentation.json), [state](docs/validation/greed-canonical.json). |
| Final R5 callback/draw smoke | All 483 recorded events matched native60; 286 paired player/tear position samples had zero error. Measured output was 100.010 FPS. One inspected capture showed MAIN/PLAYER/TEAR labels and M/P/T markers. | Same R5 DLL as the 80 Hz test; one inspected frame is not every-frame pixel coverage. [Report](docs/validation/release-r5-evidence.json). |
| Final R5 adaptive 80 Hz | Measured output 79.994 FPS, active game regression 30.003 Hz; measured callback/update counts and listed player/timer/fire/bomb states matched. The 33-shot game-tick sequence matched. | Natural tear births differed; early birth-relative displacement was compared separately. [Report](docs/validation/release-r5-evidence.json). |
| Callback order at other output rates | Earlier 90 and 45 Hz cases matched all 483 recorded events and callback state/order in the controlled fixture. | Reports retain each tested DLL hash. [Report](docs/validation/canonical-overlay-comparison.json). |
| Output versus update cadence | 100 Hz fixture: median present interval 9.999 ms, Manager regression 59.992 Hz, active game rate 30.004 Hz. | Isolated Proton/Gamescope presentation trace, not physical scanout. [Report](docs/validation/render-types-comparison.json). |
| Rendering below 60 FPS | 45 Hz fixture: median present interval 22.222 ms, Manager regression 60.004 Hz, active game rate 30.000 Hz. | Exercises multiple complete fixed ticks between some presentations. [Report](docs/validation/render-types-comparison.json). |
| Main and ten entity callback categories | All eleven categories were observed at 60, 100 and 45 Hz. | Coverage is not universal equivalence: short-lived tear/projectile totals differed; the Blood Splat Update-owned render path is covered separately below. [Report](docs/validation/render-types-comparison.json). |
| Update-owned Blood Splat rendering | Ten direct spawns: 540 normalized event records and ten terminal summaries matched native60; 30 render calls restored, including ten Update-owned calls. | Controlled direct-spawn path and callback state/order; not natural NPC-damage production or baked pixels. [Report](docs/validation/blood-callback-comparison.json). |
| Room transitions | WALK, FADE and TELEPORT had matching phase sequences and aligned Manager-tick spans; maximum paired alpha-curve timing differences were 4.24, 6.49 and 10.13 ms. | One native 60 Hz/output 100 Hz pair per transition style; not all animation types or repeatability. [Report](docs/validation/transition-matrix-comparison.json). |
| Screen-shader parameter callbacks | All 2,240 recorded parameter events matched after excluding wall time, with matching per-game-tick order. | Parameter cadence/table isolation only; GPU pixels and Sprite/Image custom shaders remain pending. [Report](docs/validation/shader-comparison.json). |
| Normal/Hard/Greed menu fades | All 16 paired fade curves matched their interior values over 18 Manager ticks; maximum relative curve timing difference was 10.474 ms. | Real difficulty changes in character selection. Greedier and other menu paths are not covered. [Report](docs/validation/menu-curve-comparison.json). |
| Ordinary floor transition | Stage 1→2 trapdoor: all 125 event snapshots matched after time/origin normalization; request through ten resumed world updates took 11.784/11.787 s. | One ordinary floor route, not every cinematic or alternate stage. [Report](docs/validation/floor-comparison.json). |
| Moving pause/resume | About three seconds paused with nonzero retained velocity: position, game frame and TimeCounter stayed fixed. Each two-second resume window advanced 60 world/120 UI ticks. | Within-run invariants; the two run seeds differ. [Report](docs/validation/pause-moving-comparison.json). |
| Glowing Hour Glass rewind | 104 normalized records matched; one use restored the room/game clock, then resumed ten world updates with matching counters. | Actual RestoreState/backwards-clock path; no live Lua VM replacement was observed. [Report](docs/validation/rewind-comparison.json). |
| Remaining coverage | Physical/runtime VRR, broader menu/floor/custom-shader paths and persistent dice/emote behavior. | No universal mod compatibility claim. |
| Physical VRR | Pending. | Requires suitable display hardware and a VRR-enabled presentation stack. |

No single FPS counter proves correct gameplay speed or smooth output. The validation separates simulation state/timers, callback order, native presentation timing and visible pixels. See the [port notes](docs/port-1.9.7.15.md) and [clock design](docs/clock-design.md) for details and remaining tests.

## Build

Use the J374-specific Linux/WSL build with **clang-cl targeting the x86 MSVC ABI**, `lld-link`, Python 3, and an x86 Windows CRT/SDK sysroot:

```sh
XWIN_ROOT=/path/to/x86-msvc-sysroot \
WINMM_SOURCE=/path/to/32-bit/winmm.dll \
bash build-wsl.sh
```

The script defines `HIGHFPS_BUILD_19715`, uses `/MT` for the static CRT, and writes `build/winmm.dll`. `WINMM_SOURCE` is parsed locally to generate forwarding exports; do not distribute or commit that system DLL. GNU-ABI MinGW is not a substitute for this build's x86 SEH/MSVC ABI requirements. The unmodified upstream `build.bat` path is not the documented J374 build.

Startup cache installation waits for the initializer thread to exit, and required-hook readiness is published only after successful patching. If installation reports failure, the fork falls back to 60 FPS with native Lua execution. Two retained R11/R13 attempts timed out after 90 seconds before fixture execution; their cause remains unresolved. Ten later startup/menu-exit smoke tests passed, which does not establish that the intermittent hang is fixed. Earlier startup limits are retained in the [port notes](docs/port-1.9.7.15.md).

Build details and rollback notes are in [docs/port-1.9.7.15.md](docs/port-1.9.7.15.md).

Standalone checks are documented in [tests/README.md](tests/README.md). `tests/run-tests.sh core` runs the production-clock, UI and mock draw-cache tests; `abi` adds the game-independent Windows x86 tests. The optional shader test uses a supplied local Lua runtime. None of these tests starts the game.

## Credits and license

MIT; see [LICENSE](LICENSE). Based on the original native high-FPS work by [hunchcldev](https://github.com/hunchcldev/isaac-highfps). REPENTOGON function signatures were used as reverse-engineering references; a signature match was checked against J374 instructions and calling conventions before use. Game executables, memory dumps, saves and system DLLs are not part of this repository.
