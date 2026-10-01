#include "g_local.h"
#include "jass/jass.h"

#include "lua.h"
#include "lauxlib.h"

#include "g_camera.h"

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

static int LuaBlzCreateUnitWithSkin(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t unitid = (uint32_t)luaL_checkinteger(L, 2);
    vec2_t location = { (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4) };
    float facing = (float)luaL_checknumber(L, 5);
    uint32_t skinId = (uint32_t)luaL_checkinteger(L, 6);
    edict_t *unit;

    if (!player) {
        lua_pushnil(L);
        return 1;
    }
    unit = unit_create(PLAYER_NUM(player), unitid, &location, facing);
    if (unit) G_ApplyUnitSkin(unit, skinId);
    if (unit) lua_pushlightuserdata(L, unit);
    else lua_pushnil(L);
    return 1;
}

static int LuaSetUnitColor(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t *color = lua_touserdata(L, 2);
    if (unit && color) G_SetUnitColorOverride(unit, *color);
    return 0;
}

static int LuaSetUnitState(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t *state = lua_touserdata(L, 2);
    float value = (float)luaL_checknumber(L, 3);
    uint32_t which = state ? *state : (uint32_t)luaL_checkinteger(L, 2);
    bool was_dead;

    if (!unit) return 0;
    was_dead = M_IsDead(unit);
    if (which == WC3_UNIT_STATE_LIFE) G_SetHealth(unit, value);
    else (&unit->health.value)[which] = value;
    if ((unit->s.flags & EF_FOW_BLOCKER) && was_dead != M_IsDead(unit)) G_FowMarkBlockersDirty();
    return 0;
}

static int LuaWaygateSetDestination(lua_State *L) {
    edict_t *waygate = lua_touserdata(L, 1);
    vec2_t destination = { (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
    S_WaygateSetDestination(waygate, &destination);
    return 0;
}

static int LuaWaygateActivate(lua_State *L) {
    edict_t *waygate = lua_touserdata(L, 1);
    S_WaygateSetActive(waygate, lua_toboolean(L, 2));
    return 0;
}

static int LuaGetRectCenterX(lua_State *L) {
    box2_t *rect = lua_touserdata(L, 1);
    lua_pushnumber(L, rect ? Box2_center(rect).x : 0.0f);
    return 1;
}

static int LuaGetRectCenterY(lua_State *L) {
    box2_t *rect = lua_touserdata(L, 1);
    lua_pushnumber(L, rect ? Box2_center(rect).y : 0.0f);
    return 1;
}

static int LuaGetRectMinX(lua_State *L) {
    box2_t *rect = lua_touserdata(L, 1);
    lua_pushnumber(L, rect ? rect->min.x : 0.0f);
    return 1;
}

static int LuaGetRectMinY(lua_State *L) {
    box2_t *rect = lua_touserdata(L, 1);
    lua_pushnumber(L, rect ? rect->min.y : 0.0f);
    return 1;
}

static int LuaGetRectMaxX(lua_State *L) {
    box2_t *rect = lua_touserdata(L, 1);
    lua_pushnumber(L, rect ? rect->max.x : 0.0f);
    return 1;
}

static int LuaGetRectMaxY(lua_State *L) {
    box2_t *rect = lua_touserdata(L, 1);
    lua_pushnumber(L, rect ? rect->max.y : 0.0f);
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

static int LuaTriggerRegisterUnitEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    edict_t *unit = lua_touserdata(L, 2);
    EVENTTYPE type = (EVENTTYPE)luaL_checkinteger(L, 3);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm || !unit)
        return luaL_error(L, "TriggerRegisterUnitEvent: invalid trigger or unit");
    event = G_MakeEvent(type);
    if (!event) return luaL_error(L, "TriggerRegisterUnitEvent: event registry is full");
    G_SetEventSubject(event, unit);
    event->trigger = trigger;
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

/* Report a native the Lua bridge cannot honor yet.  The machine-readable line
 * is what tools/wc3_map_audit.py parses, and the set of names is deduped so one
 * unfinished feature does not flood the log across every frame. */
#define LUA_UNSUPPORTED_MAX 32
static void LuaReportUnsupportedNative(cstring_t name) {
    static cstring_t reported[LUA_UNSUPPORTED_MAX];
    static uint32_t count;
    uint32_t i;

    for (i = 0; i < count; ++i)
        if (!strcmp(reported[i], name)) return;
    if (count < LUA_UNSUPPORTED_MAX) reported[count++] = name;
    fprintf(stderr, "WC3 Lua: %s is not implemented\n", name);
    fprintf(stderr, "WC3_UNSUPPORTED_NATIVE name=%s\n", name);
}

/* Condition/Filter wrap a callback.  The Lua bridge keeps a bare Lua closure and
 * TriggerAddCondition/AddAction accept it directly, so Condition is identity and
 * DestroyBoolExpr only has to accept-and-ignore. */
static int LuaCondition(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    return 1;
}

static int LuaDestroyBoolExpr(lua_State *L) {
    (void)L;
    return 0;
}

static int LuaEnableTrigger(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    if (trigger) trigger->disabled = false;
    return 0;
}

static int LuaDisableTrigger(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    if (trigger) trigger->disabled = true;
    return 0;
}

static int LuaIsTriggerEnabled(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    lua_pushboolean(L, trigger ? !trigger->disabled : false);
    return 1;
}

/* Retain an optional Lua closure as an event filter, mirroring the JASS
 * boolexpr argument.  NULL when the argument is absent or nil. */
static bool LuaRefEventFilter(lua_State *L, int index, event_t *event) {
    if (!event || lua_isnoneornil(L, index)) return true;
    luaL_checktype(L, index, LUA_TFUNCTION);
    event->lua_filter_ref = WC3_LuaRefFunction(level.lua_vm, index);
    if (event->lua_filter_ref == LUA_NOREF || event->lua_filter_ref == LUA_REFNIL) {
        event->inuse = false;
        return false;
    }
    event->lua_filter_vm = level.lua_vm;
    return true;
}

static int LuaTriggerRegisterTimerEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    float timeout = (float)luaL_checknumber(L, 2);
    bool periodic = lua_toboolean(L, 3);
    gtimer_t *timer;
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm)
        return luaL_error(L, "TriggerRegisterTimerEvent: invalid Lua trigger");
    timer = G_AllocJassTimer();
    if (!timer) return luaL_error(L, "TriggerRegisterTimerEvent: timer registry is full");
    G_TimerStart(timer, (uint32_t)(MAX(0.0f, timeout) * 1000.0f), periodic, NULL);
    event = G_MakeEvent(EVENT_GAME_TIMER_EXPIRED);
    if (!event) return luaL_error(L, "TriggerRegisterTimerEvent: event registry is full");
    event->trigger = trigger;
    event->timer = timer;
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

static int LuaTriggerRegisterPlayerEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    player_t *player = lua_touserdata(L, 2);
    EVENTTYPE type = (EVENTTYPE)luaL_checkinteger(L, 3);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm || !player)
        return luaL_error(L, "TriggerRegisterPlayerEvent: invalid trigger or player");
    event = G_MakeEvent(type);
    if (!event) return luaL_error(L, "TriggerRegisterPlayerEvent: event registry is full");
    G_SetPlayerEventSubject(event, PLAYER_ENT(player));
    event->trigger = trigger;
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

static int LuaTriggerRegisterDeathEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    edict_t *widget = lua_touserdata(L, 2);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm || !widget)
        return luaL_error(L, "TriggerRegisterDeathEvent: invalid trigger or widget");
    event = G_MakeEvent(EVENT_UNIT_DEATH);
    if (!event) return luaL_error(L, "TriggerRegisterDeathEvent: event registry is full");
    G_SetEventSubject(event, widget);
    event->trigger = trigger;
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

/* Chat events are not routed by the headless server (there is no human
 * chatter), but the map's diagnostic triggers must still register cleanly.
 * Return a real event handle so the registration graph stays well-formed. */
static int LuaTriggerRegisterPlayerChatEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    player_t *player = lua_touserdata(L, 2);
    event_t *event;

    (void)luaL_checkstring(L, 3);
    (void)lua_toboolean(L, 4);
    if (!trigger || trigger->lua_vm != level.lua_vm || !player)
        return luaL_error(L, "TriggerRegisterPlayerChatEvent: invalid trigger or player");
    event = G_MakeEvent(EVENT_PLAYER_CHAT);
    if (!event) return luaL_error(L, "TriggerRegisterPlayerChatEvent: event registry is full");
    G_SetPlayerEventSubject(event, PLAYER_ENT(player));
    event->trigger = trigger;
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

/* Player state limit events (e.g. LimitGolda at PLAYER_STATE_RESOURCE_GOLD).
 * Shares the EVENT_GAME_STATE_LIMIT shape the JASS native uses, with the
 * player's edict as subject so the owner match fires for that player. */
static int LuaTriggerRegisterPlayerStateEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    player_t *player = lua_touserdata(L, 2);
    uint32_t *state = lua_touserdata(L, 3);
    uint32_t *opcode = lua_touserdata(L, 4);
    float limitval = (float)luaL_checknumber(L, 5);
    uint32_t whichState = state ? *state : (uint32_t)luaL_checkinteger(L, 3);
    uint32_t whichOp = opcode ? *opcode : (uint32_t)luaL_checkinteger(L, 4);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm || !player)
        return luaL_error(L, "TriggerRegisterPlayerStateEvent: invalid trigger or player");
    event = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    if (!event) return luaL_error(L, "TriggerRegisterPlayerStateEvent: event registry is full");
    event->trigger = trigger;
    G_SetPlayerEventSubject(event, PLAYER_ENT(player));
    event->state = whichState;
    event->limitop = whichOp;
    event->limitval = limitval;
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

static int LuaTriggerRegisterUnitStateEvent(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    edict_t *unit = lua_touserdata(L, 2);
    uint32_t *state = lua_touserdata(L, 3);
    uint32_t *opcode = lua_touserdata(L, 4);
    float limitval = (float)luaL_checknumber(L, 5);
    uint32_t whichState = state ? *state : (uint32_t)luaL_checkinteger(L, 3);
    uint32_t whichOp = opcode ? *opcode : (uint32_t)luaL_checkinteger(L, 4);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm || !unit)
        return luaL_error(L, "TriggerRegisterUnitStateEvent: invalid trigger or unit");
    if (whichState != WC3_UNIT_STATE_LIFE || whichOp > WC3_LIMITOP_NOT_EQUAL) {
        LuaReportUnsupportedNative("TriggerRegisterUnitStateEvent");
        lua_pushnil(L);
        return 1;
    }
    event = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    if (!event) return luaL_error(L, "TriggerRegisterUnitStateEvent: event registry is full");
    event->trigger = trigger;
    G_SetEventSubject(event, unit);
    event->state = whichState;
    event->limitop = whichOp;
    event->limitval = limitval;
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

