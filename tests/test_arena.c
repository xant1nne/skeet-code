#include "arena.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Arena a;
    arena_init(&a, 128);

    char *s1 = arena_strndup(&a, "hello world", 5);
    assert(s1 != NULL);
    assert(strcmp(s1, "hello") == 0);

    void *p1 = arena_alloc(&a, 64);
    assert(p1 != NULL);
    memset(p1, 0xAA, 64);

    /* Allocate larger than block_size */
    void *p2 = arena_alloc(&a, 256);
    assert(p2 != NULL);
    memset(p2, 0xBB, 256);

    /* Reset drops secondary blocks and clears used in head */
    arena_reset(&a);

    char *s2 = arena_strndup(&a, "after reset", 11);
    assert(s2 != NULL);
    assert(strcmp(s2, "after reset") == 0);

    arena_free(&a);
    printf("test_arena: PASS\n");
    return 0;
}
