#include "wc3_lua.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define WC3_LUA_ERROR_MAX 512

struct wc3Lua_s {
    lua_State *L;
    char error[WC3_LUA_ERROR_MAX];
    bool error_pending;
    void *filter_unit;
    wc3LuaTriggerContext_t trigger_context;
};

static int WC3_LuaFourCC(lua_State *L) {
    size_t len = 0;
    const unsigned char *text = (const unsigned char *)luaL_checklstring(L, 1, &len);
    uint32_t code = 0;
    size_t i;

    if (len != 4) return luaL_error(L, "FourCC expects a four-byte string");
    for (i = 0; i < 4; ++i) code |= (uint32_t)text[i] << (i * 8);
    lua_pushinteger(L, (lua_Integer)code);
    return 1;
}

static void WC3_LuaSetError(wc3Lua_t *state, const char *phase, const char *message) {
    snprintf(state->error, sizeof(state->error), "%s: %s", phase ? phase : "error",
             message ? message : "(no message)");
    state->error_pending = true;
}

/* Capture the value on top of the Lua stack as the pending diagnostic. */
static void WC3_LuaCaptureError(wc3Lua_t *state, const char *phase) {
    const char *message = lua_tostring(state->L, -1);
    WC3_LuaSetError(state, phase, message);
    lua_pop(state->L, 1);
}

static int WC3_LuaTraceback(lua_State *L) {
    const char *message = lua_tostring(L, 1);
    if (!message) message = "(non-string Lua error)";
    luaL_traceback(L, L, message, 1);
    return 1;
}

static int WC3_LuaPCall(lua_State *L, int nargs, int nresults) {
    int const handler = lua_gettop(L) - nargs;
    lua_pushcfunction(L, WC3_LuaTraceback);
    lua_insert(L, handler);
    return lua_pcall(L, nargs, nresults, handler);
}

wc3Lua_t *WC3_LuaNewState(void) {
    wc3Lua_t *state = (wc3Lua_t *)calloc(1, sizeof(*state));

    if (!state) return NULL;
    state->L = luaL_newstate();
    if (!state->L) {
        free(state);
        return NULL;
    }
    luaL_openlibs(state->L);
    WC3_LuaRegisterNative(state, "FourCC", WC3_LuaFourCC);
    return state;
}

void WC3_LuaClose(wc3Lua_t *state) {
    if (!state) return;
    if (state->L) lua_close(state->L);
    free(state);
}

bool WC3_LuaLoadBuffer(wc3Lua_t *state, const char *source, const char *chunk_name) {
    int status;

    if (!state || !state->L) return false;
    if (!source) {
        WC3_LuaSetError(state, "load", "null map script");
        return false;
    }
    status = luaL_loadbufferx(state->L, source, strlen(source),
                              chunk_name ? chunk_name : "=(map)", "t");
    if (status != LUA_OK) {
        WC3_LuaCaptureError(state, "load");
        return false;
    }
    status = WC3_LuaPCall(state->L, 0, 0);
    if (status != LUA_OK) {
        WC3_LuaCaptureError(state, "exec");
        lua_remove(state->L, -1);
        return false;
    }
    lua_remove(state->L, -1);
    WC3_LuaClearError(state);
    return true;
}

bool WC3_LuaCall(wc3Lua_t *state, const char *function_name) {
    if (!state || !state->L || !function_name) return false;
    lua_getglobal(state->L, function_name);
    if (!lua_isfunction(state->L, -1)) {
        lua_pop(state->L, 1);
        WC3_LuaSetError(state, function_name, "not a function");
        return false;
    }
    if (WC3_LuaPCall(state->L, 0, 0) != LUA_OK) {
        WC3_LuaCaptureError(state, function_name);
        lua_remove(state->L, -1);
        return false;
    }
    lua_remove(state->L, -1);
    WC3_LuaClearError(state);
    return true;
}

bool WC3_LuaCallNumber(wc3Lua_t *state, const char *function_name, double *out) {
    double value;

    if (!state || !state->L || !function_name) return false;
    lua_getglobal(state->L, function_name);
    if (!lua_isfunction(state->L, -1)) {
        lua_pop(state->L, 1);
        WC3_LuaSetError(state, function_name, "not a function");
        return false;
    }
    if (WC3_LuaPCall(state->L, 0, 1) != LUA_OK) {
        WC3_LuaCaptureError(state, function_name);
        lua_remove(state->L, -1);
        return false;
    }
    value = lua_tonumber(state->L, -1);
    lua_pop(state->L, 1);
    lua_remove(state->L, -1);
    if (out) *out = value;
    WC3_LuaClearError(state);
    return true;
}

bool WC3_LuaErrorPending(wc3Lua_t const *state) {
    return state && state->error_pending;
}

