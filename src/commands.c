#include "commands.h"
#include "context.h"
#include "tools/registry.h"
#include "tune/tune.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CommandRegistry *command_registry_new(void) {
    CommandRegistry *reg = calloc(1, sizeof(CommandRegistry));
    if (!reg) {
        return NULL;
    }
    reg->cap = 8;
    reg->items = calloc(reg->cap, sizeof(RegisteredCommand));
    if (!reg->items) {
        free(reg);
        return NULL;
    }
    return reg;
}

void command_registry_free(CommandRegistry *reg) {
    if (!reg) {
        return;
    }
    for (size_t i = 0; i < reg->count; i++) {
        free(reg->items[i].name);
        free(reg->items[i].help);
        free(reg->items[i].owner);
    }
    free(reg->items);
    free(reg);
}

int command_register(SkeetCtx *ctx, const CommandDef *def) {
    if (!ctx || !ctx->commands || !def || !def->name || !def->handler) {
        return -1;
    }

    CommandRegistry *reg = ctx->commands;
    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].name, def->name) == 0) {
            free(reg->items[i].name);
            free(reg->items[i].help);
            free(reg->items[i].owner);
            reg->items[i].def = *def;
            reg->items[i].name = strdup(def->name);
            reg->items[i].help = def->help ? strdup(def->help) : NULL;
            reg->items[i].owner = def->owner ? strdup(def->owner) : NULL;
            reg->items[i].def.name = reg->items[i].name;
            reg->items[i].def.help = reg->items[i].help;
            reg->items[i].def.owner = reg->items[i].owner;
            return 0;
        }
    }

    if (reg->count >= reg->cap) {
        size_t new_cap = reg->cap * 2;
        RegisteredCommand *new_items = realloc(reg->items, new_cap * sizeof(RegisteredCommand));
        if (!new_items) {
            return -1;
        }
        reg->items = new_items;
        reg->cap = new_cap;
    }

    RegisteredCommand *item = &reg->items[reg->count++];
    memset(item, 0, sizeof(*item));
    item->def = *def;
    item->name = strdup(def->name);
    item->help = def->help ? strdup(def->help) : NULL;
    item->owner = def->owner ? strdup(def->owner) : NULL;
    item->def.name = item->name;
    item->def.help = item->help;
    item->def.owner = item->owner;
    return 0;
}

int command_unregister_owner(SkeetCtx *ctx, const char *owner) {
    if (!ctx || !ctx->commands || !owner) {
        return 0;
    }

    CommandRegistry *reg = ctx->commands;
    size_t write_idx = 0;
    int removed = 0;
    for (size_t i = 0; i < reg->count; i++) {
        if (reg->items[i].owner && strcmp(reg->items[i].owner, owner) == 0) {
            free(reg->items[i].name);
            free(reg->items[i].help);
            free(reg->items[i].owner);
            removed++;
        } else {
            if (write_idx != i) {
                reg->items[write_idx] = reg->items[i];
            }
            write_idx++;
        }
    }
    reg->count = write_idx;
    return removed;
}

int command_dispatch(SkeetCtx *ctx, const char *line, int *handled) {
    if (handled) {
        *handled = 0;
    }
    if (!ctx || !ctx->commands || !line || line[0] != '/') {
        return 0;
    }

    char cmd_name[128];
    const char *p = line;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        p++;
    }
    size_t name_len = (size_t)(p - line);
    if (name_len >= sizeof(cmd_name)) {
        name_len = sizeof(cmd_name) - 1;
    }
    memcpy(cmd_name, line, name_len);
    cmd_name[name_len] = '\0';

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    const char *args = p;

    CommandRegistry *reg = ctx->commands;
    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].name, cmd_name) == 0) {
            if (handled) {
                *handled = 1;
            }
            return reg->items[i].def.handler(ctx, args, reg->items[i].def.userdata);
        }
    }

    printf("Unknown command: %s. Type /help for available commands.\n", cmd_name);
    if (handled) {
        *handled = 1;
    }
    return 0;
}

