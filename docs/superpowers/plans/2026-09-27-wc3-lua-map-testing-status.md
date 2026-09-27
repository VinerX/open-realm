# WC3 Lua Map Testing — Status and Remaining Work

Date: 2026-09-27
Branch: `wc3/lua-map-testing` (pushed to `VinerX/open-realm`)
Last commit: `51f94e05 feat(wc3): report Lua tracebacks from map script calls`

Spec: `docs/superpowers/specs/2026-09-26-wc3-lua-map-testing-design.md`
Plan: `docs/superpowers/plans/2026-09-26-wc3-lua-map-testing.md`

## Goal

Make OpenRealm a deterministic headless runtime for Reforged-era Lua Warcraft III
maps, with `23-Race-Legion.w3x` as the acceptance target, leaving the JASS runtime
untouched.

## Done and verified

- Map-scoped Lua 5.3 VM (`games/warcraft-3/lua/wc3_lua.*`): load, protected
  `config()`/`main()` calls, error capture.
- W3I `scriptType` selects JASS or Lua; a Lua map never falls back to JASS.
- Lua startup native slice in `games/warcraft-3/game/lua_api.c` (groups, rects,
  locations, players, forces, regions, camera bounds/margins, day/night models,
  terrain fog, start-location priorities, player color/race/controller/team,
  `StringHash`, `SetMapMusic`, fog style, and stubs for water color and
  sound environment that log as not implemented). Shared helpers now back both
  JASS and Lua (`G_AddUnitToGroup`, `G_EnumUnitsOfPlayer`, `G_StringHash`,
  `G_SetDayNightModels`, `G_SetTerrainFog`, `G_GetCameraMargin`,
  `G_SetStartLocPrio*`), matching the rule that WC3 natives are shared across both
  runtimes.
- Lua tracebacks: a message handler appends `luaL_traceback`, so load/config/main
  failures carry file:line (standalone `t_wc3_lua` passes).
- Engine focused case passes; `tools/wc3_map_audit.py` gained the loose/campaign `kind`
  field and a loose-map report layout.
- Season9 sweep: 46 loose `.w3x` launched for 10 frames, 0 crashed (smoke only).

## Active blocker: the Lua Blizzard library

`main()` in the map reaches `ProbeStep("InitBlizzard", InitBlizzard)` and fails:
`attempt to call a nil value` (war3map.lua:68452). `InitBlizzard` is not defined in
the map; neither are the other Blizzard-library helpers the map calls
(`BJDebugMsg`, `GetLastCreatedUnit`, `GroupAddUnitSimple`,
`UnitAddAbilityBJ`, `CreateNUnitsAtLoc`, `ForGroupBJ`, `PolledWait`, and
many more). The undefined-call census for the map is about 1096 global names.

### Evidence gathered

- The retail install (`E:\Games\Warcraft III`, CASC product `w3`, build 24268)
  ships only `war3.w3mod:scripts\common.j`, `blizzard.j`, `common.ai`,
  `cheats.j`, `initcheats.j` — no Lua prelude. A full per-letter CASC
  enumeration finds no `*.lua` under `scripts\`; `common.lua`,
  `blizzard.lua`, `luahelper.lua` all fail to open.
- `Warcraft III.exe` and `World Editor.exe` contain `jass2lua` converter strings
  and an embedded `luahelper` prelude (`__jarray`, overridden `math.random`,
  `FourCC`). A Blizzard developer's explanation reproduced on Hive says that in
  Lua mode all JASS sources needed by the map are converted to Lua and executed
  by the Lua VM; Lua uses the same natives after conversion. Thus the runtime
  path is `common.j` → `Blizzard.j` → `war3map.lua`, all owned by the Lua VM.
  This matches the generated map's `InitBlizzard()` call. Sources: [Blizzard's
  hidden jass2lua transpiler](https://www.hiveworkshop.com/threads/blizzards-hidden-jass2lua-transpiler.337281/)
  and [Blizzard forum explanation of JASS-to-Lua map generation](https://us.forums.blizzard.com/en/warcraft3/t/world-editor-opening-old-custom-maps-possible-trigger-conversion-feature/5132).
- The map archives carry only `war3map.lua` (no bundled library).
- The map project's `_lua/monolith_split/` sources and all local `.lua` files
  define no BJ functions.

### Design ruling

1. Add or integrate a JASS-to-Lua translator, read `common.j` and `Blizzard.j`
   from the installed WC3 CASC, translate both, then load them into
   `level.lua_vm` before `war3map.lua`. This matches retail load order and keeps
   all native calls inside the Lua runtime's WC3 bridge. `tools/export_wc3_jass.py`
   only exports JASS source from archives; it does not translate JASS to Lua.
Cross-VM BJ bridging would need value/handle/callback marshalling and differ from
retail's single Lua execution path. Implementing a map-specific subset would miss
the broad Blizzard library surface (the undefined-name census is about 1096
names). Neither is the chosen design.

The first option is the retail-compatible design. Candidate translator:
`War3Net.CodeAnalysis.Transpilers` exposes a JASS-to-Lua transpiler, but it is a
.NET NuGet dependency and .NET is not installed in the current environment. The
user authorized asking before downloading additional dependencies; do not install
it or another toolchain without asking first. Before that, inspect existing C
parser/VM infrastructure and determine whether a small in-tree converter can
handle `common.j` and `Blizzard.j` without weakening diagnostics. Any translator
must fail on unsupported syntax instead of dropping declarations or code.

Add a test-first regression for prelude loading before production wiring.

## Remaining tasks

1. Decide and implement the Blizzard/common Lua prelude (see options above).
   Preload it into `level.lua_vm` before the map chunk; log unresolved items to
   `stderr` per repo rules; do not silently drop names.
2. Add a red-first regression covering the prelude mechanism
   (`tests/t_mapscript.c` or the standalone `t_wc3_lua`).
3. Continue the 23-Race boot past `InitBlizzard`; expect the remaining BJ-helper
   gaps, then the deferred `OnInit` queue.
4. Extend `tools/wc3_map_audit.py` to distinguish parse / config / main / runtime
   phases, and stop reporting frame-limit survival as a boot success (spec Stage 6).
5. Keep the JASS path and its regressions green throughout.

## Cleanup already done

- Removed the temporary `build/_analyze_undefined.py`.
- No stray `mpqtool` / `casc_probe` / map processes left running.
- `renderer/conchars_sysfont.h` remains modified but is unrelated; leave untouched.

