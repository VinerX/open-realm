#include "g_local.h"
#include "jass/jass.h"

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

static int LuaCreateGroup(lua_State *L) {
    ggroup_t *group = G_AllocJassGroup();
    if (!group) return luaL_error(L, "CreateGroup: group registry is full");
    lua_pushlightuserdata(L, group);
    return 1;
}

static int LuaDestroyGroup(lua_State *L) {
    G_FreeJassGroup(lua_touserdata(L, 1));
    return 0;
}

typedef struct {
    wc3Lua_t *lua;
    int filter_index;
} luaGroupFilter_t;

static bool LuaEvaluateCandidate(void *candidate, void *opaque) {
    luaGroupFilter_t *context = opaque;
    bool accepted = false;

    if (WC3_LuaErrorPending(context->lua)) return false;
    return WC3_LuaEvaluateFilter(context->lua, context->filter_index, candidate, &accepted) && accepted;
}

static bool LuaGroupFilter(edict_t *unit, void *opaque) {
    return LuaEvaluateCandidate(unit, opaque);
}

static bool LuaPlayerFilter(player_t *player, void *opaque) {
    return LuaEvaluateCandidate(player, opaque);
}

static int LuaGroupEnumUnitsOfPlayer(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    player_t *player = lua_touserdata(L, 2);
    luaGroupFilter_t context;
    if (!G_JassGroupValid(group) || !player)
        return luaL_error(L, "GroupEnumUnitsOfPlayer: invalid group or player");
    if (lua_isnoneornil(L, 3)) {
        G_EnumUnitsOfPlayer(group, player, NULL, NULL);
        return 0;
    }
    luaL_checktype(L, 3, LUA_TFUNCTION);
    context.lua = level.lua_vm;
    context.filter_index = lua_absindex(L, 3);
    G_EnumUnitsOfPlayer(group, player, LuaGroupFilter, &context);
    if (WC3_LuaErrorPending(context.lua)) {
        char error[512];
        strlcpy(error, WC3_LuaErrorMessage(context.lua), sizeof(error));
        WC3_LuaClearError(context.lua);
        return luaL_error(L, "GroupEnumUnitsOfPlayer: %s", error);
    }
    return 0;
}

static int LuaForceEnumPlayers(lua_State *L) {
    uint32_t *force = lua_touserdata(L, 1);
    luaGroupFilter_t context = { level.lua_vm, lua_absindex(L, 2) };

    if (!force) return 0;
    if (lua_isnoneornil(L, 2)) {
        G_ForceEnumPlayers(force, 0, NULL, NULL);
        return 0;
    }
    luaL_checktype(L, 2, LUA_TFUNCTION);
    G_ForceEnumPlayers(force, 0, LuaPlayerFilter, &context);
    if (WC3_LuaErrorPending(context.lua)) {
        char error[512];
        strlcpy(error, WC3_LuaErrorMessage(context.lua), sizeof(error));
        WC3_LuaClearError(context.lua);
        return luaL_error(L, "ForceEnumPlayers: %s", error);
    }
    return 0;
}

static int LuaBlzGroupGetSize(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    lua_pushinteger(L, G_JassGroupValid(group) ? (lua_Integer)group->num_units : 0);
    return 1;
}

static int LuaBlzGroupUnitAt(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    int32_t index = (int32_t)luaL_checkinteger(L, 2);
    if (G_JassGroupValid(group) && index >= 0 && (uint32_t)index < group->num_units)
        lua_pushlightuserdata(L, group->units[index]);
    else
        lua_pushnil(L);
    return 1;
}

static int LuaRect(lua_State *L) {
    box2_t *rect = lua_newuserdata(L, sizeof(*rect));
    rect->min.x = (float)luaL_checknumber(L, 1);
    rect->min.y = (float)luaL_checknumber(L, 2);
    rect->max.x = (float)luaL_checknumber(L, 3);
    rect->max.y = (float)luaL_checknumber(L, 4);
    return 1;
}

static int LuaLocation(lua_State *L) {
    vec2_t *location = lua_newuserdata(L, sizeof(*location));
    location->x = (float)luaL_checknumber(L, 1);
    location->y = (float)luaL_checknumber(L, 2);
    return 1;
}

static int LuaPlayer(lua_State *L) {
    int32_t number = (int32_t)luaL_checkinteger(L, 1);
    player_t *player = number >= 0 && number < WC3_MAX_PLAYER_SLOTS
        ? G_GetPlayerByNumber((uint32_t)number) : NULL;
    if (player) lua_pushlightuserdata(L, player);
    else lua_pushnil(L);
    return 1;
}