static int LuaTriggerRegisterUnitInRange(lua_State *L) {
    trigger_t *trigger = lua_touserdata(L, 1);
    edict_t *unit = lua_touserdata(L, 2);
    float range = (float)luaL_checknumber(L, 3);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm || !unit)
        return luaL_error(L, "TriggerRegisterUnitInRange: invalid trigger or unit");
    event = G_MakeEvent(EVENT_UNIT_IN_RANGE);
    if (!event) return luaL_error(L, "TriggerRegisterUnitInRange: event registry is full");
    G_SetEventSubject(event, unit);
    event->trigger = trigger;
    event->range = range;
    if (!LuaRefEventFilter(L, 4, event))
        return luaL_error(L, "TriggerRegisterUnitInRange: could not retain filter");
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

static int LuaTriggerRegisterRegionEvent(lua_State *L, EVENTTYPE type, cstring_t name) {
    trigger_t *trigger = lua_touserdata(L, 1);
    handle_t region = lua_touserdata(L, 2);
    region_t *whichRegion = G_RegionFromHandle(region);
    event_t *event;

    if (!trigger || trigger->lua_vm != level.lua_vm || !whichRegion)
        return luaL_error(L, "%s: invalid Lua trigger or region", name);
    event = G_MakeEvent(type);
    if (!event) return luaL_error(L, "%s: event registry is full", name);
    event->trigger = trigger;
    event->region = region;
    if (!LuaRefEventFilter(L, 3, event))
        return luaL_error(L, "%s: could not retain filter", name);
    lua_pushlightuserdata(L, G_EventHandle(event));
    return 1;
}

static int LuaTriggerRegisterEnterRegion(lua_State *L) {
    return LuaTriggerRegisterRegionEvent(L, EVENT_GAME_ENTER_REGION, "TriggerRegisterEnterRegion");
}

static int LuaTriggerRegisterLeaveRegion(lua_State *L) {
    return LuaTriggerRegisterRegionEvent(L, EVENT_GAME_LEAVE_REGION, "TriggerRegisterLeaveRegion");
}

static int LuaRegionAddRect(lua_State *L) {
    region_t *region = G_RegionFromHandle(lua_touserdata(L, 1));
    box2_t *rect = lua_touserdata(L, 2);
    if (!region || !rect) return 0;
    if (region->num_rects >= MAX_REGION_SIZE) {
        fprintf(stderr, "WC3 Lua: RegionAddRect rejected rectangle: MAX_REGION_SIZE (%u) reached\n",
                (unsigned)MAX_REGION_SIZE);
        return 0;
    }
    region->rects[region->num_rects++] = *rect;
    return 0;
}

static int LuaGetTriggerPlayer(lua_State *L) {
    void *unit = WC3_LuaGetTriggerContext(level.lua_vm).unit;
    edict_t *ent = unit;
    player_t *player = NULL;

    if (ent) player = ent->client ? &ent->client->ps : G_GetPlayerByNumber(ent->s.player);
    if (player) lua_pushlightuserdata(L, player); else lua_pushnil(L);
    return 1;
}

static int LuaCreateUnit(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t unitid = (uint32_t)luaL_checkinteger(L, 2);
    vec2_t location = { (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4) };
    float facing = (float)luaL_checknumber(L, 5);
    edict_t *unit;

    if (!player) {
        lua_pushnil(L);
        return 1;
    }
    unit = unit_create(PLAYER_NUM(player), unitid, &location, facing);
    if (unit) lua_pushlightuserdata(L, unit); else lua_pushnil(L);
    return 1;
}

static int LuaCreateUnitAtLoc(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t unitid = (uint32_t)luaL_checkinteger(L, 2);
    vec2_t *location = lua_touserdata(L, 3);
    float facing = (float)luaL_checknumber(L, 4);
    edict_t *unit;

    if (!player || !location) {
        lua_pushnil(L);
        return 1;
    }
    unit = unit_create(PLAYER_NUM(player), unitid, location, facing);
    if (unit) lua_pushlightuserdata(L, unit); else lua_pushnil(L);
    return 1;
}

static int LuaUnitAddAbility(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t abilityId = (uint32_t)luaL_checkinteger(L, 2);
    lua_pushboolean(L, G_ActorAddSkill(unit, abilityId));
    return 1;
}

static int LuaUnitRemoveAbility(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t abilityId = (uint32_t)luaL_checkinteger(L, 2);
    lua_pushboolean(L, G_ActorRemoveSkill(unit, abilityId));
    return 1;
}

static int LuaGetUnitAbilityLevel(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t abilityId = (uint32_t)luaL_checkinteger(L, 2);
    lua_pushinteger(L, unit ? (lua_Integer)G_UnitAbilityLevel(unit, abilityId) : 0);
    return 1;
}

static int LuaSetUnitAbilityLevel(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t abilityId = (uint32_t)luaL_checkinteger(L, 2);
    int32_t level = (int32_t)luaL_checkinteger(L, 3);
    lua_pushinteger(L, unit ? (lua_Integer)G_UnitSetAbilityLevel(unit, abilityId, level) : 0);
    return 1;
}

static int LuaIncUnitAbilityLevel(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t abilityId = (uint32_t)luaL_checkinteger(L, 2);
    uint32_t current;

    if (!unit) { lua_pushinteger(L, 0); return 1; }
    current = G_UnitAbilityLevel(unit, abilityId);
    if (!current) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, (lua_Integer)G_UnitSetAbilityLevel(unit, abilityId, (int32_t)current + 1));
    return 1;
}

static int LuaSetPlayerAbilityAvailable(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t abilityId = (uint32_t)luaL_checkinteger(L, 2);
    bool available = lua_toboolean(L, 3);
    if (player) G_SetPlayerAbilityAvailable(PLAYER_CLIENT(player), abilityId, available);
    return 0;
}

/* IsUnitType/UnitAddType/UnitRemoveType mirror the JASS script_unit_types
 * bitmask plus the authored-data checks in api_unit.h. */
static bool LuaResolveUnitType(edict_t *unit, uint32_t type) {
    if (!unit) return false;
    if (type < 32 && (unit->script_unit_types & (1u << type))) return true;
    if (type == WC3_UNIT_TYPE_STRUCTURE) return G_UnitIsBuilding(unit->class_id);
    if (type == 0) return G_UnitIsHero(unit);
    if (type == 1) return M_IsDead(unit);
    if (type == WC3_UNIT_TYPE_POLYMORPHED) return S_UnitPolymorphed(unit);
    if (type == 23) return G_UnitIsSleeping(unit);
    if (type == 11) return unit->stunned;
    if (type == 3) return (unit->aiflags & AI_FLYING) != 0;
    if (type == 10) return unit->summon_ability != 0;
    if (type == 14) return unit->data.UnitData && WC3_RaceFromString(unit->data.UnitData->race) == RACE_UNDEAD;
    return false;
}

static int LuaIsUnitType(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t *type = lua_touserdata(L, 2);
    uint32_t whichType = type ? *type : (uint32_t)luaL_checkinteger(L, 2);
    lua_pushboolean(L, unit && LuaResolveUnitType(unit, whichType));
    return 1;
}

static int LuaUnitAddType(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t *type = lua_touserdata(L, 2);
    uint32_t whichType = type ? *type : (uint32_t)luaL_checkinteger(L, 2);
    if (!unit || whichType >= 32) { lua_pushboolean(L, false); return 1; }
    unit->script_unit_types |= 1u << whichType;
    lua_pushboolean(L, true);
    return 1;
}

static int LuaUnitRemoveType(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t *type = lua_touserdata(L, 2);
    uint32_t whichType = type ? *type : (uint32_t)luaL_checkinteger(L, 2);
    if (!unit || whichType >= 32) { lua_pushboolean(L, false); return 1; }
    unit->script_unit_types &= ~(1u << whichType);
    lua_pushboolean(L, true);
    return 1;
}

static int LuaUnitDamageTarget(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    edict_t *target = lua_touserdata(L, 2);
    float amount = (float)luaL_checknumber(L, 3);
    if (!unit || !target || amount <= 0.0f || target->invulnerable || M_IsDead(target)) {
        lua_pushboolean(L, false);
        return 1;
    }
    T_Damage(target, unit, (int)amount);
    lua_pushboolean(L, true);
    return 1;
}

static int LuaKillUnit(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    if (unit && unit->inuse && !(unit->svflags & SVF_DEADMONSTER)) unit_die(unit, NULL);
    return 0;
}

static int LuaRemoveUnit(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    if (unit) {
        gameClient_t *owner = G_GetPlayerClientByNumber(unit->s.player);
        if (owner && owner->ps.number == unit->s.player) G_InvalidateCommands(owner);
        G_DeferFreeEdict(unit);
    }
    return 0;
}

static int LuaSetUnitOwner(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    player_t *player = lua_touserdata(L, 2);
    bool change_color = lua_toboolean(L, 3);
    if (unit && player) {
        uint32_t const previous_player = unit->s.player;
        uint32_t const previous_color = G_GetUnitTeamColor(unit);
        G_SetUnitPlayer(unit, PLAYER_NUM(player));
        if (change_color) {
            G_ClearUnitColorOverride(unit);
            G_SetUnitTeamColor(unit, player->color);
        } else if (previous_player != PLAYER_NUM(player)) {
            G_SetUnitColorOverride(unit, previous_color);
        }
    }
    return 0;
}

static int LuaGetUnitX(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    lua_pushnumber(L, unit ? unit->s.origin.x : 0.0f);
    return 1;
}

static int LuaGetUnitY(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    lua_pushnumber(L, unit ? unit->s.origin.y : 0.0f);
    return 1;
}

static int LuaGetUnitTypeId(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    lua_pushinteger(L, unit ? (lua_Integer)unit->class_id : 0);
    return 1;
}

static int LuaGetOwningPlayer(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    player_t *player = unit ? G_GetPlayerByNumber(unit->s.player) : NULL;
    if (player) lua_pushlightuserdata(L, player); else lua_pushnil(L);
    return 1;
}

static int LuaGetUnitState(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    uint32_t *state = lua_touserdata(L, 2);
    uint32_t which = state ? *state : (uint32_t)luaL_checkinteger(L, 2);
    if (!unit) { lua_pushnumber(L, 0.0f); return 1; }
    lua_pushnumber(L, (&unit->health.value)[which]);
    return 1;
}

