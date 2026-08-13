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

    /* DP and PS190 SET share this conventional IOAV representation, not raw GET framing. */
    const uint8_t expected_set_15[] = {0x84, 0x03, 0x60, 0x00, 0x0f, 0xd7};
    const uint8_t expected_set_17[] = {0x84, 0x03, 0x60, 0x00, 0x11, 0xc9};
    const uint8_t expected_set_18[] = {0x84, 0x03, 0x60, 0x00, 0x12, 0xca};
    uint8_t set_request[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE] = {};
    rss_ddc_build_conventional_set_vcp(0x60, 15, set_request);
    assert(memcmp(set_request, expected_set_15, sizeof(set_request)) == 0);
    rss_ddc_build_conventional_set_vcp(0x60, 17, set_request);
    assert(memcmp(set_request, expected_set_17, sizeof(set_request)) == 0);
    rss_ddc_build_conventional_set_vcp(0x60, 18, set_request);
    assert(memcmp(set_request, expected_set_18, sizeof(set_request)) == 0);
    const uint8_t expected_set_maximum[] = {0x84, 0x03, 0x10, 0xff, 0xff, 0xa8};
    rss_ddc_build_conventional_set_vcp(0x10, UINT16_MAX, set_request);
    assert(memcmp(set_request, expected_set_maximum, sizeof(set_request)) == 0);
    const uint8_t expected_set_brightness_61[] = {0x84, 0x03, 0x10, 0x00, 0x3d, 0x95};
    const uint8_t expected_set_brightness_62[] = {0x84, 0x03, 0x10, 0x00, 0x3e, 0x96};
    rss_ddc_build_conventional_set_vcp(0x10, 61, set_request);
    assert(memcmp(set_request, expected_set_brightness_61, sizeof(set_request)) == 0);
    rss_ddc_build_conventional_set_vcp(0x10, 62, set_request);
    assert(memcmp(set_request, expected_set_brightness_62, sizeof(set_request)) == 0);
    assert(rss_ddc_request_checksum((const uint8_t[]){0x51, 0x84, 0x03, 0x60, 0x00, 0x0f}, 6) ==
           expected_set_15[5]);

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

    const uint8_t expected_capabilities[] = {0x83, 0xf3, 0x01, 0x20, 0x3f};
    uint8_t capabilities_request[RSS_DDC_CONVENTIONAL_CAPABILITIES_REQUEST_SIZE] = {};
    rss_ddc_build_conventional_capabilities_request(0x0120, capabilities_request);
    assert(memcmp(capabilities_request, expected_capabilities, sizeof(capabilities_request)) == 0);
    const uint8_t expected_capabilities_zero[] = {0x83, 0xf3, 0x00, 0x00, 0x1e};
    rss_ddc_build_conventional_capabilities_request(0, capabilities_request);
    assert(memcmp(capabilities_request, expected_capabilities_zero, sizeof(capabilities_request)) == 0);
    const uint8_t expected_capabilities_next[] = {0x83, 0xf3, 0x00, 0x0a, 0x14};
    rss_ddc_build_conventional_capabilities_request(0x000a, capabilities_request);
    assert(memcmp(capabilities_request, expected_capabilities_next, sizeof(capabilities_request)) == 0);
    assert(rss_ddc_request_checksum(expected_capabilities_next, sizeof(expected_capabilities_next) - 1) ==
           expected_capabilities_next[4]);
    const uint8_t expected_raw_capabilities[] = {0x51, 0x83, 0xf3, 0x01, 0x20, 0x6e};
    uint8_t raw_capabilities_request[RSS_DDC_RAW_CAPABILITIES_REQUEST_SIZE] = {};
    rss_ddc_build_raw_capabilities_request(0x0120, raw_capabilities_request);
    assert(memcmp(raw_capabilities_request, expected_raw_capabilities, sizeof(raw_capabilities_request)) == 0);

    const uint8_t reply_10[] = {0x6e, 0x88, 0x02, 0x00, 0x10, 0x00, 0x00, 0x32, 0x00, 0x32, 0xa4};
    const uint8_t reply_60[] = {0x6e, 0x88, 0x02, 0x00, 0x60, 0x00, 0x00, 0x12, 0x00, 0x12, 0xd4};
    RSSDDCVCPResult result = {};
    assert(rss_ddc_parse_get_vcp_reply(reply_10, sizeof(reply_10), 0x10, &result) == RSS_DDC_OK);
    assert(result.maximum_value == 50 && result.current_value == 50);
    assert(rss_ddc_parse_get_vcp_reply(reply_60, sizeof(reply_60), 0x60, &result) == RSS_DDC_OK);
    assert(result.maximum_value == 18 && result.current_value == 18);

    const uint8_t all_zero_reply[RSS_DDC_GET_VCP_REPLY_SIZE] = {};
    assert(rss_ddc_parse_get_vcp_reply(all_zero_reply, sizeof(all_zero_reply), 0x10, &result) ==
           RSS_DDC_ERROR_REPLY_SOURCE);

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

    uint8_t capabilities_reply[] = {0x6e, 0x8a, 0xe3, 0x01, 0x20, 'v', 'c', 'p', '(', '1', '0', ')', 0x00};
    uint8_t checksum = 0x50;
    for (size_t index = 0; index + 1 < sizeof(capabilities_reply); ++index) checksum ^= capabilities_reply[index];
    capabilities_reply[sizeof(capabilities_reply) - 1] = checksum;
    RSSDDCCapabilitiesFragment fragment = {};
    assert(rss_ddc_parse_capabilities_reply(capabilities_reply, sizeof(capabilities_reply), &fragment) == RSS_DDC_OK);
    assert(fragment.offset == 0x0120 && fragment.length == 7 && memcmp(fragment.bytes, "vcp(10)", 7) == 0);
    capabilities_reply[2] = 0x02;
    assert(rss_ddc_parse_capabilities_reply(capabilities_reply, sizeof(capabilities_reply), &fragment) ==
           RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    capabilities_reply[2] = 0xe3;
    capabilities_reply[sizeof(capabilities_reply) - 1] ^= 0xff;
    assert(rss_ddc_parse_capabilities_reply(capabilities_reply, sizeof(capabilities_reply), &fragment) ==
           RSS_DDC_ERROR_REPLY_CHECKSUM);

    uint8_t maximum_capabilities_reply[RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE] = {0x6e, 0xa3, 0xe3, 0x00, 0x00};
    for (size_t index = 0; index < 32; ++index) maximum_capabilities_reply[index + 5] = 'a';
    checksum = 0x50;
    for (size_t index = 0; index + 1 < sizeof(maximum_capabilities_reply); ++index) checksum ^= maximum_capabilities_reply[index];
    maximum_capabilities_reply[sizeof(maximum_capabilities_reply) - 1] = checksum;
    assert(rss_ddc_parse_capabilities_reply(maximum_capabilities_reply, sizeof(maximum_capabilities_reply), &fragment) ==
           RSS_DDC_OK);
    assert(fragment.offset == 0 && fragment.length == 32);

    uint8_t sentinel_window[RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE];
    memset(sentinel_window, 0xcc, sizeof(sentinel_window));
    sentinel_window[0] = 0x6e; sentinel_window[1] = 0x83; sentinel_window[2] = 0xe3;
    sentinel_window[3] = 0x00; sentinel_window[4] = 0x00;
    checksum = 0x50;
    for (size_t index = 0; index < 5; ++index) checksum ^= sentinel_window[index];
    sentinel_window[5] = checksum;
    size_t frame_size = 0;
    assert(rss_ddc_capabilities_reply_frame_size(sentinel_window, sizeof(sentinel_window), &frame_size) == RSS_DDC_OK);
    assert(frame_size == 6);
    assert(rss_ddc_parse_capabilities_reply(sentinel_window, frame_size, &fragment) == RSS_DDC_OK);
    assert(fragment.offset == 0 && fragment.length == 0); /* Changed tail bytes are intentionally ignored. */
    assert(rss_ddc_validate_capabilities_fragment_offset(&fragment, 0) == RSS_DDC_OK);
    sentinel_window[4] = 0x01;
    checksum = 0x50;
    for (size_t index = 0; index < 5; ++index) checksum ^= sentinel_window[index];
    sentinel_window[5] = checksum;
    assert(rss_ddc_parse_capabilities_reply(sentinel_window, 6, &fragment) == RSS_DDC_OK);
    assert(rss_ddc_validate_capabilities_fragment_offset(&fragment, 0) == RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    sentinel_window[1] = 0xa4;
    assert(rss_ddc_capabilities_reply_frame_size(sentinel_window, sizeof(sentinel_window), &frame_size) ==
           RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    assert(rss_ddc_parse_capabilities_reply(maximum_capabilities_reply,
                                             sizeof(maximum_capabilities_reply) + 1, &fragment) ==
           RSS_DDC_ERROR_CAPABILITIES_MALFORMED);

    /* Hardware-derived LG/DCPDP13 result: only the declared 16-byte E3 prefix is protocol data. */
    const uint8_t observed_lg_reply_window[RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE] = {
        0x6e, 0x8d, 0xe3, 0x00, 0x00, 0x28, 0x70, 0x72, 0x6f, 0x74, 0x28, 0x6d, 0x6f, 0x6e, 0x69, 0x4c,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x6e, 0x8d, 0xe3, 0x00, 0x00, 0x28,
    };
    assert(rss_ddc_capabilities_reply_frame_size(observed_lg_reply_window,
                                                  sizeof(observed_lg_reply_window), &frame_size) == RSS_DDC_OK);
    assert(frame_size == 16);
    assert(rss_ddc_parse_capabilities_reply(observed_lg_reply_window, frame_size, &fragment) == RSS_DDC_OK);
    assert(fragment.offset == 0 && fragment.length == 10 && memcmp(fragment.bytes, "(prot(moni", 10) == 0);
    /* The remaining 22 modified bytes are not a second parsed frame and are never read by this parser call. */
    assert(observed_lg_reply_window[frame_size] == 0x00 && observed_lg_reply_window[37] == 0x28);

    uint8_t next_fragment_window[RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE];
    memset(next_fragment_window, 0x9d, sizeof(next_fragment_window));
    const uint8_t next_fragment_prefix[] = {0x6e, 0x8c, 0xe3, 0x00, 0x0a, 't', 'y', 'p', 'e', '(', 'l', 'c', 'd', ')', 0x00};
    memcpy(next_fragment_window, next_fragment_prefix, sizeof(next_fragment_prefix));
    checksum = 0x50;
    for (size_t index = 0; index + 1 < sizeof(next_fragment_prefix); ++index) checksum ^= next_fragment_window[index];
    next_fragment_window[sizeof(next_fragment_prefix) - 1] = checksum;
    assert(rss_ddc_capabilities_reply_frame_size(next_fragment_window, sizeof(next_fragment_window), &frame_size) ==
           RSS_DDC_OK);
    assert(frame_size == sizeof(next_fragment_prefix));
    assert(rss_ddc_parse_capabilities_reply(next_fragment_window, frame_size, &fragment) == RSS_DDC_OK);
    assert(fragment.offset == 0x000a && fragment.length == 9 && memcmp(fragment.bytes, "type(lcd)", 9) == 0);
    assert(rss_ddc_validate_capabilities_fragment_offset(&fragment, 0x000a) == RSS_DDC_OK);
    next_fragment_window[4] = 0x0b;
    checksum = 0x50;
    for (size_t index = 0; index + 1 < frame_size; ++index) checksum ^= next_fragment_window[index];
    next_fragment_window[frame_size - 1] = checksum;
    assert(rss_ddc_parse_capabilities_reply(next_fragment_window, frame_size, &fragment) == RSS_DDC_OK);
    assert(rss_ddc_validate_capabilities_fragment_offset(&fragment, 0x000a) == RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    puts("test_protocol: passed");
    return 0;
}