static int cmd_help(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)args;
    (void)userdata;
    printf("\nCommands:\n");
    CommandRegistry *reg = ctx->commands;
    for (size_t i = 0; i < reg->count; i++) {
        printf("  %-12s %s\n", reg->items[i].name, reg->items[i].help ? reg->items[i].help : "");
    }
    printf("\n");
    return 0;
}

static int cmd_tools(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)args;
    (void)userdata;
    printf("\nRegistered Tools:\n");
    size_t count = tool_registry_count(ctx);
    for (size_t i = 0; i < count; i++) {
        const ToolDef *td = tool_registry_at(ctx, i);
        printf("  %-16s %s\n", td->name, td->description ? td->description : "");
    }
    printf("\n");
    return 0;
}

static int cmd_exit(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)ctx;
    (void)args;
    (void)userdata;
    return 1;
}

static int cmd_clear(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)args;
    (void)userdata;
    if (!ctx) {
        return 0;
    }
    for (size_t i = 0; i < ctx->history_count; i++) {
        free((char *)ctx->history[i].role.ptr);
        free((char *)ctx->history[i].content.ptr);
        free((char *)ctx->history[i].tool_call_id.ptr);
        free((char *)ctx->history[i].tool_calls_json.ptr);
    }
    ctx->history_count = 0;
    printf("Conversation history cleared.\n");
    return 0;
}

static int cmd_backend(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)userdata;
    if (!ctx) {
        return 0;
    }
    while (*args == ' ' || *args == '\t') {
        args++;
    }
    if (!args[0]) {
        printf("Current backend: %s\n", ctx->backend ? ctx->backend->name : "none");
        return 0;
    }

    char bname[64];
    size_t blen = 0;
    while (args[blen] && args[blen] != ' ' && args[blen] != '\t' && blen < sizeof(bname) - 1) {
        bname[blen] = args[blen];
        blen++;
    }
    bname[blen] = '\0';

    if (strcmp(bname, "ollama") == 0) {
        if (ctx->backend) {
            backend_free(ctx->backend);
        }
        ctx->backend = backend_ollama_new(ctx->config ? ctx->config->ollama_host : NULL);
        printf("Switched backend to ollama.\n");
    } else if (strcmp(bname, "llama") == 0) {
        if (ctx->backend) {
            backend_free(ctx->backend);
        }
        ctx->backend = backend_llama_new(ctx->config ? ctx->config->llama_server : NULL,
                                         ctx->config ? ctx->config->llama_model : NULL,
                                         ctx->config ? ctx->config->llama_host : NULL,
                                         8080,
                                         ctx->config ? ctx->config->llama_flags : NULL);
        printf("Switched backend to llama.\n");
    } else {
        printf("Unknown backend: %s. Supported: ollama, llama\n", bname);
    }
    return 0;
}

