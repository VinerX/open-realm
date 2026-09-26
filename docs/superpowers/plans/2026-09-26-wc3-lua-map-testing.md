# WC3 Lua Map Testing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot Reforged-era Lua Warcraft III maps headlessly in OpenRealm, with 23-Race Legion (`SetPlayers(24)`) as the first real acceptance target, while the existing JASS path is untouched.

**Architecture:** A separate vendored Lua 5.3 library backs a WC3-owned, map-scoped Lua VM. The map loader selects JASS or Lua from W3I `scriptType`, validates the matching archive member, and dispatches `config()`/`main()` through the chosen runtime. WC3 gameplay player slots grow to 24 independently of the connected-client limit.

**Tech Stack:** C (Quake 2 style), GNU Make, vendored Lua 5.3.6, existing `shared/test.h` registry, Python auditor `tools/wc3_map_audit.py`.

**Spec:** docs/superpowers/specs/2026-09-26-wc3-lua-map-testing-design.md

## Global Constraints

- Preserve the JASS runtime and all existing JASS tests; Lua is added beside it.
- No silent fallbacks: a Lua-declared map with a missing or malformed script is an explicit failure.
- Lua state lifetime equals the loaded map, not the process.
- `MAX_PLAYERS` (map slots) and `MAX_CLIENTS` (connected clients) stay distinct concepts.
- No debug `fprintf` left in production code; investigative traces are removed when a fix lands.
- Test-first: reproduce the contract with a fixture before changing production behavior.

## Review Focus

- A Lua map whose `war3map.lua` is absent while `war3map.j` exists must fail as Lua, not run JASS.
- A Lua error inside `config()` or `main()` must fail the phase and leave the frame-budget result unproven.
- Map teardown and reload must not leak or reuse a previous map's Lua globals/closures.
- Player indices 16..23 from W3I must survive into gameplay setup without scratch truncation.
- An unsupported native call must be reported by exact name, not return a silent default.

---

## File Structure

- `vendor/lua53/src/**.c,h`, `vendor/lua53/README`, `vendor/lua53/LICENSE` - vendored Lua 5.3.6 interpreter sources.
- `Makefile` - `lua53` library rule next to the existing `lua` rule.
- `games/warcraft-3/lua/wc3_lua.h` - public API for the map-scoped Lua VM.
- `games/warcraft-3/lua/wc3_lua.c` - VM lifecycle, chunk loading, protected calls, error capture.
- `games/warcraft-3/lua/wc3_script.h` - `wc3ScriptType_t` and script-selection declarations shared by loader and runtime.
- `common/mapinfo.h` - carries script kind and the Lua source buffer.
- `common/world.c` - `CM_ReadMapScript()` selects and validates the member per script kind.
- `games/warcraft-3/game/g_main.c` - creates/destroys the map Lua VM and calls `config()`/`main()`.
- `games/warcraft-3/game/tests/t_mapscript.c` - script selection and Lua-load regressions.
- `games/warcraft-3/lua/tests/t_wc3_lua.c` - VM lifecycle, closures, protected errors (standalone build).

---

### Task 1: Vendor Lua 5.3 and add a build rule

**Files:**
- Create: `vendor/lua53/src/*.c`, `vendor/lua53/src/*.h`, `vendor/lua53/README`, `vendor/lua53/LICENSE`
- Modify: `Makefile` (add `LUA53_DIR`, `LUA53_SRCS`, `LUA53_OBJ`, `LUA53_LIB`, `LUA53_CFLAGS`, and the `$(LUA53_LIB)` rule near the existing `$(LUA_LIB)` rule)

**Interfaces:**
- Produces: `$(LIB_DIR)/liblua53.a` and `LUA53_CFLAGS := -Ivendor/lua53/src`, consumed by Task 2 and the WC3 game module link line.

- [ ] **Step 1: Confirm the vendored version string**

Run: `rg -n 'LUA_VERSION_(MAJOR|MINOR|RELEASE)' vendor/lua53/src/lua.h`
Expected: `5`, `3`, `6`.

- [ ] **Step 2: Add the Makefile variables and rule**

Mirror the existing `$(LUA_LIB)` recipe, substituting `lua53` names and the `vendor/lua53/src` directory, excluding `lua.c`/`luac.c`.

- [ ] **Step 3: Build the static library in isolation**

