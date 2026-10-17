#ifndef SKEET_LUA_HOST_H
#define SKEET_LUA_HOST_H

#include "context.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LuaHost LuaHost;

/* Creates a LuaHost instance and attempts to initialize LuaJIT / Lua 5.1 */
LuaHost *lua_host_new(SkeetCtx *ctx);

/* Frees LuaHost, closing Lua state and unregistering all Lua-owned tools/commands */
void lua_host_free(LuaHost *host);

/* Checks if Lua runtime was found and initialized */
int lua_host_is_available(const LuaHost *host);

/* Executes a Lua script from file */
int lua_host_load_file(LuaHost *host, const char *file_path);

/* Scans directory for *.lua scripts and loads them */
int lua_host_load_directory(LuaHost *host, const char *dir_path);

/* Evaluates a raw Lua string chunk */
int lua_host_eval(LuaHost *host, const char *chunk_str);

#ifdef __cplusplus
}
#endif

#endif /* SKEET_LUA_HOST_H */

