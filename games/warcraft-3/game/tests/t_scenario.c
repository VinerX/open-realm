#ifdef BZ_TESTS
/*
 * t_scenario.c - deterministic scenario driver coverage.
 *
 * The driver turns a Lua scenario_step(frame) hook into one machine-readable
 * PASS/FAIL line.  These cases pin its contract: nil keeps advancing, PASS
 * completes, any other string fails with its text as the detail, a missing
 * hook fails explicitly, and a map without a Lua VM fails rather than
 * pretending to have run.
 */
#include "test.h"
#include "../g_local.h"

#include <stdio.h>
#include <string.h>

static char scenario_source[4096];
static char scenario_result[128];

static cstring_t scenario_cvar(cstring_t name, cstring_t fallback) {
    if (!strcmp(name, "wc3_scenario")) return scenario_result;
    if (!strcmp(name, "wc3_scenario_name")) return "unit-scenario";
    return fallback;
}

static handle_t scenario_readfile(cstring_t filename, uint32_t *size) {
    uint32_t length = (uint32_t)strlen(scenario_source);
    char *buffer;

    (void)filename;
    if (!length) return NULL;
    buffer = gi.MemAlloc(length + 1);
    if (!buffer) return NULL;
    memcpy(buffer, scenario_source, length + 1);
    if (size) *size = length;
    return buffer;
}

static void scenario_begin(cstring_t source) {
    strlcpy(scenario_source, source, sizeof(scenario_source));
    strlcpy(scenario_result, "scenarios/unit.lua", sizeof(scenario_result));
    G_ScenarioReset();
}

TEST(wc3_scenario, nil_advances_then_pass_completes) {
    wc3Lua_t *lua;
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    handle_t (*old_read)(cstring_t, uint32_t *) = gi.ReadFile;
    wc3Lua_t *previous_lua = level.lua_vm;
    bool previous_started = level.started;
    PATHSTR previous_path;

    strlcpy(previous_path, level.map_path, sizeof(previous_path));
    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) {
        strlcpy(level.map_path, previous_path, sizeof(level.map_path));
        return;
    }
    level.lua_vm = lua;
    level.started = true;
    strlcpy(level.map_path, "scenario-unit.w3x", sizeof(level.map_path));
    gi.CvarString = scenario_cvar;
    gi.ReadFile = scenario_readfile;
    scenario_begin(
        "function scenario_step(frame)\n"
        "  if frame < 3 then return nil end\n"
        "  return 'PASS'\n"
        "end\n");

    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_RUNNING);
    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_RUNNING);
    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_RUNNING);
    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_PASSED);
    T_EQ(strcmp(G_ScenarioDetail(), ""), 0);

    gi.CvarString = old_cvar;
    gi.ReadFile = old_read;
    level.lua_vm = previous_lua;
    level.started = previous_started;
    strlcpy(level.map_path, previous_path, sizeof(level.map_path));
    WC3_LuaClose(lua);
}

TEST(wc3_scenario, failure_text_becomes_the_detail) {
    wc3Lua_t *lua;
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    handle_t (*old_read)(cstring_t, uint32_t *) = gi.ReadFile;
    wc3Lua_t *previous_lua = level.lua_vm;
    bool previous_started = level.started;
    PATHSTR previous_path;

    strlcpy(previous_path, level.map_path, sizeof(previous_path));
    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    level.started = true;
    strlcpy(level.map_path, "scenario-unit.w3x", sizeof(level.map_path));
    gi.CvarString = scenario_cvar;
    gi.ReadFile = scenario_readfile;
    scenario_begin(
        "function scenario_step(frame)\n"
        "  return 'FAIL: hero never appeared'\n"
        "end\n");

    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_FAILED);
    T_ASSERT(strstr(G_ScenarioDetail(), "hero never appeared") != NULL);

    gi.CvarString = old_cvar;
    gi.ReadFile = old_read;
    level.lua_vm = previous_lua;
    level.started = previous_started;
    strlcpy(level.map_path, previous_path, sizeof(level.map_path));
    WC3_LuaClose(lua);
}