Run: `gcc -Ivendor/lua53/src -c vendor/lua53/src/lapi.c -o /tmp/lapi.o`
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add vendor/lua53 Makefile
git commit -m "build: vendor Lua 5.3.6 for WC3 map scripts"
```

### Task 2: Map-scoped WC3 Lua VM

**Files:**
- Create: `games/warcraft-3/lua/wc3_lua.h`, `games/warcraft-3/lua/wc3_lua.c`
- Test: `games/warcraft-3/lua/tests/t_wc3_lua.c` (standalone `main`, no engine link)

**Interfaces:**
- Produces:
  - `wc3Lua_t *WC3_LuaNewState(void);`
  - `void WC3_LuaClose(wc3Lua_t *L);`
  - `bool WC3_LuaLoadBuffer(wc3Lua_t *L, const char *source, const char *chunk_name);`
  - `bool WC3_LuaCall(wc3Lua_t *L, const char *function_name);`
  - `bool WC3_LuaErrorPending(wc3Lua_t const *L);`
  - `const char *WC3_LuaErrorMessage(wc3Lua_t const *L);`
  - `void WC3_LuaClearError(wc3Lua_t *L);`

- [ ] **Step 1: Write the failing standalone test**

Cover: `x = 2 + 2`; a named function returning 123; a closure incrementing captured state across calls; a syntax error leaving `WC3_LuaErrorPending` true; `WC3_LuaCall` on a missing function failing without crashing; state close then new state has clean globals.

- [ ] **Step 2: Run it to verify it fails**

Run: `gcc -Ivendor/lua53/src -Igames/warcraft-3/lua games/warcraft-3/lua/tests/t_wc3_lua.c games/warcraft-3/lua/wc3_lua.c vendor/lua53/src/*.c -lm -o /tmp/t_wc3_lua` (excluding `lua.c`/`luac.c`)
Expected: link/compile failure until the module exists.

- [ ] **Step 3: Implement the module**

Open `luaL_newstate`, load standard libraries, expose `WC3_LuaLoadBuffer` via `luaL_loadbufferx` in protected mode and `WC3_LuaCall` via `lua_pcall`; store the last error message in the state.

- [ ] **Step 4: Run the test to verify it passes**

Run the Step 2 command, then `/tmp/t_wc3_lua`.
Expected: all assertions pass, exit 0.

- [ ] **Step 5: Commit**

```bash
git add games/warcraft-3/lua
git commit -m "feat(wc3): add map-scoped Lua 5.3 runtime"
```

### Task 3: Script-kind selection in the map loader

**Files:**
- Create: `games/warcraft-3/lua/wc3_script.h`
- Modify: `common/mapinfo.h` (script kind field), `common/world.c:1044` (`CM_ReadMapScript`), `games/warcraft-3/game/tests/t_mapscript.c`

**Interfaces:**
- Produces: `wc3ScriptType_t` (`WC3_SCRIPT_NONE`, `WC3_SCRIPT_JASS`, `WC3_SCRIPT_LUA`) and a loader that fills the Lua buffer from `war3map.lua` when W3I declares Lua.

- [ ] **Step 1: Write failing loader tests** — JASS still prefers `war3map.j`; a W3I-declared Lua map reads `war3map.lua`; a Lua-declared map without `war3map.lua` yields an explicit failure and no JASS fallback.
- [ ] **Step 2: Run to verify failure**
- [ ] **Step 3: Implement `CM_ReadMapScript` selection and validation**
- [ ] **Step 4: Run the engine script tests** — `make test-wc3-engine WC3_PATTERN='wc3_mapscript.*'` (or the standalone equivalent where the full build is unavailable)
- [ ] **Step 5: Commit**

### Task 4: 24 WC3 map player slots

**Files:**
- Modify: `common/mapinfo.h` (`players` capacity), `common/world.c` W3I player parsing, `games/warcraft-3/game/api/api_misc.h` (`SetPlayers`/`SetTeams`/`DefineStartLocation` bounds), `server/sv_lobby.c` and `client/cl_parse.c` only if their map-slot contract differs from the client limit, plus a regression test.

**Interfaces:**
- Produces: W3I player indices 0..23 preserved end to end; explicit capacity constants distinguishing map slots from clients.

- [ ] **Step 1: Reproduce truncation with a 24-player W3I fixture**
- [ ] **Step 2: Implement the capacity change at the owning boundary**
- [ ] **Step 3: Assert no client-limit regression** (existing `MAX_CLIENTS` tests stay green)
- [ ] **Step 4: Commit**

### Task 5: Wire the Lua VM into the WC3 script lifecycle

**Files:**
- Modify: `games/warcraft-3/game/g_local.h` (map Lua VM field), `games/warcraft-3/game/g_main.c` (create/close VM; call `config()` then `main()`), `games/warcraft-3/game/g_spawn.c` (choose runtime by script kind), `games/warcraft-3/game/g_save.c` if the new pointer must serialize.

**Interfaces:**
- Consumes: Task 2 API and Task 3 script kind.
- Produces: Lua maps execute `config()`/`main()` with diagnostics on failure; JASS maps unchanged.

- [ ] **Step 1: Add a test that a Lua fixture's `config()`/`main()` run and a failing one reports a phase error**
- [ ] **Step 2: Implement creation/teardown at map load/unload**
- [ ] **Step 3: Run `wc3_mapscript` and new lifecycle tests**
- [ ] **Step 4: Commit**

### Task 6: Headless report distinguishes script phases

**Files:**
- Modify: `tools/wc3_map_audit.py`, `tests/test_wc3_map_audit.py`, `docs/games/warcraft-3/map-audit.md`

- [ ] **Step 1: Add parser/report tests for a `script` block with `kind`, `selection`, `load`, `config`, `main`, `runtime_error`, `unsupported_natives`**
- [ ] **Step 2: Implement the report fields without breaking existing rows**
- [ ] **Step 3: Run `make test-wc3-map-audit`**
- [ ] **Step 4: Commit**

### Task 7: Boot 23-Race Legion and add scenario assertions

**Files:**
- Create: `docs/games/warcraft-3/lua-map-runtime.md`
- Modify: native bridge slices as the map's diagnostics demand.

- [ ] **Step 1: Run the auditor with `--loose-map` on the supplied map and capture the first script-phase failure**
- [ ] **Step 2: Add the missing native/helper slice for that failure with a focused fixture**
- [ ] **Step 3: Repeat until `config()`/`main()` complete and the frame budget is reached**
- [ ] **Step 4: Record the JASS/Lua boundary and diagnostics in `lua-map-runtime.md`**
- [ ] **Step 5: Commit**
