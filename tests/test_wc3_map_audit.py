"""Tests for the bounded Warcraft III campaign-map audit."""

from __future__ import annotations

import importlib.util
import os
import struct
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOTA_LOOSE = ROOT / "data/Warcraft III/Maps/DotA v6.83dAI PMV 1.42 EN.w3x"
sys.dont_write_bytecode = True  # exec_module below must not leave tools/__pycache__ behind
SPEC = importlib.util.spec_from_file_location("wc3_map_audit", ROOT / "tools/wc3_map_audit.py")
AUDIT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(AUDIT)


def cstring(text: str) -> bytes:
    return text.encode() + b"\0"


def w3i_fixture(name: str, title: str, subtitle: str) -> bytes:
    data = struct.pack("<III", 18, 0, 0)
    data += b"".join(cstring(text) for text in (name, "author", "description", "players"))
    data += bytes(48 + 8 + 4 + 1 + 4)
    data += b"".join(cstring(text) for text in ("loading text", title, subtitle))
    return data


class MapMetadataTest(unittest.TestCase):
    def test_name_lookup_failure_keeps_filename_stem(self):
        item = {
            "filename": "23-Race-Legion.w3x",
            "archive": "/nonexistent/map.w3x",
            "loose": "1",
        }
        with mock.patch.object(AUDIT, "run_mpqtool", side_effect=RuntimeError("mpqtool exited 1")):
            self.assertEqual(AUDIT.map_name(item, Path("mpqtool")), "23-Race-Legion")

    def test_wts_zero_id_and_loading_title_are_resolved(self):
        wts = "\ufeffSTRING 0\n{\nChapter Five\n}\nSTRING 1\n{\nMarch of the Scourge\n}\n"
        name = AUDIT.parse_w3i_name(w3i_fixture("Human05", "TRIGSTR_000", "TRIGSTR_001"), wts, "fallback")
        self.assertEqual(name, "Chapter Five — March of the Scourge")

    def test_map_name_is_used_without_loading_titles(self):
        self.assertEqual(AUDIT.parse_w3i_name(w3i_fixture("Human05", "", ""), "", "fallback"), "Human05")


class DiagnosticTest(unittest.TestCase):
    def test_repeated_entity_errors_are_compacted(self):
        output = """
SLK: failed to load 'UI\\SoundInfo\\Music.slk'
WC3 CreepSleep: ACsp TargetArt missing; using canonical sleep art for unit 2
WC3 CreepSleep: ACsp TargetArt missing; using canonical sleep art for unit 9
SV_FindIndex: pool full start=32 max=256 name=a.mdx
SV_FindIndex: pool full start=32 max=256 name=b.mdx
JASS runtime error: unimplemented native: EnumItemsInRect
JASS runtime error: unimplemented native: EnumItemsInRect
"""
        errors, families = AUDIT.compact_diagnostics(output, "completed", 0, False)
        self.assertIn("CREEP_SLEEP_ART ×2", errors)
        self.assertIn("MODEL_POOL_FULL ×2 (2 resources)", errors)
        self.assertIn("JASS `EnumItemsInRect` ×2", errors)
        self.assertIn("JASS:EnumItemsInRect", families)

    def test_serially_reproduced_signal_is_reported(self):
        errors, families = AUDIT.compact_diagnostics("", "crashed", -11, True)
        self.assertEqual(errors, ["SIGSEGV (exit -11; reproduced serially)"])
        self.assertEqual(families, {"SIGSEGV"})

    def test_ordinary_nonzero_exit_is_not_called_sigsegv(self):
        errors, families = AUDIT.compact_diagnostics("", "crashed", 2, False)
        self.assertEqual(errors, ["PROCESS_EXIT (code 2)"])
        self.assertEqual(families, {"PROCESS_EXIT"})