TEST(wc3_scenario, missing_hook_fails_explicitly) {
    wc3Lua_t *lua;
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    handle_t (*old_read)(cstring_t, uint32_t *) = gi.ReadFile;
    wc3Lua_t *previous_lua = level.lua_vm;
    bool previous_started = level.started;
    PATHSTR previous_path;

    strlcpy(previous_path, level.map_path, sizeof(previous_path));
    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    level.started = true;
    strlcpy(level.map_path, "scenario-unit.w3x", sizeof(level.map_path));
    gi.CvarString = scenario_cvar;
    gi.ReadFile = scenario_readfile;
    scenario_begin("value = 1\n");

    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_FAILED);
    T_ASSERT(strstr(G_ScenarioDetail(), "not a function") != NULL);

    gi.CvarString = old_cvar;
    gi.ReadFile = old_read;
    level.lua_vm = previous_lua;
    level.started = previous_started;
    strlcpy(level.map_path, previous_path, sizeof(level.map_path));
    WC3_LuaClose(lua);
}

TEST(wc3_scenario, no_lua_vm_fails_without_crashing) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    handle_t (*old_read)(cstring_t, uint32_t *) = gi.ReadFile;
    wc3Lua_t *previous_lua = level.lua_vm;
    bool previous_started = level.started;
    PATHSTR previous_path;

    strlcpy(previous_path, level.map_path, sizeof(previous_path));
    reset_entities();
    setup_test_world();
    level.lua_vm = NULL;
    level.started = true;
    strlcpy(level.map_path, "scenario-unit.w3x", sizeof(level.map_path));
    gi.CvarString = scenario_cvar;
    gi.ReadFile = scenario_readfile;
    scenario_begin("function scenario_step(frame) return 'PASS' end\n");

    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_FAILED);
    T_ASSERT(strstr(G_ScenarioDetail(), "no Lua VM") != NULL);

    gi.CvarString = old_cvar;
    gi.ReadFile = old_read;
    level.lua_vm = previous_lua;
    level.started = previous_started;
    strlcpy(level.map_path, previous_path, sizeof(level.map_path));
}

TEST(wc3_scenario, reset_restarts_a_finished_run) {
    wc3Lua_t *lua;
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    handle_t (*old_read)(cstring_t, uint32_t *) = gi.ReadFile;
    wc3Lua_t *previous_lua = level.lua_vm;
    bool previous_started = level.started;
    PATHSTR previous_path;

    strlcpy(previous_path, level.map_path, sizeof(previous_path));
    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    level.started = true;
    strlcpy(level.map_path, "scenario-unit.w3x", sizeof(level.map_path));
    gi.CvarString = scenario_cvar;
    gi.ReadFile = scenario_readfile;
    scenario_begin("function scenario_step(frame) return 'PASS' end\n");

    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_PASSED);
    /* A map lifecycle boundary resets the driver; the same map must be able
     * to run the scenario again rather than staying terminal. */
    G_ScenarioReset();
    T_EQ(G_ScenarioStatus(), SCENARIO_IDLE);
    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_PASSED);

    gi.CvarString = old_cvar;
    gi.ReadFile = old_read;
    level.lua_vm = previous_lua;
    level.started = previous_started;
    strlcpy(level.map_path, previous_path, sizeof(level.map_path));
    WC3_LuaClose(lua);
}

TEST(wc3_scenario, finished_run_does_not_repeat_the_marker) {
    wc3Lua_t *lua;
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    handle_t (*old_read)(cstring_t, uint32_t *) = gi.ReadFile;
    wc3Lua_t *previous_lua = level.lua_vm;
    bool previous_started = level.started;
    PATHSTR previous_path;

    strlcpy(previous_path, level.map_path, sizeof(previous_path));
    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    level.started = true;
    strlcpy(level.map_path, "scenario-unit.w3x", sizeof(level.map_path));
    gi.CvarString = scenario_cvar;
    gi.ReadFile = scenario_readfile;
    scenario_begin("function scenario_step(frame) return 'PASS' end\n");

    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_PASSED);
    /* Repeated frames after the terminal step must not change the outcome;
     * the audit relies on exactly one terminal marker per requested run. */
    G_ScenarioFrame();
    G_ScenarioFrame();
    T_EQ(G_ScenarioStatus(), SCENARIO_PASSED);

    gi.CvarString = old_cvar;
    gi.ReadFile = old_read;
    level.lua_vm = previous_lua;
    level.started = previous_started;
    strlcpy(level.map_path, previous_path, sizeof(level.map_path));
    WC3_LuaClose(lua);
}

#endif /* BZ_TESTS */
