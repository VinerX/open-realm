/*
 * Deterministic scenario driver for headless Warcraft III map testing.
 *
 * Armed by the wc3_scenario cvar pointing at a Lua scenario file (read through
 * the mounted VFS, so it can sit next to a loose map).  The scenario runs in
 * the map's own Lua VM after config()/main(), so it observes and drives the
 * same world the map script built.  It advances one call per server frame and
 * reports a single machine-readable PASS/FAIL line:
 *
 *   WC3_SCENARIO name=<name> status=PASS|FAIL steps=<n> detail="<text>"
 *
 * The scenario file only needs one hook:
 *
 *   function scenario_step(frame)
 *     if frame < 30 then return nil end   -- keep advancing
 *     if bad then return "FAIL: reason" end
 *     return "PASS"
 *   end
 *
 * Returning nil keeps the run going, "PASS" finishes it successfully, and any
 * other string fails with that text as the detail.
 */
#include "g_local.h"

#include "lua.h"
#include "lauxlib.h"

#define SCENARIO_NAME_MAX 64
#define SCENARIO_DETAIL_MAX 256
#define SCENARIO_DEFAULT_TIMEOUT 600

static struct {
    bool armed, started, finished, passed;
    bool missing_vm;
    uint32_t frames, timeout;
    char name[SCENARIO_NAME_MAX];
    char detail[SCENARIO_DETAIL_MAX];
} scenario;

scenarioStatus_t G_ScenarioStatus(void) {
    if (!scenario.armed) return SCENARIO_IDLE;
    if (!scenario.finished) return SCENARIO_RUNNING;
    return scenario.passed ? SCENARIO_PASSED : SCENARIO_FAILED;
}

cstring_t G_ScenarioDetail(void) {
    return scenario.detail;
}

void G_ScenarioReset(void) {
    memset(&scenario, 0, sizeof(scenario));
}

void G_LogScriptPhase(cstring_t phase, cstring_t kind, cstring_t status, cstring_t detail) {
    if (kind)
        fprintf(stderr, "WC3_SCRIPT phase=%s kind=%s", phase, kind);
    else
        fprintf(stderr, "WC3_SCRIPT phase=%s", phase);
    if (status)
        fprintf(stderr, " status=%s", status);
    if (detail && *detail)
        fprintf(stderr, " detail=\"%s\"", detail);
    fprintf(stderr, "\n");
}

static void scenario_finish(cstring_t status, cstring_t detail) {
    /* Exactly one terminal marker per run: the frame hook returns early once
     * finished, and this guard keeps that true even if a future caller adds a
     * second finish path.  The audit requires one marker for a requested
     * scenario. */
    if (scenario.finished) return;
    fprintf(stderr, "WC3_SCENARIO name=\"%s\" status=%s steps=%u detail=\"%s\"\n",
            scenario.name, status, scenario.frames, detail ? detail : "");
    scenario.finished = true;
    scenario.passed = !strcmp(status, "PASS");
    snprintf(scenario.detail, sizeof(scenario.detail), "%s", detail ? detail : "");
    /* The driver only reports; the audit classifies a FAIL marker as a script
     * error.  It never terminates the host process, so a scenario failure
     * still yields a complete log alongside the frame-limit line. */
}

static int scenario_chat(lua_State *L) {
    uint32_t player = (uint32_t)luaL_checkinteger(L, 1);
    edict_t *ent = G_GetPlayerEntityByNumber(player);
    cstring_t args[] = { "say", luaL_checkstring(L, 2) };
    if (!ent || !ent->client) return luaL_error(L, "scenario_chat: invalid player");
    G_ClientCommand(ent, 2, args);
    return 0;
}

static int scenario_mouse(lua_State *L) {
    edict_t *ent = G_GetPlayerEntityByNumber((uint32_t)luaL_checkinteger(L, 1));
    char button[16], x[32], y[32];
    if (!ent || !ent->client) return luaL_error(L, "scenario_mouse: invalid player");
    snprintf(button, sizeof(button), "%d", (int)luaL_checkinteger(L, 2));
    snprintf(x, sizeof(x), "%.9g", luaL_checknumber(L, 3));
    snprintf(y, sizeof(y), "%.9g", luaL_checknumber(L, 4));
    cstring_t args[] = { "mouseevent", "0", button, x, y };
    G_ClientCommand(ent, 5, args);
    return 0;
}