const char *WC3_LuaErrorMessage(wc3Lua_t const *state) {
    return state ? state->error : "";
}

void WC3_LuaClearError(wc3Lua_t *state) {
    if (!state) return;
    state->error[0] = '\0';
    state->error_pending = false;
}

void WC3_LuaRegisterNative(wc3Lua_t *state, const char *name, wc3LuaCFunction fn) {
    if (!state || !state->L || !name) return;
    lua_pushcfunction(state->L, fn);
    lua_setglobal(state->L, name);
}

void WC3_LuaRegisterInteger(wc3Lua_t *state, const char *name, int64_t value) {
    if (!state || !state->L || !name) return;
    lua_pushinteger(state->L, (lua_Integer)value);
    lua_setglobal(state->L, name);
}

bool WC3_LuaEvaluateFilter(wc3Lua_t *state, int function_index, void *unit, bool *accepted) {
    int const top = state && state->L ? lua_gettop(state->L) : 0;
    void *previous;
    int status;

    if (!state || !state->L || !lua_isfunction(state->L, function_index)) return false;
    previous = state->filter_unit;
    state->filter_unit = unit;
    lua_pushvalue(state->L, function_index);
    status = WC3_LuaPCall(state->L, 0, 1);
    state->filter_unit = previous;
    if (status != LUA_OK) {
        WC3_LuaCaptureError(state, "Filter");
        lua_settop(state->L, top);
        return false;
    }
    if (accepted) *accepted = lua_toboolean(state->L, -1);
    lua_settop(state->L, top);
    return true;
}

bool WC3_LuaEvaluateFilterRef(wc3Lua_t *state, int reference, void *unit, bool *accepted) {
    int const top = state && state->L ? lua_gettop(state->L) : 0;
    void *previous;
    int status;

    if (!state || !state->L || reference == LUA_NOREF || reference == LUA_REFNIL) return false;
    previous = state->filter_unit;
    state->filter_unit = unit;
    lua_rawgeti(state->L, LUA_REGISTRYINDEX, reference);
    status = WC3_LuaPCall(state->L, 0, 1);
    state->filter_unit = previous;
    if (status != LUA_OK) {
        WC3_LuaCaptureError(state, "event filter");
        lua_settop(state->L, top);
        return false;
    }
    if (accepted) *accepted = lua_toboolean(state->L, -1);
    lua_settop(state->L, top);
    return true;
}

int WC3_LuaRefFunction(wc3Lua_t *state, int function_index) {
    if (!state || !state->L || !lua_isfunction(state->L, function_index)) return LUA_NOREF;
    lua_pushvalue(state->L, function_index);
    return luaL_ref(state->L, LUA_REGISTRYINDEX);
}

void WC3_LuaUnrefFunction(wc3Lua_t *state, int reference) {
    if (state && state->L && reference != LUA_NOREF && reference != LUA_REFNIL)
        luaL_unref(state->L, LUA_REGISTRYINDEX, reference);
}

bool WC3_LuaCallRef(wc3Lua_t *state, int reference) {
    int const top = state && state->L ? lua_gettop(state->L) : 0;
    int status;

    if (!state || !state->L || reference == LUA_NOREF || reference == LUA_REFNIL) return false;
    lua_rawgeti(state->L, LUA_REGISTRYINDEX, reference);
    status = WC3_LuaPCall(state->L, 0, 0);
    if (status != LUA_OK) {
        WC3_LuaCaptureError(state, "trigger callback");
        lua_settop(state->L, top);
        return false;
    }
    lua_settop(state->L, top);
    return true;
}

bool WC3_LuaCallRefBoolean(wc3Lua_t *state, int reference, bool *result) {
    int const top = state && state->L ? lua_gettop(state->L) : 0;
    int status;

    if (!state || !state->L || reference == LUA_NOREF || reference == LUA_REFNIL) return false;
    lua_rawgeti(state->L, LUA_REGISTRYINDEX, reference);
    status = WC3_LuaPCall(state->L, 0, 1);
    if (status != LUA_OK) {
        WC3_LuaCaptureError(state, "trigger condition");
        lua_settop(state->L, top);
        return false;
    }
    if (result) *result = lua_toboolean(state->L, -1);
    lua_settop(state->L, top);
    return true;
}

wc3LuaTriggerContext_t WC3_LuaGetTriggerContext(wc3Lua_t const *state) {
    return state ? state->trigger_context : (wc3LuaTriggerContext_t){ 0 };
}

void WC3_LuaSetTriggerContext(wc3Lua_t *state, wc3LuaTriggerContext_t const *context) {
    if (!state) return;
    state->trigger_context = context ? *context : (wc3LuaTriggerContext_t){ 0 };
}

void *WC3_LuaFilterUnit(wc3Lua_t const *state) {
    return state ? state->filter_unit : NULL;
}
