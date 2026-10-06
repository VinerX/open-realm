# Legion Startup Parity Implementation Plan

> **For agentic workers:** Execute task-by-task in this existing feature checkout. Keep a progress ledger and commit scoped changes periodically.

**Goal:** Load 23-Race-Legion in OpenRealm and match retail Warcraft III through race selection, playable UI, and AI-driven gameplay.

**Architecture:** Keep map-specific observations and fixtures in `wc3-parity`; fix Warcraft III contracts in `games/warcraft-3`; fix generic loading, renderer, or client behavior only in shared engine owners. Compare the same map version, retail data build, and SD mode, and preserve each raw run.

**Tech Stack:** C, Lua/JASS compatibility layer, OpenRealm make/test system, Python/StormLib diagnostic map packer, Warcraft III 3.0.0.24268.

**Spec:** `C:\Development\Warcraft 3\wc3-parity\docs\legion-ui-investigation.md` and user request in this task.

## Global Constraints

- Use original game data from `E:\Games\Warcraft III`; do not patch that installation.
- Use Legion map v1.6.509 unless a later source/map is explicitly qualified.
- Keep map-specific behavior out of shared renderer/client/server APIs.
- Do not modify the canonical 23-Race-Legion checkout without a failing map-owned regression proving its Lua is wrong.
- Treat process exit and headless completion separately from actual in-game readiness and visible behavior.
- Preserve user worktrees, ignored maps, and generated parity evidence; stage explicit paths only.

## Review Focus

1. A long but progressing local map load must survive without masking a dead remote server.
2. User and computer slots must match the active session, while empty and defeated slots retain their distinct states.
3. All race-picker pages and custom Channel orders must dispatch exactly once and preserve authored lifecycle events.
4. Missing frame natives must not break unrelated maps or create UI elements with incorrect z-order/visibility.
5. Computer AI must start only for configured computer slots and execute authored scripts without duplicate VMs.

---

### Task 1: Keep a progressing local map load connected

**Files:** Modify `client/cl_main.c`; test in its existing `client_session` test section. Regression map/run artifacts stay under ignored `wc3-parity/artifacts/`.

**Interfaces:** Use `CL_LoadingFrame()` as evidence that the in-process listen server is actively servicing a synchronous map load. Preserve ordinary packet-age timeout behavior for dedicated and remote sessions.

- [ ] Record the current full-map visible-client failure: Legion registration exceeds the 10-second `CL_TIMEOUT_MSEC`, then `CL_CheckTimeout()` drops the listen client before queued visual commands execute.
- [ ] Add a focused regression for timeout behavior: recent local-server loading frames keep the connected local client alive; an expired loading-frame heartbeat and a dedicated/remote session do not.
- [ ] Run the targeted regression and confirm the expected failure before changing production behavior.
- [ ] Implement a separate bounded local-load heartbeat in `CL_LoadingFrame()` and consult it in `CL_CheckTimeout()`; do not rewrite packet timestamps or disable the existing timeout.
- [ ] Run the targeted test and full `make test`.
- [ ] Rebuild the production client, run the same full-map visual probe, verify two framebuffer screenshots and no timeout, and compare the map observer trace against retail.
- [ ] Commit the timeout correction and probe evidence tooling separately from later gameplay/UI fixes.

### Task 2: Match session slots and enter the authored race picker

**Files:** `games/warcraft-3/game/g_utils.c`, `games/warcraft-3/game/tests/t_mapscript.c`, `wc3-parity/tools/prepare_legion_probe.py`, `wc3-parity/tools/run_legion_probe.py`, and focused evidence docs.

**Interfaces:** `GetPlayerSlotState` reports actual playing/left state; map `GetPlayerController` continues to report authored user/computer control. The observer records slot state, picker unit/ability set, action acceptance, lifecycle events, and selected race result.

- [ ] Rerun the original and current production OpenRealm binaries after Task 1 using an identical v1.6.509 map and verify hashes in each result.
- [ ] Assert one active user and only configured AI slots, a live `h0HJ` picker, successful page changes `vengeance`, `wispharvest`, `unroot`, and race selection `curseoff`.
- [ ] Turn every newly observed map error into a minimal failing game regression or Lua-native regression before correction.
- [ ] Verify selection builds the same authored race units/buildings and initializes the same race triggers as retail.
- [ ] Run the focused and full Warcraft III test targets and repeat the observer run.
- [ ] Commit slot/picker fixes separately from Task 1.

### Task 3: Reproduce the visible Warcraft III interface

**Files:** Use screenshot evidence in `wc3-parity/artifacts/`; change `games/warcraft-3/game/hud/hud_unit.c` or generic `client/ui`/renderer owners only after attributing a visual mismatch to that owner; add an owner-level regression beside each change.

**Interfaces:** Compare equal 1280x720 SD captures at equivalent game state and selected unit. Preserve skin-authored negative coordinates, visibility, texture, tooltip, hotkey, and button order as distinct properties.

- [ ] Capture retail picker and selected-race HUD with the existing tuning-controlled SD client, recording game/map hashes and camera/state.
- [ ] Capture the matching OpenRealm states using the engine framebuffer screenshot command after Task 1.
- [ ] Compare picker layout, command-card positions, icon art, selected-unit portrait, tooltip, and page transitions; record differences before editing.
- [ ] Add focused tests for negative `abpy` hidden buttons and any affected BlzFrame operations; implement generic semantics in their existing Warcraft UI owners.
- [ ] Repeat paired captures and inspect them visually; commit only verified UI corrections.

### Task 4: Match Legion AI execution

**Files:** OpenRealm `games/warcraft-3/game/g_bot.c`, JASS/native AI bindings, and corresponding `games/warcraft-3/game/tests`; diagnostic observer/report in `wc3-parity`.

**Interfaces:** One AI VM per configured computer player, using the map-authored script path and map-specific race data; player ownership and commanded unit counts remain observable through existing JASS natives.

- [ ] Inventory configured computer slots and each `CreateAiPlayer`/AI-start path from Legion Lua and compare to the OpenRealm observer trace.
- [ ] Record an original-versus-OpenRealm AI snapshot at fixed game times: VM started, starting workers/buildings, resources, first production order, and first attack/defense order.
- [ ] Write the smallest regression for the first mismatching generic AI contract, then correct only that contract.
- [ ] Verify no duplicate VM, no AI on empty/user slots, and repeat deterministic observations for the selected race.
- [ ] Run the full test suite and at least three fresh paired runs; commit AI corrections separately.

## Execution Notes

The agreed execution mode is inline in the current task. Existing commits `55cd3a44`, `478c2869` and all prior pushed work are the baseline; do not redo them. Pause only when the evidence reveals an architectural issue that needs a design decision or an external/manual action unavailable to the agent.
