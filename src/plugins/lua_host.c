#include "lua_host.h"
#include "compat.h"
#include "log.h"
#include "tools/registry.h"
#include "commands.h"
#include "events.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SKEET_OS_WINDOWS)
    #include <windows.h>
    #include <io.h>
#else
    #include <dlfcn.h>
    #include <dirent.h>
    #include <sys/types.h>
#endif

#define LUA_REGISTRYINDEX (-10000)
#define LUA_GLOBALSINDEX  (-10002)

#define LUA_TNIL          0
#define LUA_TBOOLEAN      1
#define LUA_TNUMBER       3
#define LUA_TSTRING       4
#define LUA_TTABLE        5
#define LUA_TFUNCTION     6

typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State *L);
typedef double lua_Number;

typedef struct LuaApi {
    void *lib;
    lua_State *(*luaL_newstate)(void);
    void (*lua_close)(lua_State *L);
    void (*luaL_openlibs)(lua_State *L);
    int (*luaL_loadfile)(lua_State *L, const char *filename);
    int (*luaL_loadstring)(lua_State *L, const char *s);
    int (*lua_pcall)(lua_State *L, int nargs, int nresults, int errfunc);
    void (*lua_getfield)(lua_State *L, int idx, const char *k);
    void (*lua_setfield)(lua_State *L, int idx, const char *k);
    void (*lua_pushcclosure)(lua_State *L, lua_CFunction fn, int n);
    void (*lua_pushstring)(lua_State *L, const char *s);
    void (*lua_pushlstring)(lua_State *L, const char *s, size_t len);
    void (*lua_pushnumber)(lua_State *L, lua_Number n);
    void (*lua_pushboolean)(lua_State *L, int b);
    void (*lua_pushnil)(lua_State *L);
    const char *(*lua_tolstring)(lua_State *L, int idx, size_t *len);
    lua_Number (*lua_tonumber)(lua_State *L, int idx);
    int (*lua_toboolean)(lua_State *L, int idx);
    int (*lua_type)(lua_State *L, int idx);
    int (*lua_next)(lua_State *L, int idx);
    int (*luaL_ref)(lua_State *L, int t);
    void (*luaL_unref)(lua_State *L, int t, int ref);
    void (*lua_rawgeti)(lua_State *L, int idx, int n);
    void (*lua_createtable)(lua_State *L, int narr, int nrec);
    void (*lua_settop)(lua_State *L, int idx);
    int (*lua_gettop)(lua_State *L);
} LuaApi;

typedef struct LuaToolRef {
    int func_ref;
    char *name;
    char *owner;
} LuaToolRef;

typedef struct LuaCmdRef {
    int func_ref;
    char *name;
    char *owner;
} LuaCmdRef;

typedef struct LuaEventRef {
    int func_ref;
    EventType type;
    char *owner;
} LuaEventRef;

struct LuaHost {
    SkeetCtx *ctx;
    LuaApi api;
    lua_State *L;
    char current_owner[256];

    LuaToolRef *tools;
    size_t tool_count, tool_cap;

    LuaCmdRef *cmds;
    size_t cmd_count, cmd_cap;

    LuaEventRef *events;
    size_t event_count, event_cap;
};

static void *os_load_lib(const char *name) {
#if defined(SKEET_OS_WINDOWS)
    return (void *)LoadLibraryA(name);
#else
    return dlopen(name, RTLD_NOW | RTLD_GLOBAL);
#endif
}

static void *os_resolve_sym(void *h, const char *sym) {
#if defined(SKEET_OS_WINDOWS)
    return (void *)GetProcAddress((HMODULE)h, sym);
#else
    return dlsym(h, sym);
#endif
}

static void os_unload_lib(void *h) {
#if defined(SKEET_OS_WINDOWS)
    FreeLibrary((HMODULE)h);
#else
    dlclose(h);
#endif
}

