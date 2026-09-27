# WC3 Lua Map Testing — Status and Remaining Work

Date: 2026-09-27
Branch: `wc3/lua-map-testing` (`VinerX/open-realm`)
Acceptance map: `C:\Development\Warcraft 3\23-Race-Legion\23-Race-Legion.w3x`
Runtime data: read-only CASC at `E:\Games\Warcraft III` (build 24268)

## Design ruling

Retail Lua maps run converted `common.j` and `Blizzard.j` in the same Lua VM as
`war3map.lua`, using the same native surface. OpenRealm follows that model: its
in-tree JASS parser converts those libraries to Lua and loads them before the
map script. This avoids cross-VM handle and callback marshalling. The converter
must reject unsupported syntax rather than silently omit it.

## Milestone

`23-Race-Legion.w3x` now completes `config()` and `main()` and reaches the
600-frame budget: the bounded CASC audit reports `completed` (1 reached the
frame limit, 0 script startup errors, 0 crashes, 0 timeouts). This is a startup
smoke result, not objective/combat/visual correctness.

`config()`/`main()` completion is now the frozen bootstrap milestone, not the
goal. The active phase is deterministic scenario testing: run a bounded script
against the loaded map, advance the simulation, assert world state, and return a
machine-readable PASS/FAIL. Presentation-only gaps (`SetWaterBaseColor`,
ambient sound, local-only camera fields 8-10) are logged by exact name and are
not priorities unless a scenario needs them.

The first scenario is in place: `scenarios/23-race-legion-smoke.lua` advances
201 frames, then asserts the 24-slot setup and a live playing player slot. The
bounded CASC audit reports `WC3_SCENARIO name="legion-smoke" status=PASS steps=201`
alongside `WC3_SCRIPT phase=selection kind=lua` and `load/config/main ok`.

The second scenario is `scenarios/23-race-legion-init-integrity.lua`. It waits
for the map's own deferred init queue (`OnInit._run()`) to drain and then reads
the map's `InitErrors`/`InitFatal` globals, so a boot that merely reaches game
start can no longer hide a failed init step. It then asserts the deferred
triggers (`gg_trg_*`) and globals the rest of the map depends on exist. Against
`23-Race-Legion.w3x` the queue went from 205 failed steps to 2; both remaining
failures are one map-side defect (see below), not a missing native.

## Implemented and verified

- The parser-based translator handles the library syntax encountered so far,
  including Lua-reserved JASS identifiers. Focused translator cases passed in
  Classic and TFT.
- Lua startup natives added for map setup, players/forces, shared filters,
  alliances and player state, common enum conversions, fog/game state, groups,
  plus sound creation from a Blizzard sound label. Shared gameplay state paths
  are reused by JASS and Lua where applicable.
- Focused Lua runtime cases passed for the new shared-state and filter paths
  (Classic and TFT). Production executable and share build succeeded.
- A read-only CASC audit loaded the map and ran to the 600-frame limit. It
  passed `InitBlizzard` setup including day/night sound creation; map startup
  then stopped at the next missing Lua native, `CreateTrigger` from
  `Blizzard.j:3022`. This is a startup smoke result, not gameplay validation.
- Triggers, timer callbacks, player-unit events, and Lua event filters reuse the
  shared trigger/event lifecycle (Lua VM + registry ref, never a faked JASS
  `code` handle).
- The bounded audit now classifies a run by its engine-reported script startup
  result: `classify_run_status` downgrades any `G_StartScripts`/`G_SpawnEntities`
  load/config/main failure to `script_error` and reports a `SCRIPT_STARTUP`
  family, so a zero exit code can no longer be reported as `completed` for a map
  whose script never ran.
- Lua sound and music natives reuse the JASS sound path (`G_JassSound*`,
  `G_PlaySound`, `G_Music*`); Lua weather reuses `G_Weather*`; Lua camera setup
  and pan natives reuse the shared `g_camera.h` helpers now factored out of
  `api_camera.h`.
- The read-only CASC audit advanced past `CreateTrigger`, `CreateSound`, weather,
  camera creation, and `BlzCreateUnitWithSkin`; the startup-blocking natives
  `TriggerRegisterUnitEvent`, `SetUnitColor`, `SetUnitState`, `WaygateActivate`,
  `WaygateSetDestination`, and `GetRectCenterX/Y` are now implemented, and map
  startup completes.
- Map-script native gaps closed for the frozen map's deferred init queue
  (mirroring the JASS `api/*.h` twins): `R2S`; quest setters
  `QuestSetTitle/Description/IconPath/Required/Discovered/Enabled/Completed/`
  `Failed`; timer dialogs `CreateTimerDialog`/`DestroyTimerDialog`/
  `TimerDialogSetTitle`/`TimerDialogDisplay`; rect edges
  `GetRectMinX/MinY/MaxX/MaxY`; `CreateUnitAtLoc`. Each has a focused
  `wc3_mapscript` regression. `TriggerSleepAction` is registered as a reporting
  no-op because the Lua trigger path runs an action to completion on the shared
  state and has no coroutine scheduler to resume; a real yield is deferred.

## Known map-side defect

`war3map.lua:17085` calls `Condition(Trig_StolicaAttacked_Conditions)`, but the
function definition was dropped when the converted script was split into
`_lua/monolith_split/` sections (it exists in the pre-split `stolica.lua`).
Because Lua resolves the global at call time, the missing function raises
`bad argument #1 to 'Condition' (function expected, got nil)`, which fails
init step 14 (`InitCustomTriggers`) and cascades into step 15
(`RunInitializationTriggers`). This is a map-source fix, not engine work.

Note on the 18 failing `wc3_api.*` assertions in this environment: they also
fail on the pre-change base commit `dc683593` and are platform issues (`/tmp`
save/load paths, `dup2` redirect), not Lua regressions.

## Remaining work

The bootstrap phase is complete; the remaining work is scenario testing, not
more native coverage for its own sake.

1. Grow the deterministic scenario suite from real map-behavior checks: unit
   spawn, ownership, orders, casts, ability/state outcomes, trigger fire counts,
   and variable/table values, each asserted with a bounded step budget. Add a
   new scenario file per behavior rather than one broad run.
2. Add a native only when a named scenario is blocked by its absence. Report
   the missing name exactly, implement the slice by reusing the existing
   JASS/engine implementation, add a focused regression, then re-run the
   scenario. Do not port natives speculatively.
3. Keep JASS regressions green on every JASS-adjacent change.
4. Before a checkpoint, run focused tests, `git diff --check`, and a production
   build. Stage only task files; leave the unrelated
   `renderer/conchars_sysfont.h` modification untouched.

Earlier findings and sources for the retail load model are recorded in the
design spec and the previous version of this status note; see the linked
references in `docs/superpowers/specs/2026-09-26-wc3-lua-map-testing-design.md`.
