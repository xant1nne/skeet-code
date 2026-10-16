# Extension Points Architecture in SKEET Code

SKEET Code provides four primary extension points designed for internal modularity and external plugins (such as LuaJIT or native modules):

---

## 1. Tool Registry (`src/tools/registry.h`)

Tools expose callable capabilities to the LLM agent via JSON Schema.

```c
typedef struct ToolDef {
    uint32_t size;
    const char *name;
    const char *description;
    const char *schema_json;
    uint32_t flags;
    int (*run)(SkeetCtx *ctx, const ToolCall *call, ToolResult *result, void *userdata);
    void *userdata;
    const char *owner;
} ToolDef;
```

### Flags
- `TOOL_READONLY`: Tool does not mutate project state (e.g., `read_file`, `glob`, `grep`).
- `TOOL_NEEDS_CONFIRM`: Tool modifies files or executes commands, triggering permission checks.

### Key Functions
- `tool_register(SkeetCtx *ctx, const ToolDef *def)`
- `tool_unregister_owner(SkeetCtx *ctx, const char *owner)`
- `tool_find(SkeetCtx *ctx, const char *name)`
- `tools_build_request_json(SkeetCtx *ctx, Str *out_json)`

---

## 2. Event Bus (`src/events.h`)

An ordered pub/sub event pipeline allowing extensions to intercept, inspect, or modify agent lifecycle steps.

### Event Types
- `EVENT_SESSION_START` / `EVENT_SESSION_END`
- `EVENT_USER_MESSAGE`
- `EVENT_PROMPT_BUILD`
- `EVENT_STREAM_TOKEN`
- `EVENT_ASSISTANT_MESSAGE`
- `EVENT_BEFORE_TOOL`
- `EVENT_AFTER_TOOL`
- `EVENT_TURN_END`
- `EVENT_ERROR`

### Event Returns
- `EV_CONTINUE`: Allow processing to continue.
- `EV_DENY`: Abort or reject execution (e.g. deny tool execution).
- `EV_MODIFIED`: Event payload was modified by the handler.

### Key Functions
- `event_subscribe(ctx, type, handler, userdata, priority, owner)`
- `event_unsubscribe_owner(ctx, owner)`
- `event_emit(ctx, type, data)`

---

## 3. Command Registry (`src/commands.h`)

Dispatcher for slash commands entered in REPL mode (e.g. `/help`, `/tools`, `/backend`, `/clear`, `/exit`).

```c
typedef struct CommandDef {
    uint32_t size;
    const char *name;
    const char *help;
    CommandHandler handler;
    void *userdata;
    const char *owner;
} CommandDef;
```

### Key Functions
- `command_register(ctx, const CommandDef *def)`
- `command_unregister_owner(ctx, const char *owner)`
- `command_dispatch(ctx, const char *line, int *handled)`

---

## 4. Permission Policy (`src/permissions.h`)

Configurable security model protecting against unintended file writes and arbitrary code execution.

### Permission Modes
- `PERM_MODE_ASK`: Mutating tools prompt user for confirmation `[y/n/a]`.
- `PERM_MODE_YES`: Auto-confirm all tool executions (`--yes`).
- `PERM_MODE_READONLY`: Reject any mutating tool invocations (`--readonly`).

### Key Functions
- `perm_set_mode(ctx, mode)`
- `perm_set_tool_action(ctx, tool_name, action)`
- `perm_set_confirm_fn(ctx, confirm_fn, userdata)`
- `perm_check(ctx, call, def, preview)`

