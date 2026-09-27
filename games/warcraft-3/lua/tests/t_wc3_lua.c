/*
 * t_wc3_lua.c - Standalone checks for the map-scoped WC3 Lua runtime.
 *
 * Built without the engine or the shared test registry so the core Lua
 * lifecycle can be exercised on any host:
 *   gcc -Ivendor/lua53/src -Igames/warcraft-3/lua \
 *       games/warcraft-3/lua/tests/t_wc3_lua.c games/warcraft-3/lua/wc3_lua.c \
 *       $(ls vendor/lua53/src/*.c | grep -v -e lua.c -e luac.c) -lm -o t_wc3_lua
 */
#include "wc3_lua.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

static int counter;

static void test_arithmetic_and_function(void) {
    wc3Lua_t *L = WC3_LuaNewState();
    double out = 0;

    assert(L);
    assert(WC3_LuaLoadBuffer(L, "x = 2 + 2\nfunction Foo() return 123 end\n", "=(arith)"));
    assert(!WC3_LuaErrorPending(L));
    assert(WC3_LuaCallNumber(L, "Foo", &out));
    assert(out == 123.0);
    WC3_LuaClose(L);
}

static void test_closure_retains_state(void) {
    wc3Lua_t *L = WC3_LuaNewState();
    double out = 0;

    assert(L);
    assert(WC3_LuaLoadBuffer(L,
        "function MakeCounter()\n"
        "  local n = 0\n"
        "  return function() n = n + 1 return n end\n"
        "end\n"
        "counter = MakeCounter()\n",
        "=(closure)"));
    assert(WC3_LuaLoadBuffer(L, "", "=(noop)"));
    assert(WC3_LuaCallNumber(L, "counter", &out));
    assert(out == 1.0);
    assert(WC3_LuaCallNumber(L, "counter", &out));
    assert(out == 2.0);
    assert(WC3_LuaCallNumber(L, "counter", &out));
    assert(out == 3.0);
    WC3_LuaClose(L);
}

static void test_syntax_error_is_reported(void) {
    wc3Lua_t *L = WC3_LuaNewState();

    assert(L);
    assert(!WC3_LuaLoadBuffer(L, "function broken(\n", "=(syntax)"));
    assert(WC3_LuaErrorPending(L));
    printf("syntax error: %s\n", WC3_LuaErrorMessage(L));
    assert(strlen(WC3_LuaErrorMessage(L)) > 0);
    WC3_LuaClearError(L);
    assert(!WC3_LuaErrorPending(L));
    WC3_LuaClose(L);
}

static void test_runtime_error_preserves_state(void) {
    wc3Lua_t *L = WC3_LuaNewState();

    assert(L);
    assert(WC3_LuaLoadBuffer(L, "function boom() error('kaboom') end\n", "=(runtime)"));
    assert(!WC3_LuaCall(L, "boom"));
    assert(WC3_LuaErrorPending(L));
    printf("runtime error: %s\n", WC3_LuaErrorMessage(L));
    assert(strstr(WC3_LuaErrorMessage(L), "kaboom") != NULL);
    assert(strstr(WC3_LuaErrorMessage(L), "stack traceback") != NULL);
    assert(strstr(WC3_LuaErrorMessage(L), "boom") != NULL);
    assert(!WC3_LuaCall(L, "does_not_exist"));
    assert(strstr(WC3_LuaErrorMessage(L), "not a function") != NULL);
    WC3_LuaClose(L);
}

static void test_state_isolation_across_reload(void) {
    wc3Lua_t *first = WC3_LuaNewState();
    wc3Lua_t *second;
    double out = 0;

    assert(WC3_LuaLoadBuffer(first, "function leaked() return 42 end\n", "=(first)"));
    assert(WC3_LuaCallNumber(first, "leaked", &out));
    assert(out == 42.0);
    WC3_LuaClose(first);

    second = WC3_LuaNewState();
    assert(second);
    assert(!WC3_LuaCallNumber(second, "leaked", &out));
    assert(WC3_LuaErrorPending(second));
    WC3_LuaClose(second);
}

static int Host_add(lua_State *L) {
    int a = luaL_checkinteger(L, 1);
    int b = luaL_checkinteger(L, 2);
    lua_pushinteger(L, a + b);
    return 1;
}

static void test_native_registration(void) {
    wc3Lua_t *L = WC3_LuaNewState();
    double out = 0;

    WC3_LuaRegisterNative(L, "Host_add", Host_add);
    assert(WC3_LuaLoadBuffer(L, "function use() return Host_add(2, 3) end\n", "=(native)"));
    assert(WC3_LuaCallNumber(L, "use", &out));
    assert(out == 5.0);
    WC3_LuaClose(L);
}

static void test_string_result_and_missing_function(void) {
    wc3Lua_t *L = WC3_LuaNewState();
    char buffer[32] = "unchanged";

    assert(L);
    assert(WC3_LuaLoadBuffer(L,
        "function label() return 'legion' end\n"
        "function nothing() return nil end\n",
        "=(string)"));
    assert(WC3_LuaCallStringWithNumber(L, "label", 0.0, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "legion") == 0);
    assert(WC3_LuaCallStringWithNumber(L, "nothing", 0.0, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "") == 0);
    assert(!WC3_LuaCallStringWithNumber(L, "missing", 0.0, buffer, sizeof(buffer)));
    assert(WC3_LuaErrorPending(L));
    WC3_LuaClose(L);
}

static void test_string_result_with_number_argument(void) {
    wc3Lua_t *L = WC3_LuaNewState();
    char buffer[32] = "";

    assert(L);
    assert(WC3_LuaLoadBuffer(L,
        "function step(frame)\n"
        "  if frame >= 3 then return 'PASS' end\n"
        "  return nil\n"
        "end\n",
        "=(step)"));
    assert(WC3_LuaCallStringWithNumber(L, "step", 1.0, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "") == 0);
    assert(WC3_LuaCallStringWithNumber(L, "step", 3.0, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "PASS") == 0);
    WC3_LuaClose(L);
}

int main(void) {
    test_arithmetic_and_function();
    test_closure_retains_state();
    test_syntax_error_is_reported();
    test_runtime_error_preserves_state();
    test_state_isolation_across_reload();
    test_native_registration();
    test_string_result_and_missing_function();
    test_string_result_with_number_argument();
    printf("t_wc3_lua: all checks passed\n");
    return 0;
}
