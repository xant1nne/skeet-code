#include "str.h"

#include <stdlib.h>
#include <string.h>

Str str_from_cstr(const char *s) {
    Str out;
    out.ptr = s ? s : "";
    out.len = s ? strlen(s) : 0;
    return out;
}

int str_eq(Str a, Str b) {
    if (a.len != b.len) {
        return 0;
    }
    if (a.len == 0) {
        return 1;
    }
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

int str_eq_cstr(Str a, const char *s) {
    return str_eq(a, str_from_cstr(s));
}

char *str_dup_heap(const char *s, size_t n) {
    char *d = malloc(n + 1);
    if (!d) {
        return NULL;
    }
    if (n) {
        memcpy(d, s, n);
    }
    d[n] = '\0';
    return d;
}

void strbuf_init(StrBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void strbuf_free(StrBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

int strbuf_append(StrBuf *b, const char *p, size_t n) {
    if (n == 0) {
        return 0;
    }
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (b->len + n + 1 > cap) {
            if (cap > (size_t)-1 / 2) {
                return -1;
            }
            cap *= 2;
        }
        char *data = realloc(b->data, cap);
        if (!data) {
            return -1;
        }
        b->data = data;
        b->cap = cap;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

int strbuf_puts(StrBuf *b, const char *s) {
    if (!s) {
        return 0;
    }
    return strbuf_append(b, s, strlen(s));
}

char *strbuf_detach(StrBuf *b) {
    if (!b->data) {
        b->data = malloc(1);
        if (!b->data) {
            return NULL;
        }
        b->data[0] = '\0';
        b->cap = 1;
    }
    char *p = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return p;
}