class RunStatusTest(unittest.TestCase):
    def test_zero_exit_without_script_failure_is_completed(self):
        self.assertEqual(AUDIT.classify_run_status(0, "Game initialized.\nframe limit reached\n"), "completed")

    def test_timeout_is_reported_as_timeout(self):
        self.assertEqual(AUDIT.classify_run_status(None, ""), "timeout")

    def test_nonzero_exit_is_crashed(self):
        self.assertEqual(AUDIT.classify_run_status(-11, ""), "crashed")

    def test_lua_main_failure_downgrades_completed_run(self):
        output = (
            "Game initialized.\n"
            "G_StartScripts: Lua main failed for 23-Race-Legion.w3x: "
            "[string \"war3map.lua\"]:1: attempt to call a nil value\n"
            "frame limit reached\n"
        )
        self.assertEqual(AUDIT.classify_run_status(0, output), "script_error")

    def test_lua_config_failure_downgrades_completed_run(self):
        output = "G_SpawnEntities: Lua config failed for map.w3x: boom\n"
        self.assertEqual(AUDIT.classify_run_status(0, output), "script_error")

    def test_lua_load_and_dependency_failures_downgrade(self):
        for line in (
            "G_SpawnEntities: Lua load failed for map.w3x: boom",
            "G_SpawnEntities: Lua runtime prelude failed for map.w3x",
            "G_SpawnEntities: Lua dependency failed for Scripts\\Blizzard.j: boom",
            "G_SpawnEntities: missing selected map script in map.w3x",
            "CM_ReadMapScript: map declares Lua but war3map.lua is missing in (unknown)",
            "CM_ReadMapScript: missing war3map.j / scripts\\war3map.j in (unknown)",
        ):
            self.assertEqual(AUDIT.classify_run_status(0, line + "\n"), "script_error", line)

    def test_startup_failure_is_its_own_family(self):
        output = "G_StartScripts: Lua main failed for map.w3x: boom\n"
        errors, families = AUDIT.compact_diagnostics(output, "script_error", 0, False)
        self.assertIn("SCRIPT_STARTUP", families)
        self.assertTrue(any("SCRIPT_STARTUP" in error for error in errors))


class ScriptPhaseTest(unittest.TestCase):
    def test_lua_phases_and_kind_are_parsed(self):
        output = (
            "WC3_SCRIPT phase=selection kind=lua map=23-Race-Legion.w3x\n"
            "WC3_SCRIPT phase=load status=ok\n"
            "WC3_SCRIPT phase=config status=ok\n"
            "WC3_SCRIPT phase=main status=ok\n"
        )
        phases = AUDIT.parse_script_phases(output)
        self.assertEqual(phases["kind"], "lua")
        for phase in ("selection", "load", "config", "main"):
            self.assertEqual(phases[phase], "ok", phase)

    def test_failed_phase_is_recorded(self):
        output = (
            "WC3_SCRIPT phase=selection kind=lua map=map.w3x\n"
            "WC3_SCRIPT phase=load status=ok\n"
            "WC3_SCRIPT phase=config status=failed\n"
        )
        phases = AUDIT.parse_script_phases(output)
        self.assertEqual(phases["config"], "failed")
        self.assertIsNone(phases["main"])

    def test_missing_phase_lines_leave_none(self):
        phases = AUDIT.parse_script_phases("Game initialized.\n")
        self.assertTrue(all(value is None for value in phases.values()))

    def test_failed_engine_phase_downgrades_completed_run(self):
        output = (
            "WC3_SCRIPT phase=selection kind=lua map=map.w3x\n"
            "WC3_SCRIPT phase=config status=failed\n"
            "frame limit reached\n"
        )
        self.assertEqual(AUDIT.classify_run_status(0, output), "script_error")

    def test_unsupported_natives_are_listed_by_exact_name(self):
        output = (
            "WC3 Lua: SetWaterBaseColor presentation is not implemented\n"
            "WC3 Lua: SetWaterBaseColor presentation is not implemented\n"
            "WC3 Lua: NewSoundEnvironment('Default') audio environment is not implemented\n"
        )
        names = AUDIT.parse_unsupported_natives(output)
        self.assertIn("SetWaterBaseColor", names)
        self.assertIn("NewSoundEnvironment", names)
        self.assertEqual(names.count("SetWaterBaseColor"), 1)