static int LuaSetPlayerStartLocation(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    if (player) PLAYER_CLIENT(player)->ps.start_location = (int32_t)luaL_checkinteger(L, 2);
    return 0;
}

static int LuaForcePlayerStartLocation(lua_State *L) {
    return LuaSetPlayerStartLocation(L);
}

static int LuaGetPlayerStartLocation(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    lua_pushinteger(L, player ? PLAYER_CLIENT(player)->ps.start_location : -1);
    return 1;
}

static int LuaGetCameraMargin(lua_State *L) {
    float margin;
    int32_t which = (int32_t)luaL_checkinteger(L, 1);
    if (G_GetCameraMargin(which, &margin)) lua_pushnumber(L, margin);
    else lua_pushnil(L);
    return 1;
}

static int LuaConvertPlayerColor(lua_State *L) {
    uint32_t *color = lua_newuserdata(L, sizeof(*color));
    *color = (uint32_t)luaL_checkinteger(L, 1);
    return 1;
}

static int LuaSetPlayerColor(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t *color = lua_touserdata(L, 2);
    if (player && color) {
        G_ChangePlayerTeamColor(player, player->color, *color);
        player->color = *color;
    }
    return 0;
}

static int LuaSetPlayerTeam(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    if (player) player->team = (int32_t)luaL_checkinteger(L, 2);
    return 0;
}

static int LuaSetStartLocPrioCount(lua_State *L) {
    G_SetStartLocPrioCount((int32_t)luaL_checkinteger(L, 1), (int32_t)luaL_checkinteger(L, 2));
    return 0;
}

static int LuaSetStartLocPrio(lua_State *L) {
    G_SetStartLocPrio((int32_t)luaL_checkinteger(L, 1), (int32_t)luaL_checkinteger(L, 2),
        (int32_t)luaL_checkinteger(L, 3), (uint32_t)luaL_checkinteger(L, 4));
    return 0;
}

