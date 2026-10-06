#ifdef BZ_TESTS
/*
 * t_mapscript.c — DotA-style scripts\war3map.j lookup and null mapscript safety.
 *
 * CM_ReadMapScript must prefer war3map.j, then scripts\war3map.j. Missing both
 * leaves mapscript NULL without inventing an empty buffer. jass_dobuffer must
 * refuse NULL instead of crashing in jass_remove_comments.
 *
 * A W3I map that declares Lua (scriptType 1, format 28+) must load
 * war3map.lua and must not fall back to war3map.j.
 */
#include "test.h"
#include "../g_local.h"
#include "games/warcraft-3/common/terrain.h"
#include "jass/jass.h"

#include <stdio.h>
#include <unistd.h>

extern jassModule_t jass_funcs[];
extern bool jass_transpile_to_lua(jass_t *j, cstring_t source, string_t *lua_source);
void CM_ReadMapScript(handle_t archive);
void CM_ReadUnits(handle_t archive);
void CM_ReadAbilities(handle_t archive);
void CM_SetupTestWorldBounds(box2_t const *);
void CM_SetupTestPathmap(uint32_t, uint32_t, uint8_t const *);
void G_RegisterLuaMapConfigNatives(wc3Lua_t *lua);
void G_RegisterLuaMapRuntimeNatives(wc3Lua_t *lua);

static cstring_t const kMinimalMapScript =
    "function config takes nothing returns nothing\n"
    "endfunction\n"
    "function main takes nothing returns nothing\n"
    "endfunction\n";

static void mapscript_ignore_error(cstring_t message) { (void)message; }

static int mapscript_test_image_index(cstring_t path) {
    return path && *path ? 37 : 0;
}

TEST(wc3_mapscript, blz_frame_set_texture_sets_backdrop_art) {
    wc3Lua_t *lua = WC3_LuaNewState();
    frameDef_t *frame = NULL;
    frameDef_t saved = {0};
    int (*old_image_index)(cstring_t) = gi.ImageIndex;

    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (i && !frames[i].inuse) {
            frame = frames + i;
            saved = *frame;
            break;
        }
    }
    if (frame) frame = UI_Spawn(FT_BACKDROP, NULL);

    T_NOT_NULL(lua);
    T_NOT_NULL(frame);
    if (!lua || !frame) {
        if (lua) WC3_LuaClose(lua);
        gi.ImageIndex = old_image_index;
        return;
    }
    snprintf(frame->Name, sizeof(frame->Name), "LuaFrameBackdropTextureTest");
    gi.ImageIndex = mapscript_test_image_index;
    G_RegisterLuaMapRuntimeNatives(lua);
    T_ASSERT(WC3_LuaLoadBuffer(lua,
        "local frame = BlzGetFrameByName('LuaFrameBackdropTextureTest', 0)\n"
        "BlzFrameSetTexture(frame, 'UI\\\\CommandCard.dds', 0, true)\n",
        "blz-frame-backdrop-texture.lua"));
    T_EQ(frame->Backdrop.Background, 37);
    T_ASSERT(frame->Backdrop.BlendAll);
    T_EQ(frame->Texture.Image, 0);

    *frame = saved;
    gi.ImageIndex = old_image_index;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, blz_frame_native_updates_named_hud_frame) {
    wc3Lua_t *lua = WC3_LuaNewState();
    frameDef_t *frame = NULL;
    frameDef_t saved = {0};

    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (i && !frames[i].inuse) {
            frame = frames + i;
            saved = *frame;
            break;
        }
    }
    if (frame) frame = UI_Spawn(FT_FRAME, NULL);

    T_NOT_NULL(lua);
    T_NOT_NULL(frame);
    if (!lua || !frame) {
        if (lua) WC3_LuaClose(lua);
        return;
    }
    snprintf(frame->Name, sizeof(frame->Name), "LuaFrameNativeTest");
    G_RegisterLuaMapRuntimeNatives(lua);
    T_ASSERT(WC3_LuaLoadBuffer(lua,
        "local frame = BlzGetFrameByName('LuaFrameNativeTest', 0)\n"
        "assert(frame ~= nil)\n"
        "BlzFrameSetSize(frame, 0.25, 0.125)\n"
        "BlzFrameSetText(frame, 'parity')\n"
        "BlzFrameSetAbsPoint(frame, 6, 0.595, 0.005)\n"
        "BlzFrameSetVisible(frame, false)\n",
        "blz-frame-native.lua"));
    T_FEQ(frame->Width, 0.25f, 0.0001f);
    T_FEQ(frame->Height, 0.125f, 0.0001f);
    T_STREQ(frame->Text, "parity");
    T_FEQ(frame->Points.x[FPP_MIN].offset, 0.595f, 0.0001f);
    T_FEQ(frame->Points.y[FPP_MAX].offset, 0.005f, 0.0001f);
    T_ASSERT(frame->hidden);
    *frame = saved;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, blz_frame_create_by_type_attaches_context_named_child) {
    wc3Lua_t *lua = WC3_LuaNewState();
    frameDef_t *slots[5] = {0};
    frameDef_t saved[5] = {0};
    frameDef_t *created = NULL;
    uint32_t slot_count = 0;

    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (!i || frames[i].inuse) continue;
        slots[slot_count] = frames + i;
        saved[slot_count] = frames[i];
        if (++slot_count == sizeof(slots) / sizeof(slots[0])) break;
    }
    T_NOT_NULL(lua);
    T_EQ(slot_count, sizeof(slots) / sizeof(slots[0]));
    if (!lua || slot_count != sizeof(slots) / sizeof(slots[0])) {
        if (lua) WC3_LuaClose(lua);
        return;
    }
    frameDef_t *owner = UI_Spawn(FT_FRAME, NULL);
    snprintf(owner->Name, sizeof(owner->Name), "LuaFrameOwnerNativeTest");
    frameDef_t *template = UI_Spawn(FT_BACKDROP, NULL);
    snprintf(template->Name, sizeof(template->Name), "LuaFrameTemplateNativeTest");
    template->Width = 0.20f;
    template->Height = 0.10f;
    frameDef_t *template_child = UI_Spawn(FT_TEXT, template);
    snprintf(template_child->Name, sizeof(template_child->Name), "LuaFrameTemplateChildNativeTest");
    G_RegisterLuaMapRuntimeNatives(lua);
    T_ASSERT(WC3_LuaLoadBuffer(lua,
        "local owner = BlzGetFrameByName('LuaFrameOwnerNativeTest', 0)\n"
        "local frame = BlzCreateFrameByType('BACKDROP', 'LuaFrameCreatedNativeTest', owner, 'LuaFrameTemplateNativeTest', 0)\n"
        "assert(frame ~= nil)\n"
        "assert(BlzGetFrameByName('LuaFrameCreatedNativeTest', 0) == frame)\n"
        "assert(BlzGetFrameByName('LuaFrameTemplateChildNativeTest', 0) == BlzFrameGetChild(frame, 0))\n"
        "BlzFrameSetSize(frame, 0.20, 0.10)\n"
        "BlzFrameSetScale(frame, 0.5)\n",
        "blz-frame-create.lua"));
    created = UI_FindFrame("LuaFrameCreatedNativeTest");
    T_NOT_NULL(created);
    if (created) {
        T_EQ(created->Parent, owner);
        T_EQ(created->Type, FT_BACKDROP);
        T_FEQ(created->Width * created->Scale, 0.10f, 0.0001f);
        T_FEQ(created->Height * created->Scale, 0.05f, 0.0001f);
        T_NOT_NULL(UI_FindChildFrame(created, "LuaFrameTemplateChildNativeTest"));
    }
    FOR_LOOP(i, sizeof(slots) / sizeof(slots[0])) *slots[i] = saved[i];
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_main_failure_is_latched_once) {
    mapInfo_t info = { .scriptKind = WC3_SCRIPT_LUA };
    wc3Lua_t *lua = WC3_LuaNewState();
    mapInfo_t const *previous_info = level.mapinfo;
    wc3Lua_t *previous_lua = level.lua_vm;
    bool previous_started = level.scriptsStarted;
    PATHSTR previous_path;

    strlcpy(previous_path, level.map_path, sizeof(previous_path));

    T_NOT_NULL(lua);
    if (!lua) return;
    G_RegisterLuaMapConfigNatives(lua);
    T_ASSERT(WC3_LuaLoadBuffer(lua, "value = 1\n", "failure-test.lua"));
    level.mapinfo = &info;
    level.lua_vm = lua;
    strlcpy(level.map_path, "failure-test.w3x", sizeof(level.map_path));
    level.scriptsStarted = false;
    G_StartScripts();
    T_ASSERT(level.scriptsStarted);
    G_StartScripts();
    T_ASSERT(level.scriptsStarted);
    level.lua_vm = previous_lua;
    level.mapinfo = previous_info;
    level.scriptsStarted = previous_started;
    strlcpy(level.map_path, previous_path, sizeof(level.map_path));
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, jass_to_lua_preserves_globals_and_functions) {
    static cstring_t const source =
        "globals\n"
        "integer bj_count = 0\n"
        "integer array bj_slots\n"
        "string bj_label = \"start\"\n"
        "integer bj_rawcode = 'hfoo'\n"
        "endglobals\n"
        "function InitBlizzard takes nothing returns nothing\n"
        "local integer i = 0\n"
        "loop\n"
        "set i = i + 1\n"
        "exitwhen i >= 3\n"
        "endloop\n"
        "set bj_count = i\n"
        "set bj_slots[2] = i\n"
        "set bj_label = bj_label + \"!\"\n"
        "endfunction\n"
        "function GetCount takes nothing returns integer\n"
        "return bj_count\n"
        "endfunction\n"
        "function GetEndMinusOne takes integer end returns integer\n"
        "return end - 1\n"
        "endfunction\n";
    jass_t *j;
    string_t lua_source = NULL;
    wc3Lua_t *lua = WC3_LuaNewState();
    double count = -1;

    jass_sethost(&MAKE(jassHost_t,
        .MemAlloc = gi.MemAlloc,
        .MemFree = gi.MemFree,
        .GetTime = gi.GetTime,
        .ReadFile = gi.ReadFile,
        .natives = jass_funcs,
        .GetPlayerByNumber = G_GetPlayerByNumber,
        .TimerCoroutineValid = G_TimerCoroutineValid,
        .RuntimeError = mapscript_ignore_error,
        .SaveHandle = G_SaveJassHandle,
        .LoadHandle = G_LoadJassHandle,
        .VariableChanged = G_JassVariableChanged,
    ));
    j = jass_newstate();
    T_NOT_NULL(j);
    T_NOT_NULL(lua);
    if (!j || !lua) goto cleanup;

    T_ASSERT(jass_transpile_to_lua(j, source, &lua_source));
    T_NOT_NULL(lua_source);
    if (lua_source) {
        T_ASSERT(strstr(lua_source, "function InitBlizzard()") != NULL);
    }
    T_ASSERT(G_LoadLuaMapJass(lua, j, source, "Blizzard.j"));
    T_ASSERT(WC3_LuaCall(lua, "InitBlizzard"));
    T_ASSERT(WC3_LuaCallNumber(lua, "GetCount", &count));
    T_EQ(count, 3);
    T_ASSERT(WC3_LuaLoadBuffer(lua,
        "assert(bj_slots[2] == 3 and bj_label == 'start!' and bj_rawcode == FourCC('hfoo'))\n"
        "assert(GetEndMinusOne(7) == 6)\n",
        "verify-transpiled-globals.lua"));
    WC3_LuaRegisterInteger(lua, "expected_rawcode", MAKEFOURCC('h','f','o','o'));
    T_ASSERT(WC3_LuaLoadBuffer(lua,
        "assert(bj_rawcode == expected_rawcode)\n",
        "verify-transpiled-fourcc-byte-order.lua"));

cleanup:
    free(lua_source);
    if (lua) WC3_LuaClose(lua);
    if (j) jass_close(j);
}

