# Isolated live-game fixtures

**Use these only in a separate test game/mod directory and separate saves.** They are test programs, not production mods. Most set a fixed seed, restart a run, control movement/shooting, spawn objects, change rooms/floors or use items. The standalone test runner never installs them and never starts the game.

For a live case, explicitly choose one fixture and copy it as the `main.lua` of an isolated test mod. Preserve the selected fixture/configuration hash in the case manifest. Disable other fixtures that set seeds or restart runs. Never copy this directory into the user's live mod folder as a collection of simultaneously active scripts.

| File | Purpose and effects |
| --- | --- |
| `global-speed-fixture.lua` | Fixed seed and controlled movement/shooting; observes timers, tear dynamics and a spawned bomb. |
| `overlay-side-effects.lua` | Deliberately changes entity positions from main/player/tear render callbacks to verify ordering and persistence. |
| `floor-blood7-fixture.lua` | Logs all Blood Splat variant7 callbacks; directly spawns ten instances and attempts an additional NPC-damage producer. Coverage depends on which producer actually appears. |
| `pause-observer.lua` | Read-only Lua pause/resume observer. A separate, explicitly isolated input driver supplies real pause/movement controls. |
| `rewind-fixture.lua` | Fixed seed, room transition and a real Glowing Hour Glass use; persists its phase in this test mod's isolated SaveData. Use a new `caseToken` or clear only that test mod's isolated data for an independent run. |
| `cadence-fixture.lua` | Controlled input and cadence census; select `transition=nil`, `"WALK"`, `"FADE"` or `"TELEPORT"` in its configuration. |
| `floor-transition.lua` | Fixed seed and a natural trapdoor entry to observe an ordinary floor transition. |
| `all-render-types-fixture.lua` | Spawns test entities and draws labeled markers across main plus ten entity callback categories. Its tagged generic effect is not a substitute for the dedicated Blood Splat fixture. |
| `shader-probe/` | Complete screen-shader probe package. Install the entire folder, including `metadata.xml`, `main.lua` and `content/shaders.xml`; disable every other auto-seed/restart fixture. |

The shader probe requires both registered shader definitions, not just the Lua file. A matching Lua hash cannot establish historical shader-resource identity if the old manifest did not hash its XML. The committed package makes future runs reproducible; it does not retroactively fill that evidence gap.

Some report fixtures were configured for a specific transition or unique rewind token. The overlay-side-effects file includes the actual 300-tick duration and setup marker used by the published callback cases (SHA256 `b5d12d00cd83b963fb028905480bf31c6fd2df964f3c1ecec7b50aa5ed79f1e3`). Other configured fixture hashes can therefore differ from the template's hash. Record the exact configured file used for each new run instead of claiming an unchanged template produced every historical case.

`source-hashes.json` lists these committed fixture sources. No game executable, system/game DLL, save, prefix, screenshot or private account identifier is included. Logs may contain transient entity IDs for pairing; public summaries normalize those identities and omit full machine paths/mod lists.

An offline fake-API rewind harness is available through `tests/run-tests.sh lua-fixture` with a supplied local Lua runtime. It validates the fixture's state machine without running this script inside a game. It does not establish that a live game replaced its Lua VM during a rewind.

### Saved-run performance regression

`user-baseline-stress.lua` is an isolated Continue-only fixture. Supply a separate copy of the recorded run and its exact mod data; it refuses a new run or a different seed. It inventories the player's items and stats, records entity births/removals and requests native directional shooting through MC_INPUT_ACTION. It never creates or removes projectiles or changes inventory.

Vanilla Continue can choose a random entrance when EnterDoor is unset. At the first authoritative update the fixture therefore records the original position and places the single player at `(80, 280)` with zero velocity, once. The `ready` record proves the controlled starting state. Input-phase labels describe requested input, not guaranteed cessation of fire: Marked continues shooting after release. The archive and the real game are never mutated by this fixture; deploy it only through a silent, guarded isolated launcher. Save files and proprietary game assets must not be committed.
