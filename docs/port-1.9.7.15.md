# Repentance+ 1.9.7.15.J374 port

Version `0.13.0-j374.3` builds a dedicated DLL for the 32-bit **1.9.7.15.J374** game. Production camera source is commit `1444261`; [camera presentation](validation/camera-presentation.json) and [canonical Greed](validation/greed-canonical.json) reports bind its 80/90 Hz verification. The earlier R5 clocks remain covered by their original, separately hashed reports. Its development and build environment is Fedora WSL; runtime checks use an isolated Steam Deck/Proton game copy. The original J460 binary is incompatible with J374, including its update wrapper and graphics ABI. Do not remove version guards to load the other build.

## Executable and ABI boundary

The J374 image check expects PE timestamp `0x68D45723` and image size `0x8B6000`. Patch sites are additionally checked against their expected instructions. Absolute operands require relocation-aware comparisons; matching only a version string is insufficient.

| Item | J460 / 1.9.7.17 | J374 / 1.9.7.15 |
| --- | --- | --- |
| Manager global | `0xC7169C` | `0xC0C09C` |
| Game global | `0xC71678` | `0xC0C074` |
| Manager frame counter | `+0x4ABBC` | `+0x4ABA8` |
| Main loop | `0x931050` | `0x929330` |
| Manager update | `0x954CD0` | `0x94CAD0` |

J374 directly calls `Manager::Update(bool)` through the sequence at `0x9295D6`. Its counter increments at the end of the update: **even on entry means the full 30 Hz game phase**, and odd means the half-step phase. Copying J460's wrapper/parity assumption into this call-site adapter gives the wrong interpolation sampling phase.

The port also covers the outer input update at `0x9295CF`. Input edges must be sampled with actual fixed updates; consuming them on intermediate display frames can lose a press before game logic sees it. The adjacent native render and GC calls participate in the ordered fixed-tick pipeline.

## Build on WSL or Linux

Required tools and local inputs:

- `clang-cl` and `lld-link`, using `i686-pc-windows-msvc`.
- Python 3 for the export-table generator.
- An x86 Windows CRT/SDK sysroot, for example one prepared with `xwin` under its license terms.
- A local 32-bit `winmm.dll` whose export table supplies the forwarding names. A suitable Wine system copy can be used.

```sh
XWIN_ROOT=/path/to/x86-msvc-sysroot \
WINMM_SOURCE=/path/to/32-bit/winmm.dll \
bash build-wsl.sh
```

`build-wsl.sh` supplies `/DHIGHFPS_BUILD_19715`, `/std:c++17`, `/EHsc`, `/O2` and `/MT`. The output is `build/winmm.dll`. `/MT` links the CRT statically. The tested compiler family is Fedora clang-cl 22 with the MSVC ABI; GNU-ABI LLVM-MinGW did not support the required x86 SEH build path.

The system DLL is an input to export generation, not a file to bundle. Do not commit it, the game executable, process memory, copied saves, credentials, generated test executables or local prefixes. Build artifacts should be delivered separately with their hash and supported game version.

## Install, configure and roll back

Exit the game before replacing the DLL. Preserve the previous DLL/INI, game options, saves and any pre-existing launch options. Copy the J374 DLL beside `isaac-ng.exe`; on Proton use:

```text
WINEDLLOVERRIDES="winmm=n,b" %command%
```

Recommended starting settings:

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

There is no required 90 FPS cap and no requirement to disable VSync. With VSync enabled, the actual display mode can provide the pacing, including an 80 Hz mode. A positive `MaxFps` requests an additional cap; `MaxFps=100` cannot create 100 Hz physical output on a display currently limited to 90 Hz. `IdleFps=0` avoids a forced lower cap when paused or in menus.

Both Lua settings enable the tested fixed-render/draw-cache path. `LuaOverlay` is a legacy option name, not a statement that the new pipeline renders every callback into an FBO. Disabling it is not the recommended compatibility configuration.

