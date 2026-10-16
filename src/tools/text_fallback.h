#ifndef SKEET_TEXT_FALLBACK_H
#define SKEET_TEXT_FALLBACK_H

#include "arena.h"
#include "tools/registry.h"
#include <stddef.h>

size_t text_parse_fallback_tool_calls(const char *text, size_t len, ToolCall **out_calls, Arena *arena);

#endif
