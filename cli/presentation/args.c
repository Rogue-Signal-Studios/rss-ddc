#include "args.h"

#include <stdio.h>
#include <stdio.h>
#include <string.h>

static bool parse_tri_flag(const char *argument, const char *name, RSSDDCCliTriState *destination, bool *has_destination) {
    const size_t name_length = strlen(name);
    if (strncmp(argument, name, name_length) != 0 || argument[name_length] != '=') {
        return false;
    }
    if (!rss_ddc_cli_tri_state_parse(argument + name_length + 1, destination)) {
        fprintf(stderr, "rss-ddc: invalid value for %s: %s (expected yes, no, or auto)\n", name,
                argument + name_length + 1);
        return false;
    }
    *has_destination = true;
    return true;
}

bool rss_ddc_cli_parse_global_args(int argc, char **argv, RSSDDCCliParsedArgs *parsed) {
    if (parsed == NULL) {
        return false;
    }
    memset(parsed, 0, sizeof(*parsed));
    parsed->command_index = 1;
    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (strcmp(argument, "--verbose") == 0) {
            parsed->verbose = true;
            parsed->command_index = index + 1;
            continue;
        }
        if (parse_tri_flag(argument, "--color", &parsed->overrides.color, &parsed->overrides.has_color)) {
            parsed->command_index = index + 1;
            continue;
        }
        if (parse_tri_flag(argument, "--table", &parsed->overrides.table, &parsed->overrides.has_table)) {
            parsed->command_index = index + 1;
            continue;
        }
        if (parse_tri_flag(argument, "--unicode", &parsed->overrides.unicode, &parsed->overrides.has_unicode)) {
            parsed->command_index = index + 1;
            continue;
        }
        if (argument[0] == '-' && argument[1] == '-') {
            fprintf(stderr, "rss-ddc: unknown option %s\n", argument);
            return false;
        }
        parsed->command_index = index;
        return true;
    }
    return true;
}

const char *rss_ddc_cli_characterize_mode_name(RSSDDCCharacterizeMode mode) {
    if (mode == RSS_DDC_CHARACTERIZE_MODE_PASSIVE) {
        return "passive";
    }
    if (mode == RSS_DDC_CHARACTERIZE_MODE_DEEP) {
        return "deep";
    }
    return "default";
}

bool rss_ddc_cli_parse_characterize_mode(const char *text, RSSDDCCharacterizeMode *mode) {
    if (text == NULL || mode == NULL) {
        return false;
    }
    if (strcmp(text, "passive") == 0) {
        *mode = RSS_DDC_CHARACTERIZE_MODE_PASSIVE;
        return true;
    }
    if (strcmp(text, "default") == 0) {
        *mode = RSS_DDC_CHARACTERIZE_MODE_DEFAULT;
        return true;
    }
    if (strcmp(text, "deep") == 0) {
        *mode = RSS_DDC_CHARACTERIZE_MODE_DEEP;
        return true;
    }
    fprintf(stderr, "rss-ddc: invalid characterization mode: %s (expected passive, default, or deep)\n",
            text);
    return false;
}

bool rss_ddc_cli_parse_characterize_options(int argc, char **argv, int first_option,
                                            RSSDDCCharacterizeMode *mode) {
    if (mode == NULL) {
        return false;
    }
    *mode = RSS_DDC_CHARACTERIZE_MODE_DEFAULT;
    if (argv == NULL || first_option >= argc) {
        return true;
    }
    if (first_option < 0 || argv[first_option] == NULL) {
        return false;
    }
    const char *argument = argv[first_option];
    const char *value = NULL;
    if (strncmp(argument, "--mode=", 7) == 0) {
        value = argument + 7;
        if (first_option + 1 != argc) {
            return false;
        }
    } else if (strcmp(argument, "--mode") == 0) {
        if (first_option + 1 >= argc || first_option + 2 != argc) {
            return false;
        }
        value = argv[first_option + 1];
    } else {
        return false;
    }
    return rss_ddc_cli_parse_characterize_mode(value, mode);
}

bool rss_ddc_cli_parse_profile_update_options(int argc, char **argv, int first_option,
                                              RSSDDCCliProfileUpdateOptions *options) {
    if (options == NULL) {
        return false;
    }
    memset(options, 0, sizeof(*options));
    if (argv == NULL || first_option < 0 || first_option >= argc || argv[first_option] == NULL) {
        fprintf(stderr, "rss-ddc: profile update requires --output <file>\n");
        return false;
    }
    const char *argument = argv[first_option];
    const char *value = NULL;
    if (strncmp(argument, "--output=", 9) == 0) {
        value = argument + 9;
        if (first_option + 1 != argc || value[0] == '\0') {
            fprintf(stderr, "rss-ddc: profile update requires --output <file>\n");
            return false;
        }
    } else if (strcmp(argument, "--output") == 0) {
        if (first_option + 1 >= argc || first_option + 2 != argc || argv[first_option + 1] == NULL ||
            argv[first_option + 1][0] == '\0') {
            fprintf(stderr, "rss-ddc: profile update requires --output <file>\n");
            return false;
        }
        value = argv[first_option + 1];
    } else {
        fprintf(stderr, "rss-ddc: profile update requires --output <file>\n");
        return false;
    }
    options->output_path = value;
    return true;
}