static int cmd_tune(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)userdata;
    HwInfo hw;
    if (hw_detect(&hw) != 0) {
        printf("Failed to detect hardware.\n");
        return -1;
    }

    printf("\n\033[1;36mHardware detected:\033[0m\n");
    printf("  RAM: %.2f GB total, %.2f GB available\n",
           (double)hw.ram_total_bytes / (1024.0 * 1024.0 * 1024.0),
           (double)hw.ram_free_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("  CPU: %d physical cores (%d logical)\n", hw.cpu_physical_cores, hw.cpu_logical_cores);
    if (hw.has_gpu) {
        printf("  GPU: %s (%.2f GB VRAM total, %.2f GB free)\n",
               hw.gpu_name[0] ? hw.gpu_name : "Generic GPU",
               (double)hw.vram_total_bytes / (1024.0 * 1024.0 * 1024.0),
               (double)hw.vram_free_bytes / (1024.0 * 1024.0 * 1024.0));
    } else {
        printf("  GPU: None detected (CPU execution)\n");
    }

    while (args && (*args == ' ' || *args == '\t')) {
        args++;
    }

    const char *model_path = NULL;
    if (args && args[0]) {
        model_path = args;
    } else if (ctx->config && ctx->config->llama_model && ctx->config->llama_model[0]) {
        model_path = ctx->config->llama_model;
    }

    if (!model_path) {
        printf("\n\033[1;33mTip:\033[0m Specify a GGUF model path to calculate launch parameters:\n");
        printf("  /tune <path/to/model.gguf>\n\n");
        return 0;
    }

    GgufMeta meta;
    TuneParams tp;
    const char *manual_flags = ctx->config ? ctx->config->llama_flags : NULL;
    if (tune_model(model_path, manual_flags, &tp, &meta, &hw) != 0) {
        printf("Failed to inspect model or tune parameters for: %s\n", model_path);
        return -1;
    }

    printf("\n\033[1;36mModel: %s\033[0m\n", model_path);
    printf("  Architecture:     %s\n", meta.architecture[0] ? meta.architecture : "unknown");
    printf("  Layers:           %u\n", meta.block_count);
    printf("  Context window:   %u tokens\n", meta.context_length);
    printf("  File size:        %.2f GB\n", (double)meta.file_size_bytes / (1024.0 * 1024.0 * 1024.0));
    if (meta.expert_count > 0) {
        printf("  MoE Experts:      %u total, %u used\n", meta.expert_count, meta.expert_used_count);
    }

    printf("\n\033[1;32mRecommended llama-server settings:\033[0m\n");
    printf("  GPU Layers (-ngl):    %d / %u\n", tp.n_gpu_layers, meta.block_count);
    printf("  CPU Threads (-t):     %d\n", tp.threads);
    printf("  Context Size (-c):    %d\n", tp.ctx_size);
    printf("  Flash Attention (-fa):%s\n", tp.flash_attention ? " yes" : " no");
    printf("  KV Cache quantization:%s\n", tp.cache_type_k);
    printf("  Launch flags:         \033[1;33m%s\033[0m\n\n", tp.flags_str);

    return 0;
}

static int cmd_model(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)userdata;
    if (!ctx) return 0;

    while (args && (*args == ' ' || *args == '\t')) args++;

    if (!args || !args[0]) {
        printf("Current model: \033[1;36m%s\033[0m (backend: %s)\n",
               ctx->config && ctx->config->model ? ctx->config->model : "none",
               ctx->backend ? ctx->backend->name : "none");

        if (ctx->backend && ctx->backend->list_models) {
            ModelList list;
            if (ctx->backend->list_models(ctx->backend, &list) == 0) {
                printf("\nAvailable models (%zu):\n", list.count);
                for (size_t i = 0; i < list.count; i++) {
                    printf("  - %s%s\n", list.items[i].name,
                           (ctx->config->model && strcmp(ctx->config->model, list.items[i].name) == 0) ? " (active)" : "");
                }
                model_list_free(&list);
            }
        }
        return 0;
    }

    char new_model[128];
    size_t mlen = 0;
    while (args[mlen] && args[mlen] != ' ' && args[mlen] != '\t' && mlen < sizeof(new_model) - 1) {
        new_model[mlen] = args[mlen];
        mlen++;
    }
    new_model[mlen] = '\0';

    if (ctx->config) {
        free(ctx->config->model);
        ctx->config->model = strdup(new_model);
    }

    if (ctx->backend && ctx->backend->start) {
        printf("Restarting backend %s for model '%s'...\n", ctx->backend->name, new_model);
        if (ctx->backend->start(ctx->backend, new_model) != 0) {
            printf("\033[1;31mWarning: backend failed to start model '%s'\033[0m\n", new_model);
            return -1;
        }
    }

    printf("Switched model to: \033[1;32m%s\033[0m\n", new_model);
    return 0;
}

static int cmd_compact(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)args;
    (void)userdata;
    if (!ctx) return 0;

    size_t before = context_estimate_tokens(ctx);
    int saved = context_compact(ctx, 0);
    size_t after = context_estimate_tokens(ctx);

    printf("Context compaction complete:\n");
    printf("  Initial tokens:   ~%zu\n", before);
    printf("  Tokens freed:     ~%d\n", saved);
    printf("  Remaining tokens: ~%zu\n", after);
    return 0;
}