static int init_lua_api(LuaApi *api) {
    memset(api, 0, sizeof(*api));

    static const char *CANDIDATES[] = {
#if defined(SKEET_OS_WINDOWS)
        "luajit.dll", "lua51.dll", "lua5.1.dll"
#elif defined(SKEET_OS_MACOS)
        "libluajit-5.1.2.dylib", "libluajit-5.1.dylib", "liblua5.1.dylib"
#else
        "libluajit-5.1.so.2", "libluajit-5.1.so", "liblua5.1.so.0", "liblua5.1.so", "liblua.so"
#endif
    };

    void *lib = NULL;
    for (size_t i = 0; i < sizeof(CANDIDATES) / sizeof(CANDIDATES[0]); i++) {
        lib = os_load_lib(CANDIDATES[i]);
        if (lib) {
            log_info("[lua] Found Lua runtime library: %s", CANDIDATES[i]);
            break;
        }
    }

    if (!lib) {
        log_info("[lua] LuaJIT / Lua 5.1 dynamic library not found on system. Lua plugins disabled.");
        return -1;
    }

    api->lib = lib;
    api->luaL_newstate   = (lua_State *(*)(void))os_resolve_sym(lib, "luaL_newstate");
    api->lua_close       = (void (*)(lua_State *))os_resolve_sym(lib, "lua_close");
    api->luaL_openlibs   = (void (*)(lua_State *))os_resolve_sym(lib, "luaL_openlibs");
    api->luaL_loadfile   = (int (*)(lua_State *, const char *))os_resolve_sym(lib, "luaL_loadfile");
    api->luaL_loadstring = (int (*)(lua_State *, const char *))os_resolve_sym(lib, "luaL_loadstring");
    api->lua_pcall       = (int (*)(lua_State *, int, int, int))os_resolve_sym(lib, "lua_pcall");
    api->lua_getfield    = (void (*)(lua_State *, int, const char *))os_resolve_sym(lib, "lua_getfield");
    api->lua_setfield    = (void (*)(lua_State *, int, const char *))os_resolve_sym(lib, "lua_setfield");
    api->lua_pushcclosure= (void (*)(lua_State *, lua_CFunction, int))os_resolve_sym(lib, "lua_pushcclosure");
    api->lua_pushstring  = (void (*)(lua_State *, const char *))os_resolve_sym(lib, "lua_pushstring");
    api->lua_pushlstring = (void (*)(lua_State *, const char *, size_t))os_resolve_sym(lib, "lua_pushlstring");
    api->lua_pushnumber  = (void (*)(lua_State *, lua_Number))os_resolve_sym(lib, "lua_pushnumber");
    api->lua_pushboolean = (void (*)(lua_State *, int))os_resolve_sym(lib, "lua_pushboolean");
    api->lua_pushnil     = (void (*)(lua_State *))os_resolve_sym(lib, "lua_pushnil");
    api->lua_tolstring   = (const char *(*)(lua_State *, int, size_t *))os_resolve_sym(lib, "lua_tolstring");
    api->lua_tonumber    = (lua_Number (*)(lua_State *, int))os_resolve_sym(lib, "lua_tonumber");
    api->lua_toboolean   = (int (*)(lua_State *, int))os_resolve_sym(lib, "lua_toboolean");
    api->lua_type        = (int (*)(lua_State *, int))os_resolve_sym(lib, "lua_type");
    api->lua_next        = (int (*)(lua_State *, int))os_resolve_sym(lib, "lua_next");
    api->luaL_ref        = (int (*)(lua_State *, int))os_resolve_sym(lib, "luaL_ref");
    api->luaL_unref      = (void (*)(lua_State *, int, int))os_resolve_sym(lib, "luaL_unref");
    api->lua_rawgeti     = (void (*)(lua_State *, int, int))os_resolve_sym(lib, "lua_rawgeti");
    api->lua_createtable = (void (*)(lua_State *, int, int))os_resolve_sym(lib, "lua_createtable");
    api->lua_settop      = (void (*)(lua_State *, int))os_resolve_sym(lib, "lua_settop");
    api->lua_gettop      = (int (*)(lua_State *))os_resolve_sym(lib, "lua_gettop");

    if (!api->luaL_newstate || !api->lua_close || !api->luaL_openlibs || !api->lua_pcall) {
        log_error("[lua] Missing essential Lua C API symbols in dynamic library");
        os_unload_lib(lib);
        api->lib = NULL;
        return -1;
    }

    return 0;
}

