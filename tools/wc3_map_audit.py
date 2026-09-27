#!/usr/bin/env python3
"""Run retail Warcraft III campaign maps (or selected loose Maps/*.w3x) for a bounded frame budget.

Each map gets an isolated writable home and UDP port. The resulting JSON,
Markdown matrix, and raw logs are evidence of bounded runtime health only;
they do not prove that a mission is playable or completable.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import fnmatch
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
WTS_RE = re.compile(r"(?ms)^\s*STRING\s+(\d+)\s*\{\s*(.*?)\s*^\}")
BROAD_RE = re.compile(
    r"(?:error|failed|missing|invalid|unimplemented|assert|overflow|exhaust|\bfull\b|stopped|"
    r"unreachable|not found|fatal|signal|segmentation|abort|corrupt|cannot)", re.I)
FAMILY_MEANINGS = {
    "SIGSEGV": "Process exited on signal 11; an optional serial rerun confirms reproducibility.",
    "PROCESS_EXIT": "Process returned a nonzero exit code other than signal 11.",
    "SCRIPT_STARTUP": "Script load/config/main did not complete; the map never reached steady runtime.",
    "SCENARIO_INCOMPLETE": "A scenario was requested but the run ended without exactly one PASS/FAIL marker.",
    "SLK_MISSING": "A required SLK failed to load.",
    "CREEP_SLEEP_ART": "ACsp TargetArt is absent; engine used canonical art.",
    "CREEP_SLEEP_SPAWN": "Creep-sleep overlay creation failed.",
    "MODEL_POOL_FULL": "Server model configstring pool was full.",
    "SOUND_POOL_FULL": "Server sound configstring pool was full.",
    "INVENTORY_DATA": "AInv inv1 resolved to zero for the listed unit rawcodes.",
    "TECH_CAPACITY": "Per-player tech-state capacity was exhausted.",
    "AI_STOP": "Campaign AI script stopped on an unimplemented native.",
    "FIRE_EFFECT_REGISTRATION": "Building-fire model registration failed.",
    "DEFAULT_ZFOG": "DefaultZFog.Style is missing for version 1.",
    "PARSER_ERROR": "Runtime emitted an otherwise unqualified parser error.",
}

# A process can exit 0 after the frame budget even when its script never started
# running. These are the engine's own startup-failure signatures (Lua and JASS
# paths); reaching the frame limit only counts as health when none of them fired.
SCRIPT_STARTUP_FAILURE_PATTERN = (
    r"G_StartScripts: .* failed for |"
    r"G_SpawnEntities: (?:Lua load|Lua runtime prelude|Lua config|Lua dependency) failed for |"
    r"G_SpawnEntities: missing selected map script |"
    r"CM_ReadMapScript: map declares Lua but war3map\.lua is missing |"
    r"CM_ReadMapScript: missing war3map\.j"
)
SCRIPT_STARTUP_FAILURE_RE = re.compile(rf"^(?:{SCRIPT_STARTUP_FAILURE_PATTERN})", re.MULTILINE)


# The engine emits one line per script lifecycle phase. Parsing them lets the
# report distinguish reaching the frame budget from config/main completing,
# which the spec requires as three separate levels.
SCRIPT_PHASE_RE = re.compile(
    r"^WC3_SCRIPT\s+phase=(?P<phase>\w+)(?:\s+kind=(?P<kind>\w+))?"
    r"(?:\s+status=(?P<status>\w+))?",
    re.MULTILINE,
)
SCRIPT_PHASES = ("selection", "load", "config", "main")
UNSUPPORTED_NATIVE_RE = re.compile(r"^WC3_UNSUPPORTED_NATIVE\s+name=(?P<name>\S+)", re.MULTILINE)
# The scenario name is quoted so it may contain spaces (an unset --scenario-name
# falls back to the file path, which contains spaces).  status/steps/detail
# share the quoted grammar.
SCENARIO_RE = re.compile(
    r'^WC3_SCENARIO\s+name="(?P<name>[^"]*)"\s+status=(?P<status>PASS|FAIL)'
    r'(?:\s+steps=(?P<steps>\d+))?(?:\s+detail="(?P<detail>[^"]*)")?',
    re.MULTILINE,
)


def parse_script_phases(output: str) -> dict[str, str | None]:
    """Read the engine per-phase script markers.

    The kind field is recorded once from the selection line. Each phase
    defaults to None so a missing line stays distinct from an explicit ok.
    """
    phases: dict[str, str | None] = {name: None for name in SCRIPT_PHASES}
    phases["kind"] = None
    for match in SCRIPT_PHASE_RE.finditer(output):
        phase = match.group("phase")
        if match.group("kind"):
            phases["kind"] = match.group("kind")
        if phase in phases:
            phases[phase] = match.group("status") or "ok"
    return phases


def parse_unsupported_natives(output: str) -> list[str]:
    """List the exact native names the Lua bridge reported unimplemented."""
    return sorted({match.group("name") for match in UNSUPPORTED_NATIVE_RE.finditer(output)})


def parse_scenarios(output: str) -> list[dict[str, Any]]:
    """Read deterministic scenario PASS/FAIL lines emitted by the driver."""
    scenarios: list[dict[str, Any]] = []
    for match in SCENARIO_RE.finditer(output):
        scenarios.append({
            "name": match.group("name"),
            "status": match.group("status"),
            "steps": int(match.group("steps")) if match.group("steps") else None,
            "detail": match.group("detail") or "",
        })
    return scenarios


def classify_run_status(exit_code: int | None, output: str, expect_scenario: bool = False) -> str:
    """Map one bounded run to its honest outcome.

    A zero exit code only means the frame budget elapsed; it is not proof the
    map script ran. Any engine-reported script startup failure downgrades the
    result to script_error so the report cannot claim completion for a map
    whose load/config/main died.

    When expect_scenario is set the run must emit exactly one terminal
    WC3_SCENARIO marker.  A missing marker means the scenario never reached its
    assertion (for example the frame budget ended first); a duplicate marker
    means the protocol was violated.  Either is scenario_incomplete, never a
    silent pass.
    """
    if exit_code is None:
        return "timeout"
    phases = parse_script_phases(output)
    scenarios = parse_scenarios(output)
    # A script-level failure is the more precise diagnosis than the raw exit
    # code: an explicit failed phase, a FAIL scenario marker, or a startup
    # signature all mean config()/main()/the scenario did not complete, even
    # though the driver may return nonzero on purpose.
    if (SCRIPT_STARTUP_FAILURE_RE.search(output)
            or any(phases[phase] not in (None, "ok") for phase in SCRIPT_PHASES)
            or any(scenario["status"] == "FAIL" for scenario in scenarios)):
        return "script_error"
    # A crash must outrank a missing scenario marker: the process died before it
    # could report, so "crashed" is the true diagnosis and masking it as an
    # incomplete scenario would hide the most important failure class.
    if exit_code != 0:
        return "crashed"
    if expect_scenario and len(scenarios) != 1:
        return "scenario_incomplete"
    return "completed"


def run_mpqtool(mpqtool: Path, archive: Path, command: str, member: str) -> bytes:
    """Read archive data while retaining mpqtool's actionable error."""
    proc = subprocess.run(
        [str(mpqtool), "-mpq", str(archive), command, member], check=False, capture_output=True)
    if proc.returncode:
        detail = proc.stderr.decode(errors="replace").strip() or proc.stdout.decode(errors="replace").strip()
        raise RuntimeError(detail or f"mpqtool exited {proc.returncode}")
    return proc.stdout


