# Legion Lua native coverage

## Runtime archive

Use `23_Race_Legion_v1_6_508.w3x` from the map checkout for current runs. The
unversioned `23-Race-Legion.w3x` dated September 22 contains a call to the missing
`Trig_StolicaAttacked_Conditions`. That aborts `InitCustomTriggers` before the
cleanup trigger is created. Do not turn `Condition(nil)` into a valid condition
to conceal this archive defect.

The 508 archive contains the condition. Its actual startup still exposed 60
`TriggerCondition` errors from the absent Lua `GetItemTypeId` binding through
`Blizzard.j`. Binding the getter to the existing item edict/rawcode contract
made all 15 deferred initialization steps succeed. This does not establish
complete native coverage or full playability.

## Coverage inventory

[legion-native-inventory.json](legion-native-inventory.json) records static
references in the extracted 508 Lua, matched against `HiveWE/data/tools/common.j`.
It contains 405 native names: 201 bindings, 48 reporting stubs, 152 unregistered
names and four no-op preload operations at this checkpoint. These are binding
categories, not behavior verification. References include aliases; transitive
`Blizzard.j` calls and dynamically constructed names require additional analysis
and runtime evidence. The JSON records the extracted script SHA-256.

User scope is all natives used by this map, including indirect helper calls.
Continue with frame handles/layout and then the remaining gameplay, event,
effect, presentation and data natives. Prefer shared mechanisms and the existing
JASS implementation; each binding still needs a focused behavior regression.
Do not return fabricated success from a missing native.

## Confirmed defects and fixes

- Multi-selection icons must read `UnitProfile.art`, which contains resolved map
  object overrides. A text-profile `FindConfigValue(..., Art)` lookup loses them.
- `ForGroup` must not walk the mutable membership array while callbacks remove
  units. Removal shifts later entries and skips every second unit. Both Lua and
  JASS now copy members plus spawn timestamps before dispatch; removed or reused
  entities are excluded without reading a destroyed group again.
- The init-integrity scenario also checks that `TestRegion` is empty after
  startup. Before the group fix the 508 archive left 19 units there even though
  all initialization steps reported success. The production build after the
  group fix passed that scenario at step 201 with zero initialization errors
  and an empty region. The same run still logs player tech-state capacity
  exhaustion at 256 entries; native coverage and playability remain incomplete.
- Custom `UISetup` currently cannot run: the `BlzFrame*` functions are reporting
  stubs. Its guard requires `ConsoleUIBackdrop` and `UpperButtonBarFrame`, so
  default HUD rendering is not proof of custom frame support. Retail child
  indices and origin frames need explicit contracts against the native frame
  hierarchy; do not invent aliases just to get past the guard.

## Chat contract

`Condition` and `Filter` return typed Lua userdata retaining a callback. Returning
the function itself caused the map's `TriggerAddCondition` wrapper to wrap it
again with `safeCall`, which discards the successful return value. Every such
condition evaluated false. Consumers now unwrap the handle and retain their own
callback reference; destroying the handle does not invalidate a registered
condition. Reusing a destroyed handle reports an error.

The real 508 `-ai1` scenario reached the action after this repair, then exposed
missing `SetPlayerHandicap` and `SetPlayerName`. Both now bind to the same player
state used by JASS. The scenario passed at step 202 with no callback errors;
this establishes command delivery, not complete AI gameplay.

Retail chat codes currently supported: `greedisgood`, `keysersoze`, `leafittome`,
`iseedeadpeople`, `warpten`, `allyourbasearebelongtous`, `somebodysetusupthebomb`.
They require single-player setup or `sv_cheats`; other stock cheats remain gaps.
Unknown text continues through ordinary map chat events.

## Race selector gaps

The lobby creates `n04G` (mode selection); `CreateRaceCircles` creates `h0HJ`
after the lobby expires. UnitAbilities previously inherited only the base SLK
row, losing authored `uabi`/`uhab` lists. Stable map-local merges now apply these
lists through the existing metadata schema, including original-object edits and
custom-object inheritance.

