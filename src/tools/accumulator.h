#ifndef SKEET_ACCUMULATOR_H
#define SKEET_ACCUMULATOR_H

#include "arena.h"
#include "str.h"
#include "tools/registry.h"
#include <stddef.h>

typedef struct AccumulatedToolCall {
    int index;
    char *id;
    char *name;
    StrBuf args_buf;
} AccumulatedToolCall;

typedef struct ToolCallAccumulator {
    AccumulatedToolCall *calls;
    size_t count;
    size_t cap;
} ToolCallAccumulator;

void accumulator_init(ToolCallAccumulator *acc);
void accumulator_free(ToolCallAccumulator *acc);
int accumulator_feed_chunk(ToolCallAccumulator *acc, int index, const char *id, const char *name, const char *args_chunk);
int accumulator_feed_sse_json(ToolCallAccumulator *acc, const char *data, size_t len, char **text_chunk, char **err_msg);
size_t accumulator_get_calls(ToolCallAccumulator *acc, ToolCall **out_calls, Arena *arena);
char *accumulator_to_json(const ToolCallAccumulator *acc);

#endif
