#include "http.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

int http_init(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return -1;
    }
    return 0;
}

void http_cleanup(void) {
    curl_global_cleanup();
}

void http_response_free(HttpResponse *resp) {
    if (!resp) {
        return;
    }
    free(resp->body);
    resp->body = NULL;
    resp->body_len = 0;
    resp->status = 0;
    resp->error[0] = '\0';
}

typedef struct TransferContext {
    HttpOnData on_data;
    void *userdata;
    char *buf;
    size_t len;
    size_t cap;
    volatile sig_atomic_t *cancel;
    int cancelled;
    int callback_error;
} TransferContext;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    TransferContext *ctx = (TransferContext *)userdata;

    if (ctx->cancel && *ctx->cancel) {
        ctx->cancelled = 1;
        return 0; /* Returning 0 causes CURLE_WRITE_ERROR */
    }

    if (ctx->on_data) {
        int rc = ctx->on_data(ptr, total, ctx->userdata);
        if (rc != 0) {
            ctx->callback_error = 1;
            return 0;
        }
        return total;
    }

    if (ctx->len + total + 1 > ctx->cap) {
        size_t new_cap = ctx->cap ? ctx->cap * 2 : 4096;
        while (new_cap < ctx->len + total + 1) {
            new_cap *= 2;
        }
        char *nb = realloc(ctx->buf, new_cap);
        if (!nb) {
            return 0;
        }
        ctx->buf = nb;
        ctx->cap = new_cap;
    }
    memcpy(ctx->buf + ctx->len, ptr, total);
    ctx->len += total;
    ctx->buf[ctx->len] = '\0';

    return total;
}

static int run_transfer(CURL *easy, TransferContext *ctx, HttpResponse *out) {
    CURLM *multi = curl_multi_init();
    if (!multi) {
        if (out) {
            snprintf(out->error, sizeof(out->error), "Failed to init curl multi");
        }
        return -1;
    }

    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, ctx);

    curl_multi_add_handle(multi, easy);

    int still_running = 1;
    int result = 0;

    while (still_running) {
        if (ctx->cancel && *ctx->cancel) {
            ctx->cancelled = 1;
            result = 1;
            break;
        }

        CURLMcode mc = curl_multi_perform(multi, &still_running);
        if (mc != CURLM_OK) {
            if (out) {
                snprintf(out->error, sizeof(out->error), "curl_multi_perform: %s",
                         curl_multi_strerror(mc));
            }
            result = -1;
            break;
        }

        if (ctx->cancelled) {
            result = 1;
            break;
        }
        if (ctx->callback_error) {
            result = -1;
            break;
        }

        if (still_running) {
            int numfds = 0;
            mc = curl_multi_poll(multi, NULL, 0, 100, &numfds);
            if (mc != CURLM_OK) {
                if (out) {
                    snprintf(out->error, sizeof(out->error), "curl_multi_poll: %s",
                             curl_multi_strerror(mc));
                }
                result = -1;
                break;
            }
        }
    }

    int msgs_left = 0;
    CURLMsg *msg = NULL;
    while ((msg = curl_multi_info_read(multi, &msgs_left)) != NULL) {
        if (msg->msg == CURLMSG_DONE) {
            CURLcode easy_status = msg->data.result;
            if (ctx->cancelled || (ctx->cancel && *ctx->cancel)) {
                result = 1;
            } else if (easy_status != CURLE_OK) {
                if (out && out->error[0] == '\0') {
                    snprintf(out->error, sizeof(out->error), "%s",
                             curl_easy_strerror(easy_status));
                }
                result = -1;
            }
        }
    }

    if (out) {
        long code = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
        out->status = code;
        if (!ctx->on_data) {
            out->body = ctx->buf;
            out->body_len = ctx->len;
            ctx->buf = NULL;
            ctx->len = 0;
            ctx->cap = 0;
        }
    }

    curl_multi_remove_handle(multi, easy);
    curl_multi_cleanup(multi);

    free(ctx->buf);
    return result;
}