/* Helper to push cJSON hierarchy into Lua table */
static void push_cjson_to_lua(LuaHost *host, cJSON *node) {
    if (!node) {
        host->api.lua_pushnil(host->L);
        return;
    }
    switch (node->type) {
    case cJSON_False: host->api.lua_pushboolean(host->L, 0); break;
    case cJSON_True:  host->api.lua_pushboolean(host->L, 1); break;
    case cJSON_NULL:  host->api.lua_pushnil(host->L); break;
    case cJSON_Number: host->api.lua_pushnumber(host->L, node->valuedouble); break;
    case cJSON_String: host->api.lua_pushstring(host->L, node->valuestring); break;
    case cJSON_Array: {
        host->api.lua_createtable(host->L, cJSON_GetArraySize(node), 0);
        int idx = 1;
        for (cJSON *child = node->child; child; child = child->next) {
            push_cjson_to_lua(host, child);
            host->api.lua_rawgeti(host->L, -2, idx++);
        }
        break;
    }
    case cJSON_Object: {
        host->api.lua_createtable(host->L, 0, cJSON_GetArraySize(node));
        for (cJSON *child = node->child; child; child = child->next) {
            push_cjson_to_lua(host, child);
            host->api.lua_setfield(host->L, -2, child->string);
        }
        break;
    }
    default:
        host->api.lua_pushnil(host->L);
        break;
    }
}

/* Trampoline when LLM invokes a Lua-registered tool */
static int lua_tool_trampoline(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata) {
    LuaToolRef *ref = (LuaToolRef *)userdata;
    if (!ref || ref->func_ref <= 0) {
        result->is_error = 1;
        result->content = str_from_cstr("Invalid Lua tool reference");
        return -1;
    }

    LuaHost *host = (LuaHost *)ctx->pump_userdata; /* host stored in ctx or userdata */
    if (!host || !host->L) {
        result->is_error = 1;
        result->content = str_from_cstr("Lua host runtime is unavailable");
        return -1;
    }

    /* Push registered Lua function */
    host->api.lua_rawgeti(host->L, LUA_REGISTRYINDEX, ref->func_ref);

    /* Push args: try JSON parse to table, otherwise raw string */
    char *args_str = arena_strndup(&ctx->turn_arena, call->args_json.ptr, call->args_json.len);
    cJSON *parsed = cJSON_Parse(args_str);
    if (parsed) {
        push_cjson_to_lua(host, parsed);
        cJSON_Delete(parsed);
    } else {
        host->api.lua_pushstring(host->L, args_str ? args_str : "");
    }

    /* Call function with 1 argument, expecting 1 return value */
    if (host->api.lua_pcall(host->L, 1, 1, 0) != 0) {
        const char *err = host->api.lua_tolstring(host->L, -1, NULL);
        result->is_error = 1;
        char *arena_err = arena_strndup(&ctx->turn_arena, err ? err : "Lua error", strlen(err ? err : "Lua error"));
        result->content = (Str){ .ptr = arena_err, .len = strlen(arena_err) };
        host->api.lua_settop(host->L, -2);
        return -1;
    }

    const char *ret_str = host->api.lua_tolstring(host->L, -1, NULL);
    if (ret_str) {
        size_t rlen = strlen(ret_str);
        char *arena_copy = arena_strndup(&ctx->turn_arena, ret_str, rlen);
        result->content = (Str){ .ptr = arena_copy, .len = rlen };
        result->is_error = 0;
    } else {
        result->content = str_from_cstr("");
        result->is_error = 0;
    }

    host->api.lua_settop(host->L, -2);
    return 0;
}