static int LuaGetUnitUserData(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    lua_pushinteger(L, unit ? unit->user_data : 0);
    return 1;
}

static int LuaSetUnitUserData(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    int32_t value = (int32_t)luaL_checkinteger(L, 2);
    if (unit) unit->user_data = value;
    return 0;
}

static int LuaGetUnitCurrentOrder(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    lua_pushinteger(L, unit ? (lua_Integer)G_GetIssuedOrderId(unit) : 0);
    return 1;
}

static int LuaIssueImmediateOrder(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    cstring_t order = luaL_checkstring(L, 2);
    lua_pushboolean(L, unit_issueimmediateorder(unit, order));
    return 1;
}

static int LuaIssuePointOrder(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    cstring_t order = luaL_checkstring(L, 2);
    vec2_t point = { (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4) };
    lua_pushboolean(L, unit_issueorder(unit, order, &point));
    return 1;
}

static int LuaOrderId(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)G_OrderId(luaL_checkstring(L, 1)));
    return 1;
}

static int LuaGetSpellAbilityId(lua_State *L) {
    lua_pushinteger(L, WC3_LuaGetTriggerContext(level.lua_vm).event_value);
    return 1;
}

static int LuaGetSpellAbilityUnit(lua_State *L) {
    void *unit = WC3_LuaGetTriggerContext(level.lua_vm).unit;
    if (unit) lua_pushlightuserdata(L, unit); else lua_pushnil(L);
    return 1;
}

static int LuaGetSpellTargetUnit(lua_State *L) {
    void *target = WC3_LuaGetTriggerContext(level.lua_vm).source;
    if (target && (((edict_t *)target)->svflags & SVF_MONSTER)) lua_pushlightuserdata(L, target);
    else lua_pushnil(L);
    return 1;
}

static int LuaGetSpellTargetX(lua_State *L) {
    wc3LuaTriggerContext_t ctx = WC3_LuaGetTriggerContext(level.lua_vm);
    lua_pushnumber(L, ctx.has_point ? ctx.point_x : 0.0f);
    return 1;
}

static int LuaGetSpellTargetY(lua_State *L) {
    wc3LuaTriggerContext_t ctx = WC3_LuaGetTriggerContext(level.lua_vm);
    lua_pushnumber(L, ctx.has_point ? ctx.point_y : 0.0f);
    return 1;
}

