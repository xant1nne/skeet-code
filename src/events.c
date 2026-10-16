#include "events.h"
#include "context.h"

#include <stdlib.h>
#include <string.h>

static void free_sub(Subscriber *sub) {
    if (sub) {
        free(sub->owner);
        sub->owner = NULL;
    }
}

EventBus *event_bus_new(void) {
    EventBus *bus = calloc(1, sizeof(EventBus));
    return bus;
}

void event_bus_free(EventBus *bus) {
    if (!bus) {
        return;
    }
    for (int t = 0; t < EVENT_TYPE_COUNT; t++) {
        SubscriberList *list = &bus->lists[t];
        for (size_t i = 0; i < list->count; i++) {
            free_sub(&list->items[i]);
        }
        free(list->items);
    }
    free(bus);
}

int event_subscribe(SkeetCtx *ctx, EventType type, EventHandler handler, void *userdata, int priority, const char *owner) {
    if (!ctx || !ctx->events || (int)type < 0 || type >= EVENT_TYPE_COUNT || !handler) {
        return -1;
    }

    EventBus *bus = ctx->events;
    SubscriberList *list = &bus->lists[type];

    if (list->count >= list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 4;
        Subscriber *new_items = realloc(list->items, new_cap * sizeof(Subscriber));
        if (!new_items) {
            return -1;
        }
        list->items = new_items;
        list->cap = new_cap;
    }

    Subscriber sub;
    memset(&sub, 0, sizeof(sub));
    sub.handler = handler;
    sub.userdata = userdata;
    sub.priority = priority;
    sub.reg_order = ++bus->next_order;
    sub.owner = owner ? strdup(owner) : NULL;

    size_t insert_idx = list->count;
    for (size_t i = 0; i < list->count; i++) {
        if (priority > list->items[i].priority) {
            insert_idx = i;
            break;
        }
    }

    for (size_t i = list->count; i > insert_idx; i--) {
        list->items[i] = list->items[i - 1];
    }
    list->items[insert_idx] = sub;
    list->count++;

    return 0;
}

int event_unsubscribe_owner(SkeetCtx *ctx, const char *owner) {
    if (!ctx || !ctx->events || !owner) {
        return 0;
    }

    EventBus *bus = ctx->events;
    int total_removed = 0;

    for (int t = 0; t < EVENT_TYPE_COUNT; t++) {
        SubscriberList *list = &bus->lists[t];
        size_t write_idx = 0;
        for (size_t i = 0; i < list->count; i++) {
            if (list->items[i].owner && strcmp(list->items[i].owner, owner) == 0) {
                free_sub(&list->items[i]);
                total_removed++;
            } else {
                if (write_idx != i) {
                    list->items[write_idx] = list->items[i];
                }
                write_idx++;
            }
        }
        list->count = write_idx;
    }

    return total_removed;
}

EventResult event_emit(SkeetCtx *ctx, EventType type, EventData *data) {
    if (!ctx || !ctx->events || (int)type < 0 || type >= EVENT_TYPE_COUNT) {
        return EV_CONTINUE;
    }

    EventBus *bus = ctx->events;
    SubscriberList *list = &bus->lists[type];

    EventResult final_res = EV_CONTINUE;
    for (size_t i = 0; i < list->count; i++) {
        Subscriber *sub = &list->items[i];
        EventResult r = sub->handler(ctx, type, data, sub->userdata);
        if (r == EV_DENY) {
            return EV_DENY;
        }
        if (r == EV_MODIFIED) {
            final_res = EV_MODIFIED;
        }
    }

    return final_res;
}
