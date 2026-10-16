#include "backend.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Backend *b = backend_llama_new(NULL, "/models/test.gguf", "http://127.0.0.1:8080", 8080, NULL);
    assert(b != NULL);
    assert(strcmp(b->name, "llama") == 0);
    assert(b->start != NULL);
    assert(b->chat_stream != NULL);
    assert(b->list_models != NULL);
    assert(b->stop != NULL);

    ModelList list;
    int rc = b->list_models(b, &list);
    if (rc == 0) {
        assert(list.count >= 1);
        assert(strcmp(list.items[0].name, "test.gguf") == 0);
        model_list_free(&list);
    }

    backend_free(b);

    printf("test_backend_llama: PASS\n");
    return 0;
}
