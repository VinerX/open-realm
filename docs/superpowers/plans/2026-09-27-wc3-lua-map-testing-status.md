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

Remaining runtime gaps in that log: `SetWaterBaseColor` presentation, ambient
sound hookup, and local-only camera fields 8-10 (`CAMERA_FIELD_LOCAL_*`).

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

Note on the 18 failing `wc3_api.*` assertions in this environment: they also
fail on the pre-change base commit `dc683593` and are platform issues (`/tmp`
save/load paths, `dup2` redirect), not Lua regressions.

## Remaining work

1. Implement Lua trigger handles, event registration, conditions/actions, and
   callback execution using the existing engine trigger lifecycle. Preserve
   callback semantics in the shared Lua VM; do not fake Lua callbacks as JASS
   function pointers.
2. Continue the map audit after each bounded native slice. Follow actual
   initialization order and use focused regressions before changing behavior.
   The map references hundreds of WC3 natives; implement them in slices that
   reuse the existing JASS/engine implementations rather than in bulk.
3. Extend the audit report from a binary startup result to per-phase detail
   (`kind`, `selection`, `load`, `config`, `main`, `runtime_error`,
   `unsupported_natives`) as planned in the implementation plan's Task 6.
4. Keep JASS regressions green, complete map initialization, then proceed to
   runtime map behavior and report remaining unsupported natives explicitly.
5. Before a checkpoint, run focused tests, `git diff --check`, and a production
   build. Stage only task files; leave the unrelated
   `renderer/conchars_sysfont.h` modification untouched.

Earlier findings and sources for the retail load model are recorded in the
design spec and the previous version of this status note; see the linked
references in `docs/superpowers/specs/2026-09-26-wc3-lua-map-testing-design.md`.