TEST(wc3_mapscript, lua_runtime_registers_create_group_handle) {
    wc3Lua_t *lua = WC3_LuaNewState();
    uint32_t groups_before = level.num_groups;
    uint32_t regions_before = level.num_regions;
    uint32_t region_slot = MAX_REGIONS;
    region_t region_before = {0};

    FOR_LOOP(i, MAX_REGIONS) {
        if (!level.regions[i].inuse && !level.regions[i].exhausted) {
            region_slot = i;
            region_before = level.regions[i];
            break;
        }
    }

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "MapGroup = CreateGroup()\nMapRect = Rect(1, 2, 3, 4)\nMapLocation = Location(5, 6)\n"
        "MapPlayer = Player(PLAYER_NEUTRAL_PASSIVE)\nMapForce = CreateForce()\n"
        "SetPlayerStartLocation(MapPlayer, 7)\n"
        "assert(GetPlayerStartLocation(MapPlayer) == 7)\n"
        "SetStartLocPrioCount(2, 1)\n"
        "SetStartLocPrio(2, 0, 7, MAP_LOC_PRIO_HIGH)\n"
        "SetPlayerColor(MapPlayer, ConvertPlayerColor(8))\n"
        "SetPlayerTeam(MapPlayer, 2)\n"
        "SetPlayerRacePreference(MapPlayer, RACE_PREF_HUMAN)\n"
        "SetPlayerRaceSelectable(MapPlayer, false)\n"
        "SetPlayerController(MapPlayer, MAP_CONTROL_USER)\n"
        "ForceAddPlayer(MapForce, MapPlayer)\nassert(IsPlayerInForce(MapPlayer, MapForce))\n"
        "MapRegion = CreateRegion()\n"
        "GroupEnumUnitsOfPlayer(MapGroup, MapPlayer, nil)\n"
        "assert(BlzGroupGetSize(MapGroup) == 0)\n"
        "assert(BlzGroupUnitAt(MapGroup, 0) == nil)\n"
        "DestroyGroup(MapGroup)\n"
        "assert(StringHash('case') == 1865766789)\n"
        "assert(StringHash('CASE') == 1865766789)\n"
        "assert(StringHash('path/to') == -1197512958)\n",
        "runtime-handles-test.lua"));
    T_EQ(level.num_groups, groups_before + 1);
    T_EQ(level.setup.start_prio[2].count, 1);
    T_EQ(level.setup.start_prio[2].slots[0].location, 7);
    T_EQ(level.setup.start_prio[2].slots[0].priority, 1);
    if (level.num_groups > groups_before)
        T_ASSERT(!G_JassGroupValid(level.groups[groups_before]));
    T_ASSERT(region_slot < MAX_REGIONS);
    if (region_slot < MAX_REGIONS) {
        T_ASSERT(level.num_regions >= region_slot + 1);
        T_ASSERT(level.regions[region_slot].inuse);
        level.regions[region_slot] = region_before;
        level.num_regions = regions_before;
    }
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_runtime_registers_neutral_passive_native) {
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "assert(GetPlayerNeutralPassive() == PLAYER_NEUTRAL_PASSIVE)\n"
        "assert(GetPlayerNeutralAggressive() == PLAYER_NEUTRAL_AGGRESSIVE)\n"
        "assert(GetBJMaxPlayers() > 0)\n"
        "assert(GetBJPlayerNeutralVictim() == PLAYER_NEUTRAL_VICTIM)\n"
        "assert(GetBJPlayerNeutralExtra() == PLAYER_NEUTRAL_EXTRA)\n"
        "assert(GetBJMaxPlayerSlots() == 12)\n"
        "assert(ConvertAnimType(4) == 4 and ConvertSubAnimType(12) == 12)\n"
        "assert(ConvertEquipmentType(7) == 7 and ConvertItemTag(3) == 3)\n"
        "assert(ConvertLoadoutSlot(2) == 2)\n"
        "assert(ConvertOriginFrameType(1) == 1 and ConvertFramePointType(5) == 5)\n"
        "assert(ConvertTextAlignType(2) == 2 and ConvertFrameEventType(3) == 3)\n"
        "assert(ConvertOsKeyType(13) == 13)\n"
        "assert(ConvertAbilityBooleanField(1) == 1 and ConvertAbilityBooleanLevelArrayField(2) == 2)\n"
        "assert(ConvertAbilityBooleanLevelField(3) == 3 and ConvertAbilityIntegerField(4) == 4)\n"
        "assert(ConvertAbilityIntegerLevelArrayField(5) == 5 and ConvertAbilityIntegerLevelField(6) == 6)\n"
        "assert(ConvertAbilityRealField(7) == 7 and ConvertAbilityRealLevelArrayField(8) == 8)\n"
        "assert(ConvertAbilityRealLevelField(9) == 9 and ConvertAbilityStringField(10) == 10)\n"
        "assert(ConvertAbilityStringLevelArrayField(11) == 11 and ConvertAbilityStringLevelField(12) == 12)\n"
        "assert(ConvertArmorType(13) == 13 and ConvertDefenseType(14) == 14 and ConvertHeroAttribute(15) == 15)\n"
        "assert(ConvertItemBooleanField(16) == 16 and ConvertItemIntegerField(17) == 17)\n"
        "assert(ConvertItemRealField(18) == 18 and ConvertItemStringField(19) == 19)\n"
        "assert(ConvertMoveType(20) == 20 and ConvertPathingFlag(21) == 21 and ConvertRegenType(22) == 22)\n"
        "assert(ConvertTargetFlag(23) == 23 and ConvertUnitBooleanField(24) == 24)\n"
        "assert(ConvertUnitCategory(25) == 25 and ConvertUnitIntegerField(26) == 26)\n"
        "assert(ConvertUnitRealField(27) == 27 and ConvertUnitStringField(28) == 28)\n"
        "assert(ConvertUnitWeaponBooleanField(29) == 29 and ConvertUnitWeaponIntegerField(30) == 30)\n"
        "assert(ConvertUnitWeaponRealField(31) == 31 and ConvertUnitWeaponStringField(32) == 32)\n",
        "neutral-passive-native-test.lua"));
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_runtime_registers_map_ai_natives) {
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "assert(type(UnitSetConstructionProgress) == 'function')\n"
        "assert(type(GetUnitName) == 'function' and type(UnitShareVision) == 'function')\n"
        "assert(type(GetHandleId) == 'function' and GetHandleId(Player(0)) == GetHandleId(Player(0)))\n"
        "assert(type(ForceEnumAllies) == 'function' and type(DestroyFogModifier) == 'function')\n"
        "assert(type(IssuePointOrderLoc) == 'function' and type(IssueImmediateOrderById) == 'function')\n"
        "assert(type(MathRound) == 'function' and MathRound(-1.5) == -1 and MathRound(1.5) == 2)\n"
        "assert(type(IsUnitIdType) == 'function' and type(IsPlayerEnemy) == 'function')\n"
        "assert(type(GroupPointOrder) == 'function' and type(IssueBuildOrderById) == 'function')\n"
        "assert(type(WaygateIsActive) == 'function')\n"
        "assert(type(AddUnitAnimationProperties) == 'function' and type(IsUnitHidden) == 'function' and type(ShowUnit) == 'function')\n"
        "assert(type(GetConstructingStructure) == 'function' and type(GetHeroLevel) == 'function' and type(GetPlayerAlliance) == 'function')\n"
        "assert(type(GetRevivableUnit) == 'function' and type(GetSoldUnit) == 'function' and type(GetTrainedUnit) == 'function')\n"
        "assert(type(GetUnitDefaultMoveSpeed) == 'function' and type(GetUnitFoodMade) == 'function' and type(GetUnitLevel) == 'function')\n"
        "assert(type(GroupEnumUnitsInRangeOfLocCounted) == 'function' and type(GroupEnumUnitsSelected) == 'function')\n"
        "assert(type(IsPlayerAlly) == 'function' and type(IssueNeutralImmediateOrderById) == 'function' and type(IssueTargetOrder) == 'function')\n"
        "assert(type(MoveRectTo) == 'function' and type(OrderId2String) == 'function' and type(ReviveHero) == 'function')\n"
        "assert(type(S2I) == 'function' and type(SelectHeroSkill) == 'function' and type(SetUnitMoveSpeed) == 'function')\n"
        "assert(type(WaygateGetDestinationX) == 'function' and type(WaygateGetDestinationY) == 'function')\n"
        "assert(type(GetLearnedSkill) == 'function' and type(GetLearnedSkillLevel) == 'function')\n"
        "assert(math.abs(Cos(0)-1) < 0.000001 and math.abs(Sin(0)) < 0.000001 and math.abs(Atan(1)-0.7853981633974483) < 0.000001)\n"
        "assert(math.abs(Atan2(1, 0)-1.5707963267948966) < 0.000001)\n"
        "assert(not IsUnitIdType(FourCC('hfoo'), 15))\n"
        "local force = CreateForce(); ForceEnumAllies(force, Player(0), nil); DestroyForce(force)\n"
        "local mb = CreateMultiboard()\n"
        "MultiboardSetRowCount(mb, 2); MultiboardSetColumnCount(mb, 2)\n"
        "local item = MultiboardGetItem(mb, 1, 1)\n"
        "MultiboardSetItemValue(item, 'AI'); MultiboardSetItemWidth(item, 0.12)\n"
        "MultiboardReleaseItem(item); MultiboardSetItemsStyle(mb, true, false)\n"
        "DestroyMultiboard(mb)\n",
        "construction-progress-test.lua"));
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_trigger_context_exposes_learned_skill_and_event_id) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    wc3LuaTriggerContext_t context = {
        .event_value = MAKEFOURCC('A','H','h','b'),
        .event_id = EVENT_UNIT_HERO_SKILL,
    };

    T_NOT_NULL(lua);
    if (!lua) return;
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function CheckSkillEvent()\n"
        "  assert(GetLearnedSkill() == FourCC('AHhb'), tostring(GetLearnedSkill()))\n"
        "  assert(GetLearnedSkillLevel() == 0)\n"
        "  assert(GetTriggerEventId() == 79)\n"
        "end\n",
        "lua-skill-event-context-test.lua"));
    WC3_LuaSetTriggerContext(lua, &context);
    T_ASSERT(WC3_LuaCall(lua, "CheckSkillEvent"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_set_player_alliance_uses_shared_state) {
    player_t *source = G_GetPlayerByNumber(0);
    player_t *other = G_GetPlayerByNumber(1);
    bool before = G_GetPlayerAlliance(source, other, ALLIANCE_PASSIVE);
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunAllianceTest()\n"
        "SetPlayerAlliance(Player(0), Player(1), ConvertAllianceType(0), true)\n"
        "end\n",
        "set-player-alliance-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunAllianceTest"));
    T_ASSERT(G_GetPlayerAlliance(source, other, ALLIANCE_PASSIVE));
    G_SetPlayerAlliance(source, other, ALLIANCE_PASSIVE, before);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_set_player_state_uses_shared_state) {
    player_t *player = G_GetPlayerByNumber(0);
    gameClient_t *client = PLAYER_CLIENT(player);
    uint16_t before = client->ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunStateTest()\n"
        "SetPlayerState(Player(0), ConvertPlayerState(1), 1234)\n"
        "end\n",
        "set-player-state-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunStateTest"));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1234);
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = before;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_group_filter_uses_current_candidate) {
    uint32_t slot = globals.num_edicts;
    uint32_t old_num_edicts = globals.num_edicts;
    edict_t previous = {0};
    wc3Lua_t *lua;
    wc3Lua_t *previous_lua = level.lua_vm;

    T_ASSERT(slot < MAX_ENTITIES);
    if (slot >= MAX_ENTITIES) return;
    previous = globals.edicts[slot];
    globals.edicts[slot] = (edict_t){ .inuse = true, .svflags = SVF_MONSTER,
        .s = { .player = 0 } };
    globals.num_edicts = slot + 1;
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) goto cleanup;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunFilterTest()\n"
        "local group = CreateGroup()\n"
        "GroupEnumUnitsOfPlayer(group, Player(0), Filter(function() return GetFilterUnit() ~= nil end))\n"
        "assert(BlzGroupGetSize(group) > 0)\n"
        "DestroyGroup(group)\n"
        "end\n",
        "group-filter-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunFilterTest"));