class ScenarioTest(unittest.TestCase):
    def test_pass_and_fail_scenarios_are_parsed(self):
        output = (
            "WC3_SCENARIO name=legion-spawn status=PASS steps=12\n"
            "WC3_SCENARIO name=legion-order status=FAIL detail=\"unit missing at step 4\"\n"
        )
        scenarios = AUDIT.parse_scenarios(output)
        self.assertEqual(scenarios[0]["name"], "legion-spawn")
        self.assertEqual(scenarios[0]["status"], "PASS")
        self.assertEqual(scenarios[1]["status"], "FAIL")
        self.assertIn("unit missing", scenarios[1]["detail"])

    def test_scenario_failure_downgrades_completed_run(self):
        output = "WC3_SCENARIO name=legion-spawn status=FAIL detail=\"boom\"\nframe limit reached\n"
        self.assertEqual(AUDIT.classify_run_status(0, output), "script_error")

    def test_scenario_pass_keeps_completed_run(self):
        output = "WC3_SCENARIO name=legion-spawn status=PASS steps=8\nframe limit reached\n"
        self.assertEqual(AUDIT.classify_run_status(0, output), "completed")

    def test_parse_args_accepts_scenario(self):
        args = AUDIT.parse_args([
            "--loose-map", "data/Warcraft III/Maps/Fake.w3x",
            "--scenario", "scenarios/legion-spawn.lua",
            "--scenario-name", "legion-spawn",
        ])
        self.assertEqual(args.scenario, "scenarios/legion-spawn.lua")
        self.assertEqual(args.scenario_name, "legion-spawn")


    def test_report_renders_script_phases_and_scenarios(self):
        report = {
            "kind": "loose",
            "commit": "abc123",
            "frames": 600,
            "simulated_seconds": 60,
            "timeout_seconds": 240,
            "jobs": 1,
            "wall_seconds": 5.0,
            "maps": [{
                "edition": "TFT",
                "name": "23 Race Legion",
                "filename": "23-Race-Legion.w3x",
                "status": "completed",
                "compact_errors": [],
                "families": [],
                "script": {"kind": "lua", "selection": "ok", "load": "ok",
                           "config": "ok", "main": "ok"},
                "unsupported_natives": ["SetWaterBaseColor"],
                "scenarios": [{"name": "legion-spawn", "status": "PASS", "steps": 12, "detail": ""}],
            }],
        }
        markdown = AUDIT.render_markdown(report)
        self.assertIn("### Script phases and scenarios", markdown)
        self.assertIn("| lua | ok | ok | ok | ok |", markdown)
        self.assertIn("`legion-spawn` PASS", markdown)
        self.assertIn("`SetWaterBaseColor`", markdown)


class ReportTest(unittest.TestCase):
    def test_report_names_file_and_does_not_claim_completion(self):
        report = {
            "commit": "abc123",
            "frames": 600,
            "simulated_seconds": 60,
            "timeout_seconds": 120,
            "jobs": 4,
            "wall_seconds": 1.5,
            "maps": [{
                "edition": "RoC",
                "name": "Chapter Five — March of the Scourge",
                "filename": "Human05.w3m",
                "status": "completed",
                "compact_errors": ["JASS `SetCaptainHome` ×2"],
                "families": ["JASS:SetCaptainHome"],
            }],
        }
        markdown = AUDIT.render_markdown(report)
        self.assertIn("March of the Scourge", markdown)
        self.assertIn("`Human05.w3m`", markdown)
        self.assertIn("600 frames", markdown)
        self.assertNotIn("map works", markdown.lower())

    def test_loose_map_report_uses_one_map_table_without_campaign_sections(self):
        report = {
            "kind": "loose",
            "commit": "abc123",
            "frames": 10,
            "simulated_seconds": 1,
            "timeout_seconds": 60,
            "jobs": 1,
            "wall_seconds": 2.0,
            "maps": [{
                "edition": "TFT",
                "name": "Season Map",
                "filename": "(4)Season.w3x",
                "status": "completed",
                "compact_errors": [],
                "families": [],
            }],
        }
        markdown = AUDIT.render_markdown(report)
        self.assertIn("bounded loose-map audit", markdown)
        self.assertIn("1 loose map launched", markdown)
        self.assertIn("### Per-map results", markdown)
        self.assertIn("`(4)Season.w3x`", markdown)
        self.assertNotIn("### RoC:", markdown)
        self.assertNotIn("### TFT:", markdown)


