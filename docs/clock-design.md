# Clock and presentation design

This document describes the J374 fork's intended invariants and current implementation boundaries. Runtime evidence and open validation work are recorded in the [port notes](port-1.9.7.15.md).

## Three rates, one authoritative update order

| Domain | Cadence | Responsibility |
| --- | --- | --- |
| Main update | 60 Hz | Native input sampling and Manager update; preserves native counter parity. |
| Full game update | 30 Hz while active | World simulation, active game frame/time counters and their original pause/transition behavior. Native half-step/player work remains in the other main phase. |
| Presentation | Adaptive | Display output, native position/camera previews, and replay of supported mod/UI drawing. |

For each due main tick, the fork restores presentation previews, runs native input and Manager update, completes a native render, and then performs native Lua GC work. This render is called **canonical** because native render-side updates and Lua callbacks are allowed to execute in it. It uses the engine's current pose and preserves Update → Render → GC ordering before the next catchup tick.

Every actual output image then uses a **pure presentation** pass: native geometry can use intermediate positions, while supported Lua drawing and identified UI animation results are replayed. Pure means that the supported paths avoid re-executing gameplay/render callback side effects; it is not a proof that every reachable native render function is intrinsically read-only.

At 100 Hz, some presentations occur between main ticks. At 45 Hz, some presentations follow two complete main ticks. Replaying two callbacks after both updates would expose the second state to both callbacks, so it is not equivalent to completing each tick's native render in sequence.

## Startup publication

Required cache installation is deferred until the initializer thread has exited. The game thread must not race that thread's code-patching freeze/write/thaw sequence. Hook readiness is published after successful patch installation, not when a trampoline is allocated. Until the complete supported pipeline is ready, presentation remains capped at 60 with native Lua execution. This readiness gate is distinct from rejecting an unsupported draw inside an otherwise active cache.

## Scheduling and loading

`FixedTimeline` advances absolute 1/60-second deadlines. It does not multiply simulation rate by output FPS. Presentation pacing happens at the actual SwapBuffers boundary, after update/render work; hidden canonical renders do not consume a display deadline or change the last-present timestamp.

Explicit lifecycle hooks measure the union of nested room/floor/setup/restore work. On return, that duration shifts the next simulation deadline and interpolation timestamps together. A lifecycle boundary terminates the current catchup batch and invalidates the old presentation anchor. It also advances an epoch used to invalidate scene-dependent history and caches.

Ordinary renderer or mod execution time is not silently excluded. That distinction matters: discarding every long frame can make simulation slow down, while treating blocking loading as gameplay debt can fast-forward a transition. Pause, focus loss, suspend and lifecycle operations initiated from render callbacks need dedicated regression cases; a duration threshold alone cannot establish their correct semantics.

J374's Manager counter increments at the end of its update. Even parity on entry selects the full world tick. The port samples world history after that phase and retains the native half-step phase instead of synthesizing additional player integrator calls.

## Native previews

Non-player entities interpolate between recorded authoritative positions. The player uses a short prediction window based on the original velocity, friction and time-scale fields. Camera/room-scroll presentation independently interpolates `C_previous + cameraAlpha * (C_current - C_previous)`, with cameraAlpha from the 60 Hz main clock even when world/player prediction is frozen. This adds one main tick of camera display delay and does not add input or player-prediction delay. It preserves native camera easing and bounds instead of deriving room translation from a predicted player position; sprite pixel snapping remains native. Before another authoritative update, the fork restores the engine's backup positions and scroll state.

The native per-type Interpolate methods are not repeatedly called for display frames: some of them also modify velocity, height or other simulation state. Temporary preview positions themselves are not authoritative speed measurements. Verification must compare positions immediately around native updates, together with wall time and game/Manager counters.

Scope changes invalidate entity and camera history. Player prediction is also frozen across pause/new-game boundaries so stale velocity or a reset game frame does not imply continued movement.

## Lua draw capture and replay

Canonical main and supported entity render callbacks execute once in their native place. The cache records supported draw submissions and the state needed to replay them: resource ownership, layout, geometry, colors and relevant render context. Capture includes supported vertex writes made after a draw call returns, before the native buffer is invalidated. Replay preserves native depth allocation rather than copying obsolete depth values into a later frame.

Keys distinguish lifecycle epoch, entity generation/identity, callback, render pass and call ordinal. A reused pointer is not sufficient to identify the same entity. Native image references are retained while cached commands need them and released when invalidated.

**Coordinates are the canonical callback's coordinates.** The cache does not infer that an arbitrary screen-space quad belongs to a particular moving entity and does not translate it to the player's interpolated position. This preserves the callback's sampled drawing semantics, but mod-drawn attachments can remain visibly sampled at 60 Hz even when the base entity's native movement is smoother.

Unsupported layouts, resource/context changes, unrepresented GL operations or shader state invalidate capture/replay instead of executing Lua again at output frequency. A failed replay can omit that mod scope from an extra presentation; its canonical callback still runs at the native cadence. Missing/unsafe records are diagnostic evidence to investigate, not automatic proof that the affected drawing is harmless.