For a reversible disable, set `Enabled=0` and restart. To roll back, exit the game, restore the saved DLL and INI (or remove this fork's files if none existed), and restore the prior launch option and any options changed during installation. Keep the save backup until the replacement version has been accepted. No installation step requires changing save contents.

## What changed from the initial port

The initial J374 port retained several assumptions from the upstream high-refresh architecture. It also disabled the unported Lua overlay. The current pipeline corrects J374 parity/input handling, completes native Update → Render → GC for each fixed tick, and reuses supported drawing on additional output frames.

Short loading debt was possible in the upstream scheduler too. The fork's early multi-update catchup made that debt more visible by advancing many transition ticks before a displayed image. Current lifecycle hooks explicitly exclude measured loading/setup intervals. Ordinary rendering cost remains part of elapsed gameplay; a slow frame is not automatically classified as a load.

Native menu scalars and shared UI animation helpers have their own timing repairs. Reused preview sprites still perform required Play/type initialization. Pure presentation replays their recorded canonical animation result rather than incrementing the timeout/frame again. Cache ownership is reset across lifecycle epochs, including object-address reuse.

## Verification and limits

The evidence directory separates test cases by executable, DLL, fixture and mod-set identifiers. Reports describe the observed scope rather than asserting that the entire game is equivalent.

- [Canonical callback comparison](validation/canonical-overlay-comparison.json): native 60 Hz versus the 90 Hz pipeline, with 300 common update ticks and 483 identical recorded events after removing wall timestamps. Player and tear positions matched in 286 paired samples each. The fixture deliberately mutates positions from main/player/tear render callbacks; it belongs only in an isolated game copy.
- [Callback-type and cadence comparison](validation/render-types-comparison.json): eleven callback categories observed at native 60 Hz, output 100 Hz and output 45 Hz. The 100 Hz case measured approximately 59.992 Manager ticks/s and 30.004 active game ticks/s; the 45 Hz case measured approximately 60.004 and 30.000. Main/player/familiar/NPC/pickup/bomb fixture counters matched at the recorded snapshots. Natural short-lived tear/projectile totals differed, and the Update-owned Blood Splat render-to-texture correction is covered by its separate direct-spawn regression below.
- [Room-transition matrix](validation/transition-matrix-comparison.json): WALK, FADE and TELEPORT each have one matched native 60 Hz/output 100 Hz pair. Phase sequences matched; spans aligned from the first observed positive mode1 alpha to completion used 17/35/91 Manager ticks in both versions. Maximum timing differences along common alpha samples were 4.24/6.49/10.13 ms. No observed interval contained three or more ticks within 12 ms. These polling traces do not establish native baseline load duration, a repeatability distribution or pixel-level smoothness.
- [Screen-shader parameter comparison](validation/shader-comparison.json): 2,240 recorded events matched after excluding wall timestamps, including per-game-tick order and independent A/B shader code values. The nine-second window had 270 world updates and 540 calls each for main render and shader A/B. This validates the registered screen-shader parameter path; visible GPU colors and Sprite/Image shader selection need separate evidence. The historical manifest did not hash `shaders.xml`, so resource identity is not established as strongly as the executable/Lua fixture.
- [Menu difficulty curves](validation/menu-curve-comparison.json): real Down/Up input covered Normal/Hard/Greed. All 16 matched vignette/black/yellow fade curves used 18 Manager ticks between the same interior values; maximum relative curve timing difference was 10.474 ms. Startup poison and the one-byte menu dirty flag are excluded from state comparisons. This does not cover Greedier or every menu animation.
- [Ordinary floor transition](validation/floor-comparison.json): a natural stage 1→2 trapdoor produced 125 matching event snapshots after removing wall time and normalizing the starting UI counter. Request→new-level took 8.523/8.517 s with 502 UI ticks, arrival→unpaused took 2.928/2.940 s with 177 UI ticks, and the complete span through ten resumed world updates took 11.784/11.787 s. Alternate stages and other cinematic routes remain outside this case.
- [Update-owned Blood Splat regression](validation/blood-callback-comparison.json): ten direct spawns produced 30 render callbacks in native60 and the fixed100 build, versus 20 in the negative control. All 540 normalized event records and ten terminal summaries matched; the candidate forwarded ten callback56 invocations outside Manager Render and recorded no corresponding cache miss. The attempted NPC-damage producer did not yield this variant, so natural damage production and baked pixels remain unverified.
- [Moving pause/resume](validation/pause-moving-comparison.json): approximately three-second interior windows retained velocity `(3.572232, 0)` while position, GameFrame and TimeCounter stayed constant. Manager cadence remained approximately 60 Hz. Each two-second resumed window advanced 60 world/120 UI ticks without a sub-12 ms world-update interval. The runs used different seeds; this is a within-run pause invariant, not cross-seed world equivalence.
- [Glowing Hour Glass rewind](validation/rewind-comparison.json): 104 event records matched after explicit token/time/UI-origin normalization. Each run used the item once; GameFrame went from 45 back to restored-room frame30, then returned at31 and completed at41. Use→return took 1.027/1.040 s, followed by ten resumed updates. No runtime Lua VM replacement was observed (`vm_resume=0`); that is covered only by the separate mock/lifetime test domains.
- [Final R5 evidence](validation/release-r5-evidence.json): the exact delivered DLL measured 100.010 FPS in the callback/draw smoke and 79.994 FPS in the strict-seed global case. Its 483 callback records matched native60; the global case's measured counters and listed player/timer/fire/bomb fields matched, including the 33-shot sequence. Natural tear birth differences are retained as a limitation rather than normalized into a claim of identical initial conditions.
- Pure unit tests exercise scheduler debt/exclusion, UI timing and replay ownership independently of the game. They cannot establish physical scanout, visual smoothness or compatibility with arbitrary mods.

**Pending acceptance:** broader floor/nightmare routes, Greedier and other menu paths, render-triggered lifecycle changes and untested restart/VM-reload routes, persistent dice/emote states, broader shader behavior, extended combat, physical VRR and runtime refresh changes. The attempted runtime FPS-limit control did not change actual output cadence, so it does not validate dynamic pacing. Specific transition/menu/shader fixtures may add evidence while these broader categories remain open. Do not relabel a partial fixture as full coverage.

## Startup readiness

The initializer and game thread must not install overlapping patches concurrently. The game thread now waits for the initializer thread to have actually exited before installing the complete cache pipeline. A usable trampoline pointer alone does not mark a hook installed: readiness is published only after its patch succeeds. Missing required readiness uses a 60 FPS/native-Lua fallback instead of uncapping an incomplete pipeline.

One earlier isolated launch did not complete within its 45-second startup budget; retry succeeded. The cause of that timeout was not established. The concurrency handoff correction closes a code-level risk, but the timeout is not presented as proof that a deadlock occurred.

## Bug reports

Include the J374 executable version, fork DLL hash, INI, display/compositor mode, reproduction steps and enabled mods. Capture `%TEMP%\isaac-highfps.log` with `Diag=1` for a short controlled reproduction, then turn detailed diagnostics off. Particularly useful markers include `[pipeline]`, `[present]`, `[lifecycle]`, `[entity-cache]`, `[shader-cache]`, `[native-ui-replay]`, `[native-ui-epoch]`, `[pure-state]` and `[pure-lua]` where enabled by that build.

Report actual frame pacing and animation/gameplay timing separately. A high presentation rate can coexist with an incorrectly timed native animation or a mod graphic that updates its position only at the canonical 60 Hz sample.
