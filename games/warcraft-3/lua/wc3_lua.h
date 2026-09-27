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

/* Runtime error boundary: mirrors jass_rterror_pending()/jass_rterror_message(). */
bool WC3_LuaErrorPending(wc3Lua_t const *L);
const char *WC3_LuaErrorMessage(wc3Lua_t const *L);
void WC3_LuaClearError(wc3Lua_t *L);

/* Register a native function under a global name. */
void WC3_LuaRegisterNative(wc3Lua_t *L, const char *name, wc3LuaCFunction fn);
void WC3_LuaRegisterInteger(wc3Lua_t *L, const char *name, int64_t value);

/* Evaluate a Lua boolexpr synchronously with the candidate exposed by GetFilterUnit(). */
bool WC3_LuaEvaluateFilter(wc3Lua_t *L, int function_index, void *unit, bool *accepted);
void *WC3_LuaFilterUnit(wc3Lua_t const *L);

#endif
