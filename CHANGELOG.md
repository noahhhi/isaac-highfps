# Changelog

## v0.13.0-j374.8

Repeated ANM2 sprite rotations share native sine/cosine pairs through a bounded
index. Every projectile keeps its own native transform, collision and damage;
unsupported inputs and poor reuse return to native calculation. Coverage follows
the rendering path rather than a list of familiar names. Fate's Reward retains
its partial tear-property inheritance, and Mongo Baby is not treated as a player
attack copier.

Adaptive pressure now distinguishes early beam demand from measured split-work
cost, preserving ordinary attacks in dense but inexpensive scenes. Short built-in
shader names also use exact bounded comparisons. See the [reuse scope and
performance priorities](docs/performance-reuse.md) and [validation
results](docs/validation/sprite-rotation-reuse.json). Extreme combat is still not
proven to sustain 90 FPS without attack-output tradeoffs.

## v0.13.0

**The companion can now tell you the DLL is outdated.** The two halves of this mod age
differently: the Workshop part updates itself, the native part is installed by hand and
then silently falls behind. The DLL now publishes its release version into the Lua state
(`HIGH_FPS_VERSION`), and the companion — which is re-uploaded with every native release
and therefore knows the newest one — shows a short fortune at run start when the installed
DLL is older. Builds before 0.13.0 never published a version, so its absence identifies
them exactly and the notice covers them too.

The version string itself now lives in one place, `src/version.h`: the DLL publishes it,
package.bat names the zip after it, and package.bat reminds us to bump the companion —
because a version scheme where the two halves can drift apart quietly is how the previous
"detected by counting callbacks" scheme died.

**The companion no longer cries wolf.** "High FPS is not installed" could appear although
the DLL was running: the native part announces itself on its first Lua dispatch, which on
a slow disk lands seconds after mods have loaded, and the companion believed whatever it
saw first. It now waits a few seconds before showing the launch hint, re-checks at the
moment the run-start notice would fire instead of trusting a snapshot, and cancels any
queued notice the instant the native part appears.

## v0.12.0

**The big-room shiver is fixed.** Walking through a room large enough to scroll made Isaac
himself tremble while everything around him moved smoothly — only him, only while moving,
only while the camera scrolled. Two separate causes, and both had to fall.

The first: the mod was smoothing the wrong field. What we previewed each frame was the
camera's position — but that value is only the camera's internal focus. Once per 60 Hz
frame the engine maps it through the world-to-screen transform, clamps it to the room, and
stores the result in the room's scroll offset, and it is THAT field every sprite draw
actually adds. Our per-frame camera writes never reached the screen: the scroll offset
froze for three rendered frames while the player's position advanced on every one, so he
crept forward and snapped back sixty times a second. The preview now samples, advances and
rolls back the scroll offset itself — the value the renderer consumes — so the scroll
finally moves at the full frame rate. (Proving this took measuring the field's update
cadence in the running game: 60 Hz, no matter what we wrote into the camera. A lesson worth
the price: never assume the field you write is the field the renderer reads.)

The second: the renderer quantises every sprite's position term to the render-target pixel
grid BEFORE adding the scroll offset. Advancing the offset by the player's raw sub-frame
movement would therefore hold his true position while his drawn position kept stepping
whole pixels each time his world position crossed a grid line — a one-pixel sawtooth
against a smooth background, visible precisely because a screen-locked sprite has no
motion of its own to hide it. The offset therefore carries the SNAPPED player term,
anchored so that staircase and ramp step in the same instant: the drawn player holds one
render-target pixel exactly, and the background scrolls in whole pixels at the full frame
rate — the finest motion a pixel-snapped world can show.

**A per-frame diagnostic dump.** `Diag = 1` in the ini logs, for every rendered frame, the
exact player/scroll state the renderer is about to read. It exists because probing the
game from outside answers a different question: an external reader races the mod's own
write sequence and mostly catches the sub-millisecond windows between two writes. Off by
default; costs log I/O when on.

## v0.11.0

**A second name to load under.** No log file and no change in frame rate means the loader never
picked the DLL up, and there is nothing the DLL can report about a situation where its own code
never runs. The download now carries the same build twice, the second copy named `dbghelp.dll`
in `alternative-name/`. The executable imports that as well, so it loads at the same point in
startup. Use one or the other, never both; there is a single-instance lock so a double install
does nothing rather than something strange.

`dbghelp` was picked over the other candidates on purpose. Of everything `isaac-ng.exe` imports,
most are on Windows' KnownDLLs list and cannot be shadowed from the game folder at all. Of the
rest, `dbghelp` pulls only five functions, is not tangled up with the graphics stack the way
`opengl32` is, and is not the sort of file antivirus software takes an interest in.

**Log paths handle any locale.** They were ANSI, which fails silently when a profile or install
path holds characters the system codepage cannot carry. A failed open left logging switched off
entirely, including the fatal lines that would have said why nothing was patched, so the symptom
was "no log at all" — the same thing a DLL that never loaded looks like. Wide paths throughout
now, with a fallback next to the DLL if `%TEMP%` cannot be written.

## v0.10.0

**Mods keep working at high frame rates.** Uncapping the render loop meant the engine asked every
installed mod to draw at the display rate instead of 60 times a second. That tripled the Lua cost
of every mod present, and it broke mods that keep counters or timers in a render callback, which
was for a long time the only per-frame hook the API offered.

The mod render pass is now held at its original cadence. Engine calls into Lua go through
`lua_pcallk` and `lua_callk`, both swapped in the import table; on a frame the engine would never
have drawn, the call is emulated rather than forwarded. The callee and its arguments come off the
Lua stack, the requested number of nils go on, and `LUA_OK` comes back, which is exactly what
every call site already sees when no mod has registered anything.

That alone would leave mod graphics on one frame in three, which reads as flicker. So what mods
draw goes onto a render target of its own, composited onto every frame from inside
`LuaEngine::PostRender`. The engine's own surfaces and blit are reused, so nothing here is drawn
by us. Compositing at `SwapBuffers` was the first attempt and faulted immediately: by then the
frame's render batches are already torn down.

Per-entity render callbacks deliberately keep the full rate. They are cheap, and they draw
relative to entities whose positions are already being interpolated, so per frame is both correct
and better looking. Bracketing those too was the first attempt and cost most of the frame rate:
a batch is drawn into whatever target is bound when it is flushed, not when it was queued, so
redirecting them meant flushing the queue dozens of times a frame. That also reordered the
engine's own draws enough to make the game's UI flicker.

One consequence worth naming: a mod drawing in both the main pass and a per-entity callback used
to see them once each per frame and now sees the main pass less often. `LuaVanillaCadence = 0`
turns the whole mechanism off.

**Mods can detect the native component.** Two globals, `HIGH_FPS_NATIVE` and `HIGH_FPS_RATE`,
published into the Lua state from inside a dispatch. The Workshop companion used to count render
callbacks above 60, which the change above makes impossible.

**The refresh rate requirement is documented.** The frame limiter is removed but the OpenGL swap
interval is untouched, so vsync still pins the loop to the display. On a 60 Hz panel there is
nothing above 60 to show.

## v0.9.0

First release. Renderer runs at the display's refresh rate, logic stays at 30 Hz, and the frames
in between get interpolated entity positions rather than extrapolated guesses.
