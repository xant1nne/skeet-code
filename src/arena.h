#ifndef SKEET_ARENA_H
#define SKEET_ARENA_H

#include <stddef.h>

/* Bump allocator. Blocks only grow, so pointers stay valid until reset or free.
 * One arena per turn: reset drops the turn's scratch without freeing the first block. */

typedef struct ArenaBlock ArenaBlock;

typedef struct Arena {
    ArenaBlock *head;
    size_t block_size;
} Arena;

void arena_init(Arena *a, size_t block_size);
void *arena_alloc(Arena *a, size_t size);
char *arena_strndup(Arena *a, const char *s, size_t n);
void arena_reset(Arena *a);
void arena_free(Arena *a);

#endif
