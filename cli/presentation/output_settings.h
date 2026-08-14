#ifndef RSS_DDC_CLI_OUTPUT_SETTINGS_H
#define RSS_DDC_CLI_OUTPUT_SETTINGS_H

#include <stdbool.h>

#include "config.h"
#include "terminal.h"
#include "tri_state.h"

/** Explicit CLI overrides for presentation options. Unset values remain `auto`. */
typedef struct {
    RSSDDCCliTriState color;
    RSSDDCCliTriState table;
    RSSDDCCliTriState unicode;
    bool has_color;
    bool has_table;
    bool has_unicode;
} RSSDDCCliArgOverrides;

/** Resolved on/off presentation settings used by renderers. */
typedef struct {
    bool color;
    bool table;
    bool unicode;
} RSSDDCCliEffectiveOutput;

/**
 * Resolves effective output settings using precedence:
 * explicit CLI override > environment > config file > built-in default.
 */
void rss_ddc_cli_resolve_output(const RSSDDCCliArgOverrides *cli, const RSSDDCCliConfig *config,
                                const RSSDDCCliTerminalEnv *terminal, bool command_supports_table,
                                RSSDDCCliEffectiveOutput *effective);

#endif
