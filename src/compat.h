#ifndef SKEET_COMPAT_H
#define SKEET_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
    #define SKEET_OS_WINDOWS 1
    #include <windows.h>
    #include <direct.h>
    #include <io.h>
    #ifndef PATH_MAX
        #define PATH_MAX 4096
    #endif
    #define skeet_mkdir(path, mode) _mkdir(path)
#elif defined(__APPLE__) && defined(__MACH__)
    #define SKEET_OS_MACOS 1
    #define SKEET_OS_POSIX 1
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <limits.h>
    #define skeet_mkdir(path, mode) mkdir((path), (mode))
#else
    #define SKEET_OS_LINUX 1
    #define SKEET_OS_POSIX 1
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <limits.h>
    #define skeet_mkdir(path, mode) mkdir((path), (mode))
#endif

#ifndef PATH_MAX
    #define PATH_MAX 4096
#endif

/* Resolves absolute canonical path. Returns malloc'd string or NULL. */
char *skeet_realpath(const char *path);

/* Checks if child_path is strictly inside or equal to parent_root */
int skeet_path_is_inside(const char *parent_root, const char *child_path);

/* Resolves rel_path against workspace_root. Returns malloc'd path if inside workspace, NULL if outside or invalid. */
char *skeet_resolve_path(const char *workspace_root, const char *path);

#endif
