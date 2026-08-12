#include <assert.h>
#include <stdio.h>

#include "correlation.h"

int main(void) {
    const RSSDDCDPCorrelationFacts valid_dp = {
        .service_candidate_count = 1,
        .service_external = true,
        .epic_parent_present = true,
        .epic_provider = RSS_DDC_PROVIDER_DCPDP13,
        .ui_supported = true,
    };
    assert(rss_ddc_evaluate_dp_correlation(&valid_dp) == RSS_DDC_DP_CORRELATION_OK);

    /* Two independently selected DP displays are valid; no global count enters this decision. */
    RSSDDCDPCorrelationFacts second_selected_dp = valid_dp;
    assert(rss_ddc_evaluate_dp_correlation(&second_selected_dp) == RSS_DDC_DP_CORRELATION_OK);

    RSSDDCDPCorrelationFacts no_service = valid_dp;
    no_service.service_candidate_count = 0;
    assert(rss_ddc_evaluate_dp_correlation(&no_service) == RSS_DDC_DP_CORRELATION_NO_SERVICE);
    RSSDDCDPCorrelationFacts ambiguous_service = valid_dp;
    ambiguous_service.service_candidate_count = 2;
    assert(rss_ddc_evaluate_dp_correlation(&ambiguous_service) == RSS_DDC_DP_CORRELATION_AMBIGUOUS_SERVICE);

    /* A PS190 path may be named DisplayPort, but its provider must not select DP. */
    RSSDDCDPCorrelationFacts ps190_named_like_dp = valid_dp;
    ps190_named_like_dp.epic_provider = RSS_DDC_PROVIDER_PS190;
    assert(rss_ddc_evaluate_dp_correlation(&ps190_named_like_dp) == RSS_DDC_DP_CORRELATION_PROVIDER_MISMATCH);

    RSSDDCDPCorrelationFacts missing_parent = valid_dp;
    missing_parent.epic_parent_present = false;
    assert(rss_ddc_evaluate_dp_correlation(&missing_parent) == RSS_DDC_DP_CORRELATION_NO_EPIC_PARENT);

    RSSDDCDPCorrelationFacts unknown_provider = valid_dp;
    unknown_provider.epic_provider = RSS_DDC_PROVIDER_UNKNOWN;
    assert(rss_ddc_evaluate_dp_correlation(&unknown_provider) == RSS_DDC_DP_CORRELATION_PROVIDER_MISMATCH);

    RSSDDCDPCorrelationFacts no_ui = valid_dp;
    no_ui.ui_supported = false;
    assert(rss_ddc_evaluate_dp_correlation(&no_ui) == RSS_DDC_DP_CORRELATION_UI_UNSUPPORTED);
    assert(rss_ddc_evaluate_dp_correlation(NULL) == RSS_DDC_DP_CORRELATION_NO_SERVICE);

    /* Mixed providers are valid when each independently selected display has one scoped path. */
    const RSSDDCPS190CorrelationFacts ps190_on_ext1 = {
        .service_candidate_count = 1,
        .service_external = true,
        .epic_parent_present = true,
        .epic_provider = RSS_DDC_PROVIDER_PS190,
        .epic_name_matches = true,
        .unit_zero = true,
        .ui_supported = true,
        .branch_device_role_present = true,
        .service_role_matches_branch_device = true,
    };
    assert(rss_ddc_evaluate_ps190_correlation(&ps190_on_ext1) == RSS_DDC_PS190_CORRELATION_OK);
    assert(rss_ddc_evaluate_dp_correlation(&valid_dp) == RSS_DDC_DP_CORRELATION_OK);

    RSSDDCPS190CorrelationFacts wrong_sibling_role = ps190_on_ext1;
    wrong_sibling_role.service_role_matches_branch_device = false;
    assert(rss_ddc_evaluate_ps190_correlation(&wrong_sibling_role) == RSS_DDC_PS190_CORRELATION_ROLE_MISMATCH);
    RSSDDCPS190CorrelationFacts ambiguous_ps190 = ps190_on_ext1;
    ambiguous_ps190.service_candidate_count = 2;
    assert(rss_ddc_evaluate_ps190_correlation(&ambiguous_ps190) == RSS_DDC_PS190_CORRELATION_AMBIGUOUS_SERVICE);
    RSSDDCPS190CorrelationFacts dp_for_ps190 = ps190_on_ext1;
    dp_for_ps190.epic_provider = RSS_DDC_PROVIDER_DCPDP13;
    assert(rss_ddc_evaluate_ps190_correlation(&dp_for_ps190) == RSS_DDC_PS190_CORRELATION_PROVIDER_MISMATCH);
    puts("test_correlation: passed");
    return 0;
}