static int LuaSetTerrainFogEx(lua_State *L) {
    G_SetTerrainFog((int32_t)luaL_checkinteger(L, 1),
        (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
        (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
        (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7));
    return 0;
}

static int LuaConvertFogStyle(lua_State *L) {
    lua_pushinteger(L, luaL_checkinteger(L, 1));
    return 1;
}

static int LuaSetWaterBaseColor(lua_State *L) {
    (void)luaL_checkinteger(L, 1);
    (void)luaL_checkinteger(L, 2);
    (void)luaL_checkinteger(L, 3);
    (void)luaL_checkinteger(L, 4);
    fprintf(stderr, "WC3 Lua: SetWaterBaseColor presentation is not implemented\n");
    return 0;
}

static int LuaNewSoundEnvironment(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    fprintf(stderr, "WC3 Lua: NewSoundEnvironment('%s') audio environment is not implemented\n", name);
    return 0;
}

static int LuaSetAmbientDaySound(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    fprintf(stderr, "WC3 Lua: SetAmbientDaySound('%s') audio ambience is not implemented\n", name);
    return 0;
}

static int LuaSetAmbientNightSound(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    fprintf(stderr, "WC3 Lua: SetAmbientNightSound('%s') audio ambience is not implemented\n", name);
    return 0;
}

static int LuaSetMapMusic(lua_State *L) {
    G_MusicSetMap(luaL_checkstring(L, 1), lua_toboolean(L, 2), (int32_t)luaL_checkinteger(L, 3));
    return 0;
}

static int LuaSetPlayerRacePreference(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t *preference = lua_touserdata(L, 2);
    uint32_t value = preference ? *preference : (uint32_t)luaL_checkinteger(L, 2);
    if (player) PLAYER_CLIENT(player)->jass.race_pref |= value;
    return 0;
}

static int LuaSetPlayerRaceSelectable(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    if (player) PLAYER_CLIENT(player)->jass.race_selectable = lua_toboolean(L, 2);
    return 0;
}

static int LuaSetPlayerController(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t *controller = lua_touserdata(L, 2);
    uint32_t value = controller ? *controller : (uint32_t)luaL_checkinteger(L, 2);
    if (player) PLAYER_CLIENT(player)->jass.controller = value;
    return 0;
}

static int LuaGetPlayerController(lua_State *L) {
    lua_pushinteger(L, G_GetPlayerController(lua_touserdata(L, 1)));
    return 1;
}

static int LuaGetPlayerSlotState(lua_State *L) {
    lua_pushinteger(L, G_GetPlayerSlotState(lua_touserdata(L, 1)));
    return 1;
}

static int LuaCreateSoundFromLabel(lua_State *L) {
    gsound_t *sound = lua_newuserdata(L, sizeof(*sound));
    char path[sizeof(sound->fileName)] = { 0 };
    float volume = 1.0f;
    int sound_index = 0;
    memset(sound, 0, sizeof(*sound));
    G_SoundLabelDescriptor(luaL_checkstring(L, 1), path, sizeof(path), &sound_index, &volume);
    strlcpy(sound->fileName, path, sizeof(sound->fileName));
    sound->looping = lua_toboolean(L, 2);
    sound->is3D = lua_toboolean(L, 3);
    sound->stopwhenoutofrange = lua_toboolean(L, 4);
    sound->fadeInRate = (int32_t)luaL_checkinteger(L, 5);
    sound->fadeOutRate = (int32_t)luaL_checkinteger(L, 6);
    sound->soundIndex = sound_index;
    G_JassSoundRuntimeInit(sound);
    G_JassSoundSetVolume(sound, volume);
    return 1;
}

static int LuaSetCameraBounds(lua_State *L) {
    float bounds[8];
    FOR_LOOP(i, 8) bounds[i] = (float)luaL_checknumber(L, i + 1);
    G_SetCameraBounds(bounds);
    return 0;
}

static int LuaSetDayNightModels(lua_State *L) {
    G_SetDayNightModels(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
    return 0;
}

static int LuaCreateForce(lua_State *L) {
    uint32_t *force = lua_newuserdata(L, sizeof(*force));
    *force = 0;
    return 1;
}

static int LuaDestroyForce(lua_State *L) {
    uint32_t *force = lua_touserdata(L, 1);
    if (force) *force = 0;
    return 0;
}

static int LuaForceAddPlayer(lua_State *L) {
    uint32_t *force = lua_touserdata(L, 1);
    player_t *player = lua_touserdata(L, 2);
    if (force && player) *force |= 1u << PLAYER_NUM(player);
    return 0;
}

static int LuaForceRemovePlayer(lua_State *L) {
    uint32_t *force = lua_touserdata(L, 1);
    player_t *player = lua_touserdata(L, 2);
    if (force && player) *force &= ~(1u << PLAYER_NUM(player));
    return 0;
}

static int LuaForceClear(lua_State *L) {
    uint32_t *force = lua_touserdata(L, 1);
    if (force) *force = 0;
    return 0;
}

static int LuaIsPlayerInForce(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t *force = lua_touserdata(L, 2);
    lua_pushboolean(L, player && force && (*force & (1u << PLAYER_NUM(player))));
    return 1;
}

static int LuaCreateRegion(lua_State *L) {
    for (uint32_t i = 0; i < MAX_REGIONS; i++) {
        region_t *region = &level.regions[i];
        if (region->inuse || region->exhausted) continue;
        memset(region->rects, 0, sizeof(region->rects));
        region->num_rects = 0;
        region->inuse = true;
        if (i >= level.num_regions) level.num_regions = i + 1;
        lua_pushlightuserdata(L, G_RegionHandle(i));
        return 1;
    }
    return luaL_error(L, "CreateRegion: region registry is full");
}

static int LuaStringHash(lua_State *L) {
    lua_pushinteger(L, (int32_t)G_StringHash(luaL_checkstring(L, 1)));
    return 1;
}

static int LuaGetPlayerNeutralPassive(lua_State *L) {
    lua_pushinteger(L, G_GetPlayerNeutralPassive());
    return 1;
}

static int LuaGetPlayerNeutralAggressive(lua_State *L) {
    lua_pushinteger(L, G_GetPlayerNeutralAggressive());
    return 1;
}

static int LuaGetBJMaxPlayers(lua_State *L) {
    lua_pushinteger(L, G_GetBJMaxPlayers());
    return 1;
}

static int LuaGetGameSpeed(lua_State *L) {
    lua_pushinteger(L, G_GetGameSpeed());
    return 1;
}

static int LuaIsFogEnabled(lua_State *L) {
    lua_pushboolean(L, G_PlayerFogEnabled(NULL, false));
    return 1;
}

static int LuaIsFogMaskEnabled(lua_State *L) {
    lua_pushboolean(L, G_PlayerFogEnabled(NULL, true));
    return 1;
}

static int LuaGetBJPlayerNeutralVictim(lua_State *L) {
    lua_pushinteger(L, G_GetBJPlayerNeutralVictim());
    return 1;
}

static int LuaGetBJPlayerNeutralExtra(lua_State *L) {
    lua_pushinteger(L, G_GetBJPlayerNeutralExtra());
    return 1;
}

static int LuaGetBJMaxPlayerSlots(lua_State *L) {
    lua_pushinteger(L, G_GetBJMaxPlayerSlots());
    return 1;
}

static int LuaSetPlayerAlliance(lua_State *L) {
    player_t *source = lua_touserdata(L, 1);
    player_t *other = lua_touserdata(L, 2);
    if (!source) {
        fprintf(stderr, "SetPlayerAlliance(): sourcePlayer is nil\n");
        return 0;
    }
    if (!other) {
        fprintf(stderr, "SetPlayerAlliance(): otherPlayer is nil\n");
        return 0;
    }
    G_SetPlayerAlliance(source, other,
        (PLAYERALLIANCE)luaL_checkinteger(L, 3), lua_toboolean(L, 4));
    return 0;
}

static int LuaSetPlayerState(lua_State *L) {
    G_SetPlayerState(lua_touserdata(L, 1),
        (uint32_t)luaL_checkinteger(L, 2), (int32_t)luaL_checkinteger(L, 3));
    return 0;
}

static int LuaFilter(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    return 1;
}

static int LuaGetFilterUnit(lua_State *L) {
    void *unit = WC3_LuaFilterUnit(level.lua_vm);
    if (unit) lua_pushlightuserdata(L, unit);
    else lua_pushnil(L);
    return 1;
}

static int LuaGetFilterPlayer(lua_State *L) {
    void *player = WC3_LuaFilterUnit(level.lua_vm);
    if (player) lua_pushlightuserdata(L, player);
    else lua_pushnil(L);
    return 1;
}

static int LuaConvertEnum(lua_State *L) {
    lua_pushinteger(L, luaL_checkinteger(L, 1));
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

void G_RegisterLuaMapRuntimeNatives(wc3Lua_t *L) {
    G_RegisterLuaMapConfigNatives(L);
    WC3_LuaRegisterNative(L, "CreateGroup", LuaCreateGroup);
    WC3_LuaRegisterNative(L, "DestroyGroup", LuaDestroyGroup);
    WC3_LuaRegisterNative(L, "GroupEnumUnitsOfPlayer", LuaGroupEnumUnitsOfPlayer);
    WC3_LuaRegisterNative(L, "BlzGroupGetSize", LuaBlzGroupGetSize);
    WC3_LuaRegisterNative(L, "BlzGroupUnitAt", LuaBlzGroupUnitAt);
    WC3_LuaRegisterNative(L, "Rect", LuaRect);
    WC3_LuaRegisterNative(L, "Location", LuaLocation);
    WC3_LuaRegisterNative(L, "Player", LuaPlayer);
    WC3_LuaRegisterNative(L, "SetPlayerStartLocation", LuaSetPlayerStartLocation);
    WC3_LuaRegisterNative(L, "ForcePlayerStartLocation", LuaForcePlayerStartLocation);
    WC3_LuaRegisterNative(L, "GetPlayerStartLocation", LuaGetPlayerStartLocation);
    WC3_LuaRegisterNative(L, "GetCameraMargin", LuaGetCameraMargin);
    WC3_LuaRegisterNative(L, "ConvertPlayerColor", LuaConvertPlayerColor);
    WC3_LuaRegisterNative(L, "SetPlayerColor", LuaSetPlayerColor);
    WC3_LuaRegisterNative(L, "SetPlayerTeam", LuaSetPlayerTeam);
    WC3_LuaRegisterNative(L, "SetStartLocPrioCount", LuaSetStartLocPrioCount);
    WC3_LuaRegisterNative(L, "SetStartLocPrio", LuaSetStartLocPrio);
    WC3_LuaRegisterNative(L, "SetPlayerRacePreference", LuaSetPlayerRacePreference);
    WC3_LuaRegisterNative(L, "SetPlayerRaceSelectable", LuaSetPlayerRaceSelectable);
    WC3_LuaRegisterNative(L, "SetPlayerController", LuaSetPlayerController);
    WC3_LuaRegisterNative(L, "GetPlayerController", LuaGetPlayerController);
    WC3_LuaRegisterNative(L, "GetPlayerSlotState", LuaGetPlayerSlotState);
    WC3_LuaRegisterNative(L, "CreateSoundFromLabel", LuaCreateSoundFromLabel);
    WC3_LuaRegisterNative(L, "SetCameraBounds", LuaSetCameraBounds);
    WC3_LuaRegisterNative(L, "SetDayNightModels", LuaSetDayNightModels);
    WC3_LuaRegisterNative(L, "SetTerrainFogEx", LuaSetTerrainFogEx);
    WC3_LuaRegisterNative(L, "ConvertFogStyle", LuaConvertFogStyle);
    WC3_LuaRegisterNative(L, "SetWaterBaseColor", LuaSetWaterBaseColor);
    WC3_LuaRegisterNative(L, "NewSoundEnvironment", LuaNewSoundEnvironment);
    WC3_LuaRegisterNative(L, "SetAmbientDaySound", LuaSetAmbientDaySound);
    WC3_LuaRegisterNative(L, "SetAmbientNightSound", LuaSetAmbientNightSound);
    WC3_LuaRegisterNative(L, "SetMapMusic", LuaSetMapMusic);
    WC3_LuaRegisterInteger(L, "CAMERA_MARGIN_LEFT", 0);
    WC3_LuaRegisterInteger(L, "CAMERA_MARGIN_RIGHT", 1);
    WC3_LuaRegisterInteger(L, "CAMERA_MARGIN_TOP", 2);
    WC3_LuaRegisterInteger(L, "CAMERA_MARGIN_BOTTOM", 3);
    WC3_LuaRegisterInteger(L, "RACE_PREF_HUMAN", 1);
    WC3_LuaRegisterInteger(L, "MAP_CONTROL_USER", 0);
    WC3_LuaRegisterInteger(L, "MAP_LOC_PRIO_LOW", 0);
    WC3_LuaRegisterInteger(L, "MAP_LOC_PRIO_HIGH", 1);
    WC3_LuaRegisterInteger(L, "MAP_LOC_PRIO_NOT", 2);
    WC3_LuaRegisterInteger(L, "PLAYER_NEUTRAL_PASSIVE", PLAYER_NEUTRAL_PASSIVE);
    WC3_LuaRegisterInteger(L, "PLAYER_NEUTRAL_AGGRESSIVE", PLAYER_NEUTRAL_AGGRESSIVE);
    WC3_LuaRegisterInteger(L, "PLAYER_NEUTRAL_VICTIM", PLAYER_NEUTRAL_VICTIM);
    WC3_LuaRegisterInteger(L, "PLAYER_NEUTRAL_EXTRA", PLAYER_NEUTRAL_EXTRA);
    WC3_LuaRegisterNative(L, "CreateForce", LuaCreateForce);
    WC3_LuaRegisterNative(L, "DestroyForce", LuaDestroyForce);
    WC3_LuaRegisterNative(L, "ForceAddPlayer", LuaForceAddPlayer);
    WC3_LuaRegisterNative(L, "ForceRemovePlayer", LuaForceRemovePlayer);
    WC3_LuaRegisterNative(L, "ForceClear", LuaForceClear);
    WC3_LuaRegisterNative(L, "IsPlayerInForce", LuaIsPlayerInForce);
    WC3_LuaRegisterNative(L, "ForceEnumPlayers", LuaForceEnumPlayers);
    WC3_LuaRegisterNative(L, "CreateRegion", LuaCreateRegion);
    WC3_LuaRegisterNative(L, "StringHash", LuaStringHash);
    WC3_LuaRegisterNative(L, "GetPlayerNeutralPassive", LuaGetPlayerNeutralPassive);
    WC3_LuaRegisterNative(L, "GetPlayerNeutralAggressive", LuaGetPlayerNeutralAggressive);
    WC3_LuaRegisterNative(L, "GetBJMaxPlayers", LuaGetBJMaxPlayers);
    WC3_LuaRegisterNative(L, "GetGameSpeed", LuaGetGameSpeed);
    WC3_LuaRegisterNative(L, "IsFogEnabled", LuaIsFogEnabled);
    WC3_LuaRegisterNative(L, "IsFogMaskEnabled", LuaIsFogMaskEnabled);
    WC3_LuaRegisterNative(L, "GetBJPlayerNeutralVictim", LuaGetBJPlayerNeutralVictim);
    WC3_LuaRegisterNative(L, "GetBJPlayerNeutralExtra", LuaGetBJPlayerNeutralExtra);
    WC3_LuaRegisterNative(L, "GetBJMaxPlayerSlots", LuaGetBJMaxPlayerSlots);
    WC3_LuaRegisterNative(L, "SetPlayerAlliance", LuaSetPlayerAlliance);
    WC3_LuaRegisterNative(L, "SetPlayerState", LuaSetPlayerState);
    WC3_LuaRegisterNative(L, "Filter", LuaFilter);
    WC3_LuaRegisterNative(L, "GetFilterUnit", LuaGetFilterUnit);
    WC3_LuaRegisterNative(L, "GetFilterPlayer", LuaGetFilterPlayer);

    static cstring_t const enum_converters[] = {
        "ConvertRace", "ConvertAllianceType", "ConvertRacePref", "ConvertIGameState",
        "ConvertFGameState", "ConvertPlayerState", "ConvertPlayerGameResult", "ConvertUnitState",
        "ConvertGameEvent", "ConvertPlayerEvent", "ConvertPlayerUnitEvent", "ConvertWidgetEvent",
        "ConvertDialogEvent", "ConvertUnitEvent", "ConvertLimitOp", "ConvertUnitType",
        "ConvertGameSpeed", "ConvertPlacement", "ConvertStartLocPrio", "ConvertGameDifficulty",
        "ConvertGameType", "ConvertMapFlag", "ConvertMapVisibility", "ConvertMapSetting",
        "ConvertMapDensity", "ConvertMapControl", "ConvertPlayerSlotState", "ConvertVolumeGroup",
        "ConvertCameraField", "ConvertBlendMode", "ConvertRarityControl", "ConvertTexMapFlags",
        "ConvertFogState", "ConvertEffectType", "ConvertEquipmentType", "ConvertItemTag",
        "ConvertLoadoutSlot", "ConvertOriginFrameType", "ConvertFramePointType",
        "ConvertTextAlignType", "ConvertFrameEventType", "ConvertOsKeyType",
        "ConvertAbilityBooleanField", "ConvertAbilityBooleanLevelArrayField",
        "ConvertAbilityBooleanLevelField", "ConvertAbilityIntegerField",
        "ConvertAbilityIntegerLevelArrayField", "ConvertAbilityIntegerLevelField",
        "ConvertAbilityRealField", "ConvertAbilityRealLevelArrayField",
        "ConvertAbilityRealLevelField", "ConvertAbilityStringField",
        "ConvertAbilityStringLevelArrayField", "ConvertAbilityStringLevelField",
        "ConvertArmorType", "ConvertDefenseType", "ConvertHeroAttribute",
        "ConvertItemBooleanField", "ConvertItemIntegerField", "ConvertItemRealField",
        "ConvertItemStringField", "ConvertMoveType", "ConvertPathingFlag",
        "ConvertRegenType", "ConvertTargetFlag", "ConvertUnitBooleanField",
        "ConvertUnitCategory", "ConvertUnitIntegerField", "ConvertUnitRealField",
        "ConvertUnitStringField", "ConvertUnitWeaponBooleanField",
        "ConvertUnitWeaponIntegerField", "ConvertUnitWeaponRealField",
        "ConvertUnitWeaponStringField",
        "ConvertAnimType", "ConvertSubAnimType",
        "ConvertVersion", "ConvertItemType",
        "ConvertAttackType", "ConvertDamageType", "ConvertWeaponType", "ConvertSoundType",
        "ConvertPathingType", "ConvertMouseButtonType", "ConvertAIDifficulty", "ConvertPlayerScore",
    };
    for (uint32_t i = 0; i < sizeof(enum_converters) / sizeof(enum_converters[0]); ++i)
        WC3_LuaRegisterNative(L, enum_converters[i], LuaConvertEnum);
}

bool G_LoadLuaMapScript(wc3Lua_t *L, cstring_t source, cstring_t chunk_name) {
    if (!L) return false;
    G_RegisterLuaMapRuntimeNatives(L);
    return WC3_LuaLoadBuffer(L, source, chunk_name);
}

bool G_LoadLuaMapJass(wc3Lua_t *L, jass_t *J, cstring_t source, cstring_t chunk_name) {
    string_t lua_source = NULL;
    bool result;

    if (!L || !J || !source || !chunk_name) return false;
    G_RegisterLuaMapRuntimeNatives(L);
    if (!jass_transpile_to_lua(J, source, &lua_source)) {
        fprintf(stderr, "WC3 Lua: failed to transpile %s\n", chunk_name);
        return false;
    }
    result = WC3_LuaLoadBuffer(L, lua_source, chunk_name);
    free(lua_source);
    return result;
}
