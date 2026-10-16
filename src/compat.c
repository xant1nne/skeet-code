#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *skeet_realpath(const char *path) {
    if (!path) {
        return NULL;
    }
#if defined(SKEET_OS_WINDOWS)
    char buf[PATH_MAX];
    if (!_fullpath(buf, path, sizeof(buf))) {
        return NULL;
    }
    return strdup(buf);
#else
    char buf[PATH_MAX];
    char *res = realpath(path, buf);
    if (!res) {
        return NULL;
    }
    return strdup(buf);
#endif
}

int skeet_path_is_inside(const char *parent_root, const char *child_path) {
    if (!parent_root || !child_path) {
        return 0;
    }
    size_t plen = strlen(parent_root);
    if (strncmp(parent_root, child_path, plen) != 0) {
        return 0;
    }
    if (child_path[plen] == '\0') {
        return 1;
    }
    if (parent_root[plen - 1] == '/' || parent_root[plen - 1] == '\\') {
        return 1;
    }
    if (child_path[plen] == '/' || child_path[plen] == '\\') {
        return 1;
    }
    return 0;
}

char *skeet_resolve_path(const char *workspace_root, const char *path) {
    if (!workspace_root || !path) {
        return NULL;
    }

    char *abs_root = skeet_realpath(workspace_root);
    if (!abs_root) {
        return NULL;
    }

    char combined[PATH_MAX * 2];
    if (path[0] == '/'
#if defined(SKEET_OS_WINDOWS)
        || (path[0] && path[1] == ':')
#endif
    ) {
        strncpy(combined, path, sizeof(combined) - 1);
        combined[sizeof(combined) - 1] = '\0';
    } else {
        snprintf(combined, sizeof(combined), "%s/%s", abs_root, path);
    }

    char *real = skeet_realpath(combined);
    if (real) {
        if (!skeet_path_is_inside(abs_root, real)) {
            free(real);
            free(abs_root);
            return NULL;
        }
        free(abs_root);
        return real;
    }

    char norm[PATH_MAX];
    char temp[PATH_MAX * 2];
    strncpy(temp, combined, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *parts[256];
    size_t part_count = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(temp, "/\\", &saveptr);
    while (tok) {
        if (strcmp(tok, ".") == 0) {
            /* skip */
        } else if (strcmp(tok, "..") == 0) {
            if (part_count > 0) {
                part_count--;
            }
        } else {
            if (part_count < sizeof(parts) / sizeof(parts[0])) {
                parts[part_count++] = tok;
            }
        }
        tok = strtok_r(NULL, "/\\", &saveptr);
    }

    norm[0] = '\0';
    for (size_t i = 0; i < part_count; i++) {
        size_t cur = strlen(norm);
        snprintf(norm + cur, sizeof(norm) - cur, "/%s", parts[i]);
    }
    if (norm[0] == '\0') {
        strcpy(norm, "/");
    }

    if (!skeet_path_is_inside(abs_root, norm)) {
        free(abs_root);
        return NULL;
    }

    free(abs_root);
    return strdup(norm);
}
