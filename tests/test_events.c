#include "events.h"
#include "context.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_order[4];
static int g_order_idx = 0;

static EventResult handler_p10(SkeetCtx *ctx, EventType type, EventData *data, void *userdata) {
    (void)ctx;
    (void)type;
    (void)data;
    (void)userdata;
    g_order[g_order_idx++] = 10;
    return EV_CONTINUE;
}

static EventResult handler_p50(SkeetCtx *ctx, EventType type, EventData *data, void *userdata) {
    (void)ctx;
    (void)type;
    (void)data;
    (void)userdata;
    g_order[g_order_idx++] = 50;
    return EV_CONTINUE;
}

static EventResult handler_p100_modify(SkeetCtx *ctx, EventType type, EventData *data, void *userdata) {
    (void)ctx;
    (void)type;
    (void)userdata;
    g_order[g_order_idx++] = 100;
    if (data) {
        data->user_message.text = str_from_cstr("modified");
    }
    return EV_MODIFIED;
}

static EventResult handler_deny(SkeetCtx *ctx, EventType type, EventData *data, void *userdata) {
    (void)ctx;
    (void)type;
    (void)data;
    (void)userdata;
    return EV_DENY;
}

int main(void) {
    SkeetCtx *ctx = skeet_ctx_new(NULL, NULL, NULL, ".");
    assert(ctx != NULL);

    event_subscribe(ctx, EVENT_USER_MESSAGE, handler_p10, NULL, 10, "plugin_a");
    event_subscribe(ctx, EVENT_USER_MESSAGE, handler_p100_modify, NULL, 100, "plugin_b");
    event_subscribe(ctx, EVENT_USER_MESSAGE, handler_p50, NULL, 50, "plugin_c");

    EventData d;
    memset(&d, 0, sizeof(d));
    d.user_message.text = str_from_cstr("original");

    g_order_idx = 0;
    EventResult r = event_emit(ctx, EVENT_USER_MESSAGE, &d);
    assert(r == EV_MODIFIED);
    assert(g_order_idx == 3);
    assert(g_order[0] == 100);
    assert(g_order[1] == 50);
    assert(g_order[2] == 10);
    assert(str_eq_cstr(d.user_message.text, "modified"));

    event_subscribe(ctx, EVENT_TURN_END, handler_deny, NULL, 50, "plugin_d");
    EventResult r_deny = event_emit(ctx, EVENT_TURN_END, NULL);
    assert(r_deny == EV_DENY);

    int unreg = event_unsubscribe_owner(ctx, "plugin_b");
    assert(unreg == 1);

    g_order_idx = 0;
    d.user_message.text = str_from_cstr("second");
    r = event_emit(ctx, EVENT_USER_MESSAGE, &d);
    assert(r == EV_CONTINUE);
    assert(g_order_idx == 2);
    assert(g_order[0] == 50);
    assert(g_order[1] == 10);

    skeet_ctx_free(ctx);
    printf("test_events: PASS\n");
    return 0;
}
