#include "tools/builtins.h"
#include "tools/registry.h"
#include "context.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    SkeetCtx *ctx = skeet_ctx_new(NULL, NULL, NULL, ".");
    assert(ctx != NULL);
    assert(tools_register_all_builtins(ctx) == 0);

    /* 1. Test read_file */
    const ToolDef *rf_def = tool_find(ctx, "read_file");
    assert(rf_def != NULL);
    ToolCall rf_call = {
        .id = str_from_cstr("c_rf"),
        .name = str_from_cstr("read_file"),
        .args_json = str_from_cstr("{\"path\":\"Makefile\",\"limit\":10}")
    };
    ToolResult rf_res;
    memset(&rf_res, 0, sizeof(rf_res));
    rf_def->run(ctx, &rf_call, &rf_res, rf_def->userdata);
    assert(rf_res.is_error == 0);
    assert(rf_res.content.ptr != NULL);
    assert(strstr(rf_res.content.ptr, "CFLAGS") != NULL);

    /* 2. Test glob */
    const ToolDef *glob_def = tool_find(ctx, "glob");
    assert(glob_def != NULL);
    ToolCall glob_call = {
        .id = str_from_cstr("c_glob"),
        .name = str_from_cstr("glob"),
        .args_json = str_from_cstr("{\"pattern\":\"*.c\"}")
    };
    ToolResult glob_res;
    memset(&glob_res, 0, sizeof(glob_res));
    glob_def->run(ctx, &glob_call, &glob_res, glob_def->userdata);
    assert(glob_res.is_error == 0);
    assert(glob_res.content.ptr != NULL);

    /* 3. Test grep */
    const ToolDef *grep_def = tool_find(ctx, "grep");
    assert(grep_def != NULL);
    ToolCall grep_call = {
        .id = str_from_cstr("c_grep"),
        .name = str_from_cstr("grep"),
        .args_json = str_from_cstr("{\"pattern\":\"agent_repl\",\"path\":\"src\"}")
    };
    ToolResult grep_res;
    memset(&grep_res, 0, sizeof(grep_res));
    grep_def->run(ctx, &grep_call, &grep_res, grep_def->userdata);
    assert(grep_res.is_error == 0);
    assert(grep_res.content.ptr != NULL);
    assert(strstr(grep_res.content.ptr, "agent.c") != NULL);

    /* 4. Test write_file */
    const char *tmp_name = "test_tools_temp.txt";
    unlink(tmp_name);
    const ToolDef *wf_def = tool_find(ctx, "write_file");
    assert(wf_def != NULL);
    ToolCall wf_call = {
        .id = str_from_cstr("c_wf"),
        .name = str_from_cstr("write_file"),
        .args_json = str_from_cstr("{\"path\":\"test_tools_temp.txt\",\"content\":\"Initial content line 1\\nInitial content line 2\\n\"}")
    };
    ToolResult wf_res;
    memset(&wf_res, 0, sizeof(wf_res));
    wf_def->run(ctx, &wf_call, &wf_res, wf_def->userdata);
    if (wf_res.is_error) {
        printf("wf_res error: %s\n", wf_res.content.ptr ? wf_res.content.ptr : "null");
    }
    assert(wf_res.is_error == 0);

    /* 5. Test edit_file */
    const ToolDef *ef_def = tool_find(ctx, "edit_file");
    assert(ef_def != NULL);
    ToolCall ef_call = {
        .id = str_from_cstr("c_ef"),
        .name = str_from_cstr("edit_file"),
        .args_json = str_from_cstr("{\"path\":\"test_tools_temp.txt\",\"old_string\":\"Initial content line 1\",\"new_string\":\"Replaced content line 1\"}")
    };
    ToolResult ef_res;
    memset(&ef_res, 0, sizeof(ef_res));
    ef_def->run(ctx, &ef_call, &ef_res, ef_def->userdata);
    assert(ef_res.is_error == 0);

    unlink(tmp_name);

    /* 6. Test bash */
    const ToolDef *bash_def = tool_find(ctx, "bash");
    assert(bash_def != NULL);
    ToolCall bash_call = {
        .id = str_from_cstr("c_bash"),
        .name = str_from_cstr("bash"),
        .args_json = str_from_cstr("{\"command\":\"echo skeet_test_bash_ok\"}")
    };
    ToolResult bash_res;
    memset(&bash_res, 0, sizeof(bash_res));
    bash_def->run(ctx, &bash_call, &bash_res, bash_def->userdata);
    assert(bash_res.is_error == 0);
    assert(bash_res.content.ptr != NULL);
    assert(strstr(bash_res.content.ptr, "skeet_test_bash_ok") != NULL);

    skeet_ctx_free(ctx);
    printf("test_tools: PASS\n");
    return 0;
}
