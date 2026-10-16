#ifndef SKEET_STR_H
#define SKEET_STR_H

#include <stddef.h>

/* Bytes plus length. ptr is not required to be NUL-terminated.
 * Strings created with str_dup_heap and strbuf_cstr are NUL-terminated,
 * and len does not count that byte. */

typedef struct Str {
    const char *ptr;
    size_t len;
} Str;

typedef struct StrBuf {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

Str str_from_cstr(const char *s);
int str_eq(Str a, Str b);
int str_eq_cstr(Str a, const char *s);

/* malloc(len+1), NUL-terminated. *out_len is the byte length without the NUL. */
char *str_dup_heap(const char *s, size_t n);

void strbuf_init(StrBuf *b);
void strbuf_free(StrBuf *b);
int strbuf_append(StrBuf *b, const char *p, size_t n);
int strbuf_puts(StrBuf *b, const char *s);
char *strbuf_detach(StrBuf *b);

#endif
