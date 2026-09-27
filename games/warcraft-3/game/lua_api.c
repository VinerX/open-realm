#include "g_local.h"
#include "jass/jass.h"

#include "lua.h"
#include "lauxlib.h"

extern player_t *currentplayer;

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

static int LuaTimerStart(lua_State *L) {
    gtimer_t *timer = lua_touserdata(L, 1);
    float timeout = (float)luaL_checknumber(L, 2);
    bool periodic = lua_toboolean(L, 3);
    int reference = LUA_NOREF;

    if (!timer) return luaL_error(L, "TimerStart: invalid timer");
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TFUNCTION);
        reference = WC3_LuaRefFunction(level.lua_vm, 4);
        if (reference == LUA_NOREF || reference == LUA_REFNIL)
            return luaL_error(L, "TimerStart: could not retain callback");
    }
    if (timer->lua_vm) WC3_LuaUnrefFunction(timer->lua_vm, timer->lua_ref);
    G_TimerStart(timer, (uint32_t)(MAX(0.0f, timeout) * 1000.0f), periodic, NULL);
    timer->lua_vm = reference == LUA_NOREF ? NULL : level.lua_vm;
    timer->lua_ref = reference;
    return 0;
}

static int LuaDestroyTimer(lua_State *L) {
    gtimer_t *timer = lua_touserdata(L, 1);
    if (timer && timer->lua_vm) {
        WC3_LuaUnrefFunction(timer->lua_vm, timer->lua_ref);
        timer->lua_vm = NULL;
        timer->lua_ref = LUA_NOREF;
    }
    G_TimerDestroy(timer);
    return 0;
}

static int LuaPauseTimer(lua_State *L) {
    G_TimerPause(lua_touserdata(L, 1));
    return 0;
}

static int LuaResumeTimer(lua_State *L) {
    G_TimerResume(lua_touserdata(L, 1));
    return 0;
}

static int LuaTimerGetRemaining(lua_State *L) {
    lua_pushnumber(L, G_TimerRemaining(lua_touserdata(L, 1)) / 1000.0f);
    return 1;
}

static int LuaTimerGetElapsed(lua_State *L) {
    gtimer_t *timer = lua_touserdata(L, 1);
    lua_pushnumber(L, timer ? (timer->duration - G_TimerRemaining(timer)) / 1000.0f : 0.0f);
    return 1;
}

static int LuaTimerGetTimeout(lua_State *L) {
    gtimer_t *timer = lua_touserdata(L, 1);
    lua_pushnumber(L, timer ? timer->duration / 1000.0f : 0.0f);
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

static int LuaCreateTrigger(lua_State *L) {
    trigger_t *trigger = G_AllocJassTrigger();
    if (!trigger) return luaL_error(L, "CreateTrigger: trigger registry is full");
    trigger->lua_vm = level.lua_vm;
    lua_pushlightuserdata(L, trigger);
    return 1;
}

static int LuaTriggerAddAction(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    gTriggerAction_t *action;
    int reference;

    if (!trigger || trigger->lua_vm != level.lua_vm)
        return luaL_error(L, "TriggerAddAction: invalid Lua trigger");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    reference = WC3_LuaRefFunction(level.lua_vm, 2);
    if (reference == LUA_NOREF || reference == LUA_REFNIL)
        return luaL_error(L, "TriggerAddAction: could not retain callback");
    action = gi.MemAlloc(sizeof(*action));
    if (!action) {
        WC3_LuaUnrefFunction(level.lua_vm, reference);
        return luaL_error(L, "TriggerAddAction: allocation failed");
    }
    memset(action, 0, sizeof(*action));
    action->lua_vm = level.lua_vm;
    action->lua_ref = reference;
    ADD_TO_LIST(action, trigger->actions);
    return 0;
}

static int LuaTriggerAddCondition(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    gTriggerCondition_t *condition;
    int reference;

    if (!trigger || trigger->lua_vm != level.lua_vm)
        return luaL_error(L, "TriggerAddCondition: invalid Lua trigger");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    reference = WC3_LuaRefFunction(level.lua_vm, 2);
    if (reference == LUA_NOREF || reference == LUA_REFNIL)
        return luaL_error(L, "TriggerAddCondition: could not retain callback");
    condition = gi.MemAlloc(sizeof(*condition));
    if (!condition) {
        WC3_LuaUnrefFunction(level.lua_vm, reference);
        return luaL_error(L, "TriggerAddCondition: allocation failed");
    }
    memset(condition, 0, sizeof(*condition));
    condition->lua_vm = level.lua_vm;
    condition->lua_ref = reference;
    ADD_TO_LIST(condition, trigger->conditions);
    lua_pushlightuserdata(L, condition);
    return 1;
}

static int LuaTriggerRegisterGameStateEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm)
        return luaL_error(L, "TriggerRegisterGameStateEvent: invalid Lua trigger");
    event = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    if (!event) return luaL_error(L, "TriggerRegisterGameStateEvent: event registry is full");
    event->trigger = trigger;
    event->state = (uint32_t)luaL_checkinteger(L, 2);
    event->limitop = (uint32_t)luaL_checkinteger(L, 3);
    event->limitval = (float)luaL_checknumber(L, 4);
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

static int LuaTriggerRegisterTimerExpireEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    gtimer_t *timer = lua_touserdata(L, 2);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm || !timer)
        return luaL_error(L, "TriggerRegisterTimerExpireEvent: invalid trigger or timer");
    event = G_MakeEvent(EVENT_GAME_TIMER_EXPIRED);
    if (!event) return luaL_error(L, "TriggerRegisterTimerExpireEvent: event registry is full");
    event->trigger = trigger;
    event->timer = timer;
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

static int LuaTriggerRegisterPlayerUnitEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    player_t *player = lua_touserdata(L, 2);
    EVENTTYPE type = (EVENTTYPE)luaL_checkinteger(L, 3);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm || !player)
        return luaL_error(L, "TriggerRegisterPlayerUnitEvent: invalid trigger or player");
    event = G_MakeEvent(type);
    if (!event) return luaL_error(L, "TriggerRegisterPlayerUnitEvent: event registry is full");
    G_SetPlayerEventSubject(event, PLAYER_ENT(player));
    event->trigger = trigger;
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TFUNCTION);
        event->lua_filter_ref = WC3_LuaRefFunction(level.lua_vm, 4);
        if (event->lua_filter_ref == LUA_NOREF || event->lua_filter_ref == LUA_REFNIL) {
            event->inuse = false;
            return luaL_error(L, "TriggerRegisterPlayerUnitEvent: could not retain filter");
        }
        event->lua_filter_vm = level.lua_vm;
    }
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

bool G_LuaTriggerEvaluate(trigger_t *trigger, wc3LuaTriggerContext_t const *context) {
    wc3Lua_t *lua = trigger ? trigger->lua_vm : NULL;
    wc3LuaTriggerContext_t previous;

    if (!lua || !context) return false;
    previous = WC3_LuaGetTriggerContext(lua);
    WC3_LuaSetTriggerContext(lua, context);
    FOR_EACH_LIST(gTriggerCondition_t, condition, trigger->conditions) {
        bool result = false;
        if (condition->lua_vm != lua ||
            !WC3_LuaCallRefBoolean(lua, condition->lua_ref, &result)) {
            WC3_LuaSetTriggerContext(lua, &previous);
            return false;
        }
        if (!result) {
            WC3_LuaSetTriggerContext(lua, &previous);
            return false;
        }
    }
    WC3_LuaSetTriggerContext(lua, &previous);
    return true;
}

