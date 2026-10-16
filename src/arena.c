#include "arena.h"

#include <stdlib.h>
#include <string.h>

struct ArenaBlock {
    ArenaBlock *next;
    size_t cap;
    size_t used;
    unsigned char data[];
};

void arena_init(Arena *a, size_t block_size) {
    a->head = NULL;
    a->block_size = block_size ? block_size : (size_t)(64 * 1024);
}

static ArenaBlock *arena_new_block(size_t cap) {
    size_t bytes = sizeof(ArenaBlock) + cap;
    ArenaBlock *b = malloc(bytes);
    if (!b) {
        return NULL;
    }
    b->next = NULL;
    b->cap = cap;
    b->used = 0;
    return b;
}

void *arena_alloc(Arena *a, size_t size) {
    size_t align = 8;
    if (!a->head) {
        size_t cap = size > a->block_size ? size : a->block_size;
        a->head = arena_new_block(cap);
        if (!a->head) {
            return NULL;
        }
    }

    size_t used = a->head->used;
    size_t pad = (align - (used & (align - 1))) & (align - 1);
    if (pad > a->head->cap - used || size > a->head->cap - used - pad) {
        size_t cap = size > a->block_size ? size : a->block_size;
        ArenaBlock *b = arena_new_block(cap);
        if (!b) {
            return NULL;
        }
        b->next = a->head;
        a->head = b;
        used = 0;
        pad = 0;
    }

    void *p = a->head->data + used + pad;
    a->head->used = used + pad + size;
    return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *d = arena_alloc(a, n + 1);
    if (!d) {
        return NULL;
    }
    if (n) {
        memcpy(d, s, n);
    }
    d[n] = '\0';
    return d;
}

void arena_reset(Arena *a) {
    if (!a->head) {
        return;
    }
    ArenaBlock *b = a->head->next;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head->next = NULL;
    a->head->used = 0;
}

void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}