static int cmd_session(SkeetCtx *ctx, const char *args, void *userdata) {
    (void)userdata;
    if (!ctx) return 0;

    while (args && (*args == ' ' || *args == '\t')) args++;

    if (!args || !args[0] || strcmp(args, "list") == 0) {
        char **names = NULL;
        size_t count = 0;
        if (session_list(ctx, &names, &count) == 0) {
            printf("\nSaved Sessions (%zu):\n", count);
            for (size_t i = 0; i < count; i++) {
                printf("  - %s\n", names[i]);
            }
            if (count == 0) {
                printf("  (no saved sessions found in ~/.local/state/skeet/sessions)\n");
            }
            printf("\nUsage:\n");
            printf("  /session save <name>   - Save current session\n");
            printf("  /session load <name>   - Load saved session\n\n");
            session_list_free(names, count);
        } else {
            printf("Failed to list sessions.\n");
        }
        return 0;
    }

    if (strncmp(args, "save ", 5) == 0) {
        const char *name = args + 5;
        while (*name == ' ' || *name == '\t') name++;
        if (!name[0]) {
            printf("Error: specify a session name (/session save <name>)\n");
            return 0;
        }
        if (session_save(ctx, name) == 0) {
            printf("Session '%s' successfully saved (%zu messages).\n", name, ctx->history_count);
        } else {
            printf("Failed to save session '%s'.\n", name);
        }
        return 0;
    }

    if (strncmp(args, "load ", 5) == 0) {
        const char *name = args + 5;
        while (*name == ' ' || *name == '\t') name++;
        if (!name[0]) {
            printf("Error: specify a session name (/session load <name>)\n");
            return 0;
        }
        if (session_load(ctx, name) == 0) {
            printf("Session '%s' successfully loaded (%zu messages, ~%zu tokens).\n",
                   name, ctx->history_count, context_estimate_tokens(ctx));
        } else {
            printf("Failed to load session '%s'.\n", name);
        }
        return 0;
    }

    printf("Unknown session subcommand: '%s'. Use /session list, /session save <name>, /session load <name>\n", args);
    return 0;
}

int commands_register_builtins(SkeetCtx *ctx) {
    if (!ctx) {
        return -1;
    }

    CommandDef help_cmd = {
        .size = sizeof(CommandDef),
        .name = "/help",
        .help = "Show available commands",
        .handler = cmd_help,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &help_cmd);

    CommandDef tools_cmd = {
        .size = sizeof(CommandDef),
        .name = "/tools",
        .help = "List registered tools",
        .handler = cmd_tools,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &tools_cmd);

    CommandDef backend_cmd = {
        .size = sizeof(CommandDef),
        .name = "/backend",
        .help = "Switch or display active backend (ollama, llama)",
        .handler = cmd_backend,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &backend_cmd);

    CommandDef model_cmd = {
        .size = sizeof(CommandDef),
        .name = "/model",
        .help = "Switch or list active models (/model [name])",
        .handler = cmd_model,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &model_cmd);

    CommandDef compact_cmd = {
        .size = sizeof(CommandDef),
        .name = "/compact",
        .help = "Compact message history and summarize old tool outputs",
        .handler = cmd_compact,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &compact_cmd);

    CommandDef session_cmd = {
        .size = sizeof(CommandDef),
        .name = "/session",
        .help = "Manage sessions (/session list, save <name>, load <name>)",
        .handler = cmd_session,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &session_cmd);

    CommandDef tune_cmd = {
        .size = sizeof(CommandDef),
        .name = "/tune",
        .help = "Inspect hardware and calculate optimal model flags",
        .handler = cmd_tune,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &tune_cmd);

    CommandDef exit_cmd = {
        .size = sizeof(CommandDef),
        .name = "/exit",
        .help = "Exit session",
        .handler = cmd_exit,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &exit_cmd);

    CommandDef quit_cmd = {
        .size = sizeof(CommandDef),
        .name = "/quit",
        .help = "Exit session",
        .handler = cmd_exit,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &quit_cmd);

    CommandDef clear_cmd = {
        .size = sizeof(CommandDef),
        .name = "/clear",
        .help = "Clear conversation history",
        .handler = cmd_clear,
        .userdata = NULL,
        .owner = "core"
    };
    command_register(ctx, &clear_cmd);

    return 0;
}