/* Trampoline for slash commands defined in Lua */
static int lua_cmd_trampoline(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)ctx;
    LuaCmdRef *ref = (LuaCmdRef *)userdata;
    if (!ref || ref->func_ref <= 0) return -1;

    LuaHost *host = (LuaHost *)ctx->pump_userdata;
    if (!host || !host->L) return -1;

    host->api.lua_rawgeti(host->L, LUA_REGISTRYINDEX, ref->func_ref);
    host->api.lua_pushstring(host->L, args ? args : "");
    if (host->api.lua_pcall(host->L, 1, 0, 0) != 0) {
        const char *err = host->api.lua_tolstring(host->L, -1, NULL);
        printf("[lua error] %s\n", err ? err : "");
        host->api.lua_settop(host->L, -2);
        return -1;
    }
    return 0;
}

/* Lua C-function: skeet.register_tool({ name, description, schema, run }) */
static int l_skeet_register_tool(lua_State *L) {
    LuaHost *host = NULL;
    /* Host pointer stored in registry */
    /* We can retrieve host pointer from upvalue */
    // Using lua_touserdata on upvalue 1
    // The binding will inject upvalue
    return 0;
}

static LuaHost *g_active_host = NULL;

static int lua_cb_register_tool(lua_State *L) {
    if (!g_active_host) return 0;
    LuaHost *host = g_active_host;

    if (host->api.lua_type(L, 1) != LUA_TTABLE) {
        host->api.lua_pushstring(L, "register_tool expects a table argument");
        return 1;
    }

    host->api.lua_getfield(L, 1, "name");
    const char *name = host->api.lua_tolstring(L, -1, NULL);

    host->api.lua_getfield(L, 1, "description");
    const char *desc = host->api.lua_tolstring(L, -1, NULL);

    host->api.lua_getfield(L, 1, "schema");
    const char *schema = host->api.lua_tolstring(L, -1, NULL);
    if (!schema) schema = "{\"type\":\"object\"}";

    host->api.lua_getfield(L, 1, "run");
    if (host->api.lua_type(L, -1) != LUA_TFUNCTION) {
        host->api.lua_settop(L, -5);
        return 0;
    }

    int func_ref = host->api.luaL_ref(L, LUA_REGISTRYINDEX);
    host->api.lua_settop(L, -4); /* clean up name, desc, schema */

    if (host->tool_count >= host->tool_cap) {
        size_t ncap = host->tool_cap ? host->tool_cap * 2 : 16;
        host->tools = realloc(host->tools, ncap * sizeof(LuaToolRef));
        host->tool_cap = ncap;
    }

    LuaToolRef *tr = &host->tools[host->tool_count++];
    tr->func_ref = func_ref;
    tr->name = strdup(name ? name : "unnamed_lua_tool");
    tr->owner = strdup(host->current_owner[0] ? host->current_owner : "lua_plugin");

    ToolDef def = {
        .size = sizeof(ToolDef),
        .name = tr->name,
        .description = desc ? desc : "Lua plugin tool",
        .schema_json = schema,
        .flags = TOOL_READONLY,
        .run = lua_tool_trampoline,
        .userdata = tr,
        .owner = tr->owner
    };

    tool_register(host->ctx, &def);
    log_info("[lua] Registered tool '%s' from %s", tr->name, tr->owner);

    host->api.lua_pushboolean(L, 1);
    return 1;
}

