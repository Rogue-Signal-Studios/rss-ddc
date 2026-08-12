#include "correlation.h"

RSSDDCDPCorrelationResult rss_ddc_evaluate_dp_correlation(const RSSDDCDPCorrelationFacts *facts) {
    if (facts == NULL || facts->service_candidate_count == 0) return RSS_DDC_DP_CORRELATION_NO_SERVICE;
    if (facts->service_candidate_count != 1) return RSS_DDC_DP_CORRELATION_AMBIGUOUS_SERVICE;
    if (!facts->service_external) return RSS_DDC_DP_CORRELATION_NOT_EXTERNAL;
    if (!facts->epic_parent_present) return RSS_DDC_DP_CORRELATION_NO_EPIC_PARENT;
    if (facts->epic_provider != RSS_DDC_PROVIDER_DCPDP13) return RSS_DDC_DP_CORRELATION_PROVIDER_MISMATCH;
    if (!facts->ui_supported) return RSS_DDC_DP_CORRELATION_UI_UNSUPPORTED;
    return RSS_DDC_DP_CORRELATION_OK;
}