int http_get(const char *url, HttpResponse *out, volatile sig_atomic_t *cancel) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }

    CURL *easy = curl_easy_init();
    if (!easy) {
        if (out) {
            snprintf(out->error, sizeof(out->error), "Failed to init curl easy handle");
        }
        return -1;
    }

    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 30L);

    TransferContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cancel = cancel;

    int rc = run_transfer(easy, &ctx, out);
    curl_easy_cleanup(easy);
    return rc;
}

int http_post_stream(const char *url, const char *json_body, HttpOnData on_data,
                     void *userdata, HttpResponse *out, volatile sig_atomic_t *cancel) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }

    CURL *easy = curl_easy_init();
    if (!easy) {
        if (out) {
            snprintf(out->error, sizeof(out->error), "Failed to init curl easy handle");
        }
        return -1;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(easy, CURLOPT_POST, 1L);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, json_body ? json_body : "");
    if (json_body) {
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)strlen(json_body));
    }
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 0L); /* stream can take time */

    TransferContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.on_data = on_data;
    ctx.userdata = userdata;
    ctx.cancel = cancel;

    int rc = run_transfer(easy, &ctx, out);

    curl_slist_free_all(headers);
    curl_easy_cleanup(easy);
    return rc;
}

void sse_init(SseParser *p, SseOnEvent on_event, void *userdata) {
    memset(p, 0, sizeof(*p));
    p->on_event = on_event;
    p->userdata = userdata;
}

void sse_free(SseParser *p) {
    free(p->line);
    free(p->data);
    memset(p, 0, sizeof(*p));
}

static int append_to_buf(char **buf, size_t *len, size_t *cap, const char *src, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t new_cap = *cap ? *cap * 2 : 256;
        while (new_cap < *len + n + 1) {
            new_cap *= 2;
        }
        char *nb = realloc(*buf, new_cap);
        if (!nb) {
            return -1;
        }
        *buf = nb;
        *cap = new_cap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

static void process_line(SseParser *p) {
    /* Strip trailing \r if any */
    if (p->line_len > 0 && p->line[p->line_len - 1] == '\r') {
        p->line_len--;
        p->line[p->line_len] = '\0';
    }

    if (p->line_len == 0) {
        /* Empty line: end of event dispatch */
        if (p->data_len > 0) {
            if (p->on_event) {
                p->on_event(p->data, p->data_len, p->userdata);
            }
            p->data_len = 0;
            if (p->data) {
                p->data[0] = '\0';
            }
        }
        return;
    }

    /* Comment */
    if (p->line[0] == ':') {
        return;
    }

    /* Check prefix "data:" */
    if (strncmp(p->line, "data:", 5) == 0) {
        const char *val = p->line + 5;
        if (*val == ' ') {
            val++;
        }
        size_t val_len = strlen(val);

        if (val_len == 6 && strcmp(val, "[DONE]") == 0) {
            p->saw_done = 1;
            return;
        }

        if (p->data_len > 0) {
            append_to_buf(&p->data, &p->data_len, &p->data_cap, "\n", 1);
        }
        append_to_buf(&p->data, &p->data_len, &p->data_cap, val, val_len);
    }
}

int sse_feed(SseParser *p, const char *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = bytes[i];
        if (c == '\n') {
            process_line(p);
            p->line_len = 0;
            if (p->line) {
                p->line[0] = '\0';
            }
        } else {
            if (append_to_buf(&p->line, &p->line_len, &p->line_cap, &c, 1) != 0) {
                p->error = 1;
                return -1;
            }
        }
    }
    return 0;
}

int sse_finish(SseParser *p) {
    if (p->line_len > 0) {
        process_line(p);
        p->line_len = 0;
        if (p->line) {
            p->line[0] = '\0';
        }
    }
    if (p->data_len > 0) {
        if (p->on_event) {
            p->on_event(p->data, p->data_len, p->userdata);
        }
        p->data_len = 0;
        if (p->data) {
            p->data[0] = '\0';
        }
    }
    return p->error ? -1 : 0;
}