static int lua_cb_register_command(lua_State *L) {
    if (!g_active_host) return 0;
    LuaHost *host = g_active_host;

    const char *name = host->api.lua_tolstring(L, 1, NULL);
    if (!name || name[0] != '/') return 0;

    int fn_idx = 2;
    const char *help = "Lua command";
    if (host->api.lua_type(L, 2) == LUA_TSTRING) {
        help = host->api.lua_tolstring(L, 2, NULL);
        fn_idx = 3;
    }

    if (host->api.lua_type(L, fn_idx) != LUA_TFUNCTION) return 0;

    host->api.lua_pushvalue(L, fn_idx);
    int func_ref = host->api.luaL_ref(L, LUA_REGISTRYINDEX);

    if (host->cmd_count >= host->cmd_cap) {
        size_t ncap = host->cmd_cap ? host->cmd_cap * 2 : 8;
        host->cmds = realloc(host->cmds, ncap * sizeof(LuaCmdRef));
        host->cmd_cap = ncap;
    }

    LuaCmdRef *cr = &host->cmds[host->cmd_count++];
    cr->func_ref = func_ref;
    cr->name = strdup(name);
    cr->owner = strdup(host->current_owner[0] ? host->current_owner : "lua_plugin");

    CommandDef def = {
        .size = sizeof(CommandDef),
        .name = cr->name,
        .help = help,
        .handler = lua_cmd_trampoline,
        .userdata = cr,
        .owner = cr->owner
    };

    command_register(host->ctx, &def);
    log_info("[lua] Registered command '%s' from %s", cr->name, cr->owner);

    host->api.lua_pushboolean(L, 1);
    return 1;
}

static int lua_cb_log(lua_State *L) {
    if (!g_active_host) return 0;
    LuaHost *host = g_active_host;

    int level = (int)host->api.lua_tonumber(L, 1);
    const char *msg = host->api.lua_tolstring(L, 2, NULL);
    if (msg) {
        switch (level) {
        case 0: log_debug("[lua] %s", msg); break;
        case 1: log_info("[lua] %s", msg); break;
        case 2: log_warn("[lua] %s", msg); break;
        case 3: log_error("[lua] %s", msg); break;
        default: log_info("[lua] %s", msg); break;
        }
    }
    return 0;
}

LuaHost *lua_host_new(SkeetCtx *ctx) {
    if (!ctx) return NULL;

    LuaHost *host = calloc(1, sizeof(LuaHost));
    if (!host) return NULL;
    host->ctx = ctx;

    if (init_lua_api(&host->api) != 0) {
        /* Lua dynamic lib not found, host runs in inactive mode */
        return host;
    }

    host->L = host->api.luaL_newstate();
    if (!host->L) {
        log_error("[lua] Failed to create Lua state");
        return host;
    }

    host->api.luaL_openlibs(host->L);

    g_active_host = host;

    /* Register skeet table in globals */
    host->api.lua_createtable(host->L, 0, 8);

    host->api.lua_pushcclosure(host->L, lua_cb_register_tool, 0);
    host->api.lua_setfield(host->L, -2, "register_tool");

    host->api.lua_pushcclosure(host->L, lua_cb_register_command, 0);
    host->api.lua_setfield(host->L, -2, "register_command");

    host->api.lua_pushcclosure(host->L, lua_cb_log, 0);
    host->api.lua_setfield(host->L, -2, "log");

    /* Constants */
    host->api.lua_pushnumber(host->L, 0);
    host->api.lua_setfield(host->L, -2, "LOG_DEBUG");
    host->api.lua_pushnumber(host->L, 1);
    host->api.lua_setfield(host->L, -2, "LOG_INFO");
    host->api.lua_pushnumber(host->L, 2);
    host->api.lua_setfield(host->L, -2, "LOG_WARN");
    host->api.lua_pushnumber(host->L, 3);
    host->api.lua_setfield(host->L, -2, "LOG_ERROR");

    host->api.lua_setfield(host->L, LUA_GLOBALSINDEX, "skeet");

    log_info("[lua] Lua plugin host initialized successfully");
    return host;
}

int lua_host_is_available(const LuaHost *host) {
    return host && host->L != NULL;
}