bool G_LuaTriggerExecute(trigger_t *trigger, wc3LuaTriggerContext_t const *context) {
    wc3Lua_t *lua = trigger ? trigger->lua_vm : NULL;
    wc3LuaTriggerContext_t previous, current;

    if (!lua) return false;
    previous = WC3_LuaGetTriggerContext(lua);
    current = context ? *context : previous;
    current.trigger = trigger;
    WC3_LuaSetTriggerContext(lua, &current);
    FOR_EACH_LIST(gTriggerAction_t, action, trigger->actions) {
        if (action->lua_vm != lua || !WC3_LuaCallRef(lua, action->lua_ref)) {
            WC3_LuaSetTriggerContext(lua, &previous);
            return false;
        }
    }
    WC3_LuaSetTriggerContext(lua, &previous);
    return true;
}

bool G_LuaTimerExpired(gtimer_t *timer) {
    wc3Lua_t *lua = timer ? timer->lua_vm : NULL;
    wc3LuaTriggerContext_t previous, context = { 0 };
    char error[512];

    if (!lua || timer->lua_ref == LUA_NOREF || timer->lua_ref == LUA_REFNIL) return false;
    previous = WC3_LuaGetTriggerContext(lua);
    context.timer = timer;
    WC3_LuaSetTriggerContext(lua, &context);
    if (WC3_LuaCallRef(lua, timer->lua_ref)) {
        WC3_LuaSetTriggerContext(lua, &previous);
        return true;
    }
    WC3_LuaSetTriggerContext(lua, &previous);
    strlcpy(error, WC3_LuaErrorMessage(lua), sizeof(error));
    fprintf(stderr, "WC3 Lua: timer callback failed: %s\n", error);
    WC3_LuaClearError(lua);
    return false;
}

static wc3LuaTriggerContext_t LuaTriggerContextFromJass(jassTriggerContext_t const *context) {
    return (wc3LuaTriggerContext_t){
        .trigger = context ? context->trigger : NULL,
        .unit = context ? context->unit : NULL,
        .source = context ? context->source : NULL,
        .timer = context ? context->timer : NULL,
        .region = context ? context->region : NULL,
        .event_value = context ? context->value : 0,
    };
}

bool G_LuaTriggerEvaluateHost(handle_t handle, jassTriggerContext_t const *context) {
    wc3LuaTriggerContext_t lua_context = LuaTriggerContextFromJass(context);
    return G_LuaTriggerEvaluate(handle, &lua_context);
}

bool G_LuaTriggerExecuteHost(handle_t handle, jassTriggerContext_t const *context) {
    wc3LuaTriggerContext_t lua_context = LuaTriggerContextFromJass(context);
    return G_LuaTriggerExecute(handle, &lua_context);
}

static int LuaTriggerExecute(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    wc3LuaTriggerContext_t context;
    char error[512];
    if (!trigger || trigger->lua_vm != level.lua_vm)
        return luaL_error(L, "TriggerExecute: invalid Lua trigger");
    context = WC3_LuaGetTriggerContext(level.lua_vm);
    if (!G_LuaTriggerExecute(trigger, &context)) {
        strlcpy(error, WC3_LuaErrorMessage(level.lua_vm), sizeof(error));
        WC3_LuaClearError(level.lua_vm);
        return luaL_error(L, "TriggerExecute: %s", error);
    }
    return 0;
}

static int LuaTriggerEvaluate(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    wc3LuaTriggerContext_t context;
    bool result;
    if (!trigger || trigger->lua_vm != level.lua_vm)
        return luaL_error(L, "TriggerEvaluate: invalid Lua trigger");
    context = WC3_LuaGetTriggerContext(level.lua_vm);
    context.trigger = trigger;
    result = G_LuaTriggerEvaluate(trigger, &context);
    lua_pushboolean(L, result);
    return 1;
}

static int LuaGetTriggerUnit(lua_State *L) {
    void *unit = WC3_LuaGetTriggerContext(level.lua_vm).unit;
    if (unit) lua_pushlightuserdata(L, unit); else lua_pushnil(L);
    return 1;
}

