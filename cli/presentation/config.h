#ifndef RSS_DDC_CLI_CONFIG_H
#define RSS_DDC_CLI_CONFIG_H

#include <stddef.h>

#include "rss_ddc.h"
#include "tri_state.h"

/** Parsed `[output]` section from the rss-ddc CLI config file. */
typedef struct {
    RSSDDCCliTriState color;
    RSSDDCCliTriState table;
    RSSDDCCliTriState unicode;
    bool has_color;
    bool has_table;
    bool has_unicode;
} RSSDDCCliConfig;

/** Optional callbacks used while loading a config file. */
typedef struct {
    void (*warn)(void *context, const char *message);
    void *warn_context;
} RSSDDCCliConfigOptions;

/**
 * Writes the default config path into `buffer`.
 * Honors `$XDG_CONFIG_HOME/rss-ddc/rss-ddc.conf`, otherwise
 * `$HOME/.config/rss-ddc/rss-ddc.conf`.
 */
RSSDDCError rss_ddc_cli_config_default_path(char *buffer, size_t capacity);

/**
 * Loads presentation preferences from `path`.
 * A missing file is normal and leaves defaults at `auto`.
 * Malformed recognized values return `RSS_DDC_ERROR_ARGUMENT`.
 */
RSSDDCError rss_ddc_cli_config_load(const char *path, RSSDDCCliConfig *config, const RSSDDCCliConfigOptions *options);

#endif
