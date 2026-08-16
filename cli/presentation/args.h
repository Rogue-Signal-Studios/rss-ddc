#ifndef RSS_DDC_CLI_ARGS_H
#define RSS_DDC_CLI_ARGS_H

#include <stdbool.h>

#include "output_settings.h"
#include "rss_ddc.h"

/** Result of parsing global CLI presentation flags before the command name. */
typedef struct {
    RSSDDCCliArgOverrides overrides;
    bool verbose;
    int command_index;
} RSSDDCCliParsedArgs;

/**
 * Parses leading global flags such as `--verbose`, `--color=auto`, `--table=yes`,
 * and `--unicode=no`. Returns false when a recognized flag has an invalid value.
 */
bool rss_ddc_cli_parse_global_args(int argc, char **argv, RSSDDCCliParsedArgs *parsed);

/** Parses `passive`, `default`, or `deep`. Writes a stderr message on invalid input. */
bool rss_ddc_cli_parse_characterize_mode(const char *text, RSSDDCCharacterizeMode *mode);

/** Returns a static lowercase mode label for help and reports. */
const char *rss_ddc_cli_characterize_mode_name(RSSDDCCharacterizeMode mode);

/**
 * Parses optional `--mode <name>` or `--mode=<name>` after `characterize <index>`.
 * No extra arguments leaves `*mode` as DEFAULT.
 */
bool rss_ddc_cli_parse_characterize_options(int argc, char **argv, int first_option,
                                            RSSDDCCharacterizeMode *mode);

/** Parsed options for `profile update <index> --output <file>`. */
typedef struct {
    const char *output_path;
} RSSDDCCliProfileUpdateOptions;

/**
 * Parses required `--output <file>` or `--output=<file>` after
 * `profile update <index>`. No established user profile-path convention exists,
 * so an explicit path is required.
 */
bool rss_ddc_cli_parse_profile_update_options(int argc, char **argv, int first_option,
                                              RSSDDCCliProfileUpdateOptions *options);

#endif