Right-click teleport is `Trig_CircleMove_Code`, registered through
`TriggerRegisterPlayerMouseEventBJ`. It reads `BlzGetTriggerPlayerMouseButton`
and `BlzGetTriggerPlayerMousePosition`. Mouse down/up/move now use a subscribed
world-input command, the ordinary queued event point/value context, and shared
Lua/JASS getters. The server snapshots the active subscriptions in a reserved
player stat; UI-captured input is excluded and motion is coalesced per input pass.
Mouse enums compare their payload in JASS, matching other converted enums.

`IsTerrainPathable` was absent in Lua and returned unconditional true in JASS.
Both now query real pathing cells for walk/fly/build blocking. Other pathing
types report an explicit error until their native semantics are implemented.
Cooldown start/end/remaining and unit invulnerability now use existing shared
gameplay state rather than reporting stubs.
`SetUnitPosition` and `SetUnitPositionLoc` share the Move-owned relocation helper
with JASS, retaining collision placement, entity linking, fog invalidation and
region-transition notifications. The real mouse scenario exposed the missing
location setter after the pathability query was repaired.
The real 508 mouse scenario passed at step 202: it creates the race selector,
sends a right-button event through `G_ClientCommand`, and verifies that the
map's own registered handler moved it to the chosen walkable destination with
no callback errors. This is server/map behavior proof; the client relay has its
own focused test, and a visible UI interaction still needs a fresh user run.

The selector's command art is authored in `war3mapSkin.w3a`. The archive loader
previously ignored that member, and `FindConfigValue` only read text profiles.
The loader now merges the skin modifications into the same object rows, retaining
gameplay fields and original/custom identity. Map ability rows retain authored
`Art`, `ResearchArt` and `Unart`, including empty values and custom inheritance.
An archive regression checks that a v3 skin row augments the existing custom
ability and reaches profile lookup; a separate SLK regression checks inheritance
and reset. Lua map-script tests pass 299/299 and SLK tests 606/606 in both editions.
This restores data lookup; it does not establish visual or spell-selection parity.
The real 508 scenario passes at step 202 with no initialization errors: the race
selector now exposes 11 commands (previously zero), and the initial mode selector
exposes three. The scenario also confirms the ordinary `-ai1` chat callback.
Channel (`ANcl`) still uses an ad-hoc command procedure rather than the shared
spell lifecycle, so restoring its button art is not proof of working race casts.
Custom frame support and player tech-state capacity exhaustion also remain gaps.

## Chat transport

Enter opens a client text composer, Enter sends, Escape cancels. The client
forwards one reliable `say <text>` string directly, without executing its text
through the command buffer. The server treats the remainder as opaque text,
preserving quotes, semicolons and repeated spaces.

`EVENT_PLAYER_CHAT` keeps its own text in the event queue. Registration keeps
the player, match string and exact-match flag; non-exact registration searches
for a substring. JASS conditions/actions and Lua callbacks receive independent
text and match context. The JASS snapshot format is 7; the game save format is
48 and rejects preceding layouts. The message limit is 255 bytes.

Map-defined cheat commands are ordinary chat messages. Engine developer cheats
remain in the console; chat does not execute arbitrary engine commands.

## Verification

`scenarios/23-race-legion-init-integrity.lua` checks initialization errors,
required globals/triggers and bottom-region cleanup at simulation frame 200.
Use `+dedicated 1`, `+set com_fast_forward 1` and `+com_frame_limit 320` for a
bounded run. `+set dedicated 1` and `+set com_frame_limit ...` are not substitutes
for these commands. A run without a terminal `WC3_SCENARIO` result is incomplete.

Focused regressions live in `t_mapscript.c`, `t_jass_map.c`, `t_game.c` and the
client console module. Logs and extracted copyrighted scripts belong in ignored
`build/` paths, not the repository.

Checkpoint checks: Lua map-script tests 278/278 assertions in 61 tests (TFT),
multi-selection tests 42/42 in both editions, chat client 7/7, chat JASS 2/2 in
both editions, group-removal JASS 1/1, and save checks 1180/1180 in 136 tests.
This is focused verification, not a full-suite result or visual HUD parity.
