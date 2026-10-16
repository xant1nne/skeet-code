#include "str.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    Str s1 = str_from_cstr("hello");
    assert(s1.len == 5);
    assert(str_eq_cstr(s1, "hello"));
    assert(!str_eq_cstr(s1, "world"));

    Str s2 = (Str){ .ptr = "hello!", .len = 5 };
    assert(str_eq(s1, s2));

    char *dup = str_dup_heap("heap string", 11);
    assert(dup != NULL);
    assert(strcmp(dup, "heap string") == 0);
    free(dup);

    StrBuf buf;
    strbuf_init(&buf);
    strbuf_puts(&buf, "foo");
    strbuf_puts(&buf, "bar");
    strbuf_append(&buf, "bazqux", 3);
    assert(buf.len == 9);
    assert(strcmp(buf.data, "foobarbaz") == 0);

    char *detached = strbuf_detach(&buf);
    assert(detached != NULL);
    assert(strcmp(detached, "foobarbaz") == 0);
    free(detached);

    strbuf_free(&buf);

    printf("test_str: PASS\n");
    return 0;
}
