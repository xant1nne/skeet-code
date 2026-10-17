#include "agent.h"
#include "backend.h"
#include "config.h"
#include "context.h"
#include "http.h"
#include "log.h"
#include "permissions.h"
#include "plugins/plugin_loader.h"
#include "plugins/lua_host.h"
#include "tools/builtins.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKEET_VERSION "1.0.0"

static volatile sig_atomic_t g_cancel = 0;

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -c, --config <path>    Path to config file\n");
    printf("  -m, --model <name>     Model name to use\n");
    printf("  -b, --backend <name>   Backend to use (ollama, llama)\n");
    printf("  -H, --host <url>       Backend host URL\n");
    printf("  -w, --workspace <dir>  Workspace directory (defaults to current dir)\n");
    printf("  -p, --plugin <path>    Load dynamic library plugin (.so/.dll)\n");
    printf("  -L, --lua <path>       Load Lua plugin script (.lua)\n");
    printf("  -y, --yes              Auto-confirm all tool executions (dangerous)\n");
    printf("      --ask              Ask confirmation for mutating tools (default)\n");
    printf("      --readonly         Strictly allow only read-only tools\n");
    printf("  -l, --list-models      List available models from backend and exit\n");
    printf("  -v, --version          Show version\n");
    printf("  -h, --help             Show this help message\n");
}

