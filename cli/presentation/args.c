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