cleanup:
    level.lua_vm = previous_lua;
    if (lua) WC3_LuaClose(lua);
    globals.edicts[slot] = previous;
    globals.num_edicts = old_num_edicts;
}

TEST(wc3_mapscript, lua_force_filter_uses_current_player) {
    wc3Lua_t *lua = WC3_LuaNewState();
    wc3Lua_t *previous_lua = level.lua_vm;

    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunForceFilterTest()\n"
        "local force = CreateForce()\n"
        "ForceEnumPlayers(force, Filter(function() return GetFilterPlayer() == Player(0) end))\n"
        "assert(IsPlayerInForce(Player(0), force))\n"
        "assert(not IsPlayerInForce(Player(1), force))\n"
        "DestroyForce(force)\n"
        "end\n",
        "force-filter-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunForceFilterTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_get_game_speed_reads_shared_setup) {
    uint32_t previous_speed = level.setup.speed;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    level.setup.speed = 2;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunSpeedTest() assert(GetGameSpeed() == 2) end\n",
        "game-speed-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunSpeedTest"));
    level.setup.speed = previous_speed;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_fog_state_reads_shared_player_flags) {
    uint32_t previous_flags = game.clients[0].ps.rdflags;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    game.clients[0].ps.rdflags |= RDF_NOFOG | RDF_NOFOGMASK;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunFogTest() assert(not IsFogEnabled() and not IsFogMaskEnabled()) end\n",
        "fog-state-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunFogTest"));
    game.clients[0].ps.rdflags = previous_flags;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_player_controller_reads_shared_state) {
    player_t *player = G_GetPlayerByNumber(0);
    uint32_t previous_controller = PLAYER_CLIENT(player)->jass.controller;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunControllerTest()\n"
        "SetPlayerController(Player(0), ConvertMapControl(2))\n"
        "assert(GetPlayerController(Player(0)) == 2)\n"
        "end\n",
        "player-controller-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunControllerTest"));
    PLAYER_CLIENT(player)->jass.controller = previous_controller;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_player_controller_respects_lobby_computer_slot) {
    player_t *player = G_GetPlayerByNumber(0);
    gameClient_t *client = PLAYER_CLIENT(player);
    mapPlayer_t const *previous_mapplayer = client->mapplayer;
    uint32_t previous_controller = client->jass.controller;
    mapPlayer_t lobby_player = { .used = true, .playerType = kPlayerTypeComputer, .lobbyControllerLocked = true };
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    client->mapplayer = &lobby_player;
    client->jass.controller = 1;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunLobbyControllerTest()\n"
        "SetPlayerController(Player(0), ConvertMapControl(0))\n"
        "assert(GetPlayerController(Player(0)) == 1)\n"
        "end\n",
        "player-lobby-controller-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunLobbyControllerTest"));
    client->mapplayer = previous_mapplayer;
    client->jass.controller = previous_controller;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_player_slot_state_reads_shared_state) {
    player_t *player = G_GetPlayerByNumber(0);
    gameClient_t *client = PLAYER_CLIENT(player);
    gameClient_t *computer_client = PLAYER_CLIENT(G_GetPlayerByNumber(1));
    gameClient_t *empty_client = PLAYER_CLIENT(G_GetPlayerByNumber(2));
    mapInfo_t *mapinfo = (mapInfo_t *)level.mapinfo;
    bool previous_removed = client->jass.removed;
    bool previous_connected = client->connected;
    mapPlayer_t const *previous_mapplayer = client->mapplayer;
    mapPlayer_t const *previous_computer_mapplayer = computer_client->mapplayer;
    mapPlayer_t const *previous_empty_mapplayer = empty_client->mapplayer;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    mapinfo->players[0].used = true;
    mapinfo->players[0].playerType = kPlayerTypeHuman;
    mapinfo->players[1].used = true;
    mapinfo->players[1].playerType = kPlayerTypeComputer;
    mapinfo->players[2].used = false;
    mapinfo->players[2].playerType = kPlayerTypeComputer;
    client->mapplayer = mapinfo->players;
    computer_client->mapplayer = mapinfo->players + 1;
    empty_client->mapplayer = mapinfo->players + 2;
    client->connected = false;
    T_EQ(G_GetPlayerSlotState(player), 0);
    client->connected = true;
    T_EQ(G_GetPlayerSlotState(player), 1);
    T_EQ(G_GetPlayerSlotState(G_GetPlayerByNumber(1)), 1);
    T_EQ(G_GetPlayerSlotState(G_GetPlayerByNumber(2)), 0);
    client->jass.removed = true;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunSlotStateTest() assert(GetPlayerSlotState(Player(0)) == 2) end\n",
        "player-slot-state-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunSlotStateTest"));
    client->jass.removed = previous_removed;
    client->connected = previous_connected;
    client->mapplayer = previous_mapplayer;
    computer_client->mapplayer = previous_computer_mapplayer;
    empty_client->mapplayer = previous_empty_mapplayer;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_chat_events_match_player_and_preserve_text) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    reset_entities();
    setup_test_world();
    FOR_LOOP(i, 2) {
        globals.edicts[i].client = &game.clients[i];
        globals.edicts[i].inuse = true;
        game.clients[i].ps.number = i;
    }
    level.lua_vm = lua;
    jass_sethost(&MAKE(jassHost_t,
        .MemAlloc = gi.MemAlloc, .MemFree = gi.MemFree,
        .GetPlayerByNumber = G_GetPlayerByNumber,
        .LuaTriggerEvaluate = G_LuaTriggerEvaluateHost,
        .LuaTriggerExecute = G_LuaTriggerExecuteHost,
    ));
    T_ASSERT(G_LoadLuaMapScript(lua,
        "count = 0; exact = 0\n"
        "function config() end\n"
        "function main()\n"
        "local t = CreateTrigger()\n"
        "TriggerRegisterPlayerChatEvent(t, Player(0), '-kill', false)\n"
        "TriggerAddAction(t, function()\n"
        "assert(GetEventPlayerChatString() == 'please -kill now')\n"
        "assert(GetEventPlayerChatStringMatched() == '-kill')\n"
        "assert(GetTriggerPlayer() == Player(0)); count = count + 1\n"
        "end)\n"
        "local e = CreateTrigger()\n"
        "TriggerRegisterPlayerChatEvent(e, Player(0), '-kill', true)\n"
        "TriggerAddAction(e, function() exact = exact + 1 end)\n"
        "end\n"
        "function check() assert(count == 1 and exact == 0) end\n",
        "chat-events.lua"));
    T_ASSERT(WC3_LuaCall(lua, "main"));
    cstring_t args[] = { "say", "please -kill now" };
    G_ClientCommand(G_GetPlayerEntityByNumber(1), 2, args);
    G_ClientCommand(G_GetPlayerEntityByNumber(0), 2, args);
    G_RunEvents();
    T_ASSERT(WC3_LuaCall(lua, "check"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_mouse_event_preserves_button_point_and_player) {
    wc3Lua_t *previous = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    reset_entities();
    setup_test_world();
    FOR_LOOP(i, 2) {
        globals.edicts[i].client = &game.clients[i];
        globals.edicts[i].inuse = true;
        game.clients[i].ps.number = i;
    }
    level.lua_vm = lua;
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gi.MemAlloc, .MemFree = gi.MemFree,
        .GetPlayerByNumber = G_GetPlayerByNumber, .LuaTriggerEvaluate = G_LuaTriggerEvaluateHost,
        .LuaTriggerExecute = G_LuaTriggerExecuteHost));
    T_ASSERT(G_LoadLuaMapScript(lua,
        "count=0; function main() local t=CreateTrigger() "
        "TriggerRegisterPlayerEvent(t,Player(0),ConvertPlayerEvent(305)) "
        "TriggerAddCondition(t,Condition(function() return BlzGetTriggerPlayerMouseButton()==ConvertMouseButtonType(3) end)) "
        "TriggerAddAction(t,function() local p=BlzGetTriggerPlayerMousePosition() "
        "assert(GetLocationX(p)==125.5 and GetLocationY(p)==-72.25) "
        "assert(BlzGetTriggerPlayerMouseX()==125.5 and BlzGetTriggerPlayerMouseY()==-72.25) "
        "assert(GetTriggerPlayer()==Player(0)); RemoveLocation(p); count=count+1 end) end "
        "function check() assert(count==1) end", "mouse-events.lua"));
    T_ASSERT(WC3_LuaCall(lua, "main"));
    cstring_t args[] = { "mouseevent", "0", "3", "125.5", "-72.25" };
    G_ClientCommand(G_GetPlayerEntityByNumber(1), 5, args);
    G_ClientCommand(G_GetPlayerEntityByNumber(0), 5, args);
    G_RunEvents();
    T_ASSERT(WC3_LuaCall(lua, "check"));
    level.lua_vm = previous;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_condition_handle_preserves_map_wrapper_result) {
    wc3Lua_t *previous = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function check()\n"
        "local original = TriggerAddCondition\n"
        "local function wrapped(t, c)\n"
        "if type(c) == 'function' then c = Condition(function() pcall(c) end) end\n"
        "return original(t, c) end\n"
        "local t = CreateTrigger()\n"
        "local c = Condition(function() return true end)\n"
        "assert(type(c) == 'userdata')\n"
        "wrapped(t, c)\n"
        "DestroyBoolExpr(c)\n"
        "assert(TriggerEvaluate(t))\n"
        "assert(not pcall(original, t, c))\n"
        "end\n", "condition-handle.lua"));
    T_ASSERT(WC3_LuaCall(lua, "check"));
    level.lua_vm = previous;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_position_location_moves_existing_unit) {
    wc3Lua_t *previous = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    reset_entities(); setup_test_world(); level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function check() local u=CreateUnit(Player(0),FourCC('hfoo'),32,32,0) "
        "local p=Location(96,144); SetUnitPositionLoc(u,p); RemoveLocation(p) "
        "assert(GetUnitX(u)==96 and GetUnitY(u)==144) "
        "SetUnitPosition(u,128,160); assert(GetUnitX(u)==128 and GetUnitY(u)==160) end", "unit-position.lua"));
    T_ASSERT(WC3_LuaCall(lua, "check"));
    level.lua_vm = previous; WC3_LuaClose(lua); reset_entities();
}

