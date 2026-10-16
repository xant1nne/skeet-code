#ifndef SKEET_EVENTS_H
#define SKEET_EVENTS_H

#include "str.h"
#include <stdint.h>
#include <stddef.h>

typedef struct SkeetCtx SkeetCtx;
typedef struct ToolCall ToolCall;
typedef struct ToolResult ToolResult;

typedef enum {
    EV_CONTINUE = 0,
    EV_DENY = 1,
    EV_MODIFIED = 2
} EventResult;

typedef enum {
    EVENT_SESSION_START = 0,
    EVENT_SESSION_END = 1,
    EVENT_USER_MESSAGE = 2,
    EVENT_PROMPT_BUILD = 3,
    EVENT_STREAM_TOKEN = 4,
    EVENT_ASSISTANT_MESSAGE = 5,
    EVENT_BEFORE_TOOL = 6,
    EVENT_AFTER_TOOL = 7,
    EVENT_TURN_END = 8,
    EVENT_ERROR = 9,
    EVENT_TYPE_COUNT = 10
} EventType;

typedef struct EventData {
    uint32_t size;
    union {
        struct {
            Str text;
        } user_message;
        struct {
            Str append_prompt;
        } prompt_build;
        struct {
            const char *token;
            size_t token_len;
        } stream_token;
        struct {
            Str text;
        } assistant_message;
        struct {
            const ToolCall *call;
            Str modified_args;
            const char *deny_reason;
        } before_tool;
        struct {
            const ToolCall *call;
            ToolResult *result;
        } after_tool;
        struct {
            const char *msg;
            int code;
        } error;
    };
} EventData;

typedef EventResult (*EventHandler)(SkeetCtx *ctx, EventType type, EventData *data, void *userdata);

typedef struct Subscriber {
    EventHandler handler;
    void *userdata;
    int priority;
    uint64_t reg_order;
    char *owner;
} Subscriber;

typedef struct SubscriberList {
    Subscriber *items;
    size_t count;
    size_t cap;
} SubscriberList;

typedef struct EventBus {
    SubscriberList lists[EVENT_TYPE_COUNT];
    uint64_t next_order;
} EventBus;

EventBus *event_bus_new(void);
void event_bus_free(EventBus *bus);
int event_subscribe(SkeetCtx *ctx, EventType type, EventHandler handler, void *userdata, int priority, const char *owner);
int event_unsubscribe_owner(SkeetCtx *ctx, const char *owner);
EventResult event_emit(SkeetCtx *ctx, EventType type, EventData *data);

#endif