The `LuaOverlay` option is retained for configuration compatibility. With the current supported pipeline active it selects draw capture/replay, not the old per-callback FBO overlay implementation. `LuaVanillaCadence` and `LuaOverlay` should both remain enabled for validation of this design.

## Update-owned render-to-texture

Being inside an entity Render helper does not establish that the call belongs to a Manager render pass. Blood Splat effects can be updated, rendered into a texture and immediately removed by `EntityList::Update`. A stale canonical/pure flag from the preceding frame must not control those callbacks. Hooks use the actual Manager-render scope as well as the render role: outside that scope, the native callback is forwarded unchanged and reported separately. This preserves Update-owned one-shot callbacks instead of treating their absent cache entry as a harmless short-lived entity. The [direct-spawn regression](validation/blood-callback-comparison.json) now matches native behavior: ten instances produce 30 render callbacks, including ten Update-owned floor renders, with 540 matching normalized event records. This does not verify natural damage producers or baked-texture pixels.

## Native UI animation updates

The audit separates three kinds of render work:

1. Persistent scalar increments, such as menu vignette and character-selection fades, advance on fixed main ticks while rendering reads their current value.
2. A shared black/Hard character-colorization helper can be called both by main rendering and by Update-driven offscreen initialization. Only its pure main-render advancement is suppressed; initialization keeps native behavior.
3. Dice/emote and lobby sprites can reuse one animation object for several labels in a single render. Canonical updates record state, frame/event flags and layer indices in call order. Pure rendering preserves Play/type setup, installs the corresponding canonical result before drawing, and restores the scoped persistent timing fields afterward.

A blanket “each sprite may update once per display frame” rule would break these reused templates. Replay checks animation identity, allocation ownership and bounds. Lifecycle changes invalidate the UI records as well as the entity cache; a pure UI scope waits for a new canonical capture before touching a replacement owner.

These repairs cover the audited call sites. They do not establish that all game animations use the same clock.

## Shader callbacks and graphics boundaries

Shader-parameter callback results are a separate cache from draw commands. A shader can be queried more than once in a native render; preserving shader-name/call-ordinal order matters more than forcing a single global call count. Lifecycle changes and Lua VM close/recreation must release or invalidate old registry references without touching a new VM at a recycled address.

Caching a Lua parameter table does not establish that an arbitrary shader's resources, uniform writes, render targets or direct GL calls can be replayed safely. Custom-shader coverage and its visible output remain pending acceptance. Keep table-cadence evidence, draw-cache compatibility and pixel verification separate.

## What the tests can prove

- Scheduler tests check 60 Hz deadlines across fixed/variable output intervals, ordinary stalls and explicitly excluded lifecycle intervals.
- Callback fixtures compare native Update/Render order, recorded side effects and per-tick state against the same J374 executable at native 60 Hz.
- Native presentation traces measure actual swap-return intervals, not the panel's scanout or VRR signaling.
- Image captures check that expected drawing reached presented images. Cache-hit counters alone do not prove visible pixels.
- UI snapshots and lifecycle fixtures compare animation duration and state separately from gameplay frame counts.

The [canonical callback report](validation/canonical-overlay-comparison.json), [type/cadence report](validation/render-types-comparison.json), [room-transition matrix](validation/transition-matrix-comparison.json), [screen-shader parameter report](validation/shader-comparison.json), [menu curves](validation/menu-curve-comparison.json) and [ordinary floor report](validation/floor-comparison.json) support the bounded results quoted in the README. A final release should rerun the relevant gates against its exact DLL hash. Physical VRR, runtime refresh changes, broader shaders and untested floor/menu/lifecycle/mod cases must not be inferred from those reports. The [moving-pause](validation/pause-moving-comparison.json) and [real rewind](validation/rewind-comparison.json) reports establish their explicitly sampled paths; a live Lua VM replacement was not observed.

The final R5 DLL and production source commit `7c2a47d` are tied together in [release evidence](validation/release-r5-evidence.json). A runtime compositor-limit experiment requested a new cap but measured unchanged cadence; it is explicitly excluded from variable-rate/VRR passing evidence.

## Large-room camera regression

The previous screen-lock algorithm assumed camera translation mirrored player movement. In the controlled Greed room, native vertical scroll had already clamped at -182, but presentation continued to -184.520 before snapping back to -182. At the other boundary it reached +2.702 instead of stopping at zero. The native camera itself and gameplay state matched native60; this was a presentation defect.

The replacement uses the two engine-produced scroll samples as endpoints. Stop/reverse/clamp and scope-reset tests cover the interpolation math; 80/90 Hz Greed cases verify the actual preview fields, callback sequence and canonical state. See the separate camera/state reports in validation. The initial new-candidate 90-second startup timeout occurred during hook installation, before the fixture; its cause remains unclassified and it is excluded from successful camera cases.

The engine's 30 Hz full-world phase, 60 Hz main phase and output cadence are distinct. Their scheduling is decoupled on the engine thread; output at 90 Hz does not request 90 logic updates. A separate rendering thread would additionally require immutable snapshots and an audit of native/Lua rendering mutations. This patch does not claim thread-level asynchronous rendering.