TEST(wc3_mapscript, lua_terrain_pathability_reads_authored_flags) {
    wc3Lua_t *previous = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    uint8_t cells[] = { 0, 2, 4, 8 };
    setup_test_world();
    CM_SetupTestWorldBounds(&(box2_t){ .min = {0, 0}, .max = {64, 64} });
    CM_SetupTestPathmap(2, 2, cells);
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function check() "
        "assert(not IsTerrainPathable(16,16,ConvertPathingType(1))) "
        "assert(IsTerrainPathable(48,16,ConvertPathingType(1))) "
        "assert(IsTerrainPathable(16,48,ConvertPathingType(2))) "
        "assert(IsTerrainPathable(48,48,ConvertPathingType(3))) end", "pathability.lua"));
    T_ASSERT(WC3_LuaCall(lua, "check"));
    level.lua_vm = previous; WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_cooldown_natives_share_spell_state) {
    wc3Lua_t *previous = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    reset_entities(); setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function check() local u=CreateUnit(Player(0),FourCC('hfoo'),0,0,0) "
        "local a=FourCC('AHbz'); BlzStartUnitAbilityCooldown(u,a,12) "
        "assert(BlzGetUnitAbilityCooldownRemaining(u,a)==12) "
        "BlzEndUnitAbilityCooldown(u,a) "
        "assert(BlzGetUnitAbilityCooldownRemaining(u,a)==0) "
        "SetUnitInvulnerable(u,true); assert(BlzIsUnitInvulnerable(u)) "
        "SetUnitInvulnerable(u,false); assert(not BlzIsUnitInvulnerable(u)) end", "cooldowns.lua"));
    T_ASSERT(WC3_LuaCall(lua, "check"));
    level.lua_vm = previous;
    WC3_LuaClose(lua); reset_entities();
}

TEST(wc3_mapscript, lua_player_handicap_native_updates_shared_state) {
    wc3Lua_t *previous = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function check() SetPlayerHandicap(Player(0), 1.25) "
        "SetPlayerName(Player(0), 'Forsaken (1)') "
        "assert(GetPlayerName(Player(0)) == 'Forsaken (1)') end",
        "handicap.lua"));
    T_ASSERT(WC3_LuaCall(lua, "check"));
    T_EQ(G_GetPlayerClientByNumber(0)->jass.handicap, 1.25f);
    level.lua_vm = previous;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_trigger_execute_calls_registered_lua_action) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunTriggerTest()\n"
        "local count = 0\n"
        "local trigger = CreateTrigger()\n"
        "TriggerAddCondition(trigger, function() return GetTriggeringTrigger() == trigger end)\n"
        "TriggerAddAction(trigger, function() count = count + 1 end)\n"
        "assert(TriggerEvaluate(trigger))\n"
        "TriggerExecute(trigger)\n"
        "assert(count == 1)\n"
        "end\n",
        "lua-trigger-action-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunTriggerTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_trigger_evaluate_reports_non_handle_argument_type) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunInvalidTriggerTest() TriggerEvaluate({}) end\n",
        "lua-invalid-trigger-test.lua"));
    T_ASSERT(!WC3_LuaCall(lua, "RunInvalidTriggerTest"));
    T_ASSERT(strstr(WC3_LuaErrorMessage(lua), "arg_type=table") != NULL);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, reforged_w3e_v12_decodes_two_byte_vertex_flags) {
    uint8_t raw[] = { 0x10, 0x20, 0x00, 0x20, 0xFF, 0x03, 0x1D, 0xA7 };
    uint8_t legacy[] = { 0x10, 0x20, 0x00, 0x20, 0xFF, 0x1D, 0xA7 };
    war3mapVertex_t vertex = { 0 };

    WC3_DecodeMapVertex(raw, 12, &vertex);
    T_EQ(vertex.accurate_height, 0x2010);
    T_EQ(vertex.waterlevel, 0x2000);
    T_EQ(vertex.ground, 0x3F);
    T_ASSERT(vertex.ramp && vertex.blight && vertex.water && vertex.boundary);
    T_EQ(vertex.groundVariation, 0x1D);
    T_EQ(vertex.cliffVariation, 0);
    T_EQ(vertex.cliff, 0xA);
    T_EQ(vertex.level, 0x7);

    WC3_DecodeMapVertex(legacy, 11, &vertex);
    T_EQ(vertex.ground, 0xF);
    T_ASSERT(vertex.ramp && vertex.blight && vertex.water && vertex.boundary);
    T_EQ(vertex.groundVariation, 0x1D);
    T_EQ(vertex.cliff, 0xA);
    T_EQ(vertex.level, 0x7);
}

TEST(wc3_mapscript, lua_version_get_matches_active_edition) {
    wc3Lua_t *lua = WC3_LuaNewState();
    int32_t expected = atoi(gi.CvarString("fs_expansion", "0")) != 0 ? 1 : 0;

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunVersionTest() assert(VersionGet() == expected_version) end\n",
        "lua-version-test.lua"));
    WC3_LuaRegisterInteger(lua, "expected_version", expected);
    T_ASSERT(WC3_LuaCall(lua, "RunVersionTest"));
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_camera_bounds_reads_shared_map_bounds) {
    wc3Lua_t *lua = WC3_LuaNewState();
    double value = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunCameraBoundsTest() return GetCameraBoundMinX() end\n",
        "lua-camera-bounds-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunCameraBoundsTest", &value));
    T_EQ((float)value, level.camera_bounds.min.x);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_get_player_tech_researched_reads_shared_tech_state) {
    player_t *player = G_GetPlayerByNumber(0);
    gameClient_t *client = PLAYER_CLIENT(player);
    uint32_t tech = MAKEFOURCC('R', 'h', 'r', 't');
    int32_t previous = G_GetPlayerTechResearchedLevel(client, tech);
    wc3Lua_t *lua = WC3_LuaNewState();

    G_SetPlayerTechResearched(client, tech, 1);
    T_NOT_NULL(lua);
    if (!lua) {
        G_SetPlayerTechResearched(client, tech, previous);
        return;
    }
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunTechTest() assert(GetPlayerTechResearched(Player(0), tech_id, true)) end\n",
        "lua-player-tech-test.lua"));
    WC3_LuaRegisterInteger(lua, "tech_id", tech);
    T_ASSERT(WC3_LuaCall(lua, "RunTechTest"));
    G_SetPlayerTechResearched(client, tech, previous);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_player_tech_max_allowed_uses_shared_state) {
    player_t *player = G_GetPlayerByNumber(0);
    gameClient_t *client = PLAYER_CLIENT(player);
    uint32_t tech = MAKEFOURCC('R', 'h', 'r', 't');
    int32_t previous = G_GetPlayerTechMaxAllowed(client, tech);
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunTechMaxTest()\n"
        "SetPlayerTechMaxAllowed(Player(0), tech_id, 7)\n"
        "assert(GetPlayerTechMaxAllowed(Player(0), tech_id) == 7)\n"
        "end\n",
        "lua-player-tech-max-test.lua"));
    WC3_LuaRegisterInteger(lua, "tech_id", tech);
    T_ASSERT(WC3_LuaCall(lua, "RunTechMaxTest"));
    G_SetPlayerTechMaxAllowed(client, tech, previous);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_set_all_item_type_slots_uses_shared_stock_state) {
    int32_t previous = level.stock.item_slots;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunItemSlotsTest() SetAllItemTypeSlots(7) end\n",
        "lua-item-slots-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunItemSlotsTest"));
    T_EQ(level.stock.item_slots, 7);
    level.stock.item_slots = previous;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_timer_start_accepts_and_retains_lua_callback) {
    wc3Lua_t *previous_lua = level.lua_vm;
    uint32_t previous_timers = level.num_timers;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunTimerSetup()\n"
        "timer_calls = 0\n"
        "timer = CreateTimer()\n"
        "TimerStart(timer, 1.0, false, function()\n"
        "assert(GetExpiredTimer() == timer)\n"
        "timer_calls = timer_calls + 1\n"
        "end)\n"
        "end\n"
        "function CheckTimerCallback() assert(timer_calls == 1) end\n",
        "lua-timer-callback-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunTimerSetup"));
    if (level.num_timers > previous_timers) {
        T_ASSERT(G_LuaTimerExpired(&level.timers[previous_timers]));
        T_ASSERT(WC3_LuaCall(lua, "CheckTimerCallback"));
    }
    level.num_timers = previous_timers;
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_player_unit_event_registration_uses_shared_event_registry) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunPlayerUnitEventTest()\n"
        "local trigger = CreateTrigger()\n"
        "local event = TriggerRegisterPlayerUnitEvent(trigger, Player(0), ConvertPlayerUnitEvent(21), nil)\n"
        "assert(event ~= nil)\n"
        "end\n",
        "lua-player-unit-event-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunPlayerUnitEventTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
}

static int mapscript_captured_sound_index;
static int mapscript_capture_sound_index(cstring_t path) {
    (void)path;
    return 77;
}

