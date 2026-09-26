#include "g_local.h"

#include "lua.h"
#include "lauxlib.h"

static int LuaSetMapName(lua_State *L) {
    strlcpy(level.setup.name, luaL_checkstring(L, 1), sizeof(level.setup.name));
    return 0;
}

static int LuaSetMapDescription(lua_State *L) {
    strlcpy(level.setup.description, luaL_checkstring(L, 1), sizeof(level.setup.description));
    return 0;
}

static int LuaSetPlayers(lua_State *L) {
    int value = (int)luaL_checkinteger(L, 1);
    level.setup.players = (uint32_t)MIN(MAX(value, 0), WC3_MAX_MAP_PLAYERS);
    return 0;
}

static int LuaSetTeams(lua_State *L) {
    int value = (int)luaL_checkinteger(L, 1);
    level.setup.teams = (uint32_t)MIN(MAX(value, 0), WC3_MAX_MAP_PLAYERS);
    return 0;
}

static int LuaSetGamePlacement(lua_State *L) {
    level.setup.placement = (uint32_t)luaL_checkinteger(L, 1);
    return 0;
}

static int LuaDefineStartLocation(lua_State *L) {
    int player = (int)luaL_checkinteger(L, 1);

    if (player >= 0 && player < WC3_MAX_MAP_PLAYERS) {
        level.setup.start_locations[player].x = (float)luaL_checknumber(L, 2);
        level.setup.start_locations[player].y = (float)luaL_checknumber(L, 3);
    }
    return 0;
}

static int LuaInitHashtable(lua_State *L) {
    hashtable_t *table = G_AllocHashtable();
    if (!table) return luaL_error(L, "InitHashtable: table registry is full");
    lua_pushlightuserdata(L, table);
    return 1;
}

static int LuaCreateTimer(lua_State *L) {
    gtimer_t *timer = G_AllocJassTimer();
    if (!timer) return luaL_error(L, "CreateTimer: timer registry is full");
    lua_pushlightuserdata(L, timer);
    return 1;
}

void G_RegisterLuaMapConfigNatives(wc3Lua_t *L) {
    WC3_LuaRegisterNative(L, "SetMapName", LuaSetMapName);
    WC3_LuaRegisterNative(L, "SetMapDescription", LuaSetMapDescription);
    WC3_LuaRegisterNative(L, "SetPlayers", LuaSetPlayers);
    WC3_LuaRegisterNative(L, "SetTeams", LuaSetTeams);
    WC3_LuaRegisterNative(L, "SetGamePlacement", LuaSetGamePlacement);
    WC3_LuaRegisterNative(L, "DefineStartLocation", LuaDefineStartLocation);
    WC3_LuaRegisterNative(L, "InitHashtable", LuaInitHashtable);
    WC3_LuaRegisterNative(L, "CreateTimer", LuaCreateTimer);
    WC3_LuaRegisterInteger(L, "MAP_PLACEMENT_TEAMS_TOGETHER", 3);
}

bool G_LoadLuaMapScript(wc3Lua_t *L, cstring_t source, cstring_t chunk_name) {
    if (!L) return false;
    G_RegisterLuaMapConfigNatives(L);
    return WC3_LuaLoadBuffer(L, source, chunk_name);
}
