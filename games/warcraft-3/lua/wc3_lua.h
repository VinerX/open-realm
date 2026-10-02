#ifndef wc3_lua_h
#define wc3_lua_h

/*
 * wc3_lua.h - Map-scoped Lua 5.3 runtime for Warcraft III map scripts.
 *
 * The state lifetime equals one loaded 23-Race/synthetic map.  This module
 * owns no engine types so it can be exercised standalone; the WC3 game module
 * links it and drives config()/main() at the same lifecycle points as JASS.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct wc3Lua_s wc3Lua_t;
typedef struct {
    void *trigger;
    void *unit;
    void *source;
    void *timer;
    void *region;
    int32_t event_value;
    char chat_text[256], chat_match[256];
    float point_x, point_y;
    bool has_point;
} wc3LuaTriggerContext_t;
/* Forward declaration matches Lua's own lua_State tag, so C natives keep the
 * exact lua_CFunction signature without this header including lua.h. */
struct lua_State;
typedef int (*wc3LuaCFunction)(struct lua_State *L);

/* Create a fresh interpreter with the Warcraft-compatible standard libraries. */
wc3Lua_t *WC3_LuaNewState(void);
void WC3_LuaClose(wc3Lua_t *L);

/* Load and run a chunk (the map script).  The chunk's top-level statements run
 * immediately, defining the globals config()/main() that follow. */
bool WC3_LuaLoadBuffer(wc3Lua_t *L, const char *source, const char *chunk_name);

/* Call a zero-argument global function. */
bool WC3_LuaCall(wc3Lua_t *L, const char *function_name);
/* Call a zero-argument global function that returns a number. */
bool WC3_LuaCallNumber(wc3Lua_t *L, const char *function_name, double *out);
/* Call a global function with one numeric argument and copy its string result
 * into out.  A missing function or a Lua error returns false and leaves out
 * untouched; a non-string result is reported as an empty string.  The scenario
 * driver passes the current frame index. */
bool WC3_LuaCallStringWithNumber(wc3Lua_t *L, const char *function_name, double argument,
                                 char *out, size_t out_size);

/* Runtime error boundary: mirrors jass_rterror_pending()/jass_rterror_message(). */
bool WC3_LuaErrorPending(wc3Lua_t const *L);
const char *WC3_LuaErrorMessage(wc3Lua_t const *L);
void WC3_LuaClearError(wc3Lua_t *L);

/* Register a native function under a global name. */
void WC3_LuaRegisterNative(wc3Lua_t *L, const char *name, wc3LuaCFunction fn);
/* Register fn with the native's own name captured as upvalue 1 (for stubs that
 * report which native they stand in for). */
void WC3_LuaRegisterNativeNamed(wc3Lua_t *L, const char *name, wc3LuaCFunction fn);
void WC3_LuaRegisterInteger(wc3Lua_t *L, const char *name, int64_t value);

/* Evaluate a Lua boolexpr synchronously with the candidate exposed by GetFilterUnit(). */
bool WC3_LuaEvaluateFilter(wc3Lua_t *L, int function_index, void *unit, bool *accepted);
bool WC3_LuaEvaluateFilterRef(wc3Lua_t *L, int reference, void *unit, bool *accepted);
void *WC3_LuaFilterUnit(wc3Lua_t const *L);
/* ForGroup/ForForce expose each candidate through GetEnumUnit()/GetEnumPlayer(),
 * matching the JASS currentunit/currentenumplayer globals. */
void WC3_LuaSetEnumUnit(wc3Lua_t *L, void *unit);
void *WC3_LuaEnumUnit(wc3Lua_t const *L);
void WC3_LuaSetEnumPlayer(wc3Lua_t *L, void *player);
void *WC3_LuaEnumPlayer(wc3Lua_t const *L);

/* Retain and invoke a Lua closure for engine-owned trigger callbacks. */
int WC3_LuaRefFunction(wc3Lua_t *L, int function_index);
void WC3_LuaUnrefFunction(wc3Lua_t *L, int reference);
bool WC3_LuaCallRef(wc3Lua_t *L, int reference);
bool WC3_LuaCallRefBoolean(wc3Lua_t *L, int reference, bool *result);
wc3LuaTriggerContext_t WC3_LuaGetTriggerContext(wc3Lua_t const *L);
void WC3_LuaSetTriggerContext(wc3Lua_t *L, wc3LuaTriggerContext_t const *context);

#endif
