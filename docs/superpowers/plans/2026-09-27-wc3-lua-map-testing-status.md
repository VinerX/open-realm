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

## Remaining work

1. Implement Lua trigger handles, event registration, conditions/actions, and
   callback execution using the existing engine trigger lifecycle. Preserve
   callback semantics in the shared Lua VM; do not fake Lua callbacks as JASS
   function pointers.
2. Continue the map audit after each bounded native slice. Follow actual
   initialization order and use focused regressions before changing behavior.
3. Fix the audit reporting so script initialization failures are distinguished
   from frame-limit survival; the current report can mark a run completed while
   its log contains a `G_StartScripts` failure.
4. Keep JASS regressions green, complete map initialization, then proceed to
   runtime map behavior and report remaining unsupported natives explicitly.
5. Before a checkpoint, run focused tests, `git diff --check`, and a production
   build. Stage only task files; leave the unrelated
   `renderer/conchars_sysfont.h` modification untouched.

Earlier findings and sources for the retail load model are recorded in the
design spec and the previous version of this status note; see the linked
references in `docs/superpowers/specs/2026-09-26-wc3-lua-map-testing-design.md`.
