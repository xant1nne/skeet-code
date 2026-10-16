#ifndef SKEET_CONFIG_H
#define SKEET_CONFIG_H

/* ~/.config/skeet/config.json (or $XDG_CONFIG_HOME/skeet/config.json).
 * Missing file is created with defaults. Strings are malloc'd. */

typedef struct Config {
    char *backend;      /* "ollama" or "llama" */
    char *model;
    char *ollama_host;
    char *llama_server;
    char *llama_model;
    char *llama_host;
    char *llama_flags;
    char *log_file;     /* empty string means the default state path */
    char *path;
} Config;

/* override_path NULL uses the XDG path. Returns 0, or -1 on error. */
int config_load(Config *cfg, const char *override_path);
void config_free(Config *cfg);

/* Default config path. malloc'd, or NULL if HOME is unset and path is too long. */
char *config_default_path(void);

/* log_file when set, otherwise $XDG_STATE_HOME/skeet/skeet.log. malloc'd. */
char *config_log_path(const Config *cfg);

#endif
