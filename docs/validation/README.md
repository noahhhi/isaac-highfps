# Validation summaries

These compact reports retain case IDs, executable/DLL/fixture/configuration hashes, measured results and limits. They omit machine/user paths, account identifiers, save data, full mod lists and game/system binaries. The source-summary hash identifies the fuller local analysis from which each public report was selected.

| Report | Scope |
| --- | --- |
| [Canonical callbacks](canonical-overlay-comparison.json) | Detailed Update/Render order and deliberate callback write persistence at the tested output rates. |
| [Callback types](render-types-comparison.json) | Main plus ten entity render categories, high/low presentation rates and the earlier aggregate effect-miss evidence. The Update-owned Blood Splat correction is covered by its separate direct-spawn regression. |
| [Update-owned Blood Splat](blood-callback-comparison.json) | Native60, old100 negative control and fixed100 callback order/counts for ten direct spawns; not damage-produced blood or baked pixels. |
| [Room transitions](transition-matrix-comparison.json) | One matched WALK/FADE/TELEPORT pair each, aligned native animation phases and observed timing. |
| [Screen-shader parameters](shader-comparison.json) | Registered A/B parameter callback cadence/order and table isolation; not GPU colors or arbitrary custom shaders. |
| [Menu curves](menu-curve-comparison.json) | Normal/Hard/Greed difficulty changes and matching interior fade values. |
| [Ordinary floor transition](floor-comparison.json) | One natural stage 1→2 trapdoor route through arrival and resumed updates. |
| [Moving pause](pause-moving-comparison.json) | Frozen position/timers with nonzero velocity retained, plus measured recovery after resume. |
| [Real rewind](rewind-comparison.json) | One real Glowing Hour Glass RestoreState/backwards-clock sequence; no live VM replacement was observed. |
| [Final R5](release-r5-evidence.json) | Source commit `7c2a47d`, exact DLL hash, final 80/100 Hz measurements and one reported inspected frame. |
| [Standalone tests](standalone-tests.json) | Source-bound unit/ABI/Lua tests and an offline fake-API rewind harness, with explicit execution limits. |

Native60 is the same J374 executable with the high-FPS component disabled. A DLL hash can still appear in that case's file manifest; the configuration hash and case setup identify its disabled role. An artifact hash is evidence of which file was used, not a universal compatibility result.

The standalone test report does not validate startup patch installation or the full renderer. The ALL7 direct-spawn Update-owned RenderToTexture correction has its own passing report. Moving-pause and real rewind now have separate reports. Other untested lifecycle/menu/mod paths remain open, and runtime/physical VRR has not been validated. The earlier Blood Splat misses must not be described as harmless solely because the effect is short-lived.

## Camera correction (0.13.0-j374.3)

[camera-presentation.json](camera-presentation.json) checks the actual per-presentation camera fields against native bounds and the sampled endpoint interval. [greed-canonical.json](greed-canonical.json) independently compares native60, old90, new90 and new80 canonical events. Source is `1444261`; neither report is a physical VRR test or a universal pixel-level smoothness claim. The fixture is [large-room-greed-fixture.lua](../../tests/fixtures/large-room-greed-fixture.lua). The failed first candidate startup is retained as an unclassified installation-stage timeout, not a camera test pass.

## Menu presentation (0.13.0-j374.4)

[menu-presentation.json](menu-presentation.json) compares eight main-menu/character-page transitions, preserving capture timestamps and complete input windows. [menu-game-regression.json](menu-game-regression.json) checks the final DLL in the established Greed fixture. The analysis script is [analyze_main_menu.py](../../tests/analyze_main_menu.py); it only reads recorded data and never accesses a live game or input device. Recordings include native.log (when enabled), menu-clock.json, input-events.json, and timestamped capture/frame-NNNN.jpg plus capture/gst.log. Capture and event startup timing is approximate, so no input-to-photon latency claim is made. Arrow-only recordings and page-slide recordings are kept separate.