def enumerate_maps(data: Path, mpqtool: Path) -> list[dict[str, str]]:
    """Enumerate the 44 RoC and 53 TFT campaign members from retail archives."""
    specs = [
        (data / "War3.mpq", "Maps/Campaign", "RoC"),
        (data / "Frozen Throne/War3x.mpq", "Maps/FrozenThrone/Campaign", "TFT"),
    ]
    maps = []
    for archive, directory, edition in specs:
        if not archive.is_file():
            raise RuntimeError(f"required archive not found: {archive}")
        listing = run_mpqtool(mpqtool, archive, "ls", directory).decode(errors="replace")
        for line in listing.splitlines():
            filename = line.strip().replace("\\", "/").removesuffix("/")
            if filename.lower().endswith((".w3m", ".w3x")):
                maps.append({
                    "archive": str(archive), "edition": edition,
                    "filename": filename, "path": f"{directory}/{filename}",
                })
    return maps


def loose_map_spec(path: Path, data: Path) -> dict[str, str]:
    """Build one audit row for a disk-resident map, including outside the data tree."""
    data = data.expanduser().resolve()
    candidate = path.expanduser()
    candidate = candidate.resolve() if candidate.is_absolute() else (Path.cwd() / candidate).resolve()
    if not candidate.is_file():
        raise RuntimeError(f"loose map not found: {path}")
    suffix = candidate.suffix.lower()
    if suffix not in (".w3m", ".w3x"):
        raise RuntimeError(f"loose map must be .w3m or .w3x: {candidate}")
    try:
        rel = candidate.relative_to(data)
    except ValueError:
        rel = Path(candidate.name)
    return {
        "archive": str(candidate),
        "edition": "TFT" if suffix == ".w3x" else "RoC",
        "filename": candidate.name,
        "path": str(rel).replace("\\", "/"),
        "map_dir": str(candidate.parent),
        "loose": "1",
    }