class LooseMapTest(unittest.TestCase):
    def test_w3x_under_data_is_tft_with_relative_map_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "Warcraft III"
            target = data / "Maps" / "Fake DotA.w3x"
            target.parent.mkdir(parents=True)
            target.write_bytes(b"fake")
            item = AUDIT.loose_map_spec(target, data)
            self.assertEqual(item["edition"], "TFT")
            self.assertEqual(item["filename"], "Fake DotA.w3x")
            self.assertEqual(item["path"], "Maps/Fake DotA.w3x")
            self.assertEqual(item["archive"], str(target.resolve()))
            self.assertEqual(item["map_dir"], str(target.parent.resolve()))
            self.assertEqual(item["loose"], "1")

    def test_w3m_under_data_is_roc(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "Warcraft III"
            target = data / "Maps" / "Custom.w3m"
            target.parent.mkdir(parents=True)
            target.write_bytes(b"fake")
            item = AUDIT.loose_map_spec(target, data)
            self.assertEqual(item["edition"], "RoC")
            self.assertEqual(item["path"], "Maps/Custom.w3m")

    def test_relative_path_resolves_from_cwd(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "Warcraft III"
            target = data / "Maps" / "Rel.w3x"
            target.parent.mkdir(parents=True)
            target.write_bytes(b"fake")
            old = os.getcwd()
            try:
                os.chdir(tmp)
                item = AUDIT.loose_map_spec(Path("Warcraft III/Maps/Rel.w3x"), data)
            finally:
                os.chdir(old)
            self.assertEqual(item["path"], "Maps/Rel.w3x")

    def test_missing_map_is_rejected_and_external_map_gets_mount_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "data"
            data.mkdir()
            with self.assertRaisesRegex(RuntimeError, "loose map not found"):
                AUDIT.loose_map_spec(data / "Maps" / "Missing.w3x", data)
            outside = Path(tmp) / "elsewhere.w3x"
            outside.write_bytes(b"fake")
            item = AUDIT.loose_map_spec(outside, data)
            self.assertEqual(item["path"], "elsewhere.w3x")
            self.assertEqual(item["map_dir"], str(outside.parent.resolve()))
            bad = data / "Maps" / "note.txt"
            bad.parent.mkdir(parents=True)
            bad.write_text("nope")
            with self.assertRaisesRegex(RuntimeError, r"must be \.w3m or \.w3x"):
                AUDIT.loose_map_spec(bad, data)

    def test_parse_args_accepts_loose_map(self):
        args = AUDIT.parse_args([
            "--loose-map", "data/Warcraft III/Maps/Fake.w3x",
            "--loose-map", "data/Warcraft III/Maps/Other.w3m",
            "--frames", "10", "--jobs", "1", "--timeout", "60",
        ])
        self.assertEqual(args.loose_map, [
            "data/Warcraft III/Maps/Fake.w3x",
            "data/Warcraft III/Maps/Other.w3m",
        ])
        self.assertEqual(args.frames, 10)

    def test_filter_maps_matches_loose_path(self):
        maps = [{
            "filename": "DotA v6.83dAI PMV 1.42 EN.w3x",
            "path": "Maps/DotA v6.83dAI PMV 1.42 EN.w3x",
            "edition": "TFT",
        }]
        self.assertEqual(len(AUDIT.filter_maps(maps, "*DotA*")), 1)
        self.assertEqual(len(AUDIT.filter_maps(maps, "Maps/*")), 1)
        self.assertEqual(AUDIT.filter_maps(maps, "Human05.w3m"), [])


@unittest.skipUnless(DOTA_LOOSE.is_file(), "DotA map not installed under data/Warcraft III/Maps")
class DotALooseMapOptionalTest(unittest.TestCase):
    def test_installed_dota_resolves_under_data(self):
        data = ROOT / "data/Warcraft III"
        item = AUDIT.loose_map_spec(DOTA_LOOSE, data)
        self.assertEqual(item["edition"], "TFT")
        self.assertEqual(item["path"], "Maps/DotA v6.83dAI PMV 1.42 EN.w3x")
        self.assertEqual(item["loose"], "1")


if __name__ == "__main__":
    unittest.main()
