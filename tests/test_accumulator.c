#include "tools/accumulator.h"
#include "arena.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    ToolCallAccumulator acc;
    accumulator_init(&acc);

    const char *chunk1 = "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\",\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\"}}]}}]}";
    const char *chunk2 = "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"foo.txt\\\"}\"}}]}}]}";

    char *text = NULL;
    char *err = NULL;

    assert(accumulator_feed_sse_json(&acc, chunk1, strlen(chunk1), &text, &err) == 0);
    assert(text == NULL);
    assert(err == NULL);

    assert(accumulator_feed_sse_json(&acc, chunk2, strlen(chunk2), &text, &err) == 0);

    Arena a;
    arena_init(&a, 16 * 1024);

    ToolCall *calls = NULL;
    size_t count = accumulator_get_calls(&acc, &calls, &a);
    assert(count == 1);
    assert(calls != NULL);
    assert(str_eq_cstr(calls[0].id, "call_1"));
    assert(str_eq_cstr(calls[0].name, "read_file"));
    assert(str_eq_cstr(calls[0].args_json, "{\"path\":\"foo.txt\"}"));

    char *json = accumulator_to_json(&acc);
    assert(json != NULL);
    assert(strstr(json, "read_file") != NULL);
    assert(strstr(json, "foo.txt") != NULL);
    free(json);

    arena_free(&a);
    accumulator_free(&acc);
    printf("test_accumulator: PASS\n");
    return 0;
}
