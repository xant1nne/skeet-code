#ifndef SKEET_HTTP_H
#define SKEET_HTTP_H

#include <signal.h>
#include <stddef.h>

/* Single-threaded HTTP. The transfer loop is curl_multi_poll(), which blocks in poll().
 * cancel is checked between polls and inside the write callback. NULL cancel is ignored.
 * Returns 0, 1 cancelled, or -1. */

typedef struct HttpResponse {
    long status;
    char *body;
    size_t body_len;
    char error[256];
} HttpResponse;

typedef int (*HttpOnData)(const char *data, size_t len, void *userdata);

int http_init(void);
void http_cleanup(void);

int http_get(const char *url, HttpResponse *out, volatile sig_atomic_t *cancel);
int http_post_stream(const char *url, const char *json_body, HttpOnData on_data,
                     void *userdata, HttpResponse *out, volatile sig_atomic_t *cancel);
void http_response_free(HttpResponse *resp);

typedef struct SseParser SseParser;

typedef void (*SseOnEvent)(const char *data, size_t len, void *userdata);

struct SseParser {
    char *line;
    size_t line_len;
    size_t line_cap;
    char *data;
    size_t data_len;
    size_t data_cap;
    SseOnEvent on_event;
    void *userdata;
    int saw_done;
    int error;
};

void sse_init(SseParser *p, SseOnEvent on_event, void *userdata);
void sse_free(SseParser *p);
int sse_feed(SseParser *p, const char *bytes, size_t n);
int sse_finish(SseParser *p);

#endif
