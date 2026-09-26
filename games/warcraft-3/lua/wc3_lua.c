#include "wc3_lua.h"

#include <stdio.h>
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
};

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

wc3Lua_t *WC3_LuaNewState(void) {
    wc3Lua_t *state = (wc3Lua_t *)calloc(1, sizeof(*state));

    if (!state) return NULL;
    state->L = luaL_newstate();
    if (!state->L) {
        free(state);
        return NULL;
    }
    luaL_openlibs(state->L);
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
    status = lua_pcall(state->L, 0, 0, 0);
    if (status != LUA_OK) {
        WC3_LuaCaptureError(state, "exec");
        return false;
    }
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
    if (lua_pcall(state->L, 0, 0, 0) != LUA_OK) {
        WC3_LuaCaptureError(state, function_name);
        return false;
    }
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
    if (lua_pcall(state->L, 0, 1, 0) != LUA_OK) {
        WC3_LuaCaptureError(state, function_name);
        return false;
    }
    value = lua_tonumber(state->L, -1);
    lua_pop(state->L, 1);
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
