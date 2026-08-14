#include "color.h"

#include <stdio.h>
#include <string.h>

static const char *role_sequence(RSSDDCCliColorRole role) {
    switch (role) {
    case RSS_DDC_CLI_COLOR_DIM: return "\033[2m";
    case RSS_DDC_CLI_COLOR_GREEN: return "\033[32m";
    case RSS_DDC_CLI_COLOR_CYAN: return "\033[36m";
    case RSS_DDC_CLI_COLOR_YELLOW: return "\033[33m";
    case RSS_DDC_CLI_COLOR_MAGENTA: return "\033[35m";
    case RSS_DDC_CLI_COLOR_RED: return "\033[31m";
    case RSS_DDC_CLI_COLOR_DEFAULT: return "";
    }
    return "";
}

void rss_ddc_cli_color_begin(FILE *stream, const RSSDDCCliEffectiveOutput *output, RSSDDCCliColorRole role) {
    if (stream == NULL || output == NULL || !output->color || role == RSS_DDC_CLI_COLOR_DEFAULT) {
        return;
    }
    fputs(role_sequence(role), stream);
}

void rss_ddc_cli_color_reset(FILE *stream, const RSSDDCCliEffectiveOutput *output) {
    if (stream == NULL || output == NULL || !output->color) {
        return;
    }
    fputs("\033[0m", stream);
}

void rss_ddc_cli_color_format(char *buffer, size_t capacity, const RSSDDCCliEffectiveOutput *output,
                              RSSDDCCliColorRole role, const char *text) {
    if (buffer == NULL || capacity == 0) {
        return;
    }
    if (text == NULL) {
        buffer[0] = '\0';
        return;
    }
    if (output == NULL || !output->color || role == RSS_DDC_CLI_COLOR_DEFAULT) {
        snprintf(buffer, capacity, "%s", text);
        return;
    }
    snprintf(buffer, capacity, "%s%s\033[0m", role_sequence(role), text);
}

RSSDDCCliColorRole rss_ddc_cli_probe_observation_color(const RSSDDCProbeObservation *observation,
                                                       RSSDDCProbeInterpretationConfidence interpretation) {
    if (observation == NULL) {
        return RSS_DDC_CLI_COLOR_DEFAULT;
    }
    switch (observation->category) {
    case RSS_DDC_PROBE_RESULT_MALFORMED:
    case RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH:
    case RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR:
        return RSS_DDC_CLI_COLOR_RED;
    case RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED:
    case RSS_DDC_PROBE_RESULT_UNATTEMPTED:
        return RSS_DDC_CLI_COLOR_DIM;
    case RSS_DDC_PROBE_RESULT_VARIABLE:
        return RSS_DDC_CLI_COLOR_YELLOW;
    case RSS_DDC_PROBE_RESULT_STABLE:
        if (observation->current_exceeds_maximum) {
            return RSS_DDC_CLI_COLOR_YELLOW;
        }
        if (interpretation == RSS_DDC_PROBE_INTERPRETATION_OBSERVED_UNADVERTISED) {
            return RSS_DDC_CLI_COLOR_MAGENTA;
        }
        if (interpretation == RSS_DDC_PROBE_INTERPRETATION_OBSERVED_ADVERTISED ||
            observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_YES) {
            return RSS_DDC_CLI_COLOR_GREEN;
        }
        if (observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_YES) {
            return RSS_DDC_CLI_COLOR_CYAN;
        }
        return RSS_DDC_CLI_COLOR_GREEN;
    default:
        return RSS_DDC_CLI_COLOR_DEFAULT;
    }
}
