#include <assert.h>
#include <stdio.h>

#include "dpcd.h"

int main(void) {
    uint8_t bytes[RSS_DDC_DPCD_MAX_READ_BYTES] = {};
    assert(rss_ddc_validate_dpcd_request(0, bytes, 16) == RSS_DDC_OK);
    assert(rss_ddc_validate_dpcd_request(0x200, bytes, 8) == RSS_DDC_OK);
    assert(rss_ddc_validate_dpcd_request(0, bytes, 0) == RSS_DDC_ERROR_DPCD_LENGTH);
    assert(rss_ddc_validate_dpcd_request(0, NULL, 1) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_validate_dpcd_request(0, bytes, 17) == RSS_DDC_ERROR_DPCD_LENGTH);
    assert(rss_ddc_validate_dpcd_request(RSS_DDC_DPCD_MAX_ADDRESS, bytes, 1) == RSS_DDC_OK);
    assert(rss_ddc_validate_dpcd_request(RSS_DDC_DPCD_MAX_ADDRESS, bytes, 2) == RSS_DDC_ERROR_DPCD_RANGE);
    assert(rss_ddc_validate_dpcd_request(RSS_DDC_DPCD_MAX_ADDRESS + 1u, bytes, 1) == RSS_DDC_ERROR_DPCD_RANGE);
    assert(rss_ddc_dpcd_path_status_for_candidate_count(0) == RSS_DDC_DPCD_PATH_UNAVAILABLE);
    assert(rss_ddc_dpcd_path_status_for_candidate_count(1) == RSS_DDC_DPCD_PATH_CANDIDATE);
    assert(rss_ddc_dpcd_path_status_for_candidate_count(2) == RSS_DDC_DPCD_PATH_AMBIGUOUS);

    const uint8_t capabilities[] = {0x14, 0x0a, 0x84, 0, 0, 0x01};
    RSSDDCDPCDCapabilities decoded = {};
    assert(rss_ddc_decode_dpcd_capabilities(0, capabilities, sizeof(capabilities), &decoded) == RSS_DDC_OK);
    assert(decoded.revision == 0x14 && decoded.max_link_rate_raw == 0x0a &&
           decoded.max_lane_count == 4 && decoded.enhanced_framing && decoded.downstream_port_present);
    const uint8_t unknown_rate[] = {0x14, 0xff, 0x02, 0, 0, 0};
    assert(rss_ddc_decode_dpcd_capabilities(0, unknown_rate, sizeof(unknown_rate), &decoded) == RSS_DDC_OK);
    assert(decoded.max_link_rate_raw == 0xff && decoded.max_link_rate_name[0] == 'u');
    assert(rss_ddc_decode_dpcd_capabilities(1, capabilities, sizeof(capabilities), &decoded) == RSS_DDC_ERROR_DPCD_LENGTH);
    assert(rss_ddc_decode_dpcd_capabilities(0, capabilities, 5, &decoded) == RSS_DDC_ERROR_DPCD_LENGTH);
    puts("test_dpcd: passed");
    return 0;
}