void lua_host_free(LuaHost *host) {
    if (!host) return;

    if (g_active_host == host) {
        g_active_host = NULL;
    }

    /* Unregister tools & commands from host */
    for (size_t i = 0; i < host->tool_count; i++) {
        if (host->tools[i].owner && host->ctx) {
            tool_unregister_owner(host->ctx, host->tools[i].owner);
        }
        if (host->L && host->tools[i].func_ref > 0) {
            host->api.luaL_unref(host->L, LUA_REGISTRYINDEX, host->tools[i].func_ref);
        }
        free(host->tools[i].name);
        free(host->tools[i].owner);
    }
    free(host->tools);

    for (size_t i = 0; i < host->cmd_count; i++) {
        if (host->cmds[i].owner && host->ctx) {
            command_unregister_owner(host->ctx, host->cmds[i].owner);
        }
        if (host->L && host->cmds[i].func_ref > 0) {
            host->api.luaL_unref(host->L, LUA_REGISTRYINDEX, host->cmds[i].func_ref);
        }
        free(host->cmds[i].name);
        free(host->cmds[i].owner);
    }
    free(host->cmds);

    free(host->events);

    if (host->L) {
        host->api.lua_close(host->L);
        host->L = NULL;
    }

    if (host->api.lib) {
        os_unload_lib(host->api.lib);
        host->api.lib = NULL;
    }

    free(host);
}

int lua_host_load_file(LuaHost *host, const char *file_path) {
    if (!lua_host_is_available(host) || !file_path) return -1;

    g_active_host = host;
    snprintf(host->current_owner, sizeof(host->current_owner), "lua:%s", file_path);

    if (host->api.luaL_loadfile(host->L, file_path) != 0) {
        const char *err = host->api.lua_tolstring(host->L, -1, NULL);
        log_error("[lua] Failed to load script '%s': %s", file_path, err ? err : "");
        host->api.lua_settop(host->L, -2);
        return -1;
    }

    if (host->api.lua_pcall(host->L, 0, 0, 0) != 0) {
        const char *err = host->api.lua_tolstring(host->L, -1, NULL);
        log_error("[lua] Runtime error in script '%s': %s", file_path, err ? err : "");
        host->api.lua_settop(host->L, -2);
        return -1;
    }

    log_info("[lua] Successfully executed Lua script: %s", file_path);
    host->current_owner[0] = '\0';
    return 0;
}

int lua_host_load_directory(LuaHost *host, const char *dir_path) {
    if (!lua_host_is_available(host) || !dir_path) return 0;
    int count = 0;

#if defined(SKEET_OS_WINDOWS)
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s\\*.lua", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char full[PATH_MAX];
                snprintf(full, sizeof(full), "%s\\%s", dir_path, fd.cFileName);
                if (lua_host_load_file(host, full) == 0) {
                    count++;
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR *d = opendir(dir_path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            size_t l = strlen(de->d_name);
            if (l > 4 && strcmp(de->d_name + l - 4, ".lua") == 0) {
                char full[PATH_MAX];
                snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
                if (lua_host_load_file(host, full) == 0) {
                    count++;
                }
            }
        }
        closedir(d);
    }
#endif

    return count;
}

int lua_host_eval(LuaHost *host, const char *chunk_str) {
    if (!lua_host_is_available(host) || !chunk_str) return -1;
    g_active_host = host;

    if (host->api.luaL_loadstring(host->L, chunk_str) != 0) {
        const char *err = host->api.lua_tolstring(host->L, -1, NULL);
        log_error("[lua] Compile error in chunk: %s", err ? err : "");
        host->api.lua_settop(host->L, -2);
        return -1;
    }

    if (host->api.lua_pcall(host->L, 0, 0, 0) != 0) {
        const char *err = host->api.lua_tolstring(host->L, -1, NULL);
        log_error("[lua] Runtime error in chunk: %s", err ? err : "");
        host->api.lua_settop(host->L, -2);
        return -1;
    }

    return 0;
}

