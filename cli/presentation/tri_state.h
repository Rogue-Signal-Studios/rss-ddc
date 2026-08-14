#ifndef RSS_DDC_CLI_TRI_STATE_H
#define RSS_DDC_CLI_TRI_STATE_H

#include <stdbool.h>

/** Tri-state preference used by CLI presentation options and config values. */
typedef enum {
    RSS_DDC_CLI_TRI_AUTO = 0,
    RSS_DDC_CLI_TRI_YES,
    RSS_DDC_CLI_TRI_NO,
} RSSDDCCliTriState;

/** Parses `yes`, `no`, or `auto` into `out`. Returns false for NULL or invalid text. */
bool rss_ddc_cli_tri_state_parse(const char *text, RSSDDCCliTriState *out);

/** Returns the canonical string form of a tri-state value. */
const char *rss_ddc_cli_tri_state_string(RSSDDCCliTriState state);

#endif
