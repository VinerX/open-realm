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
#include "jass/jass.h"

#include <stdio.h>
#include <unistd.h>

extern jassModule_t jass_funcs[];
void CM_ReadMapScript(handle_t archive);

static cstring_t const kMinimalMapScript =
    "function config takes nothing returns nothing\n"
    "endfunction\n"
    "function main takes nothing returns nothing\n"
    "endfunction\n";

static void mapscript_ignore_error(cstring_t message) { (void)message; }

static bool mapscript_pack_mpq(cstring_t path, cstring_t member, cstring_t text) {
    handle_t archive;

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
    cstring_t path = "/tmp/openwarcraft3-mapscript-scripts.mpq";
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
    cstring_t path = "/tmp/openwarcraft3-mapscript-both.mpq";
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
    cstring_t path = "/tmp/openwarcraft3-mapscript-missing.mpq";
    handle_t archive;

    T_ASSERT(mapscript_pack_mpq(path, NULL, NULL));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_NULL(world.info.mapscript);
}

static void mapscript_set_kind(uint32_t file_format, uint32_t script_type) {
    world.info.fileFormat = file_format;
    world.info.scriptType = script_type;
}

TEST(wc3_mapscript, lua_declared_loads_war3map_lua) {
    cstring_t path = "/tmp/openwarcraft3-mapscript-lua.mpq";
    handle_t archive;
    cstring_t lua = "function config() end\nfunction main() end\n";

    T_ASSERT(mapscript_pack_mpq(path, "war3map.lua", lua));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    mapscript_set_kind(28, WC3_W3I_SCRIPT_LUA);
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_EQ(world.info.scriptKind, WC3_SCRIPT_LUA);
    T_NOT_NULL(world.info.mapscript);
    T_ASSERT(strstr(world.info.mapscript, "function config") != NULL);
    mapscript_clear_loaded();
}

TEST(wc3_mapscript, lua_declared_never_falls_back_to_jass) {
    cstring_t path = "/tmp/openwarcraft3-mapscript-lua-no-member.mpq";
    handle_t archive;

    T_ASSERT(mapscript_pack_mpq(path, "war3map.j", kMinimalMapScript));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    mapscript_set_kind(28, WC3_W3I_SCRIPT_LUA);
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_EQ(world.info.scriptKind, WC3_SCRIPT_LUA);
    T_NULL(world.info.mapscript);
    mapscript_clear_loaded();
}

TEST(wc3_mapscript, legacy_format_without_scriptkind_stays_jass) {
    cstring_t path = "/tmp/openwarcraft3-mapscript-legacy.mpq";
    handle_t archive;

    T_ASSERT(mapscript_pack_mpq(path, "war3map.j", kMinimalMapScript));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    mapscript_clear_loaded();
    mapscript_set_kind(25, 0);
    CM_ReadMapScript(archive);
    SFileCloseArchive(archive);
    unlink(path);
    T_EQ(world.info.scriptKind, WC3_SCRIPT_JASS);
    T_NOT_NULL(world.info.mapscript);
    mapscript_clear_loaded();
}

#endif /* BZ_TESTS */