static int LuaGetTriggeringTrigger(lua_State *L) {
    void *trigger = WC3_LuaGetTriggerContext(level.lua_vm).trigger;
    if (trigger) lua_pushlightuserdata(L, trigger); else lua_pushnil(L);
    return 1;
}

static int LuaGetExpiredTimer(lua_State *L) {
    void *timer = WC3_LuaGetTriggerContext(level.lua_vm).timer;
    if (timer) lua_pushlightuserdata(L, timer); else lua_pushnil(L);
    return 1;
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

static int LuaAddWeatherEffect(lua_State *L) {
    box2_t *where = lua_touserdata(L, 1);
    gweather_t *effect = G_WeatherAdd(where, (uint32_t)luaL_checkinteger(L, 2), false);
    if (effect) lua_pushlightuserdata(L, effect);
    else lua_pushnil(L);
    return 1;
}

static int LuaRemoveWeatherEffect(lua_State *L) {
    G_WeatherRemove(lua_touserdata(L, 1));
    return 0;
}

static int LuaEnableWeatherEffect(lua_State *L) {
    G_WeatherEnable(lua_touserdata(L, 1), lua_toboolean(L, 2));
    return 0;
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

/* Sound descriptors are game-owned handles; Lua receives one light userdata
 * per handle, matching CreateSoundFromLabel above and the JASS sound path. */
static int LuaCreateSound(lua_State *L) {
    gsound_t *sound = lua_newuserdata(L, sizeof(*sound));
    cstring_t fileName = luaL_checkstring(L, 1);
    memset(sound, 0, sizeof(*sound));
    strlcpy(sound->fileName, fileName, sizeof(sound->fileName));
    sound->looping = lua_toboolean(L, 2);
    sound->is3D = lua_toboolean(L, 3);
    sound->stopwhenoutofrange = lua_toboolean(L, 4);
    sound->fadeInRate = (int32_t)luaL_checkinteger(L, 5);
    sound->fadeOutRate = (int32_t)luaL_checkinteger(L, 6);
    sound->soundIndex = gi.SoundIndex(fileName);
    G_JassSoundRuntimeInit(sound);
    return 1;
}

static int LuaCreateSoundFilenameWithLabel(lua_State *L) {
    gsound_t *sound = lua_newuserdata(L, sizeof(*sound));
    cstring_t fileName = luaL_checkstring(L, 1);
    float volume = 1.0f;
    memset(sound, 0, sizeof(*sound));
    strlcpy(sound->fileName, fileName, sizeof(sound->fileName));
    sound->looping = lua_toboolean(L, 2);
    sound->is3D = lua_toboolean(L, 3);
    sound->stopwhenoutofrange = lua_toboolean(L, 4);
    sound->fadeInRate = (int32_t)luaL_checkinteger(L, 5);
    sound->fadeOutRate = (int32_t)luaL_checkinteger(L, 6);
    sound->soundIndex = gi.SoundIndex(fileName);
    G_JassSoundRuntimeInit(sound);
    if (G_SoundLabelDescriptor(luaL_checkstring(L, 7), NULL, 0, NULL, &volume))
        G_JassSoundSetVolume(sound, volume);
    return 1;
}

static int LuaSetSoundParamsFromLabel(lua_State *L) {
    gsound_t *sound = lua_touserdata(L, 1);
    float volume = 1.0f;

    /* Authored volume only; pitch/channel/distance remain mixer gaps. */
    if (sound && G_SoundLabelDescriptor(luaL_checkstring(L, 2), NULL, 0, NULL, &volume))
        G_JassSoundSetVolume(sound, volume);
    return 0;
}

static int LuaSetSoundVolume(lua_State *L) {
    gsound_t *sound = lua_touserdata(L, 1);
    int32_t volume = (int32_t)luaL_checkinteger(L, 2);
    if (sound) G_JassSoundSetVolume(sound, (float)MAX(0, MIN(volume, 127)) / 127.0f);
    return 0;
}

static int LuaSetSoundDuration(lua_State *L) {
    gsound_t *sound = lua_touserdata(L, 1);
    if (sound) sound->duration = (uint32_t)MAX(0, (int32_t)luaL_checkinteger(L, 2));
    return 0;
}

/* Pitch/channel are accepted but not transported: the entity sound path has no
 * pitch/channel field yet, matching the JASS SetSoundPitch/SetSoundChannel gap. */
static int LuaSetSoundPitch(lua_State *L) {
    (void)luaL_checknumber(L, 2);
    return 0;
}

static int LuaSetSoundChannel(lua_State *L) {
    (void)luaL_checkinteger(L, 2);
    return 0;
}

static int LuaGetSoundDuration(lua_State *L) {
    gsound_t *sound = lua_touserdata(L, 1);
    lua_pushinteger(L, sound ? (lua_Integer)sound->duration : 0);
    return 1;
}

static int LuaGetSoundFileDuration(lua_State *L) {
    lua_pushinteger(L, G_SoundFileDuration(luaL_checkstring(L, 1)));
    return 1;
}

static int LuaSetSoundPosition(lua_State *L) {
    gsound_t *sound = lua_touserdata(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float z = (float)luaL_checknumber(L, 4);
    if (sound) G_JassSoundSetPosition(sound, &MAKE(vec3_t, x, y, z));
    return 0;
}

static int LuaAttachSoundToUnit(lua_State *L) {
    gsound_t *sound = lua_touserdata(L, 1);
    edict_t *unit = lua_touserdata(L, 2);
    if (sound) G_JassSoundAttach(sound, unit);
    return 0;
}

static int LuaStartSound(lua_State *L) {
    gsound_t *sound = lua_touserdata(L, 1);
    jassSoundPlayback_t playback;
    float attenuation;

    if (!sound || !sound->soundIndex) return 0;
    G_JassSoundPlayback(sound, &playback);
    attenuation = sound->is3D ? 1.0f : 0.0f;

    if (currentplayer) {
        edict_t *recipient = PLAYER_ENT(currentplayer);
        if (!recipient || !recipient->client || !recipient->client->connected) return 0;
        if (playback.positioned)
            G_PlaySound(&playback.origin, recipient, CHAN_OWNER | CHAN_RELIABLE, sound->soundIndex,
                        playback.volume, attenuation, 0.0f);
        else
            G_PlaySound(NULL, recipient, CHAN_OWNER | CHAN_RELIABLE, sound->soundIndex,
                        playback.volume, attenuation, 0.0f);
        return 0;
    }

    if (playback.positioned)
        G_PlaySound(&playback.origin, playback.emitter, CHAN_RELIABLE, sound->soundIndex,
                    playback.volume, attenuation, 0.0f);
    else
        G_PlaySound(NULL, NULL, CHAN_RELIABLE, sound->soundIndex, playback.volume, attenuation, 0.0f);
    return 0;
}

static int LuaPlayMusic(lua_State *L) {
    G_MusicPlay(luaL_checkstring(L, 1), 0, 0);
    return 0;
}

static int LuaPlayMusicEx(lua_State *L) {
    cstring_t musicName = luaL_checkstring(L, 1);
    int32_t frommsecs = (int32_t)luaL_checkinteger(L, 2);
    int32_t fadeinmsecs = (int32_t)luaL_checkinteger(L, 3);
    G_MusicPlay(musicName, MAX(0, frommsecs), MAX(0, fadeinmsecs));
    return 0;
}

static int LuaClearMapMusic(lua_State *L) {
    (void)L;
    G_MusicClearMap();
    return 0;
}

static int LuaPlayThematicMusic(lua_State *L) {
    G_MusicPlayThematic(luaL_checkstring(L, 1), 0);
    return 0;
}

static int LuaPlayThematicMusicEx(lua_State *L) {
    cstring_t musicFileName = luaL_checkstring(L, 1);
    int32_t frommsecs = (int32_t)luaL_checkinteger(L, 2);
    G_MusicPlayThematic(musicFileName, MAX(0, frommsecs));
    return 0;
}

static int LuaEndThematicMusic(lua_State *L) {
    (void)L;
    G_MusicEndThematic();
    return 0;
}

static int LuaStopMusic(lua_State *L) {
    G_MusicStop(lua_toboolean(L, 1));
    return 0;
}

static int LuaResumeMusic(lua_State *L) {
    (void)L;
    G_MusicResume();
    return 0;
}

static int LuaSetMusicVolume(lua_State *L) {
    G_MusicSetVolume((int32_t)luaL_checkinteger(L, 1));
    return 0;
}

static int LuaSetMusicPlayPosition(lua_State *L) {
    G_MusicSetPosition((int32_t)luaL_checkinteger(L, 1));
    return 0;
}

static int LuaSetThematicMusicVolume(lua_State *L) {
    G_MusicSetThematicVolume((int32_t)luaL_checkinteger(L, 1));
    return 0;
}

static int LuaSetThematicMusicPlayPosition(lua_State *L) {
    G_MusicSetThematicPosition((int32_t)luaL_checkinteger(L, 1));
    return 0;
}

static int LuaSetCameraBounds(lua_State *L) {
    float bounds[8];
    FOR_LOOP(i, 8) bounds[i] = (float)luaL_checknumber(L, i + 1);
    G_SetCameraBounds(bounds);
    return 0;
}

static int LuaGetCameraBoundMinX(lua_State *L) {
    lua_pushnumber(L, level.camera_bounds.min.x);
    return 1;
}

static int LuaGetCameraBoundMinY(lua_State *L) {
    lua_pushnumber(L, level.camera_bounds.min.y);
    return 1;
}

static int LuaGetCameraBoundMaxX(lua_State *L) {
    lua_pushnumber(L, level.camera_bounds.max.x);
    return 1;
}

static int LuaGetCameraBoundMaxY(lua_State *L) {
    lua_pushnumber(L, level.camera_bounds.max.y);
    return 1;
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

static int LuaVersionGet(lua_State *L) {
    lua_pushinteger(L, G_GetWarcraftVersion());
    return 1;
}

static int LuaVersionCompatible(lua_State *L) {
    lua_pushboolean(L, luaL_checkinteger(L, 1) == 0);
    return 1;
}

static int LuaVersionSupported(lua_State *L) {
    lua_pushboolean(L, luaL_checkinteger(L, 1) == 0);
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

static int LuaGetPlayerTechResearched(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t tech = (uint32_t)luaL_checkinteger(L, 2);
    (void)lua_toboolean(L, 3);
    lua_pushboolean(L, player &&
        G_GetPlayerTechResearchedLevel(PLAYER_CLIENT(player), tech) > 0);
    return 1;
}

static int LuaSetPlayerTechMaxAllowed(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t tech = (uint32_t)luaL_checkinteger(L, 2);
    int32_t maximum = (int32_t)luaL_checkinteger(L, 3);
    if (player) G_SetPlayerTechMaxAllowed(PLAYER_CLIENT(player), tech, maximum);
    return 0;
}

static int LuaGetPlayerTechMaxAllowed(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t tech = (uint32_t)luaL_checkinteger(L, 2);
    lua_pushinteger(L, player ? G_GetPlayerTechMaxAllowed(PLAYER_CLIENT(player), tech) : -1);
    return 1;
}

static int LuaSetAllItemTypeSlots(lua_State *L) {
    G_SetAllStockSlots(true, (int32_t)luaL_checkinteger(L, 1));
    return 0;
}

static int LuaSetAllUnitTypeSlots(lua_State *L) {
    G_SetAllStockSlots(false, (int32_t)luaL_checkinteger(L, 1));
    return 0;
}

static int LuaSetItemTypeSlots(lua_State *L) {
    G_SetStockSlots(lua_touserdata(L, 1), true, (int32_t)luaL_checkinteger(L, 2));
    return 0;
}

static int LuaSetUnitTypeSlots(lua_State *L) {
    G_SetStockSlots(lua_touserdata(L, 1), false, (int32_t)luaL_checkinteger(L, 2));
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
    WC3_LuaRegisterNative(L, "TimerStart", LuaTimerStart);
    WC3_LuaRegisterNative(L, "DestroyTimer", LuaDestroyTimer);
    WC3_LuaRegisterNative(L, "PauseTimer", LuaPauseTimer);
    WC3_LuaRegisterNative(L, "ResumeTimer", LuaResumeTimer);
    WC3_LuaRegisterNative(L, "TimerGetRemaining", LuaTimerGetRemaining);
    WC3_LuaRegisterNative(L, "TimerGetElapsed", LuaTimerGetElapsed);
    WC3_LuaRegisterNative(L, "TimerGetTimeout", LuaTimerGetTimeout);
    WC3_LuaRegisterInteger(L, "MAP_PLACEMENT_TEAMS_TOGETHER", 3);
}

void G_RegisterLuaMapRuntimeNatives(wc3Lua_t *L) {
    G_RegisterLuaMapConfigNatives(L);
    WC3_LuaRegisterNative(L, "CreateGroup", LuaCreateGroup);
    WC3_LuaRegisterNative(L, "DestroyGroup", LuaDestroyGroup);
    WC3_LuaRegisterNative(L, "CreateTrigger", LuaCreateTrigger);
    WC3_LuaRegisterNative(L, "TriggerAddAction", LuaTriggerAddAction);
    WC3_LuaRegisterNative(L, "TriggerAddCondition", LuaTriggerAddCondition);
    WC3_LuaRegisterNative(L, "TriggerRegisterGameStateEvent", LuaTriggerRegisterGameStateEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterTimerExpireEvent", LuaTriggerRegisterTimerExpireEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterPlayerUnitEvent", LuaTriggerRegisterPlayerUnitEvent);
    WC3_LuaRegisterNative(L, "TriggerEvaluate", LuaTriggerEvaluate);
    WC3_LuaRegisterNative(L, "TriggerExecute", LuaTriggerExecute);
    WC3_LuaRegisterNative(L, "GetTriggerUnit", LuaGetTriggerUnit);
    WC3_LuaRegisterNative(L, "GetTriggeringTrigger", LuaGetTriggeringTrigger);
    WC3_LuaRegisterNative(L, "GetExpiredTimer", LuaGetExpiredTimer);
    WC3_LuaRegisterNative(L, "GroupEnumUnitsOfPlayer", LuaGroupEnumUnitsOfPlayer);
    WC3_LuaRegisterNative(L, "BlzGroupGetSize", LuaBlzGroupGetSize);
    WC3_LuaRegisterNative(L, "BlzGroupUnitAt", LuaBlzGroupUnitAt);
    WC3_LuaRegisterNative(L, "Rect", LuaRect);
    WC3_LuaRegisterNative(L, "Location", LuaLocation);
    WC3_LuaRegisterNative(L, "AddWeatherEffect", LuaAddWeatherEffect);
    WC3_LuaRegisterNative(L, "RemoveWeatherEffect", LuaRemoveWeatherEffect);
    WC3_LuaRegisterNative(L, "EnableWeatherEffect", LuaEnableWeatherEffect);
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
    WC3_LuaRegisterNative(L, "CreateSound", LuaCreateSound);
    WC3_LuaRegisterNative(L, "CreateSoundFilenameWithLabel", LuaCreateSoundFilenameWithLabel);
    WC3_LuaRegisterNative(L, "CreateSoundFromLabel", LuaCreateSoundFromLabel);
    WC3_LuaRegisterNative(L, "SetSoundParamsFromLabel", LuaSetSoundParamsFromLabel);
    WC3_LuaRegisterNative(L, "SetSoundVolume", LuaSetSoundVolume);
    WC3_LuaRegisterNative(L, "SetSoundPitch", LuaSetSoundPitch);
    WC3_LuaRegisterNative(L, "SetSoundChannel", LuaSetSoundChannel);
    WC3_LuaRegisterNative(L, "SetSoundDuration", LuaSetSoundDuration);
    WC3_LuaRegisterNative(L, "GetSoundDuration", LuaGetSoundDuration);
    WC3_LuaRegisterNative(L, "GetSoundFileDuration", LuaGetSoundFileDuration);
    WC3_LuaRegisterNative(L, "SetSoundPosition", LuaSetSoundPosition);
    WC3_LuaRegisterNative(L, "AttachSoundToUnit", LuaAttachSoundToUnit);
    WC3_LuaRegisterNative(L, "StartSound", LuaStartSound);
    WC3_LuaRegisterNative(L, "PlayMusic", LuaPlayMusic);
    WC3_LuaRegisterNative(L, "PlayMusicEx", LuaPlayMusicEx);
    WC3_LuaRegisterNative(L, "ClearMapMusic", LuaClearMapMusic);
    WC3_LuaRegisterNative(L, "PlayThematicMusic", LuaPlayThematicMusic);
    WC3_LuaRegisterNative(L, "PlayThematicMusicEx", LuaPlayThematicMusicEx);
    WC3_LuaRegisterNative(L, "EndThematicMusic", LuaEndThematicMusic);
    WC3_LuaRegisterNative(L, "StopMusic", LuaStopMusic);
    WC3_LuaRegisterNative(L, "ResumeMusic", LuaResumeMusic);
    WC3_LuaRegisterNative(L, "SetMusicVolume", LuaSetMusicVolume);
    WC3_LuaRegisterNative(L, "SetMusicPlayPosition", LuaSetMusicPlayPosition);
    WC3_LuaRegisterNative(L, "SetThematicMusicVolume", LuaSetThematicMusicVolume);
    WC3_LuaRegisterNative(L, "SetThematicMusicPlayPosition", LuaSetThematicMusicPlayPosition);
    WC3_LuaRegisterNative(L, "SetCameraBounds", LuaSetCameraBounds);
    WC3_LuaRegisterNative(L, "GetCameraBoundMinX", LuaGetCameraBoundMinX);
    WC3_LuaRegisterNative(L, "GetCameraBoundMinY", LuaGetCameraBoundMinY);
    WC3_LuaRegisterNative(L, "GetCameraBoundMaxX", LuaGetCameraBoundMaxX);
    WC3_LuaRegisterNative(L, "GetCameraBoundMaxY", LuaGetCameraBoundMaxY);
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
    WC3_LuaRegisterNative(L, "VersionGet", LuaVersionGet);
    WC3_LuaRegisterNative(L, "VersionCompatible", LuaVersionCompatible);
    WC3_LuaRegisterNative(L, "VersionSupported", LuaVersionSupported);
    WC3_LuaRegisterNative(L, "SetPlayerAlliance", LuaSetPlayerAlliance);
    WC3_LuaRegisterNative(L, "SetPlayerState", LuaSetPlayerState);
    WC3_LuaRegisterNative(L, "GetPlayerTechResearched", LuaGetPlayerTechResearched);
    WC3_LuaRegisterNative(L, "SetPlayerTechMaxAllowed", LuaSetPlayerTechMaxAllowed);
    WC3_LuaRegisterNative(L, "GetPlayerTechMaxAllowed", LuaGetPlayerTechMaxAllowed);
    WC3_LuaRegisterNative(L, "SetAllItemTypeSlots", LuaSetAllItemTypeSlots);
    WC3_LuaRegisterNative(L, "SetAllUnitTypeSlots", LuaSetAllUnitTypeSlots);
    WC3_LuaRegisterNative(L, "SetItemTypeSlots", LuaSetItemTypeSlots);
    WC3_LuaRegisterNative(L, "SetUnitTypeSlots", LuaSetUnitTypeSlots);
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
