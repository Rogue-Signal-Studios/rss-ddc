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

/**
 * Narrows a native-DP proxy to the selected Service EPIC role. This is a
 * structural predicate, not a global uniqueness test: callers must count all
 * matches and reject anything other than exactly one.
 */
bool rss_ddc_dp_device_proxy_matches(bool external, const char *epic_name, const char *candidate_role,
                                     const char *selected_service_role);

/*
 * PS190 binds its AV Service role to the role of the selected display's unique
 * BranchDeviceID-matched DCPDP device. The role is deliberately a relationship
 * rather than a fixed DCPEXT ordinal because external-display ordering varies.
 */
typedef struct {
    unsigned int service_candidate_count;
    bool service_external;
    bool epic_parent_present;
    RSSDDCProvider epic_provider;
    bool epic_name_matches;
    bool unit_zero;
    bool ui_supported;
    bool branch_device_role_present;
    bool service_role_matches_branch_device;
} RSSDDCPS190CorrelationFacts;

typedef enum {
    RSS_DDC_PS190_CORRELATION_OK = 0,
    RSS_DDC_PS190_CORRELATION_NO_SERVICE,
    RSS_DDC_PS190_CORRELATION_AMBIGUOUS_SERVICE,
    RSS_DDC_PS190_CORRELATION_NOT_EXTERNAL,
    RSS_DDC_PS190_CORRELATION_NO_EPIC_PARENT,
    RSS_DDC_PS190_CORRELATION_PROVIDER_MISMATCH,
    RSS_DDC_PS190_CORRELATION_EPIC_NAME_MISMATCH,
    RSS_DDC_PS190_CORRELATION_UNIT_MISMATCH,
    RSS_DDC_PS190_CORRELATION_UI_UNSUPPORTED,
    RSS_DDC_PS190_CORRELATION_BRANCH_DEVICE_ROLE_MISSING,
    RSS_DDC_PS190_CORRELATION_ROLE_MISMATCH,
} RSSDDCPS190CorrelationResult;

/** Evaluates only selected-display PS190 facts; global provider counts are intentionally absent. */
RSSDDCPS190CorrelationResult rss_ddc_evaluate_ps190_correlation(const RSSDDCPS190CorrelationFacts *facts);

/** Registry EPIC provider class observed on the Mac Studio XL2730Z path; not a public runtime provider yet. */
#define RSS_DDC_REGISTRY_CLASS_DCPDP_SERVICE "DCPDPService"

typedef struct {
    unsigned int service_candidate_count;
    bool service_external;
    bool epic_parent_present;
    const char *epic_provider_class;
    bool ui_supported;
} RSSDDCDCPDPServiceCorrelationFacts;

typedef enum {
    RSS_DDC_DCPDP_SERVICE_CORRELATION_OK = 0,
    RSS_DDC_DCPDP_SERVICE_CORRELATION_NO_SERVICE,
    RSS_DDC_DCPDP_SERVICE_CORRELATION_AMBIGUOUS_SERVICE,
    RSS_DDC_DCPDP_SERVICE_CORRELATION_NOT_EXTERNAL,
    RSS_DDC_DCPDP_SERVICE_CORRELATION_NO_EPIC_PARENT,
    RSS_DDC_DCPDP_SERVICE_CORRELATION_PROVIDER_MISMATCH,
    RSS_DDC_DCPDP_SERVICE_CORRELATION_UI_UNSUPPORTED,
} RSSDDCDCPDPServiceCorrelationResult;

/**
 * Validation-only Service identity for DCPDPService. Normal capability dispatch
 * must remain unsupported until hardware evidence promotes this provider class.
 */
RSSDDCDCPDPServiceCorrelationResult rss_ddc_evaluate_dcpdpservice_correlation(
    const RSSDDCDCPDPServiceCorrelationFacts *facts);

/** True only when Service correlation succeeded and exactly one same-role device proxy exists. */
bool rss_ddc_dcpdpservice_dpcd_validation_ready(RSSDDCDCPDPServiceCorrelationResult correlation,
                                                unsigned int scoped_device_proxy_candidates);

/** True when selected-display DCPDPService Service correlation succeeded. */
bool rss_ddc_dcpdpservice_get_validation_ready(RSSDDCDCPDPServiceCorrelationResult correlation);

enum {
    RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_ADDRESS = 0x00000u,
    RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_LENGTH = 16u,
    RSS_DDC_DCPDP_SERVICE_GET_VALIDATION_VCP = 0x10u,
    RSS_DDC_DCPDP_SERVICE_GET_VALIDATION_DELAY_US = 50000u,
};

#endif
