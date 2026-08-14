#ifndef RSS_DDC_CLI_COLOR_H
#define RSS_DDC_CLI_COLOR_H

#include <stdbool.h>
#include <stdio.h>

#include "output_settings.h"
#include "rss_ddc.h"

/** ANSI styling tokens used by the CLI presentation layer. */
typedef enum {
    RSS_DDC_CLI_COLOR_DEFAULT = 0,
    RSS_DDC_CLI_COLOR_DIM,
    RSS_DDC_CLI_COLOR_GREEN,
    RSS_DDC_CLI_COLOR_CYAN,
    RSS_DDC_CLI_COLOR_YELLOW,
    RSS_DDC_CLI_COLOR_MAGENTA,
    RSS_DDC_CLI_COLOR_RED,
} RSSDDCCliColorRole;

/** Writes an ANSI SGR sequence when color is enabled; otherwise writes nothing. */
void rss_ddc_cli_color_begin(FILE *stream, const RSSDDCCliEffectiveOutput *output, RSSDDCCliColorRole role);

/** Resets ANSI styling when color is enabled. */
void rss_ddc_cli_color_reset(FILE *stream, const RSSDDCCliEffectiveOutput *output);

/** Writes a styled string into `buffer` when color is enabled; otherwise copies `text`. */
void rss_ddc_cli_color_format(char *buffer, size_t capacity, const RSSDDCCliEffectiveOutput *output,
                              RSSDDCCliColorRole role, const char *text);

/** Returns the semantic color role for a probe observation row. */
RSSDDCCliColorRole rss_ddc_cli_probe_observation_color(const RSSDDCProbeObservation *observation,
                                                       RSSDDCProbeInterpretationConfidence interpretation);

#endif