def clean_title(text: str) -> str:
    """Remove WC3 color/newline markup from report-facing map titles."""
    text = re.sub(r"\|c[0-9A-Fa-f]{8}|\|r", "", text)
    text = text.replace("\\n", " ").replace("\r", " ").replace("\n", " ")
    return re.sub(r"\s+", " ", text).strip()


def parse_wts(source: str) -> dict[str, str]:
    """Parse trigger strings, including the BOM-prefixed STRING 0 used by campaigns."""
    return {str(int(key)): value for key, value in WTS_RE.findall(source.lstrip("\ufeff"))}


def resolve_trig(text: str, strings: dict[str, str]) -> str:
    """Resolve one TRIGSTR token without inventing a fallback value."""
    match = re.fullmatch(r"TRIGSTR_0*(\d+)", text, re.I)
    return strings.get(str(int(match.group(1))), text) if match else text


def read_cstring(data: bytes, offset: int) -> tuple[str, int]:
    """Read one W3I NUL-terminated string and return the next byte offset."""
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError("unterminated W3I string")
    return data[offset:end].decode(errors="replace"), end + 1


def parse_w3i_name(info: bytes, wts: str, fallback: str) -> str:
    """Prefer the human-readable loading title/subtitle over the internal map ID."""
    if len(info) < 12:
        raise ValueError("war3map.w3i is too small")
    version = struct.unpack_from("<I", info)[0]
    offset = 12 + (16 if version >= 28 else 0)
    name, offset = read_cstring(info, offset)
    for _ in range(3):
        _, offset = read_cstring(info, offset)
    offset += 48 + 8 + 4 + 1 + 4
    if version >= 25:
        _, offset = read_cstring(info, offset)
    _, offset = read_cstring(info, offset)
    title, offset = read_cstring(info, offset)
    subtitle, _ = read_cstring(info, offset)
    strings = parse_wts(wts)
    fields = [clean_title(resolve_trig(text, strings)) for text in (title, subtitle)]
    fields = [text for text in fields if text]
    if fields:
        return " — ".join(dict.fromkeys(fields))
    return clean_title(resolve_trig(name, strings)) or fallback


def map_name(item: dict[str, str], mpqtool: Path) -> str:
    """Extract nested W3I/WTS metadata and resolve the report-facing name."""
    fallback = Path(item["filename"]).stem
    try:
        if item.get("loose"):
            archive = Path(item["archive"])
            info = run_mpqtool(mpqtool, archive, "cat", "war3map.w3i")
            try:
                wts = run_mpqtool(mpqtool, archive, "cat", "war3map.wts").decode(errors="replace")
            except RuntimeError:
                wts = ""
            return parse_w3i_name(info, wts, fallback)
        payload = run_mpqtool(mpqtool, Path(item["archive"]), "cat", item["path"])
        with tempfile.NamedTemporaryFile(prefix="wc3-map-audit-", suffix=Path(item["filename"]).suffix) as nested:
            nested.write(payload)
            nested.flush()
            info = run_mpqtool(mpqtool, Path(nested.name), "cat", "war3map.w3i")
            try:
                wts = run_mpqtool(mpqtool, Path(nested.name), "cat", "war3map.wts").decode(errors="replace")
            except RuntimeError:
                wts = ""
        return parse_w3i_name(info, wts, fallback)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        # A display-name lookup must not discard an otherwise complete audit;
        # the map already has a result and a log.  Record why the friendly name
        # is unavailable and keep the filename stem.
        print(f"warning: could not read map name for {item['filename']}: {error}", file=sys.stderr)
        return fallback


