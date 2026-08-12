#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"

int main(void) {
    /* Conventional DCPDP13 payloads omit 0x51 because IOAV carries it separately. */
    const uint8_t expected_conventional_10[] = {0x82, 0x01, 0x10, 0xfd};
    const uint8_t expected_conventional_60[] = {0x82, 0x01, 0x60, 0x8d};
    uint8_t conventional[RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE] = {};
    rss_ddc_build_conventional_get_vcp(0x10, conventional);
    assert(memcmp(conventional, expected_conventional_10, sizeof(conventional)) == 0);
    rss_ddc_build_conventional_get_vcp(0x60, conventional);
    assert(memcmp(conventional, expected_conventional_60, sizeof(conventional)) == 0);

    /* Historical PS190 SET uses the conventional IOAV representation, not raw GET framing. */
    const uint8_t expected_set_17[] = {0x84, 0x03, 0x60, 0x00, 0x11, 0xc9};
    const uint8_t expected_set_18[] = {0x84, 0x03, 0x60, 0x00, 0x12, 0xca};
    uint8_t set_request[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE] = {};
    rss_ddc_build_conventional_set_vcp(0x60, 17, set_request);
    assert(memcmp(set_request, expected_set_17, sizeof(set_request)) == 0);
    rss_ddc_build_conventional_set_vcp(0x60, 18, set_request);
    assert(memcmp(set_request, expected_set_18, sizeof(set_request)) == 0);
    const uint8_t expected_set_maximum[] = {0x84, 0x03, 0x10, 0xff, 0xff, 0xa8};
    rss_ddc_build_conventional_set_vcp(0x10, UINT16_MAX, set_request);
    assert(memcmp(set_request, expected_set_maximum, sizeof(set_request)) == 0);

    /* Hardware-validated PS190 request/reply fixtures; this test is fully synthetic. */
    const uint8_t expected_10[] = {0x51, 0x82, 0x01, 0x10, 0xac};
    const uint8_t expected_60[] = {0x51, 0x82, 0x01, 0x60, 0xdc};
    uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE] = {};
    rss_ddc_build_raw_get_vcp(0x10, request);
    assert(memcmp(request, expected_10, sizeof(request)) == 0);
    rss_ddc_build_raw_get_vcp(0x60, request);
    assert(memcmp(request, expected_60, sizeof(request)) == 0);
    assert(rss_ddc_request_checksum(expected_10, sizeof(expected_10) - 1) == expected_10[4]);
    assert(rss_ddc_request_checksum(expected_60, sizeof(expected_60) - 1) == expected_60[4]);

    const uint8_t reply_10[] = {0x6e, 0x88, 0x02, 0x00, 0x10, 0x00, 0x00, 0x32, 0x00, 0x32, 0xa4};
    const uint8_t reply_60[] = {0x6e, 0x88, 0x02, 0x00, 0x60, 0x00, 0x00, 0x12, 0x00, 0x12, 0xd4};
    RSSDDCVCPResult result = {};
    assert(rss_ddc_parse_get_vcp_reply(reply_10, sizeof(reply_10), 0x10, &result) == RSS_DDC_OK);
    assert(result.maximum_value == 50 && result.current_value == 50);
    assert(rss_ddc_parse_get_vcp_reply(reply_60, sizeof(reply_60), 0x60, &result) == RSS_DDC_OK);
    assert(result.maximum_value == 18 && result.current_value == 18);

    uint8_t malformed[RSS_DDC_GET_VCP_REPLY_SIZE] = {};
    memcpy(malformed, reply_10, sizeof(malformed));
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed) - 1, 0x10, &result) == RSS_DDC_ERROR_REPLY_LENGTH);
    malformed[0] = 0x00;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_REPLY_SOURCE);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[1] = 0x87;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_REPLY_SOURCE);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[2] = 0x03;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_REPLY_COMMAND);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[3] = 0x01;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_REPLY_STATUS);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[4] = 0x60;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_REPLY_VCP);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[10] ^= 0xff;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_REPLY_CHECKSUM);
    puts("test_protocol: passed");
    return 0;
}
