#include "config.h"

#include "cJSON.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *d = malloc(n + 1);
    if (!d) {
        return NULL;
    }
    memcpy(d, s, n + 1);
    return d;
}

static int mkdir_p(const char *path) {
    size_t len = strlen(path);
    if (len == 0 || len >= 4096) {
        return -1;
    }
    char tmp[4096];
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/') {
            continue;
        }
        tmp[i] = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        tmp[i] = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static char *parent_dir(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) {
        return NULL;
    }
    size_t n = (size_t)(slash - path);
    char *d = malloc(n + 1);
    if (!d) {
        return NULL;
    }
    memcpy(d, path, n);
    d[n] = '\0';
    return d;
}

char *config_default_path(void) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char buf[4096];
    int n;
    if (xdg && xdg[0]) {
        n = snprintf(buf, sizeof buf, "%s/skeet/config.json", xdg);
    } else if (home && home[0]) {
        n = snprintf(buf, sizeof buf, "%s/.config/skeet/config.json", home);
    } else {
        return NULL;
    }
    if (n < 0 || (size_t)n >= sizeof buf) {
        return NULL;
    }
    return dup_cstr(buf);
}

static char *default_log_path(void) {
    const char *xdg = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    char buf[4096];
    int n;
    if (xdg && xdg[0]) {
        n = snprintf(buf, sizeof buf, "%s/skeet/skeet.log", xdg);
    } else if (home && home[0]) {
        n = snprintf(buf, sizeof buf, "%s/.local/state/skeet/skeet.log", home);
    } else {
        return dup_cstr("skeet.log");
    }
    if (n < 0 || (size_t)n >= sizeof buf) {
        return NULL;
    }
    return dup_cstr(buf);
}

static void config_clear(Config *cfg) {
    free(cfg->backend);
    free(cfg->model);
    free(cfg->ollama_host);
    free(cfg->llama_server);
    free(cfg->llama_model);
    free(cfg->llama_host);
    free(cfg->llama_flags);
    free(cfg->log_file);
    free(cfg->path);
    memset(cfg, 0, sizeof *cfg);
}

static int set_defaults(Config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->backend = dup_cstr("ollama");
    cfg->model = dup_cstr("");
    cfg->ollama_host = dup_cstr("http://127.0.0.1:11434");
    cfg->llama_server = dup_cstr("llama-server");
    cfg->llama_model = dup_cstr("");
    cfg->llama_host = dup_cstr("http://127.0.0.1:8080");
    cfg->llama_flags = dup_cstr("");
    cfg->log_file = dup_cstr("");
    if (!cfg->backend || !cfg->model || !cfg->ollama_host || !cfg->llama_server ||
        !cfg->llama_model || !cfg->llama_host || !cfg->llama_flags || !cfg->log_file) {
        config_clear(cfg);
        return -1;
    }
    return 0;
}

static const char *obj_str(const cJSON *root, const char *key, const char *fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring) {
        return item->valuestring;
    }
    return fallback;
}

static int assign(char **dst, const char *value) {
    char *copy = dup_cstr(value ? value : "");
    if (!copy) {
        return -1;
    }
    free(*dst);
    *dst = copy;
    return 0;
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

static int write_default_file(const char *path) {
    char *dir = parent_dir(path);
    if (dir) {
        if (mkdir_p(dir) != 0) {
            free(dir);
            return -1;
        }
        free(dir);
    }
    FILE *f = fopen(path, "wx");
    if (!f) {
        if (errno == EEXIST) {
            return 0;
        }
        return -1;
    }
    const char *text =
        "{\n"
        "  \"backend\": \"ollama\",\n"
        "  \"model\": \"\",\n"
        "  \"ollama_host\": \"http://127.0.0.1:11434\",\n"
        "  \"llama_server\": \"llama-server\",\n"
        "  \"llama_model\": \"\",\n"
        "  \"llama_host\": \"http://127.0.0.1:8080\",\n"
        "  \"llama_flags\": \"\",\n"
        "  \"log_file\": \"\"\n"
        "}\n";
    size_t len = strlen(text);
    size_t n = fwrite(text, 1, len, f);
    if (fclose(f) != 0 || n != len) {
        return -1;
    }
    return 0;
}

int config_load(Config *cfg, const char *override_path) {
    if (set_defaults(cfg) != 0) {
        return -1;
    }
    char *owned_path = NULL;
    const char *path = override_path;
    if (!path) {
        owned_path = config_default_path();
        path = owned_path;
    }
    if (!path) {
        config_clear(cfg);
        return -1;
    }
    cfg->path = dup_cstr(path);
    free(owned_path);
    if (!cfg->path) {
        config_clear(cfg);
        return -1;
    }

    size_t len = 0;
    char *text = read_file(cfg->path, &len);
    if (!text) {
        if (errno != ENOENT) {
            config_clear(cfg);
            return -1;
        }
        if (write_default_file(cfg->path) != 0) {
            config_clear(cfg);
            return -1;
        }
        return 0;
    }

    cJSON *root = cJSON_ParseWithLength(text, len);
    free(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        fprintf(stderr, "skeet: %s is not a JSON object\n", cfg->path);
        config_clear(cfg);
        return -1;
    }

    int rc = 0;
    rc |= assign(&cfg->backend, obj_str(root, "backend", cfg->backend));
    rc |= assign(&cfg->model, obj_str(root, "model", cfg->model));
    rc |= assign(&cfg->ollama_host, obj_str(root, "ollama_host", cfg->ollama_host));
    rc |= assign(&cfg->llama_server, obj_str(root, "llama_server", cfg->llama_server));
    rc |= assign(&cfg->llama_model, obj_str(root, "llama_model", cfg->llama_model));
    rc |= assign(&cfg->llama_host, obj_str(root, "llama_host", cfg->llama_host));
    rc |= assign(&cfg->llama_flags, obj_str(root, "llama_flags", cfg->llama_flags));
    rc |= assign(&cfg->log_file, obj_str(root, "log_file", cfg->log_file));
    cJSON_Delete(root);
    if (rc != 0) {
        config_clear(cfg);
        return -1;
    }
    return 0;
}

void config_free(Config *cfg) {
    config_clear(cfg);
}

char *config_log_path(const Config *cfg) {
    if (cfg->log_file && cfg->log_file[0]) {
        return dup_cstr(cfg->log_file);
    }
    return default_log_path();
}
