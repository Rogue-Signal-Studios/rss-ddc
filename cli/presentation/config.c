#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *text) {
    if (text == NULL) {
        return;
    }
    char *start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1])) {
        text[--length] = '\0';
    }
}

static void config_warn(const RSSDDCCliConfigOptions *options, const char *message) {
    if (options != NULL && options->warn != NULL) {
        options->warn(options->warn_context, message);
    }
}

static void config_set_defaults(RSSDDCCliConfig *config) {
    config->color = RSS_DDC_CLI_TRI_AUTO;
    config->table = RSS_DDC_CLI_TRI_AUTO;
    config->unicode = RSS_DDC_CLI_TRI_AUTO;
    config->has_color = false;
    config->has_table = false;
    config->has_unicode = false;
}

static RSSDDCError config_set_output_key(RSSDDCCliConfig *config, const char *key, const char *value,
                                         const char *path, unsigned long line_number) {
    RSSDDCCliTriState parsed = RSS_DDC_CLI_TRI_AUTO;
    if (!rss_ddc_cli_tri_state_parse(value, &parsed)) {
        fprintf(stderr, "rss-ddc: %s:%lu: invalid value for %s: %s\n", path, line_number, key, value);
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (strcmp(key, "color") == 0) {
        config->color = parsed;
        config->has_color = true;
        return RSS_DDC_OK;
    }
    if (strcmp(key, "table") == 0) {
        config->table = parsed;
        config->has_table = true;
        return RSS_DDC_OK;
    }
    if (strcmp(key, "unicode") == 0) {
        config->unicode = parsed;
        config->has_unicode = true;
        return RSS_DDC_OK;
    }
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_cli_config_default_path(char *buffer, size_t capacity) {
    if (buffer == NULL || capacity == 0) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    const char *base = NULL;
    if (xdg != NULL && xdg[0] != '\0') {
        base = xdg;
    } else if (home != NULL && home[0] != '\0') {
        base = home;
        if (snprintf(buffer, capacity, "%s/.config/rss-ddc/rss-ddc.conf", base) >= (int)capacity) {
            return RSS_DDC_ERROR_ARGUMENT;
        }
        return RSS_DDC_OK;
    } else {
        return RSS_DDC_ERROR_SYSTEM;
    }
    if (snprintf(buffer, capacity, "%s/rss-ddc/rss-ddc.conf", base) >= (int)capacity) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_cli_config_load(const char *path, RSSDDCCliConfig *config, const RSSDDCCliConfigOptions *options) {
    if (path == NULL || config == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    config_set_defaults(config);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return RSS_DDC_OK;
    }
    char line[512];
    bool in_output = false;
    unsigned long line_number = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        ++line_number;
        trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end == NULL) {
                config_warn(options, "ignored malformed config section header");
                in_output = false;
                continue;
            }
            *end = '\0';
            in_output = strcmp(line + 1, "output") == 0;
            if (!in_output) {
                char message[128];
                snprintf(message, sizeof(message), "ignored unknown config section [%s]", line + 1);
                config_warn(options, message);
            }
            continue;
        }
        char *equals = strchr(line, '=');
        if (equals == NULL) {
            config_warn(options, "ignored malformed config line");
            continue;
        }
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        trim(key);
        trim(value);
        if (!in_output) {
            char message[160];
            snprintf(message, sizeof(message), "ignored unknown config key %s", key);
            config_warn(options, message);
            continue;
        }
        RSSDDCError error = config_set_output_key(config, key, value, path, line_number);
        if (error != RSS_DDC_OK) {
            fclose(file);
            return error;
        }
    }
    fclose(file);
    return RSS_DDC_OK;
}
