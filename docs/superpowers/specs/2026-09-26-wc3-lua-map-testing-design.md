# OpenRealm WC3 Lua Map Testing Design

## Goal

Make OpenRealm a deterministic headless runtime for testing Reforged-era Warcraft III maps, with 23-Race Legion as the first real acceptance target. The initial milestone boots the actual map through Lua `config()` and `main()` with all 24 map player slots represented. Existing JASS maps continue to use their current runtime.

Gameplay/runtime behavior and repeatable automated checks are the priority. Renderer fidelity and Warcraft UI presentation are outside the first milestone.

## Verified Repository and Map Facts

- `tools/wc3_map_audit.py` already starts isolated headless map processes, supports custom loose maps, and writes JSON, Markdown, and logs. It is a bounded smoke audit; reaching its frame limit alone does not mean a map booted correctly.
- `common/world.c:CM_ReadMapScript()` reads only `war3map.j` and `scripts\\war3map.j`. `common/mapinfo.h` and `common/world.c` already parse the W3I `scriptType` field for format 28+, but the WC3 runtime does not select a Lua path.
- WC3 startup loads `common.j` and `Blizzard.j` into JASS, then loads and executes the map script. `config()` is called during spawn and `main()` later through `G_StartScripts()`.
- The repository vendors Lua 5.4.8 for the WoW UI. WC3 map compatibility targets Lua 5.3 semantics; the WC3 runtime will use a separate Lua 5.3 build, leaving the WoW runtime unchanged. Blizzard's 1.31 editor notes establish the Lua map option but do not name the embedded Lua patch version; the precise upstream 5.3 patch selected for the new build must be recorded with its source and license.
- `MAX_PLAYERS` is 16 and `MAX_CLIENTS` is 24. `mapInfo_t.players` is sized by `MAX_PLAYERS`; W3I player records with indices outside that range are currently read into a scratch record. The 16-player constant also serves non-WC3 code, so map-slot capacity and connected-client capacity must remain distinct.
- The supplied map is `23-Race-Legion.w3x` (about 123 MiB). Its source tree contains a 68,551-line `war3map.lua`. `config()` calls `SetPlayers(24)`, `SetTeams(24)`, and defines start locations 0 through 23. `main()` creates map content and registers deferred initialization. The script uses `load`, closures, and timer callbacks.

## Architecture

### Map Script Selection and Lifecycle

W3I `scriptType` is the authoritative selector when present. The loader validates that the archive contains the matching script member and reports a map-scoped error for a missing or malformed script. A Lua map never silently falls back to JASS. Older W3I formats without `scriptType` retain the existing JASS path unless archive metadata provides an unambiguous supported selector.

The Lua state belongs to the loaded WC3 map and is created and destroyed with that map's script lifecycle. Lua loads the map chunk, exposes Warcraft-compatible standard libraries and registered WC3 APIs, then invokes `config()` and `main()` at the same lifecycle points as JASS. Lua load/call errors become runtime diagnostics and a failed map-test result; they do not crash the host process or mark the script phase successful.

### WC3 API Bridge

Expose the native and helper functions required by the target map through a WC3-owned Lua binding layer. Reuse existing WC3 domain implementations where their contracts match; keep Lua values, handle identity, callbacks, and Lua registry references in the binding/runtime layer. Do not route Lua callbacks through fake JASS `code` handles. Unsupported calls must report the exact missing function and fail the relevant script phase instead of returning a silent default.

Compatibility is incremental and driven by the target map plus small synthetic fixtures. The initial native set covers script loading, `config()`, `main()`, and the first deterministic frames. Subsequent slices add the functions required by named map scenarios. A coverage report distinguishes registered, invoked, and behavior-verified functions.

### Player Slots

Represent 24 WC3 map-player slots end to end in W3I metadata and WC3 gameplay setup. Preserve the separate limit for connected engine clients. Audit lobby serialization and common consumers before changing shared limits; use explicit capacities for map slots and clients where their contracts differ. The initial acceptance is dedicated/headless, so it does not require 24 connected clients.

### Headless Map Testing

Extend the existing WC3 map audit path rather than add a second process launcher. Preserve per-run process isolation, temporary user data, unique ports, bounded frames/time, raw logs, and machine-readable reports. Add an explicit script/runtime result with statuses for selection, load, `config()`, `main()`, runtime errors, and unsupported natives.

Report three distinct levels:

1. **Archive/script validation:** map metadata and selected script are readable and the script parses.
2. **Headless boot:** `config()` and `main()` complete, the script runtime remains healthy, and the requested frame budget is reached.
3. **Scenario checks:** deterministic actions or authored test hooks reach declared milestones and assertions.

A frame-limit exit is never reported as a successful boot unless the required script phases succeeded. Scenario support is added after the boot path and must use bounded deterministic inputs.

### Diagnostics and Failure Handling

Include map name, script kind, phase (`load`, `config`, `main`, callback/tick), Lua chunk/line where available, and native name in diagnostics. Keep full process logs and aggregate actionable script failures in the report. Preserve JASS failure behavior and regression coverage.

## Acceptance Criteria

- JASS campaign and custom-map paths remain on JASS and keep their current behavior.
- Synthetic Lua fixtures prove Lua 5.3 parsing, closure retention, protected runtime errors, and map-state teardown/recreation.
- The supplied 23-Race Legion map is detected as Lua from its authoritative metadata, loads, executes `config()` and `main()`, and reaches a bounded headless frame target without an unhandled Lua error.
- The map runtime preserves player slots 0 through 23 and accepts `SetPlayers(24)`, `SetTeams(24)`, and all 24 configured start locations without truncation.
- Missing script files, syntax errors, runtime errors, and unsupported native calls produce explicit failed report statuses and actionable diagnostics.
- A real scenario assertion is required before any report claims gameplay behavior passed. Renderer/UI fidelity is not implied by headless success.

## Delivery Stages

1. Capture the target map's W3I/script contract and the existing auditor baseline; add minimal Lua fixtures.
2. Add a separate Lua 5.3 library build and WC3 map-scoped VM lifecycle.
3. Route W3I-declared Lua maps to `war3map.lua`, retain the JASS path, and report script errors.
4. Expand WC3 map-player capacity to 24 independently of connected clients; cover W3I parsing, setup natives, and serialization boundaries.
5. Bridge the native/API subset needed for `config()` and `main()`, including closures/timer callbacks as required by the map.
6. Extend the headless report to distinguish parse, config, main, runtime, and frame-budget results.
7. Boot 23-Race Legion, use its diagnostics and missing-native reports to implement the next compatibility slices, and add deterministic scenario assertions.

## Non-Goals

- Replacing or removing the JASS runtime.
- Implementing every Warcraft III native before running the target map.
- Reproducing Reforged rendering, full UI, menus, or 24 simultaneous network clients.
- Treating a process that merely survives to the frame limit as proof of playable gameplay.