def compact_diagnostics(output: str, status: str, exit_code: int | None,
                        serial_crash: bool) -> tuple[list[str], set[str]]:
    """Collapse entity/resource floods while preserving counts and native names."""
    exact = collections.Counter(line.strip() for line in output.splitlines() if BROAD_RE.search(line))
    errors: list[str] = []
    families: set[str] = set()

    def take(pattern: str) -> list[tuple[str, int]]:
        matched = []
        regex = re.compile(pattern, re.I)
        for line in list(exact):
            if regex.search(line):
                matched.append((line, exact.pop(line)))
        return matched

    for line, count in take(r"^SLK: failed to load"):
        path = re.search(r"'([^']+)'", line)
        errors.append(f"SLK_MISSING `{path.group(1) if path else line}` ×{count}")
        families.add("SLK_MISSING")
    rows = take(r"CreepSleep: ACsp TargetArt missing")
    if rows:
        errors.append(f"CREEP_SLEEP_ART ×{sum(count for _, count in rows)}")
        families.add("CREEP_SLEEP_ART")
    rows = take(r"CreepSleep: failed to spawn ACsp overlay")
    if rows:
        errors.append(f"CREEP_SLEEP_SPAWN ×{sum(count for _, count in rows)}")
        families.add("CREEP_SLEEP_SPAWN")
    for start, family in ((32, "MODEL_POOL_FULL"), (288, "SOUND_POOL_FULL")):
        rows = take(rf"^SV_FindIndex: pool full start={start}")
        if rows:
            errors.append(f"{family} ×{sum(count for _, count in rows)} ({len(rows)} resources)")
            families.add(family)
    rows = take(r"^G_InventoryCapacity:")
    if rows:
        rawcodes: collections.Counter[str] = collections.Counter()
        for line, count in rows:
            match = re.search(r"G_InventoryCapacity: (\S+)", line)
            rawcodes[match.group(1) if match else "?"] += count
        detail = ", ".join(f"{name}×{count}" for name, count in sorted(rawcodes.items()))
        errors.append(f"INVENTORY_DATA ×{sum(rawcodes.values())} ({detail})")
        families.add("INVENTORY_DATA")
    rows = take(r"^G_FindTechSlot:.*capacity .* exhausted")
    if rows:
        errors.append(f"TECH_CAPACITY ×{sum(count for _, count in rows)}")
        families.add("TECH_CAPACITY")
    for line, count in sorted(take(rf"^(?:{SCRIPT_STARTUP_FAILURE_PATTERN})")):
        errors.append(f"SCRIPT_STARTUP `{line}` ×{count}")
        families.add("SCRIPT_STARTUP")
    for line, count in take(r"^JASS runtime error: unimplemented native:"):
        native = line.rsplit(":", 1)[-1].strip()
        errors.append(f"JASS `{native}` ×{count}")
        families.add(f"JASS:{native}")
    for line, count in take(r"^WC3 AI:.*stopped: unimplemented native:"):
        match = re.search(r"script (\S+) stopped: unimplemented native: (\S+)", line)
        detail = f"`{match.group(1)}` → `{match.group(2)}`" if match else f"`{line}`"
        errors.append(f"AI_STOP {detail} ×{count}")
        families.add("AI_STOP")
    rows = take(r"^onfire_level_changed: failed to register")
    if rows:
        errors.append(f"FIRE_EFFECT_REGISTRATION ×{sum(count for _, count in rows)} ({len(rows)} resources)")
        families.add("FIRE_EFFECT_REGISTRATION")
    for pattern, family in ((r"DefaultZFog\.Style is missing", "DEFAULT_ZFOG"),
                            (r"^Parser Error$", "PARSER_ERROR")):
        rows = take(pattern)
        if rows:
            errors.append(f"{family} ×{sum(count for _, count in rows)}")
            families.add(family)
    for line, count in sorted(exact.items()):
        errors.append(f"`{line}` ×{count}")
        families.add(line)
    if status == "crashed":
        if exit_code == -11:
            suffix = "; reproduced serially" if serial_crash else ""
            errors.insert(0, f"SIGSEGV (exit {exit_code}{suffix})")
            families.add("SIGSEGV")
        else:
            errors.insert(0, f"PROCESS_EXIT (code {exit_code})")
            families.add("PROCESS_EXIT")
    if status == "scenario_incomplete":
        count = len(parse_scenarios(output))
        detail = "no terminal scenario marker" if count == 0 else f"{count} terminal scenario markers"
        errors.insert(0, f"SCENARIO_INCOMPLETE ({detail})")
        families.add("SCENARIO_INCOMPLETE")
    return errors, families


