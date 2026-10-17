#define _POSIX_C_SOURCE 200809L
#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>

struct LoadedPlugin {
    void *handle;
    int (*deinit)(void);
    char *path;
    struct LoadedPlugin *next;
};

static const SkeetHostApi *g_host_api = NULL;
static struct LoadedPlugin *g_plugins = NULL;

static void host_log(int level, const char *fmt, ...) {
    if (g_host_api && g_host_api->log) {
        va_list ap;
        va_start(ap, fmt);
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        g_host_api->log(level, buf);
    } else {
        // fallback to stderr
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }
}

int plugins_loader_init(const SkeetHostApi *host_api) {
    if (!host_api) return -1;
    g_host_api = host_api;
    host_log(SKEET_LOG_INFO, "plugins: loader initialized (owner=%s)", host_api->owner ? host_api->owner : "<none>");
    return 0;
}

static int is_shared_object(const char *name) {
    size_t n = strlen(name);
    if (n > 3 && strcmp(name + n - 3, ".so") == 0) return 1;
    return 0;
}

int plugins_loader_load_dir(const char *dir_path) {
    if (!dir_path) return -1;
    DIR *d = opendir(dir_path);
    if (!d) {
        host_log(SKEET_LOG_DEBUG, "plugins: couldn't open dir %s", dir_path);
        return 0; // not fatal
    }
    struct dirent *ent;
    int loaded = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!is_shared_object(ent->d_name)) continue;
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

        host_log(SKEET_LOG_INFO, "plugins: loading %s", full);
        void *h = dlopen(full, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            host_log(SKEET_LOG_ERROR, "plugins: dlopen failed for %s: %s", full, dlerror());
            continue;
        }
        uint32_t (*abi_ver)(void) = dlsym(h, "skeet_plugin_abi_version");
        if (!abi_ver) {
            host_log(SKEET_LOG_WARN, "plugins: %s missing skeet_plugin_abi_version", full);
            dlclose(h);
            continue;
        }
        uint32_t ver = abi_ver();
        if (ver != 1) {
            host_log(SKEET_LOG_WARN, "plugins: %s unsupported ABI version %u", full, ver);
            dlclose(h);
            continue;
        }
        int (*init_fn)(const SkeetHostApi *) = dlsym(h, "skeet_plugin_init");
        if (!init_fn) {
            host_log(SKEET_LOG_WARN, "plugins: %s missing skeet_plugin_init", full);
            dlclose(h);
            continue;
        }
        int (*deinit_fn)(void) = dlsym(h, "skeet_plugin_deinit");

        int rc = init_fn(g_host_api);
        if (rc != 0) {
            host_log(SKEET_LOG_ERROR, "plugins: %s init failed (%d)", full, rc);
            if (deinit_fn) deinit_fn();
            dlclose(h);
            continue;
        }

        struct LoadedPlugin *p = calloc(1, sizeof(*p));
        p->handle = h;
        p->deinit = deinit_fn;
        p->path = strdup(full);
        p->next = g_plugins;
        g_plugins = p;
        loaded++;
    }
    closedir(d);
    return loaded;
}

int plugins_loader_load_default_paths(void) {
    int total = 0;
    // Project-local .skeet/plugins
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        char proj_path[8192];
        snprintf(proj_path, sizeof(proj_path), "%s/.skeet/plugins", cwd);
        total += plugins_loader_load_dir(proj_path);
    }
    // ~/.config/skeet/plugins
    const char *home = getenv("HOME");
    if (home) {
        char config_path[8192];
        snprintf(config_path, sizeof(config_path), "%s/.config/skeet/plugins", home);
        total += plugins_loader_load_dir(config_path);
    }
    // /etc/skeet/plugins (system)
    total += plugins_loader_load_dir("/etc/skeet/plugins");
    return total;
}

void plugins_loader_unload_all(void) {
    struct LoadedPlugin *p = g_plugins;
    while (p) {
        host_log(SKEET_LOG_INFO, "plugins: unloading %s", p->path);
        if (p->deinit) {
            p->deinit();
        }
        if (p->handle) dlclose(p->handle);
        free(p->path);
        struct LoadedPlugin *n = p->next;
        free(p);
        p = n;
    }
    g_plugins = NULL;
}