static int scenario_unit_commands(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    gameCommandButton_t buttons[12];
    uint8_t count;
    if (!unit || !unit->inuse) return luaL_error(L, "scenario_unit_commands: invalid unit");
    count = G_GetCommandButtons(unit, buttons, sizeof(buttons) / sizeof(buttons[0]));
    lua_newtable(L);
    lua_pushstring(L, GetClassName(unit->class_id)); lua_setfield(L, -2, "type");
    if (unit->data.UnitAbilities) {
        lua_pushstring(L, unit->data.UnitAbilities->abilList); lua_setfield(L, -2, "abilities");
        lua_pushstring(L, unit->data.UnitAbilities->heroAbilList); lua_setfield(L, -2, "hero_abilities");
    }
    if (unit->data.UnitProfile) {
        lua_pushstring(L, unit->data.UnitProfile->sellUnits); lua_setfield(L, -2, "sell_units");
        lua_pushstring(L, unit->data.UnitProfile->trains); lua_setfield(L, -2, "trains");
    }
    FOR_LOOP(i, count) {
        lua_pushstring(L, buttons[i].command);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static void scenario_load(void) {
    cstring_t path = gi.CvarString("wc3_scenario", "");
    cstring_t timeout = gi.CvarString("wc3_scenario_timeout", "");
    handle_t buffer = NULL;
    string_t source;
    uint32_t size = 0;

    scenario.armed = path && *path;
    if (!scenario.armed) return;
    if (!level.lua_vm) {
        scenario.missing_vm = true;
        return;
    }
    WC3_LuaRegisterNative(level.lua_vm, "scenario_chat", scenario_chat);
    WC3_LuaRegisterNative(level.lua_vm, "scenario_mouse", scenario_mouse);
    WC3_LuaRegisterNative(level.lua_vm, "scenario_unit_commands", scenario_unit_commands);
    snprintf(scenario.name, sizeof(scenario.name), "%s",
             gi.CvarString("wc3_scenario_name", path));
    scenario.timeout = timeout && *timeout ? (uint32_t)atoi(timeout) : SCENARIO_DEFAULT_TIMEOUT;
    if (!scenario.timeout) scenario.timeout = SCENARIO_DEFAULT_TIMEOUT;

    buffer = gi.ReadFile(path, &size);
    if (!buffer || !size) {
        /* The scenario may also be an absolute host path outside the mounted
         * VFS directories; fall back to a direct read before failing. */
        FILE *file = fopen(path, "rb");
        if (file) {
            long length;
            if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) >= 0 &&
                fseek(file, 0, SEEK_SET) == 0) {
                buffer = gi.MemAlloc((uint32_t)length + 1);
                if (buffer && fread(buffer, 1, (size_t)length, file) == (size_t)length) {
                    ((char *)buffer)[length] = '\0';
                    size = (uint32_t)length;
                } else {
                    if (buffer) gi.MemFree(buffer);
                    buffer = NULL;
                }
            }
            fclose(file);
        }
    }
    if (!buffer || !size) {
        scenario_finish("FAIL", "scenario file not readable");
        return;
    }
    /* Normalize to a NUL-terminated copy: a VFS read can fill the size without
     * appending a terminator, and the Lua loader takes a C string. */
    source = gi.MemAlloc(size + 1);
    if (!source) {
        gi.MemFree(buffer);
        scenario_finish("FAIL", "scenario allocation failed");
        return;
    }
    memcpy(source, buffer, size);
    source[size] = '\0';
    gi.MemFree(buffer);
    if (!G_LoadLuaMapScript(level.lua_vm, source, path))
        scenario_finish("FAIL", WC3_LuaErrorMessage(level.lua_vm));
    gi.MemFree(source);
    scenario.started = true;
}

void G_ScenarioFrame(void) {
    char result[SCENARIO_DETAIL_MAX] = "";

    if (!level.started || !level.map_path[0]) return;
    if (!scenario.armed) scenario_load();
    if (!scenario.armed || scenario.finished) return;
    if (scenario.missing_vm) {
        scenario_finish("FAIL", "map has no Lua VM");
        return;
    }
    if (!scenario.started) {
        if (scenario.timeout && scenario.frames++ >= scenario.timeout)
            scenario_finish("FAIL", "timeout");
        return;
    }
    if (!WC3_LuaCallStringWithNumber(level.lua_vm, "scenario_step", (double)scenario.frames,
                                     result, sizeof(result))) {
        scenario_finish("FAIL", WC3_LuaErrorMessage(level.lua_vm));
        return;
    }
    scenario.frames++;
    if (!strcmp(result, "PASS"))
        scenario_finish("PASS", NULL);
    else if (*result)
        scenario_finish("FAIL", result);
    else if (scenario.frames >= scenario.timeout)
        scenario_finish("FAIL", "timeout");
}