static void mapscript_capture_sound_index_play(edict_t *ent, int channel, int sound,
                                               float volume, float attenuation, float timeofs) {
    (void)ent; (void)channel; (void)volume; (void)attenuation; (void)timeofs;
    mapscript_captured_sound_index = sound;
}

static void mapscript_capture_positioned_sound(vec3_t const *origin, edict_t *ent, int channel, int sound,
                                               float volume, float attenuation, float timeofs) {
    (void)origin;
    mapscript_capture_sound_index_play(ent, channel, sound, volume, attenuation, timeofs);
}

TEST(wc3_mapscript, lua_create_sound_round_trips_duration_and_volume) {
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunCreateSoundTest()\n"
        "local s = CreateSound(\"Sound\\\\Custom\\\\voice.wav\", false, true, false, 0, 0, \"DefaultEAXON\")\n"
        "assert(GetSoundDuration(s) == 0)\n"
        "SetSoundDuration(s, 2275)\n"
        "assert(GetSoundDuration(s) == 2275)\n"
        "SetSoundVolume(s, 127)\n"
        "end\n",
        "lua-create-sound-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunCreateSoundTest"));
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_start_sound_transports_through_shared_sound_path) {
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    void (*old_sound)(edict_t *, int, int, float, float, float) = gi.Sound;
    void (*old_positioned)(vec3_t const *, edict_t *, int, int, float, float, float) = gi.PositionedSound;
    wc3Lua_t *lua = WC3_LuaNewState();

    mapscript_captured_sound_index = 0;
    gi.SoundIndex = mapscript_capture_sound_index;
    gi.Sound = mapscript_capture_sound_index_play;
    gi.PositionedSound = mapscript_capture_positioned_sound;

    T_NOT_NULL(lua);
    if (!lua) {
        gi.SoundIndex = old_soundindex; gi.Sound = old_sound; gi.PositionedSound = old_positioned;
        return;
    }
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunStartSoundTest()\n"
        "local s = CreateSound(\"Sound\\\\Custom\\\\voice.wav\", false, true, false, 0, 0, \"\")\n"
        "StartSound(s)\n"
        "end\n",
        "lua-start-sound-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunStartSoundTest"));
    T_EQ(mapscript_captured_sound_index, 77);
    gi.SoundIndex = old_soundindex; gi.Sound = old_sound; gi.PositionedSound = old_positioned;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_add_weather_effect_uses_shared_weather_state) {
    wc3Lua_t *lua = WC3_LuaNewState();
    uint32_t snow = MAKEFOURCC('S', 'N', 'l', 's');
    uint32_t slot = MAX_WEATHER_EFFECTS;

    T_NOT_NULL(lua);
    if (!lua) return;
    FOR_LOOP(i, MAX_WEATHER_EFFECTS) {
        if (!level.weather_effects[i].inuse) { slot = i; break; }
    }
    T_ASSERT(slot < MAX_WEATHER_EFFECTS);
    if (slot >= MAX_WEATHER_EFFECTS) { WC3_LuaClose(lua); return; }
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunWeatherTest()\n"
        "local r = Rect(-8192.0, 24960.0, 10304.0, 30464.0)\n"
        "local we = AddWeatherEffect(r, weather_id)\n"
        "assert(we ~= nil)\n"
        "EnableWeatherEffect(we, true)\n"
        "end\n",
        "lua-weather-test.lua"));
    WC3_LuaRegisterInteger(lua, "weather_id", snow);
    T_ASSERT(WC3_LuaCall(lua, "RunWeatherTest"));
    T_ASSERT(level.weather_effects[slot].inuse);
    T_EQ(level.weather_effects[slot].effect_id, snow);
    T_ASSERT(level.weather_effects[slot].enabled);
    T_FEQ(level.weather_effects[slot].bounds.min.x, -8192.0f, 0.001f);
    T_FEQ(level.weather_effects[slot].bounds.max.y, 30464.0f, 0.001f);
    G_WeatherRemove(&level.weather_effects[slot]);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_camera_setup_round_trips_authored_fields) {
    wc3Lua_t *lua = WC3_LuaNewState();
    double distance = 0.0, rotation = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunCameraSetupTest()\n"
        "local setup = CreateCameraSetup()\n"
        "CameraSetupSetField(setup, field_distance, 3980.0, 0.0)\n"
        "CameraSetupSetField(setup, field_rotation, 89.0, 0.0)\n"
        "CameraSetupSetDestPosition(setup, -3864.1, 398.5, 0.0)\n"
        "assert(math.abs(CameraSetupGetDestPositionX(setup) - (-3864.1)) < 0.01)\n"
        "assert(math.abs(CameraSetupGetDestPositionY(setup) - 398.5) < 0.01)\n"
        "camera_setup = setup\n"
        "end\n"
        "function GetSetupDistance() return CameraSetupGetField(camera_setup, field_distance) end\n"
        "function GetSetupRotation() return CameraSetupGetField(camera_setup, field_rotation) end\n",
        "lua-camera-setup-test.lua"));
    WC3_LuaRegisterInteger(lua, "field_distance", CAMERA_FIELD_TARGET_DISTANCE);
    WC3_LuaRegisterInteger(lua, "field_rotation", CAMERA_FIELD_ROTATION);
    T_ASSERT(WC3_LuaCall(lua, "RunCameraSetupTest"));
    T_ASSERT(WC3_LuaCallNumber(lua, "GetSetupDistance", &distance));
    T_ASSERT(WC3_LuaCallNumber(lua, "GetSetupRotation", &rotation));
    T_FEQ((float)distance, 3980.0f, 0.001f);
    T_FEQ((float)rotation, 89.0f, 0.001f);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_blz_create_unit_with_skin_uses_shared_unit_create) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    uint32_t hero = MAKEFOURCC('H', 'p', 'a', 'l');

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunBlzCreateUnitTest()\n"
        "local u = BlzCreateUnitWithSkin(Player(1), unit_id, 32.0, 32.0, 0.0, unit_id)\n"
        "assert(u ~= nil)\n"
        "end\n",
        "lua-blz-create-unit-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", hero);
    T_ASSERT(WC3_LuaCall(lua, "RunBlzCreateUnitTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_trigger_register_unit_event_uses_shared_event_registry) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunUnitEventTest()\n"
        "local trigger = CreateTrigger()\n"
        "local u = BlzCreateUnitWithSkin(Player(0), unit_id, 32.0, 32.0, 0.0, unit_id)\n"
        "local event = TriggerRegisterUnitEvent(trigger, u, EVENT_UNIT_DEATH)\n"
        "assert(event ~= nil)\n"
        "end\n",
        "lua-unit-event-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('H', 'p', 'a', 'l'));
    WC3_LuaRegisterInteger(lua, "EVENT_UNIT_DEATH", EVENT_UNIT_DEATH);
    T_ASSERT(WC3_LuaCall(lua, "RunUnitEventTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_set_unit_state_and_rect_center_reuse_shared_helpers) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    double center_x = 0.0, center_y = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunSetUnitStateTest()\n"
        "local u = BlzCreateUnitWithSkin(Player(0), unit_id, 32.0, 32.0, 0.0, unit_id)\n"
        "SetUnitState(u, UNIT_STATE_MANA, 123.0)\n"
        "local r = Rect(-10.0, -20.0, 30.0, 40.0)\n"
        "center_x = GetRectCenterX(r)\n"
        "center_y = GetRectCenterY(r)\n"
        "end\n"
        "function GetCenterX() return center_x end\n"
        "function GetCenterY() return center_y end\n",
        "lua-set-unit-state-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('H', 'p', 'a', 'l'));
    WC3_LuaRegisterInteger(lua, "UNIT_STATE_MANA", UNIT_STATE_MANA);
    T_ASSERT(WC3_LuaCall(lua, "RunSetUnitStateTest"));
    T_ASSERT(WC3_LuaCallNumber(lua, "GetCenterX", &center_x));
    T_ASSERT(WC3_LuaCallNumber(lua, "GetCenterY", &center_y));
    T_FEQ((float)center_x, 10.0f, 0.001f);
    T_FEQ((float)center_y, 10.0f, 0.001f);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, blz_unit_max_hp_reads_and_updates_shared_health_state) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    double max_hp = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunMaxHpTest()\n"
        "local u = BlzCreateUnitWithSkin(Player(0), unit_id, 32.0, 32.0, 0.0, unit_id)\n"
        "assert(BlzGetUnitMaxHP(u) > 0)\n"
        "BlzSetUnitMaxHP(u, 900)\n"
        "return BlzGetUnitMaxHP(u)\n"
        "end\n",
        "lua-blz-unit-max-hp-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('H', 'p', 'a', 'l'));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunMaxHpTest", &max_hp));
    T_FEQ((float)max_hp, 900.0f, 0.001f);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, blz_unit_armor_reads_and_updates_combat_armor) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    double armor = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunArmorTest()\n"
        "local u = BlzCreateUnitWithSkin(Player(0), unit_id, 32.0, 32.0, 0.0, unit_id)\n"
        "BlzSetUnitArmor(u, 30.0)\n"
        "return BlzGetUnitArmor(u)\n"
        "end\n",
        "lua-blz-unit-armor-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('H', 'p', 'a', 'l'));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunArmorTest", &armor));
    T_FEQ((float)armor, 30.0f, 0.001f);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, blz_unit_mana_regeneration_field_updates_runtime_rate) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    double regen = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunManaRegenFieldTest()\n"
        "local u = BlzCreateUnitWithSkin(Player(0), unit_id, 32.0, 32.0, 0.0, unit_id)\n"
        "BlzSetUnitRealFieldBJ(u, mana_regen_field, 300.0)\n"
        "return BlzGetUnitRealField(u, mana_regen_field)\n"
        "end\n",
        "lua-blz-unit-real-field-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('H', 'p', 'a', 'l'));
    WC3_LuaRegisterInteger(lua, "mana_regen_field", MAKEFOURCC('u', 'm', 'p', 'r'));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunManaRegenFieldTest", &regen));
    T_FEQ((float)regen, 300.0f, 0.001f);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, blz_unit_name_field_updates_script_and_hud_name) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    double passed = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunUnitNameFieldTest()\n"
        "local u = BlzCreateUnitWithSkin(Player(0), unit_id, 32.0, 32.0, 0.0, unit_id)\n"
        "BlzSetUnitStringFieldBJ(u, unit_name_field, 'Race Capital')\n"
        "return GetUnitName(u) == 'Race Capital' and 1 or 0\n"
        "end\n",
        "lua-blz-unit-name-field-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('H', 'p', 'a', 'l'));
    WC3_LuaRegisterInteger(lua, "unit_name_field", MAKEFOURCC('u', 'n', 'a', 'm'));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunUnitNameFieldTest", &passed));
    T_EQ((int)passed, 1);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

