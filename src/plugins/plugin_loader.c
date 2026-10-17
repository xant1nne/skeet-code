#include "plugin_loader.h"
#include "compat.h"
#include "log.h"
#include "tools/registry.h"
#include "commands.h"

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

typedef struct ToolBinding {
    skeet_tool_exec_fn exec;
    void *userdata;
    char *owner;
} ToolBinding;

typedef struct CmdBinding {
    skeet_cmd_exec_fn exec;
    void *userdata;
    char *owner;
} CmdBinding;

typedef struct HostContextBridge {
    SkeetCtx *ctx;
    const char *owner;
    ToolBinding *tools;
    size_t tool_count;
    size_t tool_cap;
    CmdBinding *cmds;
    size_t cmd_count;
    size_t cmd_cap;
} HostContextBridge;

static int plugin_tool_trampoline(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata) {
    ToolBinding *tb = (ToolBinding *)userdata;
    if (!tb || !tb->exec) {
        result->is_error = 1;
        result->content = str_from_cstr("Plugin tool error: invalid handler");
        return -1;
    }

    char *args_buf = arena_strndup(&ctx->turn_arena, call->args_json.ptr, call->args_json.len);
    char *out_res = NULL;
    int is_err = 0;

    int rc = tb->exec(args_buf, &out_res, &is_err, tb->userdata);

    result->is_error = is_err || (rc != 0);
    if (out_res) {
        size_t rlen = strlen(out_res);
        char *arena_copy = arena_strndup(&ctx->turn_arena, out_res, rlen);
        result->content = (Str){ .ptr = arena_copy, .len = rlen };
        free(out_res);
    } else {
        result->content = str_from_cstr(result->is_error ? "Plugin tool returned error with no message" : "");
    }
    return rc;
}

static int plugin_cmd_trampoline(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)ctx;
    CmdBinding *cb = (CmdBinding *)userdata;
    if (!cb || !cb->exec) return -1;

    char *out_msg = NULL;
    int rc = cb->exec(args, &out_msg, cb->userdata);
    if (out_msg) {
        printf("%s\n", out_msg);
        free(out_msg);
    }
    return rc;
}

static void host_log_cb(int level, const char *msg) {
    switch (level) {
    case SKEET_LOG_DEBUG: log_debug("[plugin] %s", msg); break;
    case SKEET_LOG_INFO:  log_info("[plugin] %s", msg); break;
    case SKEET_LOG_WARN:  log_warn("[plugin] %s", msg); break;
    case SKEET_LOG_ERROR: log_error("[plugin] %s", msg); break;
    default: log_info("[plugin] %s", msg); break;
    }
}

static void host_free_cb(void *ptr) {
    free(ptr);
}

static int host_register_tool_cb(
    const char *name,
    const char *description,
    const char *schema_json,
    uint32_t flags,
    skeet_tool_exec_fn exec,
    void *userdata
) {
    HostContextBridge *bridge = (HostContextBridge *)userdata;
    if (!bridge || !bridge->ctx || !name) return SKEET_ERR;

    if (bridge->tool_count >= bridge->tool_cap) {
        size_t ncap = bridge->tool_cap ? bridge->tool_cap * 2 : 16;
        ToolBinding *nt = realloc(bridge->tools, ncap * sizeof(ToolBinding));
        if (!nt) return SKEET_ERR;
        bridge->tools = nt;
        bridge->tool_cap = ncap;
    }

    ToolBinding *tb = &bridge->tools[bridge->tool_count++];
    tb->exec = exec;
    tb->userdata = NULL;
    tb->owner = strdup(bridge->owner);

    uint32_t tool_flags = 0;
    if (flags & SKEET_TOOL_READONLY) tool_flags |= TOOL_READONLY;
    if (flags & SKEET_TOOL_NEEDS_CONFIRM) tool_flags |= TOOL_NEEDS_CONFIRM;

    ToolDef def = {
        .size = sizeof(ToolDef),
        .name = name,
        .description = description,
        .schema_json = schema_json,
        .flags = tool_flags,
        .run = plugin_tool_trampoline,
        .userdata = tb,
        .owner = bridge->owner
    };

    return tool_register(bridge->ctx, &def);
}

static int host_register_cmd_cb(
    const char *name,
    const char *help,
    skeet_cmd_exec_fn exec,
    void *userdata
) {
    HostContextBridge *bridge = (HostContextBridge *)userdata;
    if (!bridge || !bridge->ctx || !name) return SKEET_ERR;

    if (bridge->cmd_count >= bridge->cmd_cap) {
        size_t ncap = bridge->cmd_cap ? bridge->cmd_cap * 2 : 8;
        CmdBinding *nc = realloc(bridge->cmds, ncap * sizeof(CmdBinding));
        if (!nc) return SKEET_ERR;
        bridge->cmds = nc;
        bridge->cmd_cap = ncap;
    }

    CmdBinding *cb = &bridge->cmds[bridge->cmd_count++];
    cb->exec = exec;
    cb->userdata = NULL;
    cb->owner = strdup(bridge->owner);

    CommandDef def = {
        .size = sizeof(CommandDef),
        .name = name,
        .help = help,
        .handler = plugin_cmd_trampoline,
        .userdata = cb,
        .owner = bridge->owner
    };

    return command_register(bridge->ctx, &def);
}