static int LuaGetSpellTargetLoc(lua_State *L) {
    wc3LuaTriggerContext_t ctx = WC3_LuaGetTriggerContext(level.lua_vm);
    vec2_t *location = lua_newuserdata(L, sizeof(*location));
    location->x = ctx.has_point ? ctx.point_x : 0.0f;
    location->y = ctx.has_point ? ctx.point_y : 0.0f;
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
        .point_x = context ? context->point_x : 0.0f,
        .point_y = context ? context->point_y : 0.0f,
        .has_point = context ? context->has_point : false,
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

static trigger_t *LuaCheckTrigger(lua_State *L, int index, cstring_t native) {
    trigger_t *trigger = lua_touserdata(L, index);
    uint32_t const count = MIN(level.num_triggers, MAX_TRIGGERS);
    FOR_LOOP(i, count) {
        if (&level.triggers[i] != trigger) continue;
        if (level.triggers[i].lua_vm == level.lua_vm) return &level.triggers[i];
        luaL_error(L, "%s: invalid Lua trigger (arg_type=%s registry_slot=%u)",
                   native, lua_typename(L, lua_type(L, index)), (unsigned)i);
        return NULL;
    }
    luaL_error(L, "%s: invalid Lua trigger (arg_type=%s registry_slot=absent)",
               native, lua_typename(L, lua_type(L, index)));
    return NULL;
}

static int LuaTriggerExecute(lua_State *L) {
    trigger_t *trigger = LuaCheckTrigger(L, 1, "TriggerExecute");
    wc3LuaTriggerContext_t context;
    char error[512];
    context = WC3_LuaGetTriggerContext(level.lua_vm);
    if (!G_LuaTriggerExecute(trigger, &context)) {
        strlcpy(error, WC3_LuaErrorMessage(level.lua_vm), sizeof(error));
        WC3_LuaClearError(level.lua_vm);
        return luaL_error(L, "TriggerExecute: %s", error);
    }
    return 0;
}

static int LuaTriggerEvaluate(lua_State *L) {
    trigger_t *trigger = LuaCheckTrigger(L, 1, "TriggerEvaluate");
    wc3LuaTriggerContext_t context;
    bool result;
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

static int LuaGetAttacker(lua_State *L) {
    void *unit = WC3_LuaGetTriggerContext(level.lua_vm).unit;
    if (unit) lua_pushlightuserdata(L, unit); else lua_pushnil(L);
    return 1;
}

/* Damage events publish the damaged unit as the context unit, the attacker as
 * the context source, and the damage as the event value, matching the JASS
 * GetEventDamage/GetEventDamageSource pair in api_misc.h. */
static int LuaGetEventDamage(lua_State *L) {
    lua_pushnumber(L, (lua_Number)WC3_LuaGetTriggerContext(level.lua_vm).event_value);
    return 1;
}

static int LuaGetEventDamageSource(lua_State *L) {
    void *source = WC3_LuaGetTriggerContext(level.lua_vm).source;
    if (source) lua_pushlightuserdata(L, source); else lua_pushnil(L);
    return 1;
}

/* Reforged aliases: the damaged unit is the context unit and the attacker is
 * the context source, so both reuse the same values as the classic pair. */
static int LuaBlzGetEventDamageTarget(lua_State *L) {
    void *unit = WC3_LuaGetTriggerContext(level.lua_vm).unit;
    if (unit) lua_pushlightuserdata(L, unit); else lua_pushnil(L);
    return 1;
}

static int LuaBlzGetEventDamageSource(lua_State *L) {
    return LuaGetEventDamageSource(L);
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

static int LuaGetUnitLoc(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    vec2_t *location = lua_newuserdata(L, sizeof(*location));
    *location = unit ? unit->s.origin2 : MAKE(vec2_t, 0.0f, 0.0f);
    return 1;
}

static int LuaAddWeatherEffect(lua_State *L) {
    box2_t *where = lua_touserdata(L, 1);
    gweather_t *effect = G_WeatherAdd(where, (uint32_t)luaL_checkinteger(L, 2), false);
    if (effect) lua_pushlightuserdata(L, effect);
    else lua_pushnil(L);
    return 1;
}

/* Camera setups own their own authored state; the shared g_camera.h helpers
 * keep the angle/FOV conventions identical to the JASS camera natives. */
static camerasetup_t *LuaCameraSetup(lua_State *L, int index) {
    return lua_isnoneornil(L, index) ? NULL : lua_touserdata(L, index);
}

static CAMERAFIELD LuaCameraField(lua_State *L, int index) {
    uint32_t *field = lua_touserdata(L, index);
    return field ? (CAMERAFIELD)*field : (CAMERAFIELD)luaL_checkinteger(L, index);
}

static int LuaCreateCameraSetup(lua_State *L) {
    camerasetup_t *setup = lua_newuserdata(L, sizeof(*setup));
    gameCamera_t cam;
    memset(setup, 0, sizeof(*setup));
    CL_GameDefaultCamera(&cam);
    setup->viewangles = (vec3_t){ cam.pitch, 0, cam.yaw };
    setup->fov = cam.fov;
    setup->target_distance = cam.distance;
    setup->near_z = cam.znear;
    setup->far_z = cam.zfar;
    return 1;
}

static int LuaCameraSetupSetField(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    if (setup) G_SetCameraStateField(setup, LuaCameraField(L, 2), (float)luaL_checknumber(L, 3));
    return 0;
}

static int LuaCameraSetupGetField(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    lua_pushnumber(L, G_GetCameraStateField(setup, LuaCameraField(L, 2)));
    return 1;
}

static int LuaCameraSetupSetDestPosition(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    if (setup) {
        setup->position.x = (float)luaL_checknumber(L, 2);
        setup->position.y = (float)luaL_checknumber(L, 3);
    }
    return 0;
}

static int LuaCameraSetupGetDestPositionX(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    lua_pushnumber(L, setup ? setup->position.x : 0.0f);
    return 1;
}

static int LuaCameraSetupGetDestPositionY(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    lua_pushnumber(L, setup ? setup->position.y : 0.0f);
    return 1;
}

static int LuaCameraSetupGetDestPositionLoc(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    vec2_t *location = lua_newuserdata(L, sizeof(*location));
    *location = setup ? setup->position : (vec2_t){ 0 };
    return 1;
}

static int LuaCameraSetupApply(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    G_ApplyCameraSetup(setup, lua_toboolean(L, 2), false, 0.0f, 0.0f);
    return 0;
}

static int LuaCameraSetupApplyWithZ(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    G_ApplyCameraSetup(setup, true, true, (float)luaL_checknumber(L, 2), 0.0f);
    return 0;
}

static int LuaCameraSetupApplyForceDuration(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    G_ApplyCameraSetup(setup, lua_toboolean(L, 2), false, 0.0f,
                       (float)luaL_checknumber(L, 3) * 1000.0f);
    return 0;
}

static int LuaCameraSetupApplyForceDurationWithZ(lua_State *L) {
    camerasetup_t *setup = LuaCameraSetup(L, 1);
    G_ApplyCameraSetup(setup, true, true, (float)luaL_checknumber(L, 2),
                       (float)luaL_checknumber(L, 3) * 1000.0f);
    return 0;
}

static int LuaSetCameraField(lua_State *L) {
    G_SetCameraFieldForCurrentPlayer(LuaCameraField(L, 1),
        (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
    return 0;
}

static int LuaAdjustCameraField(lua_State *L) {
    CAMERAFIELD field = LuaCameraField(L, 1);
    float offset = (float)luaL_checknumber(L, 2);
    float duration = (float)luaL_checknumber(L, 3);
    gameClient_t *gc = G_CurrentCameraClient("AdjustCameraField");
    if (gc) {
        camerasetup_t current = G_CameraStateAtTime(gc, G_Time());
        G_SetCameraFieldForCurrentPlayer(field, G_GetCameraStateField(&current, field) + offset, duration);
    }
    return 0;
}

static int LuaGetCameraField(lua_State *L) {
    CAMERAFIELD field = LuaCameraField(L, 1);
    player_t const *p = currentplayer;
    float value = 0.0f;

    /* Mirror the JASS getter: live player state, with angles and FOV in radians. */
    if (p) switch (field) {
        case CAMERA_FIELD_TARGET_DISTANCE: value = p->distance; break;
        case CAMERA_FIELD_FARZ: value = p->zfar; break;
        case CAMERA_FIELD_NEARZ: value = p->znear; break;
        case CAMERA_FIELD_ANGLE_OF_ATTACK:
            value = G_CameraDegreesToRadians(G_CameraPitchToAuthored(p->viewangles.x)); break;
        case CAMERA_FIELD_FIELD_OF_VIEW:
            value = G_CameraDegreesToRadians(G_CameraVerticalToHorizontalFov(p->fov)); break;
        case CAMERA_FIELD_ROLL: value = G_CameraDegreesToRadians(p->viewangles.y); break;
        case CAMERA_FIELD_ROTATION: value = G_CameraDegreesToRadians(G_CameraRotation(p)); break;
        case CAMERA_FIELD_ZOFFSET: value = G_CameraZOffset(p); break;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            break;
    }
    lua_pushnumber(L, value);
    return 1;
}

static int LuaSetCameraTargetController(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    float xoffset = (float)luaL_checknumber(L, 2);
    float yoffset = (float)luaL_checknumber(L, 3);
    bool inherit = lua_toboolean(L, 4);
    gameClient_t *gc = G_CurrentCameraClient("SetCameraTargetController");
    if (!gc) return 0;
    gc->camera.target_controller = unit;
    gc->camera.target_offset = (vec2_t){ xoffset, yoffset };
    gc->camera.target_inherit_orientation = inherit;
    if (unit) {
        vec2_t position = { unit->s.origin2.x + xoffset, unit->s.origin2.y + yoffset };
        gc->camera.old_state = gc->camera.state;
        gc->camera.state.position = G_ClampCameraPosition(gc, &position);
        if (inherit) {
            gc->camera.old_state.viewangles.z = 90.0f - (float)RAD2DEG(unit->s.angle);
            gc->camera.state.viewangles.z = 90.0f - (float)RAD2DEG(unit->s.angle);
        }
        gc->camera.start_time = G_Time();
        gc->camera.end_time = gc->camera.start_time;
    } else {
        gc->camera.target_offset = (vec2_t){ 0, 0 };
        gc->camera.target_inherit_orientation = false;
    }
    return 0;
}

static int LuaSetCameraPosition(lua_State *L) {
    G_SetCameraPositionForCurrentPlayer("SetCameraPosition", (float)luaL_checknumber(L, 1),
        (float)luaL_checknumber(L, 2), false, 0.0f, 0.0f);
    return 0;
}

static int LuaSetCameraQuickPosition(lua_State *L) {
    gameClient_t *gc = G_CurrentCameraClient("SetCameraQuickPosition");
    if (!gc) return 0;
    /* Warcraft's quick position is the spacebar recall point. It must not
     * mutate the current camera target when the script assigns it. */
    gc->camera.quick_position = MAKE(vec2_t, (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2));
    gc->camera.quick_position_set = true;
    return 0;
}

static int LuaPanCameraTo(lua_State *L) {
    G_SetCameraPositionForCurrentPlayer("PanCameraTo", (float)luaL_checknumber(L, 1),
        (float)luaL_checknumber(L, 2), false, 0.0f, 0.0f);
    return 0;
}

static int LuaPanCameraToTimed(lua_State *L) {
    G_SetCameraPositionForCurrentPlayer("PanCameraToTimed", (float)luaL_checknumber(L, 1),
        (float)luaL_checknumber(L, 2), false, 0.0f, (float)luaL_checknumber(L, 3));
    return 0;
}

static int LuaPanCameraToWithZ(lua_State *L) {
    G_SetCameraPositionForCurrentPlayer("PanCameraToWithZ", (float)luaL_checknumber(L, 1),
        (float)luaL_checknumber(L, 2), true, (float)luaL_checknumber(L, 3), 0.0f);
    return 0;
}

static int LuaPanCameraToTimedWithZ(lua_State *L) {
    G_SetCameraPositionForCurrentPlayer("PanCameraToTimedWithZ", (float)luaL_checknumber(L, 1),
        (float)luaL_checknumber(L, 2), true, (float)luaL_checknumber(L, 3),
        (float)luaL_checknumber(L, 4));
    return 0;
}

static int LuaStopCamera(lua_State *L) {
    gameClient_t *gc = G_CurrentCameraClient("StopCamera");
    if (gc) {
        uint32_t now = G_Time();
        gc->camera.state = G_CameraStateAtTime(gc, now);
        gc->camera.old_state = gc->camera.state;
        gc->camera.start_time = gc->camera.end_time = now;
    }
    (void)L;
    return 0;
}

static int LuaResetToGameCamera(lua_State *L) {
    float duration = (float)luaL_checknumber(L, 1);
    gameClient_t *gc = G_CurrentCameraClient("ResetToGameCamera");
    gameCamera_t cam;
    if (!gc) return 0;
    if (G_SkipCutscene()) duration = 0.0f;
    G_ClearCameraTarget(gc, "ResetToGameCamera");
    gc->camera.old_state = gc->camera.state;
    CL_GameDefaultCamera(&cam);
    gc->camera.state.viewangles = (vec3_t){ cam.pitch, 0, cam.yaw };
    gc->camera.state.fov = cam.fov;
    gc->camera.state.target_distance = cam.distance;
    gc->camera.state.z_offset = 0.0f;
    gc->camera.state.near_z = cam.znear;
    gc->camera.state.far_z = cam.zfar;
    gc->camera.start_time = G_Time();
    gc->camera.end_time = gc->camera.start_time + (uint32_t)(duration * 1000.0f);
    return 0;
}

static int LuaGetCameraTargetPositionX(lua_State *L) {
    gameClient_t *gc = G_CurrentCameraClient("GetCameraTargetPositionX");
    lua_pushnumber(L, gc ? gc->ps.vieworigin.x : 0.0f);
    return 1;
}

static int LuaGetCameraTargetPositionY(lua_State *L) {
    gameClient_t *gc = G_CurrentCameraClient("GetCameraTargetPositionY");
    lua_pushnumber(L, gc ? gc->ps.vieworigin.y : 0.0f);
    return 1;
}

static int LuaGetCameraTargetPositionZ(lua_State *L) {
    gameClient_t *gc = G_CurrentCameraClient("GetCameraTargetPositionZ");
    lua_pushnumber(L, gc ? gc->ps.vieworigin.z : 0.0f);
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
    fprintf(stderr, "WC3_UNSUPPORTED_NATIVE name=SetWaterBaseColor\n");
    return 0;
}

static int LuaNewSoundEnvironment(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    fprintf(stderr, "WC3 Lua: NewSoundEnvironment('%s') audio environment is not implemented\n", name);
    fprintf(stderr, "WC3_UNSUPPORTED_NATIVE name=NewSoundEnvironment\n");
    return 0;
}

static int LuaSetAmbientDaySound(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    fprintf(stderr, "WC3 Lua: SetAmbientDaySound('%s') audio ambience is not implemented\n", name);
    fprintf(stderr, "WC3_UNSUPPORTED_NATIVE name=SetAmbientDaySound\n");
    return 0;
}

static int LuaSetAmbientNightSound(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    fprintf(stderr, "WC3 Lua: SetAmbientNightSound('%s') audio ambience is not implemented\n", name);
    fprintf(stderr, "WC3_UNSUPPORTED_NATIVE name=SetAmbientNightSound\n");
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

static int LuaSetPlayerTechResearched(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t tech = (uint32_t)luaL_checkinteger(L, 2);
    int32_t level_value = (int32_t)luaL_checkinteger(L, 3);
    if (player) G_SetPlayerTechResearched(PLAYER_CLIENT(player), tech, level_value);
    return 0;
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

static int LuaGetEnumUnit(lua_State *L) {
    void *unit = WC3_LuaEnumUnit(level.lua_vm);
    if (unit) lua_pushlightuserdata(L, unit); else lua_pushnil(L);
    return 1;
}

static int LuaGetEnumPlayer(lua_State *L) {
    void *player = WC3_LuaEnumPlayer(level.lua_vm);
    if (player) lua_pushlightuserdata(L, player); else lua_pushnil(L);
    return 1;
}

static int LuaGroupAddUnit(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    edict_t *unit = lua_touserdata(L, 2);
    lua_pushboolean(L, G_JassGroupValid(group) && unit && G_AddUnitToGroup(group, unit));
    return 1;
}

static int LuaGroupRemoveUnit(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    edict_t *unit = lua_touserdata(L, 2);
    bool removed = false;

    if (G_JassGroupValid(group) && unit) {
        FOR_LOOP(i, group->num_units) {
            if (group->units[i] == unit) {
                for (uint32_t j = i; j < group->num_units - 1; ++j)
                    group->units[j] = group->units[j + 1];
                group->num_units--;
                removed = true;
                break;
            }
        }
    }
    lua_pushboolean(L, removed);
    return 1;
}

static int LuaGroupClear(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    if (G_JassGroupValid(group)) group->num_units = 0;
    return 0;
}

static int LuaFirstOfGroup(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    if (G_JassGroupValid(group) && group->num_units > 0) lua_pushlightuserdata(L, group->units[0]);
    else lua_pushnil(L);
    return 1;
}

/* Enumerate a group into the callback with each candidate exposed through
 * GetEnumUnit(), mirroring the JASS ForGroup/currentunit pair. */
static int LuaForGroup(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    void *previous;
    uint32_t index;

    if (!G_JassGroupValid(group)) return 0;
    if (!lua_isnoneornil(L, 2)) {
        int reference;
        luaL_checktype(L, 2, LUA_TFUNCTION);
        reference = WC3_LuaRefFunction(level.lua_vm, 2);
        if (reference == LUA_NOREF || reference == LUA_REFNIL)
            return luaL_error(L, "ForGroup: could not retain callback");
        previous = WC3_LuaEnumUnit(level.lua_vm);
        for (index = 0; index < group->num_units; ++index) {
            WC3_LuaSetEnumUnit(level.lua_vm, group->units[index]);
            if (!WC3_LuaCallRef(level.lua_vm, reference)) {
                char error[512];
                strlcpy(error, WC3_LuaErrorMessage(level.lua_vm), sizeof(error));
                WC3_LuaClearError(level.lua_vm);
                WC3_LuaSetEnumUnit(level.lua_vm, previous);
                WC3_LuaUnrefFunction(level.lua_vm, reference);
                return luaL_error(L, "ForGroup: %s", error);
            }
        }
        WC3_LuaSetEnumUnit(level.lua_vm, previous);
        WC3_LuaUnrefFunction(level.lua_vm, reference);
    }
    return 0;
}

static int LuaForForce(lua_State *L) {
    uint32_t *force = lua_touserdata(L, 1);
    void *previous;
    int reference;

    if (!force || lua_isnoneornil(L, 2)) return 0;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    reference = WC3_LuaRefFunction(level.lua_vm, 2);
    if (reference == LUA_NOREF || reference == LUA_REFNIL)
        return luaL_error(L, "ForForce: could not retain callback");
    previous = WC3_LuaEnumPlayer(level.lua_vm);
    FOR_LOOP(i, WC3_MAX_PLAYER_SLOTS) {
        player_t *candidate;
        if (!(*force & (1u << i))) continue;
        candidate = G_GetPlayerByNumber(i);
        if (!candidate) continue;
        WC3_LuaSetEnumPlayer(level.lua_vm, candidate);
        if (!WC3_LuaCallRef(level.lua_vm, reference)) {
            char error[512];
            strlcpy(error, WC3_LuaErrorMessage(level.lua_vm), sizeof(error));
            WC3_LuaClearError(level.lua_vm);
            WC3_LuaSetEnumPlayer(level.lua_vm, previous);
            WC3_LuaUnrefFunction(level.lua_vm, reference);
            return luaL_error(L, "ForForce: %s", error);
        }
    }
    WC3_LuaSetEnumPlayer(level.lua_vm, previous);
    WC3_LuaUnrefFunction(level.lua_vm, reference);
    return 0;
}

static int LuaGroupEnumUnitsInRect(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    box2_t *rect = lua_touserdata(L, 2);
    luaGroupFilter_t context;

    if (!G_JassGroupValid(group) || !rect)
        return luaL_error(L, "GroupEnumUnitsInRect: invalid group or rect");
    context.lua = level.lua_vm;
    context.filter_index = lua_absindex(L, 3);
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (!(ent->svflags & SVF_MONSTER) || G_IsDeferredFree(ent)) continue;
        if (!Box2_containsPoint(rect, &ent->s.origin2)) continue;
        if (!lua_isnoneornil(L, 3) && !LuaEvaluateCandidate(ent, &context)) continue;
        G_AddUnitToGroup(group, ent);
    }
    if (WC3_LuaErrorPending(level.lua_vm)) {
        char error[512];
        strlcpy(error, WC3_LuaErrorMessage(level.lua_vm), sizeof(error));
        WC3_LuaClearError(level.lua_vm);
        return luaL_error(L, "GroupEnumUnitsInRect: %s", error);
    }
    return 0;
}

static int LuaGroupEnumUnitsInRange(lua_State *L) {
    ggroup_t *group = lua_touserdata(L, 1);
    vec2_t center = { (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
    float radius = (float)luaL_checknumber(L, 4);
    luaGroupFilter_t context;

    if (!G_JassGroupValid(group))
        return luaL_error(L, "GroupEnumUnitsInRange: invalid group");
    context.lua = level.lua_vm;
    context.filter_index = lua_absindex(L, 5);
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (!(ent->svflags & SVF_MONSTER) || G_IsDeferredFree(ent)) continue;
        if (Vector2_distance(&ent->s.origin2, &center) > radius) continue;
        if (!lua_isnoneornil(L, 5) && !LuaEvaluateCandidate(ent, &context)) continue;
        G_AddUnitToGroup(group, ent);
    }
    if (WC3_LuaErrorPending(level.lua_vm)) {
        char error[512];
        strlcpy(error, WC3_LuaErrorMessage(level.lua_vm), sizeof(error));
        WC3_LuaClearError(level.lua_vm);
        return luaL_error(L, "GroupEnumUnitsInRange: %s", error);
    }
    return 0;
}

static int LuaDisplayTextToPlayer(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    cstring_t message = luaL_checkstring(L, 4);
    UI_ShowText(PLAYER_ENT(player), &MAKE(vec2_t, x, y), message, -1.0f);
    return 0;
}

static int LuaDisplayTimedTextToPlayer(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float duration = (float)luaL_checknumber(L, 4);
    cstring_t message = luaL_checkstring(L, 5);
    UI_ShowText(PLAYER_ENT(player), &MAKE(vec2_t, x, y), message, duration);
    return 0;
}

static int LuaDisplayTimedTextFromPlayer(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float duration = (float)luaL_checknumber(L, 4);
    cstring_t message = luaL_checkstring(L, 5);
    UI_ShowText(PLAYER_ENT(player), &MAKE(vec2_t, x, y), message, duration);
    return 0;
}

static int LuaI2R(lua_State *L) {
    lua_pushnumber(L, (lua_Number)luaL_checkinteger(L, 1));
    return 1;
}

static int LuaR2I(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)luaL_checknumber(L, 1));
    return 1;
}

static int LuaI2S(lua_State *L) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d", (int)luaL_checkinteger(L, 1));
    lua_pushstring(L, buffer);
    return 1;
}

/* R2S mirrors the JASS api_misc.h twin exactly: the fixed "%f" form (six
 * fractional digits), not a trimmed number, so tooltip text matches JASS. */
static int LuaR2S(lua_State *L) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%f", (double)luaL_checknumber(L, 1));
    lua_pushstring(L, buffer);
    return 1;
}

static int LuaSubString(lua_State *L) {
    cstring_t source = luaL_checkstring(L, 1);
    int32_t start = (int32_t)luaL_checkinteger(L, 2);
    int32_t end = (int32_t)luaL_checkinteger(L, 3);
    int32_t len, n;

    if (!source) { lua_pushstring(L, ""); return 1; }
    len = (int32_t)strlen(source);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) { lua_pushstring(L, ""); return 1; }
    n = end - start;
    lua_pushlstring(L, source + start, (size_t)n);
    return 1;
}

static int LuaStringLength(lua_State *L) {
    size_t length = 0;
    luaL_checklstring(L, 1, &length);
    lua_pushinteger(L, (lua_Integer)length);
    return 1;
}

static int LuaGetRandomInt(lua_State *L) {
    int32_t low = (int32_t)luaL_checkinteger(L, 1);
    int32_t high = (int32_t)luaL_checkinteger(L, 2);
    if (low >= high) { lua_pushinteger(L, low); return 1; }
    lua_pushinteger(L, low + rand() % (high - low + 1));
    return 1;
}

static int LuaGetRandomReal(lua_State *L) {
    float low = (float)luaL_checknumber(L, 1);
    float high = (float)luaL_checknumber(L, 2);
    if (low >= high) { lua_pushnumber(L, low); return 1; }
    lua_pushnumber(L, low + ((float)rand() / (float)RAND_MAX) * (high - low));
    return 1;
}

static int LuaGetLocationX(lua_State *L) {
    vec2_t *location = lua_touserdata(L, 1);
    lua_pushnumber(L, location ? location->x : 0.0f);
    return 1;
}

static int LuaGetLocationY(lua_State *L) {
    vec2_t *location = lua_touserdata(L, 1);
    lua_pushnumber(L, location ? location->y : 0.0f);
    return 1;
}

static int LuaRemoveLocation(lua_State *L) {
    (void)L;
    return 0;
}

static int LuaRemoveRect(lua_State *L) {
    (void)L;
    return 0;
}

static int LuaGetWorldBounds(lua_State *L) {
    box2_t *rect = lua_newuserdata(L, sizeof(*rect));
    *rect = CM_GetWorldBounds();
    return 1;
}

static int LuaSetGameSpeed(lua_State *L) {
    uint32_t *value = lua_touserdata(L, 1);
    if (value) level.setup.speed = *value;
    return 0;
}

static int LuaSetMapFlag(lua_State *L) {
    uint32_t *flag = lua_touserdata(L, 1);
    bool value = lua_toboolean(L, 2);
    if (flag) {
        SET_FLAG(level.setup.map_flags, *flag, value);
    }
    return 0;
}

static int LuaCreateItem(lua_State *L) {
    int32_t itemid = (int32_t)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    ItemData_t const *data;
    edict_t *item;

    if (!itemid) {
        fprintf(stderr, "WC3 Lua: CreateItem refusing empty item ID at (%.1f, %.1f)\n", x, y);
        lua_pushnil(L);
        return 1;
    }
    data = G_ItemData((uint32_t)itemid);
    if (!data || !data->file) {
        fprintf(stderr, "WC3 Lua: CreateItem unresolved item ID 0x%08x at (%.1f, %.1f)\n",
                (uint32_t)itemid, x, y);
        lua_pushnil(L);
        return 1;
    }
    item = SP_SpawnAtLocation((uint32_t)itemid, 0, &MAKE(vec2_t, x, y));
    if (item) lua_pushlightuserdata(L, item); else lua_pushnil(L);
    return 1;
}

static int LuaRemoveItem(lua_State *L) {
    edict_t *item = lua_touserdata(L, 1);
    if (item) G_RemoveItem(item);
    return 0;
}

/* Mirrors api_unit.h UnitItemInSlot: out-of-range or empty slots yield nil, and
 * an occupied slot returns the item edict directly. */
static int LuaUnitItemInSlot(lua_State *L) {
    edict_t *unit = lua_touserdata(L, 1);
    int32_t slot = (int32_t)luaL_checkinteger(L, 2);
    edict_t *item;

    if (!unit || slot < 0 || (uint32_t)slot >= G_InventoryCapacity(unit)) {
        lua_pushnil(L);
        return 1;
    }
    item = unit->inventory[slot];
    if (item) lua_pushlightuserdata(L, item); else lua_pushnil(L);
    return 1;
}

static int LuaAddSpecialEffect(lua_State *L) {
    cstring_t model = luaL_checkstring(L, 1);
    vec2_t where = { (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
    edict_t *effect = G_SpawnModelEffect(model, &where, NULL, NULL, false);
    if (effect) lua_pushlightuserdata(L, effect); else lua_pushnil(L);
    return 1;
}

static int LuaAddSpecialEffectTarget(lua_State *L) {
    cstring_t model = luaL_checkstring(L, 1);
    edict_t *target = lua_touserdata(L, 2);
    cstring_t attach = luaL_checkstring(L, 3);
    edict_t *effect = target ? G_SpawnModelEffect(model, NULL, target, attach, false) : NULL;
    if (effect) lua_pushlightuserdata(L, effect); else lua_pushnil(L);
    return 1;
}

static int LuaDestroyEffect(lua_State *L) {
    G_DestroyEffect(lua_touserdata(L, 1));
    return 0;
}

static int LuaAddLightning(lua_State *L) {
    cstring_t code = luaL_checkstring(L, 1);
    bool check_visibility = lua_toboolean(L, 2);
    vec3_t source = { (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4), 0.0f };
    vec3_t target = { (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6), 0.0f };
    uint32_t effect_id = code && strlen(code) >= 4
        ? MAKEFOURCC(code[0], code[1], code[2], code[3]) : 0;
    gLightning_t *bolt;

    (void)check_visibility;
    bolt = G_LightningAdd(&(lightningAddParams_t){
        .effect_id = effect_id, .source = &source, .target = &target, .color = COLOR32_WHITE,
    });
    if (bolt) lua_pushlightuserdata(L, bolt); else lua_pushnil(L);
    return 1;
}

/* Preload* warm the client's file cache.  OpenRealm has no preloader, so these
 * accept their arguments and do nothing. */
static int LuaNoop(lua_State *L) {
    (void)L;
    return 0;
}

/* Blz* frame/sync/tooltip natives belong to the Reforged UI layer OpenRealm does
 * not implement.  Report each name once and return an empty string: several
 * getters feed string operations such as #tooltip, so a nil would turn a missing
 * UI feature into a script abort instead of a reported gap. */
static int LuaBlzUnsupported(lua_State *L) {
    cstring_t name = luaL_checkstring(L, lua_upvalueindex(1));
    LuaReportUnsupportedNative(name);
    lua_pushstring(L, "");
    return 1;
}

static int LuaBlzGetTriggerSyncData(lua_State *L) {
    LuaReportUnsupportedNative("BlzGetTriggerSyncData");
    lua_pushstring(L, "");
    return 1;
}

/* ExecuteFunc runs a global function by name; the Lua VM calls it directly,
 * matching the JASS coroutine-by-name dispatch for ordinary (non-yielding)
 * bodies. */
static int LuaExecuteFunc(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        fprintf(stderr, "WC3 Lua: ExecuteFunc('%s') is not a function\n", name);
        return 0;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "WC3 Lua: ExecuteFunc('%s') failed: %s\n", name, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return 0;
}

/* TriggerSleepAction yields the current JASS coroutine; the Lua bridge runs a
 * trigger action to completion on the shared main state and has no coroutine
 * scheduler to resume it, so a sleep cannot yet be honored.  Report the gap
 * once and return immediately rather than blocking or aborting the action, so
 * the 201 other natives in the same trigger still execute.  A real yield needs
 * a deferred callback queue driven from G_ScenarioFrame/G_RunEvents. */
static int LuaTriggerSleepAction(lua_State *L) {
    (void)L;
    LuaReportUnsupportedNative("TriggerSleepAction");
    return 0;
}

static int LuaGetLocalPlayer(lua_State *L) {
    if (currentplayer) lua_pushlightuserdata(L, currentplayer); else lua_pushnil(L);
    return 1;
}

static int LuaGetPlayerId(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    lua_pushinteger(L, player ? (lua_Integer)player->number : 0);
    return 1;
}

static int LuaBlzGetPlayerTownHallCount(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    lua_pushinteger(L, player ? G_PlayerTownHallCount(PLAYER_NUM(player)) : 0);
    return 1;
}

static int LuaGetPlayerState(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t *state = lua_touserdata(L, 2);
    uint32_t which = state ? *state : (uint32_t)luaL_checkinteger(L, 2);
    gameClient_t *client = player ? PLAYER_CLIENT(player) : NULL;
    lua_pushinteger(L, client ? (lua_Integer)client->ps.stats[which] : 0);
    return 1;
}

static int LuaGetPlayerColor(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t *color = lua_newuserdata(L, sizeof(*color));
    *color = player ? player->color : 0;
    return 1;
}

static int LuaGetPlayerName(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    gameClient_t *client = player ? PLAYER_CLIENT(player) : NULL;
    cstring_t name = "";

    if (client && client->jass.name[0]) name = client->jass.name;
    else if (player && player->name) name = player->name;
    lua_pushstring(L, name);
    return 1;
}

static int LuaGetPlayerTechCount(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t techid = (uint32_t)luaL_checkinteger(L, 2);
    lua_pushinteger(L, player ? (lua_Integer)G_GetPlayerTechCountValue(PLAYER_CLIENT(player), techid) : 0);
    return 1;
}

static int LuaGetHeroStr(lua_State *L) {
    edict_t *hero = lua_touserdata(L, 1);
    (void)lua_toboolean(L, 2);
    lua_pushinteger(L, hero ? (lua_Integer)hero->hero.str : 0);
    return 1;
}

static int LuaGetHeroAgi(lua_State *L) {
    edict_t *hero = lua_touserdata(L, 1);
    (void)lua_toboolean(L, 2);
    lua_pushinteger(L, hero ? (lua_Integer)hero->hero.agi : 0);
    return 1;
}

static int LuaGetHeroInt(lua_State *L) {
    edict_t *hero = lua_touserdata(L, 1);
    (void)lua_toboolean(L, 2);
    lua_pushinteger(L, hero ? (lua_Integer)hero->hero.intel : 0);
    return 1;
}

static int LuaCreateQuest(lua_State *L) {
    quest_t *quest = G_MakeQuest();
    if (quest) lua_pushlightuserdata(L, quest); else lua_pushnil(L);
    return 1;
}

static int LuaDestroyQuest(lua_State *L) {
    quest_t *quest = lua_touserdata(L, 1);
    if (G_QuestValid(quest)) G_RemoveQuest(quest);
    return 0;
}

/* Quest property setters mirror the JASS api_quest.h twins.  Blizzard.j's
 * CreateQuestBJ sets all six on the quest it creates, so the map's quest
 * journal needs every one of them, not just the first that fails. */
static int LuaQuestSetTitle(lua_State *L) {
    quest_t *quest = lua_touserdata(L, 1);
    cstring_t title = luaL_checkstring(L, 2);
    if (!G_QuestValid(quest)) return 0;
    free(quest->title);
    quest->title = strdup(title);
    return 0;
}

static int LuaQuestSetDescription(lua_State *L) {
    quest_t *quest = lua_touserdata(L, 1);
    cstring_t description = luaL_checkstring(L, 2);
    if (!G_QuestValid(quest)) return 0;
    free(quest->description);
    quest->description = strdup(description);
    return 0;
}

static int LuaQuestSetIconPath(lua_State *L) {
    quest_t *quest = lua_touserdata(L, 1);
    cstring_t iconPath = luaL_checkstring(L, 2);
    if (!G_QuestValid(quest)) return 0;
    free(quest->iconPath);
    quest->iconPath = strdup(iconPath);
    return 0;
}

static int LuaQuestSetRequired(lua_State *L) {
    quest_t *quest = lua_touserdata(L, 1);
    if (G_QuestValid(quest)) quest->required = lua_toboolean(L, 2);
    return 0;
}

static int LuaQuestSetDiscovered(lua_State *L) {
    quest_t *quest = lua_touserdata(L, 1);
    if (G_QuestValid(quest)) quest->discovered = lua_toboolean(L, 2);
    return 0;
}

static int LuaQuestSetEnabled(lua_State *L) {
    quest_t *quest = lua_touserdata(L, 1);
    if (G_QuestValid(quest)) quest->enabled = lua_toboolean(L, 2);
    return 0;
}

static int LuaQuestSetCompleted(lua_State *L) {
    quest_t *quest = lua_touserdata(L, 1);
    if (G_QuestValid(quest)) quest->completed = lua_toboolean(L, 2);
    return 0;
}

static int LuaQuestSetFailed(lua_State *L) {
    quest_t *quest = lua_touserdata(L, 1);
    if (G_QuestValid(quest)) quest->failed = lua_toboolean(L, 2);
    return 0;
}

/* Timer dialogs present the same authoritative engine timer through the HUD
 * countdown; the Lua natives delegate to the same G_* entry points as JASS. */
static int LuaCreateTimerDialog(lua_State *L) {
    gtimer_t *timer = lua_touserdata(L, 1);
    timerdialog_t *dialog = G_AllocTimerDialog(timer);
    if (!dialog) return luaL_error(L, "CreateTimerDialog: timer-dialog registry is full");
    lua_pushlightuserdata(L, dialog);
    return 1;
}

static int LuaDestroyTimerDialog(lua_State *L) {
    G_FreeTimerDialog(lua_touserdata(L, 1));
    return 0;
}

static int LuaTimerDialogSetTitle(lua_State *L) {
    timerdialog_t *dialog = lua_touserdata(L, 1);
    cstring_t title = luaL_checkstring(L, 2);
    if (!dialog || !dialog->inuse) return 0;
    strlcpy(dialog->title, G_LevelString(title), sizeof(dialog->title));
    dialog->title_set = true;
    G_MarkTimerDialogDirty(dialog);
    return 0;
}

static int LuaTimerDialogDisplay(lua_State *L) {
    timerdialog_t *dialog = lua_touserdata(L, 1);
    bool display = lua_toboolean(L, 2);
    G_SetTimerDialogVisible(dialog, currentplayer, display);
    return 0;
}

static int LuaCreateFogModifierRect(lua_State *L) {
    player_t *player = lua_touserdata(L, 1);
    uint32_t *state = lua_touserdata(L, 2);
    box2_t *rect = lua_touserdata(L, 3);
    bool useSharedVision = lua_toboolean(L, 4);
    fogModifier_t *mod = lua_newuserdata(L, sizeof(*mod));

    memset(mod, 0, sizeof(*mod));
    mod->player = player ? PLAYER_NUM(player) : 0;
    mod->state = state ? *state : 0;
    mod->use_shared_vision = useSharedVision;
    if (rect) {
        mod->is_rect = true;
        mod->rect = *rect;
    }
    return 1;
}

static int LuaFogModifierStart(lua_State *L) {
    G_FogModifierStart(lua_touserdata(L, 1));
    return 0;
}

static int LuaFogModifierStop(lua_State *L) {
    G_FogModifierStop(lua_touserdata(L, 1));
    return 0;
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
    WC3_LuaRegisterNative(L, "BlzCreateUnitWithSkin", LuaBlzCreateUnitWithSkin);
    WC3_LuaRegisterNative(L, "SetUnitColor", LuaSetUnitColor);
    WC3_LuaRegisterNative(L, "SetUnitState", LuaSetUnitState);
    WC3_LuaRegisterNative(L, "WaygateSetDestination", LuaWaygateSetDestination);
    WC3_LuaRegisterNative(L, "WaygateActivate", LuaWaygateActivate);
    WC3_LuaRegisterNative(L, "GetRectCenterX", LuaGetRectCenterX);
    WC3_LuaRegisterNative(L, "GetRectCenterY", LuaGetRectCenterY);
    WC3_LuaRegisterNative(L, "GetRectMinX", LuaGetRectMinX);
    WC3_LuaRegisterNative(L, "GetRectMinY", LuaGetRectMinY);
    WC3_LuaRegisterNative(L, "GetRectMaxX", LuaGetRectMaxX);
    WC3_LuaRegisterNative(L, "GetRectMaxY", LuaGetRectMaxY);
    WC3_LuaRegisterNative(L, "DestroyGroup", LuaDestroyGroup);
    WC3_LuaRegisterNative(L, "CreateTrigger", LuaCreateTrigger);
    WC3_LuaRegisterNative(L, "TriggerAddAction", LuaTriggerAddAction);
    WC3_LuaRegisterNative(L, "TriggerAddCondition", LuaTriggerAddCondition);
    WC3_LuaRegisterNative(L, "TriggerRegisterGameStateEvent", LuaTriggerRegisterGameStateEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterTimerExpireEvent", LuaTriggerRegisterTimerExpireEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterPlayerUnitEvent", LuaTriggerRegisterPlayerUnitEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterUnitEvent", LuaTriggerRegisterUnitEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterTimerEvent", LuaTriggerRegisterTimerEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterPlayerEvent", LuaTriggerRegisterPlayerEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterDeathEvent", LuaTriggerRegisterDeathEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterPlayerChatEvent", LuaTriggerRegisterPlayerChatEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterPlayerStateEvent", LuaTriggerRegisterPlayerStateEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterUnitStateEvent", LuaTriggerRegisterUnitStateEvent);
    WC3_LuaRegisterNative(L, "TriggerRegisterUnitInRange", LuaTriggerRegisterUnitInRange);
    WC3_LuaRegisterNative(L, "TriggerRegisterEnterRegion", LuaTriggerRegisterEnterRegion);
    WC3_LuaRegisterNative(L, "TriggerRegisterLeaveRegion", LuaTriggerRegisterLeaveRegion);
    WC3_LuaRegisterNative(L, "RegionAddRect", LuaRegionAddRect);
    WC3_LuaRegisterNative(L, "Condition", LuaCondition);
    WC3_LuaRegisterNative(L, "DestroyBoolExpr", LuaDestroyBoolExpr);
    WC3_LuaRegisterNative(L, "EnableTrigger", LuaEnableTrigger);
    WC3_LuaRegisterNative(L, "DisableTrigger", LuaDisableTrigger);
    WC3_LuaRegisterNative(L, "IsTriggerEnabled", LuaIsTriggerEnabled);
    WC3_LuaRegisterNative(L, "CreateUnit", LuaCreateUnit);
    WC3_LuaRegisterNative(L, "CreateUnitAtLoc", LuaCreateUnitAtLoc);
    WC3_LuaRegisterNative(L, "UnitAddAbility", LuaUnitAddAbility);
    WC3_LuaRegisterNative(L, "UnitRemoveAbility", LuaUnitRemoveAbility);
    WC3_LuaRegisterNative(L, "GetUnitAbilityLevel", LuaGetUnitAbilityLevel);
    WC3_LuaRegisterNative(L, "SetUnitAbilityLevel", LuaSetUnitAbilityLevel);
    WC3_LuaRegisterNative(L, "IncUnitAbilityLevel", LuaIncUnitAbilityLevel);
    WC3_LuaRegisterNative(L, "SetPlayerAbilityAvailable", LuaSetPlayerAbilityAvailable);
    WC3_LuaRegisterNative(L, "IsUnitType", LuaIsUnitType);
    WC3_LuaRegisterNative(L, "UnitAddType", LuaUnitAddType);
    WC3_LuaRegisterNative(L, "UnitRemoveType", LuaUnitRemoveType);
    WC3_LuaRegisterNative(L, "UnitDamageTarget", LuaUnitDamageTarget);
    WC3_LuaRegisterNative(L, "KillUnit", LuaKillUnit);
    WC3_LuaRegisterNative(L, "RemoveUnit", LuaRemoveUnit);
    WC3_LuaRegisterNative(L, "SetUnitOwner", LuaSetUnitOwner);
    WC3_LuaRegisterNative(L, "GetUnitX", LuaGetUnitX);
    WC3_LuaRegisterNative(L, "GetUnitY", LuaGetUnitY);
    WC3_LuaRegisterNative(L, "GetUnitTypeId", LuaGetUnitTypeId);
    WC3_LuaRegisterNative(L, "GetOwningPlayer", LuaGetOwningPlayer);
    WC3_LuaRegisterNative(L, "GetUnitState", LuaGetUnitState);
    WC3_LuaRegisterNative(L, "GetUnitUserData", LuaGetUnitUserData);
    WC3_LuaRegisterNative(L, "SetUnitUserData", LuaSetUnitUserData);
    WC3_LuaRegisterNative(L, "GetUnitCurrentOrder", LuaGetUnitCurrentOrder);
    WC3_LuaRegisterNative(L, "IssueImmediateOrder", LuaIssueImmediateOrder);
    WC3_LuaRegisterNative(L, "IssuePointOrder", LuaIssuePointOrder);
    WC3_LuaRegisterNative(L, "OrderId", LuaOrderId);
    WC3_LuaRegisterNative(L, "GetSpellAbilityId", LuaGetSpellAbilityId);
    WC3_LuaRegisterNative(L, "GetSpellAbilityUnit", LuaGetSpellAbilityUnit);
    WC3_LuaRegisterNative(L, "GetSpellTargetUnit", LuaGetSpellTargetUnit);
    WC3_LuaRegisterNative(L, "GetSpellTargetX", LuaGetSpellTargetX);
    WC3_LuaRegisterNative(L, "GetSpellTargetY", LuaGetSpellTargetY);
    WC3_LuaRegisterNative(L, "GetSpellTargetLoc", LuaGetSpellTargetLoc);
    WC3_LuaRegisterNative(L, "GetTriggerPlayer", LuaGetTriggerPlayer);
    WC3_LuaRegisterNative(L, "GetLocalPlayer", LuaGetLocalPlayer);
    WC3_LuaRegisterNative(L, "GetPlayerId", LuaGetPlayerId);
    WC3_LuaRegisterNative(L, "BlzGetPlayerTownHallCount", LuaBlzGetPlayerTownHallCount);
    WC3_LuaRegisterNative(L, "GetPlayerState", LuaGetPlayerState);
    WC3_LuaRegisterNative(L, "GetPlayerColor", LuaGetPlayerColor);
    WC3_LuaRegisterNative(L, "GetPlayerName", LuaGetPlayerName);
    WC3_LuaRegisterNative(L, "GetPlayerTechCount", LuaGetPlayerTechCount);
    WC3_LuaRegisterNative(L, "GetHeroStr", LuaGetHeroStr);
    WC3_LuaRegisterNative(L, "GetHeroAgi", LuaGetHeroAgi);
    WC3_LuaRegisterNative(L, "GetHeroInt", LuaGetHeroInt);
    WC3_LuaRegisterNative(L, "CreateQuest", LuaCreateQuest);
    WC3_LuaRegisterNative(L, "DestroyQuest", LuaDestroyQuest);
    WC3_LuaRegisterNative(L, "QuestSetTitle", LuaQuestSetTitle);
    WC3_LuaRegisterNative(L, "QuestSetDescription", LuaQuestSetDescription);
    WC3_LuaRegisterNative(L, "QuestSetIconPath", LuaQuestSetIconPath);
    WC3_LuaRegisterNative(L, "QuestSetRequired", LuaQuestSetRequired);
    WC3_LuaRegisterNative(L, "QuestSetDiscovered", LuaQuestSetDiscovered);
    WC3_LuaRegisterNative(L, "QuestSetEnabled", LuaQuestSetEnabled);
    WC3_LuaRegisterNative(L, "QuestSetCompleted", LuaQuestSetCompleted);
    WC3_LuaRegisterNative(L, "QuestSetFailed", LuaQuestSetFailed);
    WC3_LuaRegisterNative(L, "CreateTimerDialog", LuaCreateTimerDialog);
    WC3_LuaRegisterNative(L, "DestroyTimerDialog", LuaDestroyTimerDialog);
    WC3_LuaRegisterNative(L, "TimerDialogSetTitle", LuaTimerDialogSetTitle);
    WC3_LuaRegisterNative(L, "TimerDialogDisplay", LuaTimerDialogDisplay);
    WC3_LuaRegisterNative(L, "CreateFogModifierRect", LuaCreateFogModifierRect);
    WC3_LuaRegisterNative(L, "FogModifierStart", LuaFogModifierStart);
    WC3_LuaRegisterNative(L, "FogModifierStop", LuaFogModifierStop);
    WC3_LuaRegisterNative(L, "TriggerEvaluate", LuaTriggerEvaluate);
    WC3_LuaRegisterNative(L, "TriggerExecute", LuaTriggerExecute);
    WC3_LuaRegisterNative(L, "TriggerSleepAction", LuaTriggerSleepAction);
    WC3_LuaRegisterNative(L, "GetTriggerUnit", LuaGetTriggerUnit);
    WC3_LuaRegisterNative(L, "GetAttacker", LuaGetAttacker);
    WC3_LuaRegisterNative(L, "GetEventDamage", LuaGetEventDamage);
    WC3_LuaRegisterNative(L, "GetEventDamageSource", LuaGetEventDamageSource);
    WC3_LuaRegisterNative(L, "BlzGetEventDamageTarget", LuaBlzGetEventDamageTarget);
    WC3_LuaRegisterNative(L, "BlzGetEventDamageSource", LuaBlzGetEventDamageSource);
    WC3_LuaRegisterNative(L, "GetTriggeringTrigger", LuaGetTriggeringTrigger);
    WC3_LuaRegisterNative(L, "GetExpiredTimer", LuaGetExpiredTimer);
    WC3_LuaRegisterNative(L, "GroupEnumUnitsOfPlayer", LuaGroupEnumUnitsOfPlayer);
    WC3_LuaRegisterNative(L, "BlzGroupGetSize", LuaBlzGroupGetSize);
    WC3_LuaRegisterNative(L, "BlzGroupUnitAt", LuaBlzGroupUnitAt);
    WC3_LuaRegisterNative(L, "Rect", LuaRect);
    WC3_LuaRegisterNative(L, "Location", LuaLocation);
    WC3_LuaRegisterNative(L, "GetUnitLoc", LuaGetUnitLoc);
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
    WC3_LuaRegisterNative(L, "CreateCameraSetup", LuaCreateCameraSetup);
    WC3_LuaRegisterNative(L, "CameraSetupSetField", LuaCameraSetupSetField);
    WC3_LuaRegisterNative(L, "CameraSetupGetField", LuaCameraSetupGetField);
    WC3_LuaRegisterNative(L, "CameraSetupSetDestPosition", LuaCameraSetupSetDestPosition);
    WC3_LuaRegisterNative(L, "CameraSetupGetDestPositionX", LuaCameraSetupGetDestPositionX);
    WC3_LuaRegisterNative(L, "CameraSetupGetDestPositionY", LuaCameraSetupGetDestPositionY);
    WC3_LuaRegisterNative(L, "CameraSetupGetDestPositionLoc", LuaCameraSetupGetDestPositionLoc);
    WC3_LuaRegisterNative(L, "CameraSetupApply", LuaCameraSetupApply);
    WC3_LuaRegisterNative(L, "CameraSetupApplyWithZ", LuaCameraSetupApplyWithZ);
    WC3_LuaRegisterNative(L, "CameraSetupApplyForceDuration", LuaCameraSetupApplyForceDuration);
    WC3_LuaRegisterNative(L, "CameraSetupApplyForceDurationWithZ", LuaCameraSetupApplyForceDurationWithZ);
    WC3_LuaRegisterNative(L, "SetCameraField", LuaSetCameraField);
    WC3_LuaRegisterNative(L, "AdjustCameraField", LuaAdjustCameraField);
    WC3_LuaRegisterNative(L, "GetCameraField", LuaGetCameraField);
    WC3_LuaRegisterNative(L, "SetCameraTargetController", LuaSetCameraTargetController);
    WC3_LuaRegisterNative(L, "SetCameraPosition", LuaSetCameraPosition);
    WC3_LuaRegisterNative(L, "SetCameraQuickPosition", LuaSetCameraQuickPosition);
    WC3_LuaRegisterNative(L, "PanCameraTo", LuaPanCameraTo);
    WC3_LuaRegisterNative(L, "PanCameraToTimed", LuaPanCameraToTimed);
    WC3_LuaRegisterNative(L, "PanCameraToWithZ", LuaPanCameraToWithZ);
    WC3_LuaRegisterNative(L, "PanCameraToTimedWithZ", LuaPanCameraToTimedWithZ);
    WC3_LuaRegisterNative(L, "StopCamera", LuaStopCamera);
    WC3_LuaRegisterNative(L, "ResetToGameCamera", LuaResetToGameCamera);
    WC3_LuaRegisterNative(L, "GetCameraTargetPositionX", LuaGetCameraTargetPositionX);
    WC3_LuaRegisterNative(L, "GetCameraTargetPositionY", LuaGetCameraTargetPositionY);
    WC3_LuaRegisterNative(L, "GetCameraTargetPositionZ", LuaGetCameraTargetPositionZ);
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
    WC3_LuaRegisterNative(L, "SetPlayerTechResearched", LuaSetPlayerTechResearched);
    WC3_LuaRegisterNative(L, "SetPlayerTechMaxAllowed", LuaSetPlayerTechMaxAllowed);
    WC3_LuaRegisterNative(L, "GetPlayerTechMaxAllowed", LuaGetPlayerTechMaxAllowed);
    WC3_LuaRegisterNative(L, "SetAllItemTypeSlots", LuaSetAllItemTypeSlots);
    WC3_LuaRegisterNative(L, "SetAllUnitTypeSlots", LuaSetAllUnitTypeSlots);
    WC3_LuaRegisterNative(L, "SetItemTypeSlots", LuaSetItemTypeSlots);
    WC3_LuaRegisterNative(L, "SetUnitTypeSlots", LuaSetUnitTypeSlots);
    WC3_LuaRegisterNative(L, "Filter", LuaFilter);
    WC3_LuaRegisterNative(L, "GetFilterUnit", LuaGetFilterUnit);
    WC3_LuaRegisterNative(L, "GetFilterPlayer", LuaGetFilterPlayer);
    WC3_LuaRegisterNative(L, "GetEnumUnit", LuaGetEnumUnit);
    WC3_LuaRegisterNative(L, "GetEnumPlayer", LuaGetEnumPlayer);
    WC3_LuaRegisterNative(L, "GroupAddUnit", LuaGroupAddUnit);
    WC3_LuaRegisterNative(L, "GroupRemoveUnit", LuaGroupRemoveUnit);
    WC3_LuaRegisterNative(L, "GroupClear", LuaGroupClear);
    WC3_LuaRegisterNative(L, "FirstOfGroup", LuaFirstOfGroup);
    WC3_LuaRegisterNative(L, "ForGroup", LuaForGroup);
    WC3_LuaRegisterNative(L, "ForForce", LuaForForce);
    WC3_LuaRegisterNative(L, "GroupEnumUnitsInRect", LuaGroupEnumUnitsInRect);
    WC3_LuaRegisterNative(L, "GroupEnumUnitsInRange", LuaGroupEnumUnitsInRange);
    WC3_LuaRegisterNative(L, "DisplayTextToPlayer", LuaDisplayTextToPlayer);
    WC3_LuaRegisterNative(L, "DisplayTimedTextToPlayer", LuaDisplayTimedTextToPlayer);
    WC3_LuaRegisterNative(L, "DisplayTimedTextFromPlayer", LuaDisplayTimedTextFromPlayer);
    WC3_LuaRegisterNative(L, "ExecuteFunc", LuaExecuteFunc);
    WC3_LuaRegisterNative(L, "I2R", LuaI2R);
    WC3_LuaRegisterNative(L, "R2I", LuaR2I);
    WC3_LuaRegisterNative(L, "I2S", LuaI2S);
    WC3_LuaRegisterNative(L, "R2S", LuaR2S);
    WC3_LuaRegisterNative(L, "SubString", LuaSubString);
    WC3_LuaRegisterNative(L, "StringLength", LuaStringLength);
    WC3_LuaRegisterNative(L, "GetRandomInt", LuaGetRandomInt);
    WC3_LuaRegisterNative(L, "GetRandomReal", LuaGetRandomReal);
    WC3_LuaRegisterNative(L, "GetLocationX", LuaGetLocationX);
    WC3_LuaRegisterNative(L, "GetLocationY", LuaGetLocationY);
    WC3_LuaRegisterNative(L, "RemoveLocation", LuaRemoveLocation);
    WC3_LuaRegisterNative(L, "RemoveRect", LuaRemoveRect);
    WC3_LuaRegisterNative(L, "GetWorldBounds", LuaGetWorldBounds);
    WC3_LuaRegisterNative(L, "SetGameSpeed", LuaSetGameSpeed);
    WC3_LuaRegisterNative(L, "SetMapFlag", LuaSetMapFlag);
    WC3_LuaRegisterNative(L, "CreateItem", LuaCreateItem);
    WC3_LuaRegisterNative(L, "RemoveItem", LuaRemoveItem);
    WC3_LuaRegisterNative(L, "UnitItemInSlot", LuaUnitItemInSlot);
    WC3_LuaRegisterNative(L, "AddSpecialEffect", LuaAddSpecialEffect);
    WC3_LuaRegisterNative(L, "AddSpecialEffectTarget", LuaAddSpecialEffectTarget);
    WC3_LuaRegisterNative(L, "DestroyEffect", LuaDestroyEffect);
    WC3_LuaRegisterNative(L, "AddLightning", LuaAddLightning);
    WC3_LuaRegisterNative(L, "Preload", LuaNoop);
    WC3_LuaRegisterNative(L, "PreloadEnd", LuaNoop);
    WC3_LuaRegisterNative(L, "PreloadGenClear", LuaNoop);
    WC3_LuaRegisterNative(L, "PreloadGenStart", LuaNoop);
    WC3_LuaRegisterNative(L, "PreloadGenEnd", LuaNoop);
    WC3_LuaRegisterNative(L, "BlzGetTriggerSyncData", LuaBlzGetTriggerSyncData);

    /* Reforged Blz* UI/sync/tooltip natives: registered as reporting stubs that
     * log WC3_UNSUPPORTED_NATIVE once and yield nil. */
    static cstring_t const blz_stubs[] = {
        "BlzGetFrameByName", "BlzCreateFrame", "BlzCreateFrameByType", "BlzGetOriginFrame",
        "BlzLoadTOCFile", "BlzGetAbilityTooltip", "BlzSetAbilityTooltip",
        "BlzSetAbilityExtendedTooltip", "BlzTriggerRegisterPlayerSyncEvent", "BlzSendSyncData",
        "BlzFrameSetText", "BlzFrameSetSize", "BlzFrameSetPoint", "BlzFrameSetAbsPoint",
        "BlzFrameSetAllPoints", "BlzFrameSetEnable", "BlzFrameSetScale", "BlzFrameSetVisible",
        "BlzFrameSetLevel", "BlzFrameSetTexture", "BlzFrameSetTooltip", "BlzFrameSetTextAlignment",
        "BlzFrameClearAllPoints", "BlzFrameClick", "BlzFrameGetChild",
        "BlzGetTriggerPlayerMouseButton", "BlzGetTriggerPlayerMousePosition",
        "BlzEnableSelections", "BlzGetEventAttackType",
        "BlzSetEventDamage", "BlzGetUnitAbilityCooldown",
        "BlzGetUnitAbilityCooldownRemaining", "BlzGetUnitArmor", "BlzGetUnitBaseDamage",
        "BlzGetUnitBooleanField", "BlzGetUnitMaxHP", "BlzGetUnitMaxMana", "BlzGetUnitRealField",
        "BlzGetUnitStringField", "BlzGetUnitWeaponBooleanField", "BlzGetUnitWeaponIntegerField",
        "BlzIsUnitInvulnerable", "BlzPlaySpecialEffect", "BlzSetSpecialEffectColor",
        "BlzSetSpecialEffectScale", "BlzSetSpecialEffectTime", "BlzSetUnitArmor",
        "BlzSetUnitBaseDamage", "BlzSetUnitIntegerFieldBJ", "BlzSetUnitMaxHP", "BlzSetUnitMaxMana",
        "BlzSetUnitName", "BlzSetUnitRealFieldBJ", "BlzSetUnitStringFieldBJ",
        "BlzSetUnitWeaponBooleanFieldBJ", "BlzSetUnitWeaponIntegerFieldBJ",
        "BlzStartUnitAbilityCooldown", "BlzEndUnitAbilityCooldown", "BlzUnitCancelTimedLife",
        "BlzUnitDisableAbility", "BlzUnitHideAbility", "BlzUnitInterruptAttack",
    };
    for (uint32_t i = 0; i < sizeof(blz_stubs) / sizeof(blz_stubs[0]); ++i) {
        WC3_LuaRegisterNativeNamed(L, blz_stubs[i], LuaBlzUnsupported);
    }

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