def run_map(item: dict[str, str], index: int, args: argparse.Namespace, log_path: Path) -> dict[str, Any]:
    """Run one bounded server in an isolated home and retain its complete log."""
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="wc3-map-audit-home-") as home:
        env = os.environ.copy()
        env["XDG_DATA_HOME"] = home
        command = [
            str(args.binary), "-data", str(args.data),
            *(["-tft"] if item["edition"] == "TFT" else []),
            *(["+set", "extra_data", item["map_dir"]] if item.get("map_dir") else []),
            "+dedicated", "1", "+set", "game_port", str(args.port_base + index),
            "+set", "com_fast_forward", "1", "+set", "vid_hidden", "1",
            "+set", "skip_cutscene", "1", "+map", item["path"],
            "+com_frame_limit", str(args.frames),
            *(["+set", "wc3_scenario", args.scenario] if args.scenario else []),
            *(["+set", "wc3_scenario_name", args.scenario_name] if args.scenario_name else []),
        ]
        try:
            proc = subprocess.run(
                command, cwd=ROOT, env=env, check=False, capture_output=True,
                text=True, errors="replace", timeout=args.timeout)
            output = proc.stdout + proc.stderr
            exit_code = proc.returncode
            status = classify_run_status(exit_code, output, bool(args.scenario))
        except subprocess.TimeoutExpired as error:
            stdout = error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else error.stdout or ""
            stderr = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else error.stderr or ""
            output, exit_code = stdout + stderr, None
            status = classify_run_status(exit_code, output, bool(args.scenario))
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(output, encoding="utf-8")
    return {
        **item, "status": status, "exit_code": exit_code,
        "wall_seconds": round(time.monotonic() - started, 3), "log": str(log_path),
    }


