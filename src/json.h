#ifndef SKEET_JSON_H
#define SKEET_JSON_H

#include "backend.h"

#include "cJSON.h"

#include <stddef.h>

/* Thin helpers so call sites do not walk cJSON by hand for the shapes we use. */

const cJSON *json_obj(const cJSON *obj, const char *key);
const char *json_obj_str(const cJSON *obj, const char *key);
const cJSON *json_obj_arr(const cJSON *obj, const char *key);
int json_arr_len(const cJSON *arr);
const cJSON *json_arr_at(const cJSON *arr, int index);

/* OpenAI-compatible chat request. malloc'd compact JSON, free with free(). */
char *json_chat_body(const Request *request);

/* One SSE JSON payload from /v1/chat/completions.
 *  0  parsed; *text is a malloc'd fragment or NULL when this event has no text
 * -1  not JSON
 * If the payload is an error object, *text stays NULL and *err_msg is malloc'd.
 * Either pointer may be NULL if the caller does not want that output.
 */
int json_chat_delta(const char *data, size_t len, char **text, char **err_msg);

#endif
