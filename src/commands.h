#ifndef SKEET_COMMANDS_H
#define SKEET_COMMANDS_H

#include <stdint.h>
#include <stddef.h>

typedef struct SkeetCtx SkeetCtx;

typedef int (*CommandHandler)(SkeetCtx *ctx, const char *args, void *userdata);

typedef struct CommandDef {
    uint32_t size;
    const char *name;
    const char *help;
    CommandHandler handler;
    void *userdata;
    const char *owner;
} CommandDef;

typedef struct RegisteredCommand {
    CommandDef def;
    char *name;
    char *help;
    char *owner;
} RegisteredCommand;

typedef struct CommandRegistry {
    RegisteredCommand *items;
    size_t count;
    size_t cap;
} CommandRegistry;

CommandRegistry *command_registry_new(void);
void command_registry_free(CommandRegistry *reg);
int command_register(SkeetCtx *ctx, const CommandDef *def);
int command_unregister_owner(SkeetCtx *ctx, const char *owner);
int command_dispatch(SkeetCtx *ctx, const char *line, int *handled);
int commands_register_builtins(SkeetCtx *ctx);

#endif

