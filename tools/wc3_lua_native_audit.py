#!/usr/bin/env python3
"""Find map Lua calls to Warcraft natives that the engine Lua VM does not register.

The audit is static: it compares calls in a map's generated/split Lua with native
declarations in common.txt and the engine's Lua registration table. It is a
candidate report, not proof that a particular call is reached at runtime.
"""

from __future__ import annotations

import argparse
import bisect
import re
from pathlib import Path

TOKEN = re.compile(
    r"(?P<comment>--\[(=*)\[.*?\]\2\]|--[^\n]*)"
    r"|(?P<longstring>\[(=*)\[.*?\]\4\])"
    r"|(?P<string>'(?:\\.|[^'\\])*'|\"(?:\\.|[^\"\\])*\")"
    r"|(?P<identifier>[A-Za-z_][A-Za-z_0-9]*)"
    r"|(?P<punct>.)", re.DOTALL)
NATIVE_DECL = re.compile(r"^\s*(?:constant\s+)?native\s+([A-Za-z_][A-Za-z_0-9]*)\s+takes\b", re.M)
DIRECT_REGISTRATION = re.compile(
    r'WC3_LuaRegisterNative(?:Named)?\s*\(\s*L\s*,\s*"([^"]+)"')
JASS_REGISTRATION = re.compile(r'\{\s*"([^"]+)"\s*,\s*[A-Za-z_][A-Za-z_0-9]*\s*\}')
ARRAY_REGISTRATION = re.compile(
    r"static\s+cstring_t\s+const\s+(?:blz_stubs|enum_converters|ai_common_names)\s*\[\s*\]\s*=\s*\{(.*?)\};", re.S)


def lua_tokens(source: str):
    for match in TOKEN.finditer(source):
        if match.lastgroup in ("comment", "longstring", "string"):
            continue
        yield match.group(0), match.start()


def lua_symbols(path: Path) -> tuple[set[str], dict[str, set[tuple[int, str]]]]:
    source = path.read_text(encoding="utf-8", errors="replace")
    tokens = list(lua_tokens(source))
    offsets = [offset for _, offset in tokens]
    line_starts = [0] + [i + 1 for i, char in enumerate(source) if char == "\n"]
    functions: set[str] = set()
    calls: dict[str, set[tuple[int, str]]] = {}

    for index, (token, offset) in enumerate(tokens):
        if token == "function" and index + 1 < len(tokens):
            name = tokens[index + 1][0]
            if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
                functions.add(name)
        if (re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", token) and
                index + 2 < len(tokens) and tokens[index + 1][0] == "=" and
                tokens[index + 2][0] == "function"):
            functions.add(token)
        if (re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", token) and
                index + 1 < len(tokens) and tokens[index + 1][0] == "("):
            line = bisect.bisect_right(line_starts, offset)
            calls.setdefault(token, set()).add((line, str(path)))
    return functions, calls


def registered_natives(path: Path, jass_module: Path | None = None) -> set[str]:
    source = path.read_text(encoding="utf-8", errors="replace")
    names = set(DIRECT_REGISTRATION.findall(source))
    for body in ARRAY_REGISTRATION.findall(source):
        names.update(re.findall(r'"([^"]+)"', body))
    if jass_module:
        names.update(JASS_REGISTRATION.findall(jass_module.read_text(encoding="utf-8", errors="replace")))
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map_dir", type=Path, help="Extracted map directory containing war3map.lua and/or split Lua")
    parser.add_argument("--common", type=Path, default=Path("games/warcraft-3/game/common.txt"))
    parser.add_argument("--lua-api", type=Path, default=Path("games/warcraft-3/game/lua_api.c"))
    parser.add_argument("--jass-api", type=Path, default=Path("games/warcraft-3/game/api/api_module.c"))
    parser.add_argument("--common-j", type=Path, default=Path("games/warcraft-3/tests/resources-src/Scripts/common.j"))
    parser.add_argument("--blizzard-j", type=Path, default=Path("games/warcraft-3/tests/resources-src/Scripts/Blizzard.j"))
    parser.add_argument("--common-ai", type=Path, default=Path("games/warcraft-3/tests/resources-src/Scripts/common.ai"))
    parser.add_argument("--format", choices=("summary", "full"), default="summary")
    args = parser.parse_args()

    files = sorted(args.map_dir.rglob("*.lua"))
    if not files:
        parser.error(f"no Lua sources found under {args.map_dir}")
    native_names = set(NATIVE_DECL.findall(args.common.read_text(encoding="utf-8", errors="replace")))
    registered = registered_natives(args.lua_api)
    jass_registered = set(JASS_REGISTRATION.findall(args.jass_api.read_text(encoding="utf-8", errors="replace")))
    functions: set[str] = set()
    for path in (args.common_j, args.blizzard_j, args.common_ai):
        if path.exists():
            script = path.read_text(encoding="utf-8", errors="replace")
            functions.update(re.findall(r"^\s*function\s+([A-Za-z_][A-Za-z_0-9]*)\s+takes\b", script, re.M))
    calls: dict[str, set[tuple[int, str]]] = {}
    ai_files: set[str] = set()
    for path in files:
        defined, called = lua_symbols(path)
        functions.update(defined)
        for name, sites in called.items():
            calls.setdefault(name, set()).update(sites)
        if (any(part.lower() in {"_ai", "ai", "scripts"} for part in path.parts) or
                path.suffix.lower() == ".ai" or re.search(r"(?:^|_)ai(?:_|\.|$)", path.stem.lower())):
            ai_files.add(str(path))

    missing = {name for name in calls if name in native_names and name not in registered and name not in functions}
    optional = set()
    for name in missing:
        guard = re.compile(rf"if\s+{re.escape(name)}\s*~=\s*nil\s+then(?:(?!\bend\b).)*?\b{re.escape(name)}\s*\(", re.S)
        if any(path.suffix == ".lua" and guard.search(path.read_text(encoding="utf-8", errors="replace"))
               for path in files if str(path) in {site for _, site in calls[name]}):
            optional.add(name)
    required = missing - optional
    ai_missing = {name for name in required if any(path in ai_files for _, path in calls[name])}
    print(f"Lua files: {len(files)}")
    print(f"Common native declarations: {len(native_names)}")
    lua_registered = registered_natives(args.lua_api)
    print(f"Lua runtime registrations: {len(lua_registered)}")
    print(f"JASS C API entries (not exposed automatically to Lua): {len(jass_registered)}")
    print(f"Common natives called by map: {sum(name in native_names for name in calls)}")
    print(f"Common natives called by map without Lua registration: {len(missing)}")
    print(f"Unregistered common natives called by AI sources: {len(ai_missing)}")
    print(f"Optional calls behind an explicit nil check: {len(optional)}")
    for label, names in (("AI source candidates", ai_missing), ("Other map candidates", required - ai_missing),
                         ("Optional nil-guarded natives", optional)):
        if not names:
            continue
        print(f"\n{label}:")
        for name in sorted(names):
            locations = sorted(calls[name])
            if args.format == "full":
                for line, path in locations:
                    print(f"  {name}: {path}:{line}")
            else:
                shown = ", ".join(f"{Path(path).name}:{line}" for line, path in locations[:5])
                extra = f" (+{len(locations) - 5} sites)" if len(locations) > 5 else ""
                print(f"  {name}: {shown}{extra}")
    return 1 if ai_missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
