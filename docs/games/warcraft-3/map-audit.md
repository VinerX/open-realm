# Warcraft III Campaign Map Audit

`tools/wc3_map_audit.py` runs every retail RoC and TFT campaign map for a
bounded number of headless server frames, and can smoke-run a loose custom map
under the data tree with `--loose-map`. It retains raw logs and produces a
JSON report plus a GitHub-ready Markdown matrix containing the human-readable
map name, filename, result, and errors observed for every map.

This is a smoke audit, not a completion test. Reaching the frame limit does not
show that objectives progress, the player can survive, cinematics render, or a
mission can be won.

## Prerequisites

Build the engine and MPQ diagnostic tool, and ensure the retail installation is
available at `data/Warcraft III`:

```sh
make build/bin/openwarcraft3 build/bin/mpqtool
```

The Make target builds these dependencies automatically.

## Run All Campaign Maps

The canonical audit uses 600 frames (60 simulated seconds at the fixed 10 Hz
server loop), four isolated workers, a 120-second per-map wall timeout, and a
serial confirmation run for every first-pass crash:

```sh
make audit-wc3-maps
```

Outputs are written to:

- `build/wc3-map-audit/report.json`: complete machine-readable results;
- `build/wc3-map-audit/report.md`: issue-ready summary and per-map tables; and
- `build/wc3-map-audit/logs/`: complete first-pass and serial-rerun logs.

To replace the body of the tracking issue with a fresh report:

```sh
gh issue edit 418 --body-file build/wc3-map-audit/report.md
```