static bool mapscript_pack_mpq(cstring_t path, cstring_t member, cstring_t text) {
    handle_t archive = NULL;

    unlink(path);
    if (!SFileCreateArchive(path, 0, 16, &archive))
        return false;
    if (member && text) {
        if (!SFileAddFileFromBuffer(archive, member, text, (uint32_t)strlen(text))) {
            SFileCloseArchive(archive);
            unlink(path);
            return false;
        }
    } else if (!SFileAddFileFromBuffer(archive, "dummy.txt", "x", 1)) {
        SFileCloseArchive(archive);
        unlink(path);
        return false;
    }
    if (!SFileCloseArchive(archive)) {
        unlink(path);
        return false;
    }
    return true;
}

static void mapscript_clear_loaded(void) {
    if (world.info.mapscript) {
        gi.MemFree(world.info.mapscript);
        world.info.mapscript = NULL;
    }
}

TEST(wc3_mapscript, jass_dobuffer_null_returns_false) {
    jass_t *j;

    jass_sethost(&MAKE(jassHost_t,
        .MemAlloc = gi.MemAlloc,
        .MemFree = gi.MemFree,
        .GetTime = gi.GetTime,
        .ReadFile = gi.ReadFile,
        .natives = jass_funcs,
        .GetPlayerByNumber = G_GetPlayerByNumber,
        .TimerCoroutineValid = G_TimerCoroutineValid,
        .RuntimeError = mapscript_ignore_error,
        .SaveHandle = G_SaveJassHandle,
        .LoadHandle = G_LoadJassHandle,
        .VariableChanged = G_JassVariableChanged,
    ));
    j = jass_newstate();
    T_NOT_NULL(j);
    T_ASSERT(!jass_dobuffer(j, NULL));
    T_ASSERT(jass_rterror_pending(j));
    T_ASSERT(!jass_dobuffer_ex(j, NULL, JASS_MODE_JASS));
    T_ASSERT(jass_rterror_pending(j));
    jass_close(j);
}

TEST(wc3_mapscript, read_scripts_war3map_j_when_root_absent) {
    cstring_t path = "build/tests/openwarcraft3-mapscript-scripts.mpq";
    handle_t archive;

    T_ASSERT(mapscript_pack_mpq(path, "scripts\\war3map.j", kMinimalMapScript));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_NOT_NULL(world.info.mapscript);
    T_ASSERT(strstr(world.info.mapscript, "function config") != NULL);
    T_ASSERT(strstr(world.info.mapscript, "function main") != NULL);
    mapscript_clear_loaded();
}

TEST(wc3_mapscript, root_war3map_j_preferred_over_scripts) {
    cstring_t path = "build/tests/openwarcraft3-mapscript-both.mpq";
    handle_t archive;
    cstring_t root = "function config takes nothing returns nothing\nendfunction\n"
                  "function main takes nothing returns nothing\nendfunction\n"
                  "// root\n";
    cstring_t nested = "function config takes nothing returns nothing\nendfunction\n"
                    "function main takes nothing returns nothing\nendfunction\n"
                    "// scripts\n";

    unlink(path);
    T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
    T_ASSERT(SFileAddFileFromBuffer(archive, "war3map.j", root, (uint32_t)strlen(root)));
    T_ASSERT(SFileAddFileFromBuffer(archive, "scripts\\war3map.j", nested, (uint32_t)strlen(nested)));
    T_ASSERT(SFileCloseArchive(archive));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_NOT_NULL(world.info.mapscript);
    T_ASSERT(strstr(world.info.mapscript, "// root") != NULL);
    T_ASSERT(strstr(world.info.mapscript, "// scripts") == NULL);
    mapscript_clear_loaded();
}

TEST(wc3_mapscript, missing_script_leaves_null_without_crash) {
    cstring_t path = "build/tests/openwarcraft3-mapscript-missing.mpq";
    handle_t archive;

    T_ASSERT(mapscript_pack_mpq(path, NULL, NULL));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_NULL(world.info.mapscript);
}

/* CM_ReadMapScript reads the map-scoped W3I fields from the shared world.info,
 * so each case must restore the previous selector to keep the suite ordered
 * independently. */
typedef struct { uint32_t fileFormat, scriptType; } mapscript_selector_t;

static mapscript_selector_t mapscript_selector_push(uint32_t file_format, uint32_t script_type) {
    mapscript_selector_t saved = { world.info.fileFormat, world.info.scriptType };
    world.info.fileFormat = file_format;
    world.info.scriptType = script_type;
    return saved;
}

static void mapscript_selector_pop(mapscript_selector_t saved) {
    world.info.fileFormat = saved.fileFormat;
    world.info.scriptType = saved.scriptType;
}

TEST(wc3_mapscript, lua_declared_loads_war3map_lua) {
    cstring_t path = "build/tests/openwarcraft3-mapscript-lua.mpq";
    handle_t archive;
    cstring_t lua = "function config() end\nfunction main() end\n";
    mapscript_selector_t saved;

    T_ASSERT(mapscript_pack_mpq(path, "war3map.lua", lua));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    saved = mapscript_selector_push(28, WC3_W3I_SCRIPT_LUA);
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_EQ(world.info.scriptKind, WC3_SCRIPT_LUA);
    T_NOT_NULL(world.info.mapscript);
    T_ASSERT(strstr(world.info.mapscript, "function config") != NULL);
    mapscript_clear_loaded();
    mapscript_selector_pop(saved);
}

TEST(wc3_mapscript, lua_declared_never_falls_back_to_jass) {
    cstring_t path = "build/tests/openwarcraft3-mapscript-lua-no-member.mpq";
    handle_t archive;
    mapscript_selector_t saved;

    T_ASSERT(mapscript_pack_mpq(path, "war3map.j", kMinimalMapScript));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    saved = mapscript_selector_push(28, WC3_W3I_SCRIPT_LUA);
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_EQ(world.info.scriptKind, WC3_SCRIPT_LUA);
    T_NULL(world.info.mapscript);
    mapscript_clear_loaded();
    mapscript_selector_pop(saved);
}

TEST(wc3_mapscript, legacy_format_without_scriptkind_stays_jass) {
    cstring_t path = "build/tests/openwarcraft3-mapscript-legacy.mpq";
    handle_t archive;
    mapscript_selector_t saved;

    T_ASSERT(mapscript_pack_mpq(path, "war3map.j", kMinimalMapScript));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    saved = mapscript_selector_push(25, 0);
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_EQ(world.info.scriptKind, WC3_SCRIPT_JASS);
    T_NOT_NULL(world.info.mapscript);
    mapscript_clear_loaded();
    mapscript_selector_pop(saved);
}

TEST(wc3_mapscript, lua_config_updates_all_24_map_start_locations) {
    wc3Lua_t *lua;

    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_ASSERT(lua != NULL);
    if (!lua) return;
    G_RegisterLuaMapConfigNatives(lua);
    T_ASSERT(WC3_LuaLoadBuffer(lua,
        "function config()\n"
        "SetMapName('Legion')\nSetMapDescription('24 slots')\n"
        "SetPlayers(24)\nSetTeams(24)\n"
        "SetGamePlacement(MAP_PLACEMENT_TEAMS_TOGETHER)\n"
        "for i=0,23 do DefineStartLocation(i, i*10, -i*10) end\n"
        "end\n", "=(config-test)"));
    T_ASSERT(WC3_LuaCall(lua, "config"));
    T_EQ(level.setup.players, 24);
    T_EQ(level.setup.teams, 24);
    T_EQ(strcmp(level.setup.name, "Legion"), 0);
    T_EQ(level.setup.start_locations[23].x, 230.0f);
    T_EQ(level.setup.start_locations[23].y, -230.0f);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, reforged_v3_object_data_reads_set_headers) {
    cstring_t path = "build/tests/openwarcraft3-reforged-v3-units.mpq";
    uint32_t units[] = {
        3, 1,
        MAKEFOURCC('h','f','o','o'), 0, 1, 0, 1,
        MAKEFOURCC('u','h','p','m'), mod_int, 650, MAKEFOURCC('h','f','o','o'),
        1,
        MAKEFOURCC('h','f','o','o'), MAKEFOURCC('h','0','0','1'), 1, 0, 1,
        MAKEFOURCC('u','h','p','m'), mod_int, 700, MAKEFOURCC('h','0','0','1')
    };
    uint32_t abilities[] = {
        3, 1,
        MAKEFOURCC('A','H','b','z'), 0, 1, 0, 1,
        MAKEFOURCC('a','l','e','v'), mod_int, 2, 0, 3, MAKEFOURCC('A','H','b','z'),
        1,
        MAKEFOURCC('A','H','b','z'), MAKEFOURCC('A','0','0','1'), 1, 0, 1,
        MAKEFOURCC('a','l','e','v'), mod_int, 1, 0, 4, MAKEFOURCC('A','0','0','1')
    };
    uint32_t skin[] = {
        3, 0, 1,
        MAKEFOURCC('A','H','b','z'), MAKEFOURCC('A','0','0','1'), 1, 0, 1,
        MAKEFOURCC('a','a','r','t'), mod_string, 0, 0, MAKEFOURCC('a','r','t',0), 0
    };
    handle_t archive = NULL;

    unlink(path);
    T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
    if (!archive) return;
    T_ASSERT(SFileAddFileFromBuffer(archive, "war3map.w3u", units, sizeof(units)));
    T_ASSERT(SFileAddFileFromBuffer(archive, "war3map.w3a", abilities, sizeof(abilities)));
    T_ASSERT(SFileAddFileFromBuffer(archive, "war3mapSkin.w3a", skin, sizeof(skin)));
    SFileCloseArchive(archive);
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    if (!archive) { unlink(path); return; }
    CM_ReadUnits(archive);
    CM_ReadAbilities(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_EQ(world.info.num_originalUnits, 1);
    T_EQ(world.info.num_userCreatedUnits, 1);
    T_EQ(world.info.originalUnits[0].numbeOfModifications, 1);
    T_EQ(world.info.originalUnits[0].modifications[0].type, mod_int);
    T_EQ(*(uint32_t *)world.info.originalUnits[0].modifications[0].data, 650);
    T_EQ(world.info.userCreatedUnits[0].numbeOfModifications, 1);
    T_EQ(*(uint32_t *)world.info.userCreatedUnits[0].modifications[0].data, 700);
    T_EQ(world.info.num_originalAbilities, 1);
    T_EQ(world.info.originalAbilities[0].modifications[0].level, 2);
    T_EQ(*(uint32_t *)world.info.originalAbilities[0].modifications[0].data, 3);
    T_EQ(world.info.userCreatedAbilities[0].modifications[0].level, 1);
    T_EQ(*(uint32_t *)world.info.userCreatedAbilities[0].modifications[0].data, 4);
    T_EQ(world.info.num_userCreatedAbilities, 1);
    T_EQ(world.info.userCreatedAbilities[0].numbeOfModifications, 2);
    if (world.info.userCreatedAbilities[0].numbeOfModifications == 2)
        T_STREQ(world.info.userCreatedAbilities[0].modifications[1].data, "art");
    G_SetMapAbilityOverrides(&world.info);
    T_STREQ(FindConfigValue("A001", "Art"), "art");
    G_SetMapAbilityOverrides(NULL);
    gi.MemFree(world.info.originalUnits[0].modifications[0].data);
    gi.MemFree(world.info.originalUnits[0].modifications);
    gi.MemFree(world.info.originalUnits);
    gi.MemFree(world.info.userCreatedUnits[0].modifications[0].data);
    gi.MemFree(world.info.userCreatedUnits[0].modifications);
    gi.MemFree(world.info.userCreatedUnits);
    gi.MemFree(world.info.originalAbilities[0].modifications[0].data);
    gi.MemFree(world.info.originalAbilities[0].modifications);
    gi.MemFree(world.info.originalAbilities);
    gi.MemFree(world.info.userCreatedAbilities[0].modifications[0].data);
    if (world.info.userCreatedAbilities[0].numbeOfModifications == 2)
        gi.MemFree(world.info.userCreatedAbilities[0].modifications[1].data);
    gi.MemFree(world.info.userCreatedAbilities[0].modifications);
    gi.MemFree(world.info.userCreatedAbilities);
    world.info.originalUnits = world.info.userCreatedUnits = NULL;
    world.info.num_originalUnits = world.info.num_userCreatedUnits = 0;
    world.info.originalAbilities = world.info.userCreatedAbilities = NULL;
    world.info.num_originalAbilities = world.info.num_userCreatedAbilities = 0;
}

TEST(wc3_mapscript, lua_init_hashtable_returns_engine_handle) {
    wc3Lua_t *lua;
    double handle_type;
    uint32_t timer_count;

    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_ASSERT(lua != NULL);
    if (!lua) return;
    timer_count = level.num_timers;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "lua_table = InitHashtable()\n"
        "lua_timer = CreateTimer()\n"
        "function hashtable_type()\n"
        "  return lua_table ~= nil and type(lua_table) == 'userdata' and "
        "lua_timer ~= nil and type(lua_timer) == 'userdata' and 1 or 0\n"
        "end\n", "=(hashtable-test)"));
    T_ASSERT(WC3_LuaCallNumber(lua, "hashtable_type", &handle_type));
    T_EQ(handle_type, 1.0);
    T_EQ(level.num_timers, timer_count + 1);
    WC3_LuaClose(lua);
    G_ClearHashtableRegistry();
}