def render_markdown(report: dict[str, Any]) -> str:
    """Render the audit as a GitHub-issue-ready per-map matrix."""
    maps = report["maps"]
    status = collections.Counter(item["status"] for item in maps)
    family_maps: collections.Counter[str] = collections.Counter()
    for item in maps:
        family_maps.update(item["families"])
    reproduced = sum(item.get("serial_crash", False) for item in maps if item["status"] == "crashed")
    sim = report["simulated_seconds"]
    sim_unit = "second" if sim == 1 else "seconds"
    kind = report.get("kind", "campaign")
    map_kind = "loose map" if kind == "loose" else "retail campaign map"
    map_count = f"{len(maps)} {map_kind}{'s' if len(maps) != 1 else ''}"
    lines = [
        f"## Actual bounded {kind}-map audit", "",
        f"Audited commit `{report['commit']}` with `build/bin/openwarcraft3`.", "",
        f"{map_count.capitalize()} launched headlessly with `com_fast_forward=1` for "
        f"**{report['frames']} frames / {sim:.0f} simulated {sim_unit}**. Each process used "
        f"an isolated writable home, a unique UDP port, and a {report['timeout_seconds']}s wall timeout. "
        f"The {report['jobs']}-worker sweep finished in {report['wall_seconds']:.1f}s.", "",
        f"Result: **{status['completed']} reached the frame limit, {status['script_error']} had script startup errors, "
        f"{status['scenario_incomplete']} ended without a scenario result, {status['crashed']} crashed, "
        f"{status['timeout']} timed out**. {reproduced} crashes reproduced serially.", "",
        "> Reaching the frame limit is a startup/runtime smoke result. It does not prove objectives, combat, "
        "cinematics, mission completion, or visual correctness.", "", "### Error-family reach", "",
        "| Family | Maps | Meaning |", "| --- | ---: | --- |",
    ]
    for family, count in family_maps.most_common():
        meaning = (f"Unimplemented native `{family.split(':', 1)[1]}` executed."
                   if family.startswith("JASS:") else FAMILY_MEANINGS.get(family, family))
        lines.append(f"| `{family}` | {count} | {meaning.replace('|', '&#124;')} |")
    if any(item.get("script") or item.get("scenarios") for item in maps):
        lines.extend([
            "", "### Script phases and scenarios", "",
            "| Map | Kind | selection | load | config | main | Scenarios | Unsupported natives |",
            "| --- | --- | --- | --- | --- | --- | --- | --- |",
        ])
        for item in maps:
            script = item.get("script") or {}
            scenarios = item.get("scenarios") or []
            rendered = ", ".join(f"`{s['name']}` {s['status']}" for s in scenarios) or "none"
            natives = ", ".join(f"`{n}`" for n in item.get("unsupported_natives") or []) or "none"
            phases = " | ".join(script.get(name) or "—" for name in SCRIPT_PHASES)
            lines.append(
                f"| {item['name'].replace('|', '&#124;')} | {script.get('kind') or '—'} | "
                f"{phases} | {rendered.replace('|', '&#124;')} | {natives.replace('|', '&#124;')} |")
    editions = ("RoC", "TFT") if kind == "campaign" else (None,)
    for edition in editions:
        title = f"{edition}: per-map results" if edition else "Per-map results"
        lines.extend([
            "", f"### {title}", "",
            "| Map name | Filename | Result | Errors observed |",
            "| --- | --- | --- | --- |",
        ])
        for item in maps:
            if edition and item["edition"] != edition:
                continue
            name = item["name"].replace("|", "&#124;")
            errors = "; ".join(item["compact_errors"]).replace("|", "&#124;") or "none"
            if item["status"] == "crashed":
                result = "SIGSEGV" if item["exit_code"] == -11 else f"exit {item['exit_code']}"
            else:
                result = item["status"]
            lines.append(f"| {name} | `{item['filename']}` | **{result}** | {errors} |")
    return "\n".join(lines) + "\n"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=ROOT / "data/Warcraft III")
    parser.add_argument("--mpqtool", type=Path, default=ROOT / "build/bin/mpqtool")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/bin/openwarcraft3")
    parser.add_argument(
        "--frames", type=int, default=600, help="server frames per map (10 frames = 1 simulated second)")
    parser.add_argument("--timeout", type=int, default=120, help="wall-clock seconds per map")
    parser.add_argument("--jobs", type=int, default=4, help="concurrent isolated map processes")
    parser.add_argument("--port-base", type=int, default=28100, help="first unique UDP port")
    parser.add_argument("--map", default="*", help="shell-style filename or archive-path filter")
    parser.add_argument(
        "--loose-map", action="append", default=[], metavar="PATH",
        help="audit a disk-resident .w3m/.w3x under --data instead of campaign archives")
    parser.add_argument("--limit", type=int, default=0, help="limit maps after filtering")
    parser.add_argument("--rerun-crashes", action="store_true", help="confirm first-pass crashes serially")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/wc3-map-audit")
    parser.add_argument("--fail-on-crash", action="store_true", help="exit nonzero after writing the report")
    parser.add_argument(
        "--scenario", default="", metavar="PATH",
        help="run a Lua scenario file (VFS path) for a deterministic PASS/FAIL result")
    parser.add_argument(
        "--scenario-name", default="", metavar="NAME",
        help="report label for --scenario (defaults to the scenario file path)")
    return parser.parse_args(argv)