Hero walk / save / load on the same campaign enumerator is a separate local
diagnostic: `make audit-wc3-hero-saveload`. See
[Save/Load](save-load.md#hero-walk--save--load).

Custom-map playability for DotA is tracked separately in
[#431](https://github.com/corepunch/open-realm/issues/431).

## Focused and Longer Runs

Pass runner arguments through `WC3_AUDIT_ARGS`:

```sh
make audit-wc3-maps WC3_AUDIT_ARGS='--map Human05.w3m --frames 18000 --jobs 1'
make audit-wc3-maps WC3_AUDIT_ARGS='--map "Human*.w3m" --frames 1200 --jobs 2'
```

At 10 Hz, `18000` frames requests 30 simulated minutes. For Human05 that can
exercise timer expiry only if gameplay is able to advance without player
actions. It still cannot prove that the defense is winnable. A deterministic
scenario must separately establish a valid player state and assert timer,
attack-wave, objective, survival, and victory milestones.

Useful direct options include:

```text
--frames N          server-frame budget per map
--timeout N         wall-clock seconds per map
--jobs N            concurrent isolated map processes
--map GLOB          filename or archive-path filter
--loose-map PATH    audit a disk-resident .w3m/.w3x under --data (repeatable)
--limit N           audit only the first N filtered maps
--rerun-crashes     rerun first-pass crashes serially
--fail-on-crash     return nonzero after reports have been written
--output-dir PATH   report/log destination
```

## Per-Phase Script Result and Scenarios

The engine prints one `WC3_SCRIPT` line per script lifecycle phase
(`selection`, `load`, `config`, `main`) and a `WC3_SCENARIO` line when a
deterministic scenario runs. The report parses these into a script table so a
map that reached the frame budget without completing `config()`/`main()` can
no longer be reported as `completed`:

```text
WC3_SCRIPT phase=selection kind=lua
WC3_SCRIPT phase=load status=ok
WC3_SCRIPT phase=config status=ok
WC3_SCRIPT phase=main status=ok
WC3_SCENARIO name=legion-smoke status=PASS steps=201 detail=""
```

Pass `--scenario PATH` to run a Lua scenario file in the map's own VM after
`config()`/`main()`; `--scenario-name NAME` sets the report label. A scenario
file defines one hook:

```lua
function scenario_step(frame)
  if frame < 200 then return nil end  -- keep advancing the simulation
  if not SomeObservedState() then return 'FAIL: what was expected' end
  return 'PASS'
end
```

Returning `nil` keeps the run advancing, `"PASS"` finishes it successfully, and
any other string fails with that text as the detail. A failed phase, a `FAIL`
scenario marker, or a startup signature all classify the run as `script_error`.

Minimal invocation against 23-Race Legion (reads CASC read-only, so the game
data path stays outside the workspace):

```sh
python tools/wc3_map_audit.py \
  --data 'E:\Games\Warcraft III' \
  --binary build/bin/openwarcraft3 --mpqtool build/bin/mpqtool \
  --jobs 1 --frames 600 --timeout 240 \
  --scenario scenarios/23-race-legion-smoke.lua --scenario-name legion-smoke \
  --loose-map 'C:\Development\Warcraft 3\23-Race-Legion\23-Race-Legion.w3x' \
  --output-dir build/wc3-map-audit-legion
```

Each worker receives a temporary `XDG_DATA_HOME` and a unique `game_port`.
This prevents persistent campaign state and socket collisions from leaking
between maps. Four workers is intentionally conservative because large maps can
consume significant memory.

## Run the Tool Tests

Parser, diagnostic-compaction, and report tests require no retail data:

```sh
make test-wc3-map-audit
# equivalent:
python3 tests/test_wc3_map_audit.py
```

For an integration check against retail data without sweeping all maps:

```sh
make audit-wc3-maps WC3_AUDIT_ARGS='--map Human05.w3m --frames 10 --jobs 1'
```

## Interpretation

The report preserves exact native names, AI script paths, unit rawcodes, event
counts, and full raw logs. High-volume unit/resource messages are compacted
into counted families so one map remains one readable table row.

`completed` means only that the process reached `com_frame_limit` with exit
code zero. `SIGSEGV`, `timeout`, and `auditor-error` are process-level failures.
Warnings and runtime errors remain listed even when the process reaches its
frame limit. Visual correctness requires a rendered pass or human inspection;
mission completion requires scripted milestones or human play.

## Custom Maps

Pass `--loose-map` for a disk-resident scenario under the data tree (typically
`data/Warcraft III/Maps/*.w3x`). The auditor still isolates `XDG_DATA_HOME`,
assigns a unique `game_port`, enables `com_fast_forward` / `vid_hidden` /
`+dedicated 1`, and adds `-tft` for `.w3x` (RoC for `.w3m`). Crash and timeout
outcomes are recorded the same way as campaign maps.

```sh
python3 tools/wc3_map_audit.py \
  --data 'data/Warcraft III' \
  --binary build/bin/openwarcraft3 \
  --mpqtool build/bin/mpqtool \
  --jobs 1 --frames 10 --timeout 60 \
  --loose-map 'data/Warcraft III/Maps/DotA v6.83dAI PMV 1.42 EN.w3x'
```

Equivalent Make form:

```sh
make audit-wc3-maps WC3_AUDIT_ARGS="--jobs 1 --frames 10 --timeout 60 --loose-map 'data/Warcraft III/Maps/DotA v6.83dAI PMV 1.42 EN.w3x'"
```

The loose map may also live outside the Warcraft III data tree. Its parent
folder is mounted as `extra_data`, while `--data` remains the CASC game
installation. For example:

```powershell
make audit-wc3-maps WC3DATA='E:\Games\Warcraft III' WC3_AUDIT_ARGS='--jobs 1 --frames 600 --timeout 180 --loose-map "C:\Development\Warcraft 3\23-Race-Legion\23-Race-Legion.w3x" --output-dir build/wc3-map-audit-23-race'
```

This runs one TFT map headlessly for at most 600 server frames and keeps the
report and full log under `build/wc3-map-audit-23-race`. Reaching the frame
budget demonstrates bounded startup/runtime only; it does not establish that
the map is fully playable.

To audit every loose `.w3x` in a folder from PowerShell, pass each file as a
repeated `--loose-map` argument. The game data path can remain a read-only CASC
installation:

```powershell
$maps = Get-ChildItem -LiteralPath 'C:\Users\Dmitry\Documents\Warcraft III\Maps\Download\Season9' -File -Filter '*.w3x'
$auditArgs = @('--data', 'E:\Games\Warcraft III', '--binary', 'build/bin/openwarcraft3.exe', '--mpqtool', 'build/bin/mpqtool.exe', '--jobs', '4', '--frames', '10', '--timeout', '60', '--output-dir', 'build/wc3-map-audit-season9')
foreach ($map in $maps) { $auditArgs += @('--loose-map', $map.FullName) }
$env:PATH = "$env:TEMP\wc3lua\msys2out\msys64\ucrt64\bin;$PWD\build\lib;$env:PATH"
python tools/wc3_map_audit.py @auditArgs
```

This bounded smoke pass launched 46 Season9 maps to the 10-frame limit with no
process crashes. That result does not establish successful script initialization
or full playability; inspect each map's log and error families in the report.

Without `--loose-map`, the enumerator only walks retail campaign members inside
`War3.mpq` / `War3x.mpq`. See [DotA Custom-Map Playability](dota-map-playability.md).
