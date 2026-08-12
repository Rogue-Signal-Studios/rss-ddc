#ifndef RSS_DDC_CORRELATION_H
#define RSS_DDC_CORRELATION_H

#include <stdbool.h>

#include "rss_ddc.h"

/*
 * The portable decision boundary for the conventional DP Service path. The
 * platform collector supplies facts from the display-correlated DCPAV service;
 * this code deliberately has no transport-name or connector input because
 * those are not provider identities.
 */
typedef struct {
    unsigned int service_candidate_count;
    bool service_external;
    bool epic_parent_present;
    RSSDDCProvider epic_provider;
    bool ui_supported;
} RSSDDCDPCorrelationFacts;

typedef enum {
    RSS_DDC_DP_CORRELATION_OK = 0,
    RSS_DDC_DP_CORRELATION_NO_SERVICE,
    RSS_DDC_DP_CORRELATION_AMBIGUOUS_SERVICE,
    RSS_DDC_DP_CORRELATION_NOT_EXTERNAL,
    RSS_DDC_DP_CORRELATION_NO_EPIC_PARENT,
    RSS_DDC_DP_CORRELATION_PROVIDER_MISMATCH,
    RSS_DDC_DP_CORRELATION_UI_UNSUPPORTED,
} RSSDDCDPCorrelationResult;

/**
 * Accepts exactly one external, display-correlated DCPAV Service whose
 * immediate EPIC parent identifies DCPDP13Service and whose IOAV interface
 * is enabled.
 */
RSSDDCDPCorrelationResult rss_ddc_evaluate_dp_correlation(const RSSDDCDPCorrelationFacts *facts);

#endif