int main(int argc, char **argv) {
    const char *config_path = NULL;
    const char *cli_model = NULL;
    const char *cli_backend = NULL;
    const char *cli_host = NULL;
    const char *cli_workspace = ".";
    const char *cli_plugins[32];
    size_t cli_plugins_count = 0;
    const char *cli_lua[32];
    size_t cli_lua_count = 0;
    int flag_list_models = 0;
    PermMode perm_mode = PERM_MODE_ASK;

    enum {
        OPT_ASK = 1000,
        OPT_READONLY
    };

    static struct option long_options[] = {
        {"config",      required_argument, 0, 'c'},
        {"model",       required_argument, 0, 'm'},
        {"backend",     required_argument, 0, 'b'},
        {"host",        required_argument, 0, 'H'},
        {"workspace",   required_argument, 0, 'w'},
        {"plugin",      required_argument, 0, 'p'},
        {"lua",         required_argument, 0, 'L'},
        {"yes",         no_argument,       0, 'y'},
        {"ask",         no_argument,       0, OPT_ASK},
        {"readonly",    no_argument,       0, OPT_READONLY},
        {"list-models", no_argument,       0, 'l'},
        {"version",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:m:b:H:w:p:L:ylvh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            config_path = optarg;
            break;
        case 'm':
            cli_model = optarg;
            break;
        case 'b':
            cli_backend = optarg;
            break;
        case 'H':
            cli_host = optarg;
            break;
        case 'w':
            cli_workspace = optarg;
            break;
        case 'p':
            if (cli_plugins_count < sizeof(cli_plugins) / sizeof(cli_plugins[0])) {
                cli_plugins[cli_plugins_count++] = optarg;
            }
            break;
        case 'L':
            if (cli_lua_count < sizeof(cli_lua) / sizeof(cli_lua[0])) {
                cli_lua[cli_lua_count++] = optarg;
            }
            break;
        case 'y':
            perm_mode = PERM_MODE_YES;
            break;
        case OPT_ASK:
            perm_mode = PERM_MODE_ASK;
            break;
        case OPT_READONLY:
            perm_mode = PERM_MODE_READONLY;
            break;
        case 'l':
            flag_list_models = 1;
            break;
        case 'v':
            printf("skeet %s\n", SKEET_VERSION);
            return 0;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    Config cfg;
    if (config_load(&cfg, config_path) != 0) {
        fprintf(stderr, "skeet: error loading config\n");
        return 1;
    }

    if (cli_backend) {
        free(cfg.backend);
        cfg.backend = strdup(cli_backend);
    }
    if (cli_model) {
        free(cfg.model);
        cfg.model = strdup(cli_model);
    }
    if (cli_host) {
        if (strcmp(cfg.backend, "ollama") == 0) {
            free(cfg.ollama_host);
            cfg.ollama_host = strdup(cli_host);
        } else {
            free(cfg.llama_host);
            cfg.llama_host = strdup(cli_host);
        }
    }

    char *log_path = config_log_path(&cfg);
    log_open(log_path);
    free(log_path);

    log_info("skeet starting (backend=%s, model=%s, workspace=%s)", cfg.backend, cfg.model, cli_workspace);

    if (http_init() != 0) {
        fprintf(stderr, "skeet: failed to initialize curl\n");
        config_free(&cfg);
        log_close();
        return 1;
    }

    Backend *backend = NULL;
    if (strcmp(cfg.backend, "ollama") == 0) {
        backend = backend_ollama_new(cfg.ollama_host);
    } else if (strcmp(cfg.backend, "llama") == 0) {
        backend = backend_llama_new(cfg.llama_server, cfg.llama_model, cfg.llama_host, 8080, cfg.llama_flags);
    } else {
        fprintf(stderr, "skeet: backend '%s' not supported in this build\n", cfg.backend);
        http_cleanup();
        config_free(&cfg);
        log_close();
        return 1;
    }

    if (!backend) {
        fprintf(stderr, "skeet: failed to initialize backend '%s'\n", cfg.backend);
        http_cleanup();
        config_free(&cfg);
        log_close();
        return 1;
    }

    if (flag_list_models) {
        ModelList list;
        int rc = backend->list_models(backend, &list);
        if (rc != 0) {
            fprintf(stderr, "skeet: failed to list models from %s\n", backend->name);
        } else {
            printf("Available models (%zu):\n", list.count);
            for (size_t i = 0; i < list.count; i++) {
                printf("  - %s\n", list.items[i].name);
            }
            model_list_free(&list);
        }
        backend_free(backend);
        http_cleanup();
        config_free(&cfg);
        log_close();
        return rc == 0 ? 0 : 1;
    }

    /* Auto-detect model if unset */
    if (!cfg.model || cfg.model[0] == '\0') {
        ModelList list;
        int rc = backend->list_models(backend, &list);
        if (rc == 0 && list.count > 0) {
            free(cfg.model);
            cfg.model = strdup(list.items[0].name);
            log_info("Selected first available model: %s", cfg.model);
            model_list_free(&list);
        } else {
            if (rc == 0) {
                model_list_free(&list);
            }
            fprintf(stderr, "skeet: no model specified in config or arguments, and none discovered\n");
            fprintf(stderr, "Specify one with --model <name>\n");
            backend_free(backend);
            http_cleanup();
            config_free(&cfg);
            log_close();
            return 1;
        }
    }

    if (backend->start && backend->start(backend, cfg.model) != 0) {
        fprintf(stderr, "skeet: backend failed to start for model '%s'\n", cfg.model);
        backend_free(backend);
        http_cleanup();
        config_free(&cfg);
        log_close();
        return 1;
    }

    /* Create master SkeetCtx containing registries, arenas, history, perms */
    SkeetCtx *ctx = skeet_ctx_new(&cfg, backend, &g_cancel, cli_workspace);
    if (!ctx) {
        fprintf(stderr, "skeet: failed to initialize application context\n");
        backend_free(backend);
        http_cleanup();
        config_free(&cfg);
        log_close();
        return 1;
    }

    perm_set_mode(ctx, perm_mode);
    tools_register_all_builtins(ctx);

    /* Initialize dynamic plugin manager (.so / .dll / cdylib) */
    PluginManager *pm = plugin_manager_new(ctx);
    if (pm) {
        plugin_load_directory(pm, "plugins");
        for (size_t i = 0; i < cli_plugins_count; i++) {
            plugin_load(pm, cli_plugins[i]);
        }
    }

    /* Initialize Lua plugin host (LuaJIT / Lua 5.1) */
    LuaHost *lua_host = lua_host_new(ctx);
    if (lua_host_is_available(lua_host)) {
        lua_host_load_directory(lua_host, "plugins");
        lua_host_load_directory(lua_host, ".skeet/plugins");
        for (size_t i = 0; i < cli_lua_count; i++) {
            lua_host_load_file(lua_host, cli_lua[i]);
        }
    }

    int rc = agent_repl(ctx, cfg.model);

    if (lua_host) {
        lua_host_free(lua_host);
    }

    if (pm) {
        plugin_manager_free(pm);
    }

    skeet_ctx_free(ctx);
    backend_free(backend);
    http_cleanup();
    config_free(&cfg);
    log_close();

    return rc;
}
