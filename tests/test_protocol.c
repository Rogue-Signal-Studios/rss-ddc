#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"

int main(void) {
    const uint8_t expected_10[] = {0x51, 0x82, 0x01, 0x10, 0xac};
    const uint8_t expected_60[] = {0x51, 0x82, 0x01, 0x60, 0xdc};
    uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE] = {};
    rss_ddc_build_raw_get_vcp(0x10, request);
    assert(memcmp(request, expected_10, sizeof(request)) == 0);
    rss_ddc_build_raw_get_vcp(0x60, request);
    assert(memcmp(request, expected_60, sizeof(request)) == 0);

    const uint8_t reply_10[] = {0x6e, 0x88, 0x02, 0x00, 0x10, 0x00, 0x00, 0x32, 0x00, 0x32, 0xa4};
    const uint8_t reply_60[] = {0x6e, 0x88, 0x02, 0x00, 0x60, 0x00, 0x00, 0x12, 0x00, 0x12, 0xd4};
    RSSDDCVCPResult result = {};
    assert(rss_ddc_parse_get_vcp_reply(reply_10, sizeof(reply_10), 0x10, &result) == RSS_DDC_OK);
    assert(result.maximum_value == 50 && result.current_value == 50);
    assert(rss_ddc_parse_get_vcp_reply(reply_60, sizeof(reply_60), 0x60, &result) == RSS_DDC_OK);
    assert(result.maximum_value == 18 && result.current_value == 18);

    uint8_t malformed[RSS_DDC_GET_VCP_REPLY_SIZE] = {};
    memcpy(malformed, reply_10, sizeof(malformed));
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed) - 1, 0x10, &result) == RSS_DDC_ERROR_PROTOCOL);
    malformed[0] = 0x00;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_PROTOCOL);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[1] = 0x87;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_PROTOCOL);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[2] = 0x03;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_PROTOCOL);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[3] = 0x01;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_PROTOCOL);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[4] = 0x60;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_PROTOCOL);
    memcpy(malformed, reply_10, sizeof(malformed)); malformed[10] ^= 0xff;
    assert(rss_ddc_parse_get_vcp_reply(malformed, sizeof(malformed), 0x10, &result) == RSS_DDC_ERROR_PROTOCOL);
    puts("test_protocol: passed");
    return 0;
}