TEST(wc3_mapscript, lua_create_unit_and_ability_helpers_use_shared_engine) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    uint32_t hero = MAKEFOURCC('H', 'p', 'a', 'l');

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunCreateUnitTest()\n"
        "local u = CreateUnit(Player(0), unit_id, 32.0, 32.0, 0.0)\n"
        "assert(u ~= nil)\n"
        "assert(GetUnitTypeId(u) == unit_id)\n"
        "assert(GetOwningPlayer(u) == Player(0))\n"
        "assert(type(GetUnitX(u)) == 'number' and type(GetUnitY(u)) == 'number')\n"
        "assert(type(UnitAddAbility(u, ability_id)) == 'boolean')\n"
        "assert(type(UnitRemoveAbility(u, ability_id)) == 'boolean')\n"
        "assert(GetUnitAbilityLevel(u, ability_id) >= 0)\n"
        "end\n",
        "lua-create-unit-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", hero);
    WC3_LuaRegisterInteger(lua, "ability_id", MAKEFOURCC('A', 'H', 'b', 'z'));
    T_ASSERT(WC3_LuaCall(lua, "RunCreateUnitTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_issue_immediate_order_matches_jass_engine_path) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua;

    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) { reset_entities(); return; }
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunImmediateOrderTest()\n"
        "local unit = CreateUnit(Player(0), unit_id, 32.0, 32.0, 0.0)\n"
        "assert(unit ~= nil)\n"
        "assert(IssueImmediateOrder(unit, 'stop'))\n"
        "assert(GetUnitCurrentOrder(unit) == OrderId('stop'))\n"
        "RemoveUnit(unit)\n"
        "end\n",
        "lua-immediate-order-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('h', 'f', 'o', 'o'));
    T_ASSERT(WC3_LuaCall(lua, "RunImmediateOrderTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_issue_point_order_matches_jass_engine_path) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua;

    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) { reset_entities(); return; }
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunPointOrderTest()\n"
        "local unit = CreateUnit(Player(0), unit_id, 32.0, 32.0, 0.0)\n"
        "assert(unit ~= nil)\n"
        "assert(IssuePointOrder(unit, 'move', 128.0, 96.0))\n"
        "assert(GetUnitCurrentOrder(unit) == OrderId('move'))\n"
        "RemoveUnit(unit)\n"
        "end\n",
        "lua-point-order-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('h', 'f', 'o', 'o'));
    T_ASSERT(WC3_LuaCall(lua, "RunPointOrderTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_completed_move_has_no_current_order_but_preserves_issued_event) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua;
    edict_t *unit = NULL;
    double order = -1;

    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function StartParityMove()\n"
        "parity_unit = CreateUnit(Player(0), unit_id, 32, 32, 0)\n"
        "assert(IssuePointOrder(parity_unit, 'move', 72, 32))\n"
        "end\n"
        "function CurrentParityOrder() return GetUnitCurrentOrder(parity_unit) end\n",
        "=(completed-move-parity)"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('h', 'f', 'o', 'o'));
    T_ASSERT(WC3_LuaCall(lua, "StartParityMove"));
    T_ASSERT(WC3_LuaCallNumber(lua, "CurrentParityOrder", &order));
    T_EQ(order, G_OrderId("move"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h', 'f', 'o', 'o')) unit = &g_edicts[i];
    }
    T_NOT_NULL(unit);
    if (unit) {
        for (int i = 0; i < 10 && unit->currentmove && unit->currentmove->think &&
             !strcmp(unit->currentmove->animation, "walk"); i++) unit->currentmove->think(unit);
        T_STREQ(unit->currentmove->animation, "stand");
        T_ASSERT(WC3_LuaCallNumber(lua, "CurrentParityOrder", &order));
        T_EQ(order, 0);
        T_EQ(G_GetIssuedOrderId(unit), G_OrderId("move"));
    }
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_unit_user_data_matches_jass_semantics) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua;

    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) { reset_entities(); return; }
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunUnitUserDataTest()\n"
        "local unit = CreateUnit(Player(0), unit_id, 32.0, 32.0, 0.0)\n"
        "assert(unit ~= nil)\n"
        "assert(GetUnitUserData(unit) == 0)\n"
        "SetUnitUserData(unit, 42)\n"
        "assert(GetUnitUserData(unit) == 42)\n"
        "SetUnitUserData(unit, -7)\n"
        "assert(GetUnitUserData(unit) == -7)\n"
        "assert(GetUnitUserData(nil) == 0)\n"
        "SetUnitUserData(nil, 99)\n"
        "RemoveUnit(unit)\n"
        "end\n",
        "lua-unit-user-data-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('h', 'f', 'o', 'o'));
    T_ASSERT(WC3_LuaCall(lua, "RunUnitUserDataTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_condition_returns_callable_and_drives_trigger) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunConditionTest()\n"
        "local trigger = CreateTrigger()\n"
        "TriggerAddCondition(trigger, Condition(function() return true end))\n"
        "TriggerAddAction(trigger, function() ran = true end)\n"
        "assert(IsTriggerEnabled(trigger))\n"
        "DisableTrigger(trigger)\n"
        "assert(not IsTriggerEnabled(trigger))\n"
        "EnableTrigger(trigger)\n"
        "assert(IsTriggerEnabled(trigger))\n"
        "assert(TriggerEvaluate(trigger))\n"
        "TriggerExecute(trigger)\n"
        "assert(ran == true)\n"
        "end\n",
        "lua-condition-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunConditionTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_trigger_register_timer_event_allocates_timer) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    uint32_t timers_before = level.num_timers;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunRegisterTimerTest()\n"
        "local trigger = CreateTrigger()\n"
        "local event = TriggerRegisterTimerEvent(trigger, 1.0, true)\n"
        "assert(event ~= nil)\n"
        "end\n",
        "lua-register-timer-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunRegisterTimerTest"));
    T_EQ(level.num_timers, timers_before + 1);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_for_group_removes_every_member) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function DeleteGroup()\n"
        "local g = CreateGroup(); local n = 0\n"
        "for i=1,4 do GroupAddUnit(g, CreateUnit(Player(0), FourCC('hfoo'), i*32, 32, 0)) end\n"
        "ForGroup(g, function() n=n+1; RemoveUnit(GetEnumUnit()) end)\n"
        "assert(n == 4, 'deleted ' .. tostring(n) .. '/4')\n"
        "assert(BlzGroupGetSize(g) == 0)\nDestroyGroup(g)\nend\n", "group-delete.lua"));
    T_ASSERT(WC3_LuaCall(lua, "DeleteGroup"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_group_membership_tracks_add_remove_clear_destroy) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua;

    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunGroupMembershipTest()\n"
        " local g = CreateGroup()\n"
        " local u = CreateUnit(Player(0), FourCC('hfoo'), 0, 0, 0)\n"
        " assert(not IsUnitInGroup(u, g))\n"
        " assert(not IsUnitInGroup(nil, g))\n"
        " assert(not IsUnitInGroup(u, nil))\n"
        " GroupAddUnit(g, u); assert(IsUnitInGroup(u, g))\n"
        " GroupRemoveUnit(g, u); assert(not IsUnitInGroup(u, g))\n"
        " GroupAddUnit(g, u); GroupClear(g); assert(not IsUnitInGroup(u, g))\n"
        " GroupAddUnit(g, u); DestroyGroup(g); assert(not IsUnitInGroup(u, g))\n"
        " RemoveUnit(u)\n"
        "end\n", "lua-group-membership-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunGroupMembershipTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_for_group_exposes_enum_unit) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua;
    double visited = -1.0;

    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) return;
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunForGroupTest()\n"
        "local group = CreateGroup()\n"
        "local u = CreateUnit(Player(0), unit_id, 10.0, 20.0, 0.0)\n"
        "GroupAddUnit(group, u)\n"
        "assert(BlzGroupGetSize(group) == 1)\n"
        "count = 0\n"
        "ForGroup(group, function() count = count + 1; assert(GetEnumUnit() == u) end)\n"
        "assert(FirstOfGroup(group) == u)\n"
        "GroupRemoveUnit(group, u)\n"
        "assert(BlzGroupGetSize(group) == 0)\n"
        "end\n"
        "function GetCount() return count end\n",
        "lua-for-group-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('h', 'f', 'o', 'o'));
    T_ASSERT(WC3_LuaCall(lua, "RunForGroupTest"));
    T_ASSERT(WC3_LuaCallNumber(lua, "GetCount", &visited));
    T_EQ(visited, 1.0);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_string_and_math_helpers_match_jass) {
    wc3Lua_t *lua = WC3_LuaNewState();
    double value = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunStringTest()\n"
        "assert(SubString('hello', 1, 3) == 'el')\n"
        "assert(StringLength('hello') == 5)\n"
        "assert(I2S(42) == '42')\n"
        "assert(I2R(3) == 3.0)\n"
        "assert(R2I(3.9) == 3)\n"
        "return I2R(R2I(7.5))\n"
        "end\n",
        "lua-string-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunStringTest", &value));
    T_EQ(value, 7.0);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_get_world_bounds_returns_engine_bounds) {
    wc3Lua_t *lua = WC3_LuaNewState();
    double value = 0.0;
    box2_t bounds = CM_GetWorldBounds();
    float expected = (bounds.min.x + bounds.max.x) * 0.5f;

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunWorldBoundsTest()\n"
        "local r = GetWorldBounds()\n"
        "return GetRectCenterX(r)\n"
        "end\n",
        "lua-world-bounds-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunWorldBoundsTest", &value));
    T_FEQ((float)value, expected, 0.001f);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_r2s_and_string_helpers_match_jass) {
    wc3Lua_t *lua = WC3_LuaNewState();
    double value = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunR2STest()\n"
        "assert(R2S(1.5) == '1.500000')\n"
        "assert(StringLength(R2S(0.0)) == 8)\n"
        "return R2S(2.5)\n"
        "end\n",
        "lua-r2s-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunR2STest", &value));
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_quest_setters_populate_quest_record) {
    wc3Lua_t *lua = WC3_LuaNewState();
    quest_t *quest = NULL;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunQuestTest()\n"
        "question = CreateQuest()\n"
        "QuestSetTitle(question, 'The Title')\n"
        "QuestSetDescription(question, 'The Description')\n"
        "QuestSetIconPath(question, 'ReplaceableTextures\\\\CommandButtons\\\\BTNTest.blp')\n"
        "QuestSetRequired(question, true)\n"
        "QuestSetDiscovered(question, true)\n"
        "QuestSetEnabled(question, true)\n"
        "end\n",
        "lua-quest-test.lua"));
    T_ASSERT(WC3_LuaCall(lua, "RunQuestTest"));
    /* Locate the quest the script created by its title. */
    FOR_LOOP(i, MAX_QUESTS) if (level.quests[i].inuse && level.quests[i].title &&
                                !strcmp(level.quests[i].title, "The Title")) quest = &level.quests[i];
    T_NOT_NULL(quest);
    if (quest) {
        T_STREQ(quest->description, "The Description");
        T_ASSERT(quest->required && quest->discovered && quest->enabled);
    }
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_trigger_sleep_action_does_not_abort_action) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    double reached = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunSleepTest()\n"
        "local trigger = CreateTrigger()\n"
        "TriggerAddAction(trigger, function() TriggerSleepAction(1.0); after_sleep = 1 end)\n"
        "TriggerExecute(trigger)\n"
        "return after_sleep\n"
        "end\n",
        "lua-sleep-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunSleepTest", &reached));
    T_EQ(reached, 1.0);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_rect_edges_and_create_unit_at_loc_match_jass) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    double result = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunRectUnitTest()\n"
        "local rect = Rect(10.0, 20.0, 30.0, 40.0)\n"
        "assert(GetRectMinX(rect) == 10.0)\n"
        "assert(GetRectMinY(rect) == 20.0)\n"
        "assert(GetRectMaxX(rect) == 30.0)\n"
        "assert(GetRectMaxY(rect) == 40.0)\n"
        "local loc = Location(5.0, 6.0)\n"
        "local u = CreateUnitAtLoc(Player(0), unit_id, loc, 0.0)\n"
        "assert(u ~= nil)\n"
        "assert(GetUnitTypeId(u) == unit_id)\n"
        "return GetUnitX(u) + GetUnitY(u)\n"
        "end\n",
        "lua-rect-unit-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('h', 'f', 'o', 'o'));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunRectUnitTest", &result));
    T_FEQ((float)result, 11.0f, 0.001f);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_distance_math_natives_match_jass) {
    wc3Lua_t *lua = WC3_LuaNewState();
    double value = 0;

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunDistanceMathTest()\n"
        " assert(Pow(-3, 2) == 9)\n"
        " assert(Pow(4, -1) == 0.25)\n"
        " assert(Pow(7, 0) == 1)\n"
        " assert(SquareRoot(0) == 0)\n"
        " return SquareRoot(Pow(3, 2) + Pow(4, 2))\n"
        "end\n", "lua-distance-math-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunDistanceMathTest", &value));
    T_FEQ(value, 5.0, 0.0001);
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_order_id_matches_jass_engine_lookup) {
    wc3Lua_t *lua = WC3_LuaNewState();
    double value = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunOrderIdTest() return OrderId('attack') end\n",
        "lua-order-id-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunOrderIdTest", &value));
    T_EQ((uint32_t)value, G_OrderId("attack"));
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_get_attacker_uses_shared_trigger_unit_context) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    wc3LuaTriggerContext_t previous_context, context = { 0 };
    edict_t *attacker;
    double value = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    attacker = G_Spawn();
    T_NOT_NULL(attacker);
    if (!attacker) goto cleanup;
    context.unit = attacker;
    previous_context = WC3_LuaGetTriggerContext(lua);
    WC3_LuaSetTriggerContext(lua, &context);
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunAttackerTest() return GetAttacker() ~= nil and 1 or 0 end\n",
        "lua-get-attacker-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunAttackerTest", &value));
    T_EQ(value, 1.0);
    WC3_LuaSetTriggerContext(lua, &previous_context);
