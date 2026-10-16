#include "json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    Message msgs[2];
    msgs[0].role = str_from_cstr("user");
    msgs[0].content = str_from_cstr("Hello");
    msgs[1].role = str_from_cstr("assistant");
    msgs[1].content = str_from_cstr("Hi there!");

    Request req;
    req.model = str_from_cstr("test-model");
    req.messages = msgs;
    req.message_count = 2;

    char *body = json_chat_body(&req);
    assert(body != NULL);
    assert(strstr(body, "\"model\":\"test-model\"") != NULL);
    assert(strstr(body, "\"stream\":true") != NULL);
    assert(strstr(body, "\"content\":\"Hello\"") != NULL);
    free(body);

    /* Test chat delta parsing */
    const char *sse_payload = "{\"choices\":[{\"delta\":{\"content\":\"chunk1\"}}]}";
    char *text = NULL;
    char *err = NULL;
    int rc = json_chat_delta(sse_payload, strlen(sse_payload), &text, &err);
    assert(rc == 0);
    assert(text != NULL);
    assert(strcmp(text, "chunk1") == 0);
    assert(err == NULL);
    free(text);

    /* Test error payload */
    const char *err_payload = "{\"error\":{\"message\":\"model not found\"}}";
    rc = json_chat_delta(err_payload, strlen(err_payload), &text, &err);
    assert(rc == 0);
    assert(text == NULL);
    assert(err != NULL);
    assert(strcmp(err, "model not found") == 0);
    free(err);

    printf("test_json: PASS\n");
    return 0;
}
