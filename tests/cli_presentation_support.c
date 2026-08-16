#include "rss_ddc.h"

RSSDDCCharacterizeOptions rss_ddc_default_characterize_options(void) {
    RSSDDCCharacterizeOptions options = {.mode = RSS_DDC_CHARACTERIZE_MODE_DEFAULT,
                                         .knowledge_policy = RSS_DDC_CHARACTERIZE_KNOWLEDGE_NORMAL};
    return options;
}

const char *rss_ddc_probe_result_category_name(RSSDDCProbeResultCategory category) {
    switch (category) {
    case RSS_DDC_PROBE_RESULT_STABLE: return "stable";
    case RSS_DDC_PROBE_RESULT_VARIABLE: return "variable";
    case RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED: return "protocol-reported";
    case RSS_DDC_PROBE_RESULT_MALFORMED: return "malformed";
    case RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH: return "semantic-mismatch";
    case RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR: return "transport-error";
    case RSS_DDC_PROBE_RESULT_UNATTEMPTED: return "unattempted";
    }
    return "unknown";
}

const char *rss_ddc_probe_interpretation_name(RSSDDCProbeInterpretationConfidence interpretation) {
    switch (interpretation) {
    case RSS_DDC_PROBE_INTERPRETATION_OBSERVED_PROTOCOL_VALID: return "observed-protocol-valid";
    case RSS_DDC_PROBE_INTERPRETATION_OBSERVED_ADVERTISED: return "observed-advertised";
    case RSS_DDC_PROBE_INTERPRETATION_OBSERVED_UNADVERTISED: return "observed-unadvertised";
    case RSS_DDC_PROBE_INTERPRETATION_UNKNOWN: return "unknown";
    }
    return "unknown";
}

const char *rss_ddc_probe_repeat_error_name(const RSSDDCProbeObservation *observation) {
    if (observation == NULL || !observation->repeat_attempted) {
        return "not-attempted";
    }
    return rss_ddc_error_string(observation->repeat_error);
}
