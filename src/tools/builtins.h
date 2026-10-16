#ifndef SKEET_BUILTINS_H
#define SKEET_BUILTINS_H

typedef struct SkeetCtx SkeetCtx;

int tools_register_all_builtins(SkeetCtx *ctx);
int tool_register_read_file(SkeetCtx *ctx);
int tool_register_glob(SkeetCtx *ctx);
int tool_register_grep(SkeetCtx *ctx);
int tool_register_edit_file(SkeetCtx *ctx);
int tool_register_write_file(SkeetCtx *ctx);
int tool_register_bash(SkeetCtx *ctx);

#endif