cleanup:
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_death_units_use_shared_trigger_context) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    wc3LuaTriggerContext_t previous_context, context = { 0 };
    double result = 0;
    edict_t *unit, *killer;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    unit = G_Spawn();
    T_NOT_NULL(unit);
    if (!unit) goto cleanup_dying_unit;
    killer = G_Spawn();
    T_NOT_NULL(killer);
    if (!killer) goto cleanup_dying_unit;
    context.unit = unit;
    context.source = killer;
    previous_context = WC3_LuaGetTriggerContext(lua);
    WC3_LuaSetTriggerContext(lua, &context);
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunDeathUnitsTest()\n"
        " assert(GetDyingUnit() ~= nil)\n"
        " assert(GetKillingUnit() ~= nil)\n"
        " return GetDyingUnit() ~= GetKillingUnit() and 1 or 0\n"
        "end\n",
        "lua-get-dying-unit-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunDeathUnitsTest", &result));
    T_FEQ(result, 1.0, 0.0001);
    WC3_LuaSetTriggerContext(lua, &previous_context);
cleanup_dying_unit:
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_get_event_damage_source_reads_shared_context) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    wc3LuaTriggerContext_t previous_context, context = { 0 };
    edict_t *source;
    double value = 0.0;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    source = G_Spawn();
    T_NOT_NULL(source);
    if (!source) goto cleanup;
    context.source = source;
    previous_context = WC3_LuaGetTriggerContext(lua);
    WC3_LuaSetTriggerContext(lua, &context);
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunDamageSourceTest() return GetEventDamageSource() ~= nil and 1 or 0 end\n",
        "lua-event-damage-source-test.lua"));
    T_ASSERT(WC3_LuaCallNumber(lua, "RunDamageSourceTest", &value));
    T_EQ(value, 1.0);
    WC3_LuaSetTriggerContext(lua, &previous_context);
cleanup:
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_item_type_id_handles_null_and_spawned_item) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    setup_test_world();
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function CheckItemId()\n"
        "assert(GetItemTypeId(nil) == 0)\n"
        "local item = CreateItem(FourCC('spro'), 32, 32)\n"
        "assert(item ~= nil, 'CreateItem returned nil')\n"
        "assert(GetItemTypeId(item) == FourCC('spro'), tostring(GetItemTypeId(item)) .. '/' .. tostring(FourCC('spro')))\n"
        "RemoveItem(item)\nend\n", "item-type-id.lua"));
    bool ok = WC3_LuaCall(lua, "CheckItemId");
    if (!ok) fprintf(stderr, "item type regression: %s\n", WC3_LuaErrorMessage(lua));
    T_ASSERT(ok);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
}

TEST(wc3_mapscript, lua_unit_item_in_slot_matches_inventory_state) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua;

    reset_entities();
    setup_test_world();
    lua = WC3_LuaNewState();
    T_NOT_NULL(lua);
    if (!lua) { reset_entities(); return; }
    level.lua_vm = lua;
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunItemSlotTest()\n"
        "local unit = CreateUnit(Player(0), unit_id, 32.0, 32.0, 0.0)\n"
        "assert(unit ~= nil)\n"
        "assert(UnitItemInSlot(unit, 0) == nil)\n"
        "end\n",
        "lua-unit-item-in-slot-test.lua"));
    WC3_LuaRegisterInteger(lua, "unit_id", MAKEFOURCC('h', 'f', 'o', 'o'));
    T_ASSERT(WC3_LuaCall(lua, "RunItemSlotTest"));
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, lua_set_player_tech_researched_updates_engine_state) {
    wc3Lua_t *previous_lua = level.lua_vm;
    wc3Lua_t *lua = WC3_LuaNewState();
    uint32_t tech = MAKEFOURCC('R', 't', 's', 't');
    player_t *player;

    T_NOT_NULL(lua);
    if (!lua) return;
    reset_entities();
    setup_test_world();
    level.lua_vm = lua;
    player = G_GetPlayerByNumber(0);
    T_NOT_NULL(player);
    T_ASSERT(G_LoadLuaMapScript(lua,
        "function RunSetTechTest() SetPlayerTechResearched(Player(0), tech_id, 2) end\n",
        "lua-set-player-tech-test.lua"));
    WC3_LuaRegisterInteger(lua, "tech_id", tech);
    T_ASSERT(WC3_LuaCall(lua, "RunSetTechTest"));
    T_EQ(G_GetPlayerTechResearchedLevel(PLAYER_CLIENT(player), tech), 2);
    level.lua_vm = previous_lua;
    WC3_LuaClose(lua);
    reset_entities();
}

TEST(wc3_mapscript, event_registry_exceeds_legacy_1024_slot_limit) {
    event_t *event = NULL;
    uint32_t allocated = 0;

    reset_entities();
    setup_test_world();
    while (allocated < 1025) {
        event = G_MakeEvent(EVENT_PLAYER_CHAT);
        if (!event) break;
        allocated++;
    }
    T_NOT_NULL(event);
    T_EQ(allocated, 1025);
    reset_entities();
}

#endif /* BZ_TESTS */
