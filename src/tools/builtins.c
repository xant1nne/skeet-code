#include "tools/builtins.h"

int tools_register_all_builtins(SkeetCtx *ctx) {
    if (!ctx) {
        return -1;
    }
    tool_register_read_file(ctx);
    tool_register_glob(ctx);
    tool_register_grep(ctx);
    tool_register_edit_file(ctx);
    tool_register_write_file(ctx);
    tool_register_bash(ctx);
    return 0;
}