def filter_maps(maps: list[dict[str, str]], pattern: str) -> list[dict[str, str]]:
    """Keep maps whose filename or +map path matches the shell-style filter."""
    needle = pattern.lower()
    return [
        item for item in maps
        if fnmatch.fnmatch(item["filename"].lower(), needle)
        or fnmatch.fnmatch(item["path"].lower(), needle)
    ]


def main() -> int:
    args = parse_args()
    if args.frames < 1 or args.timeout < 1 or args.jobs < 1:
        print("error: frames, timeout, and jobs must be positive", file=sys.stderr)
        return 2
    if args.port_base < 1024 or args.port_base + 1000 >= 65536:
        print("error: port-base must leave room for unique audit ports", file=sys.stderr)
        return 2
    for tool in (args.mpqtool, args.binary):
        if not tool.is_file():
            print(f"error: executable not found: {tool}", file=sys.stderr)
            return 2
    try:
        if args.loose_map:
            maps = [loose_map_spec(Path(path), args.data) for path in args.loose_map]
        else:
            maps = enumerate_maps(args.data, args.mpqtool)
        maps = filter_maps(maps, args.map)
        if args.limit:
            maps = maps[:args.limit]
        if not maps:
            kind = "loose" if args.loose_map else "campaign"
            raise RuntimeError(f"no {kind} maps matched {args.map!r}")
        args.output_dir.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        results: list[dict[str, Any] | None] = [None] * len(maps)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            pending = {}
            for index, item in enumerate(maps):
                log = args.output_dir / "logs" / f"{index:03d}-{Path(item['filename']).stem}.log"
                pending[pool.submit(run_map, item, index, args, log)] = index
            for future in concurrent.futures.as_completed(pending):
                index = pending[future]
                try:
                    results[index] = future.result()
                except Exception as error:
                    results[index] = {
                        **maps[index], "status": "auditor-error", "exit_code": None,
                        "wall_seconds": 0, "log": "", "auditor_error": str(error),
                    }
                done = sum(result is not None for result in results)
                print(f"[{done:02d}/{len(results)}] {maps[index]['filename']}: {results[index]['status']}", flush=True)
        report_maps = [item for item in results if item is not None]
        if args.rerun_crashes:
            for index, item in enumerate(report_maps):
                if item["status"] != "crashed":
                    continue
                log = args.output_dir / "logs" / f"rerun-{Path(item['filename']).stem}.log"
                rerun = run_map(item, index, args, log)
                item["serial_crash"] = rerun["status"] == "crashed" and rerun["exit_code"] == item["exit_code"]
                item["serial_rerun"] = {
                    "status": rerun["status"], "exit_code": rerun["exit_code"],
                    "wall_seconds": rerun["wall_seconds"], "log": rerun["log"],
                }
        for item in report_maps:
            item["name"] = map_name(item, args.mpqtool)
            output = Path(item["log"]).read_text(encoding="utf-8", errors="replace") if item.get("log") else item.get("auditor_error", "")
            errors, families = compact_diagnostics(
                output, item["status"], item["exit_code"], item.get("serial_crash", False))
            item["compact_errors"], item["families"] = errors, sorted(families)
            item["script"] = parse_script_phases(output)
            item["unsupported_natives"] = parse_unsupported_natives(output)
            item["scenarios"] = parse_scenarios(output)
        report = {
            "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "frames": args.frames, "simulated_seconds": args.frames / 10,
            "timeout_seconds": args.timeout, "jobs": args.jobs,
            "wall_seconds": round(time.monotonic() - started, 3),
            "kind": "loose" if args.loose_map else "campaign", "maps": report_maps,
        }
        markdown = render_markdown(report)
        (args.output_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (args.output_dir / "report.md").write_text(markdown, encoding="utf-8")
        print(f"wrote {args.output_dir / 'report.json'}")
        print(f"wrote {args.output_dir / 'report.md'}")
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    failed = any(item["status"] != "completed" for item in report_maps)
    return 1 if args.fail_on_crash and failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