PluginManager *plugin_manager_new(SkeetCtx *ctx) {
    if (!ctx) return NULL;
    PluginManager *pm = calloc(1, sizeof(PluginManager));
    if (!pm) return NULL;
    pm->ctx = ctx;
    pm->cap = 8;
    pm->plugins = malloc(pm->cap * sizeof(PluginInstance));
    return pm;
}

void plugin_manager_free(PluginManager *pm) {
    if (!pm) return;
    plugin_unload_all(pm);
    free(pm->plugins);
    free(pm);
}

static void *os_dlopen(const char *path) {
#if defined(SKEET_OS_WINDOWS)
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *os_dlsym(void *handle, const char *symbol) {
#if defined(SKEET_OS_WINDOWS)
    return (void *)GetProcAddress((HMODULE)handle, symbol);
#else
    return dlsym(handle, symbol);
#endif
}

static void os_dlclose(void *handle) {
#if defined(SKEET_OS_WINDOWS)
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

int plugin_load(PluginManager *pm, const char *path) {
    if (!pm || !path) return -1;

    void *lib = os_dlopen(path);
    if (!lib) {
        log_warn("Failed to open plugin dynamic library: %s", path);
        return -1;
    }

    uint32_t (*abi_fn)(void) = (uint32_t (*)(void))os_dlsym(lib, "skeet_plugin_abi_version");
    if (!abi_fn) {
        log_error("Plugin %s does not export skeet_plugin_abi_version", path);
        os_dlclose(lib);
        return -1;
    }

    uint32_t abi_ver = abi_fn();
    if (abi_ver != SKEET_PLUGIN_ABI_VERSION) {
        log_error("Plugin %s ABI mismatch (expected %u, got %u)", path, SKEET_PLUGIN_ABI_VERSION, abi_ver);
        os_dlclose(lib);
        return -1;
    }

    int (*init_fn)(const SkeetHostApi *) = (int (*)(const SkeetHostApi *))os_dlsym(lib, "skeet_plugin_init");
    if (!init_fn) {
        log_error("Plugin %s does not export skeet_plugin_init", path);
        os_dlclose(lib);
        return -1;
    }

    int (*deinit_fn)(void) = (int (*)(void))os_dlsym(lib, "skeet_plugin_deinit");

    /* Create owner identifier based on file basename */
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) last_slash = strrchr(path, '\\');
    const char *owner_name = last_slash ? last_slash + 1 : path;

    HostContextBridge *bridge = calloc(1, sizeof(HostContextBridge));
    bridge->ctx = pm->ctx;
    bridge->owner = owner_name;

    SkeetHostApi host_api = {
        .size = sizeof(SkeetHostApi),
        .owner = owner_name,
        .workspace_root = pm->ctx->workspace_root,
        .register_tool = (skeet_host_register_tool_fn)host_register_tool_cb,
        .register_command = (skeet_host_register_cmd_fn)host_register_cmd_cb,
        .log = host_log_cb,
        .free_func = host_free_cb
    };

    /* Note: During init_fn, host_api and bridge are used */
    int rc = init_fn(&host_api);
    if (rc != 0) {
        log_error("Plugin %s init returned error %d", path, rc);
        os_dlclose(lib);
        free(bridge);
        return -1;
    }

    if (pm->count >= pm->cap) {
        size_t ncap = pm->cap * 2;
        PluginInstance *np = realloc(pm->plugins, ncap * sizeof(PluginInstance));
        if (np) {
            pm->plugins = np;
            pm->cap = ncap;
        }
    }

    PluginInstance *pinst = &pm->plugins[pm->count++];
    pinst->path = strdup(path);
    pinst->owner = strdup(owner_name);
    pinst->lib_handle = lib;
    pinst->deinit_fn = deinit_fn;

    log_info("Successfully loaded plugin: %s (owner=%s)", path, owner_name);
    return 0;
}

int plugin_load_directory(PluginManager *pm, const char *dir_path) {
    if (!pm || !dir_path) return -1;
    int loaded = 0;

#if defined(SKEET_OS_WINDOWS)
    char search_pattern[PATH_MAX];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.dll", dir_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, fd.cFileName);
                if (plugin_load(pm, full_path) == 0) {
                    loaded++;
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR *d = opendir(dir_path);
    if (!d) return 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        size_t len = strlen(name);
        if (len > 3 && strcmp(name + len - 3, ".so") == 0) {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);
            if (plugin_load(pm, full_path) == 0) {
                loaded++;
            }
        }
    }
    closedir(d);
#endif

    return loaded;
}

void plugin_unload_all(PluginManager *pm) {
    if (!pm) return;
    for (size_t i = 0; i < pm->count; i++) {
        PluginInstance *p = &pm->plugins[i];
        if (p->deinit_fn) {
            p->deinit_fn();
        }
        if (p->owner && pm->ctx) {
            tool_unregister_owner(pm->ctx, p->owner);
            command_unregister_owner(pm->ctx, p->owner);
        }
        if (p->lib_handle) {
            os_dlclose(p->lib_handle);
        }
        free(p->path);
        free(p->owner);
    }
    pm->count = 0;
}

