#include "http.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestEventCollector {
    char events[10][256];
    size_t count;
} TestEventCollector;

static void on_event(const char *data, size_t len, void *userdata) {
    TestEventCollector *c = (TestEventCollector *)userdata;
    if (c->count < 10) {
        size_t n = len < 255 ? len : 255;
        memcpy(c->events[c->count], data, n);
        c->events[c->count][n] = '\0';
        c->count++;
    }
}

int main(void) {
    TestEventCollector col;
    memset(&col, 0, sizeof(col));

    SseParser p;
    sse_init(&p, on_event, &col);

    const char *stream1 =
        ": comment\n"
        "data: {\"foo\":\"bar\"}\n\n"
        "data: {\"chunk\": 1}\n\n"
        "data: [DONE]\n\n";

    int rc = sse_feed(&p, stream1, strlen(stream1));
    assert(rc == 0);
    assert(p.saw_done == 1);
    assert(col.count == 2);
    assert(strcmp(col.events[0], "{\"foo\":\"bar\"}") == 0);
    assert(strcmp(col.events[1], "{\"chunk\": 1}") == 0);

    /* Test feeding byte-by-byte */
    memset(&col, 0, sizeof(col));
    sse_free(&p);
    sse_init(&p, on_event, &col);

    const char *stream2 = "data: byte-by-byte\n\n";
    for (size_t i = 0; i < strlen(stream2); i++) {
        rc = sse_feed(&p, &stream2[i], 1);
        assert(rc == 0);
    }
    assert(col.count == 1);
    assert(strcmp(col.events[0], "byte-by-byte") == 0);

    sse_free(&p);

    printf("test_sse: PASS\n");
    return 0;
}
