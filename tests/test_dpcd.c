#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dpcd.h"
#include "reader.h"

typedef struct {
    unsigned int construct_calls;
    unsigned int read_calls;
    unsigned int release_calls;
    RSSDDCError construct_result;
    RSSDDCError read_result;
    uint32_t address;
    size_t length;
} ValidationMock;

static RSSDDCError mock_construct(void *opaque, void **device) {
    ValidationMock *mock = opaque;
    ++mock->construct_calls;
    if (mock->construct_result != RSS_DDC_OK) return mock->construct_result;
    *device = mock;
    return RSS_DDC_OK;
}

static RSSDDCError mock_read(void *opaque, void *device, uint32_t address, uint8_t *bytes, size_t length) {
    ValidationMock *mock = opaque;
    assert(device == mock);
    ++mock->read_calls;
    mock->address = address;
    mock->length = length;
    memset(bytes, 0xa5, length);
    return mock->read_result;
}

static void mock_release(void *opaque, void *device) {
    ValidationMock *mock = opaque;
    assert(device == mock);
    ++mock->release_calls;
}

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
    const uint8_t lg_sample[] = {0x12, 0x14, 0xc4, 0x01, 0x01, 0x00, 0x01, 0x80,
                                 0x02, 0x00, 0x06, 0x00, 0x00, 0x00, 0x83, 0x00};
    assert(rss_ddc_decode_dpcd_capabilities(0, lg_sample, sizeof(lg_sample), &decoded) == RSS_DDC_OK);
    assert(decoded.revision == 0x12 && decoded.max_link_rate_raw == 0x14 &&
           strcmp(decoded.max_link_rate_name, "HBR2 (5.40 Gbps/lane)") == 0 && decoded.max_lane_count == 4 &&
           decoded.enhanced_framing && !decoded.downstream_port_present);
    const uint8_t unknown_rate[] = {0x14, 0xff, 0x02, 0, 0, 0};
    assert(rss_ddc_decode_dpcd_capabilities(0, unknown_rate, sizeof(unknown_rate), &decoded) == RSS_DDC_OK);
    assert(decoded.max_link_rate_raw == 0xff && decoded.max_link_rate_name[0] == 'u');
    assert(rss_ddc_decode_dpcd_capabilities(1, capabilities, sizeof(capabilities), &decoded) == RSS_DDC_ERROR_DPCD_LENGTH);
    assert(rss_ddc_decode_dpcd_capabilities(0, capabilities, 5, &decoded) == RSS_DDC_ERROR_DPCD_LENGTH);

    ValidationMock mock = {};
    RSSDDCDPCDReadCallbacks callbacks = {
        .context = &mock, .construct = mock_construct, .read = mock_read, .release = mock_release,
    };
    assert(rss_ddc_run_dpcd_candidate_read(1, &callbacks, 0, bytes, 17) == RSS_DDC_ERROR_DPCD_LENGTH);
    assert(rss_ddc_run_dpcd_candidate_read(1, &callbacks, RSS_DDC_DPCD_MAX_ADDRESS, bytes, 2) ==
           RSS_DDC_ERROR_DPCD_RANGE);
    assert(mock.construct_calls == 0 && mock.read_calls == 0 && mock.release_calls == 0);
    assert(rss_ddc_run_dpcd_candidate_read(0, &callbacks, 0, bytes, 16) == RSS_DDC_ERROR_SAFETY_GATE);
    assert(mock.construct_calls == 0 && mock.read_calls == 0 && mock.release_calls == 0);
    assert(rss_ddc_run_dpcd_candidate_read(2, &callbacks, 0, bytes, 16) == RSS_DDC_ERROR_SAFETY_GATE);
    assert(mock.construct_calls == 0 && mock.read_calls == 0 && mock.release_calls == 0);
    mock.construct_result = RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    assert(rss_ddc_run_dpcd_candidate_read(1, &callbacks, 0, bytes, 16) == RSS_DDC_ERROR_SERVICE_CONSTRUCTION);
    assert(mock.construct_calls == 1 && mock.read_calls == 0 && mock.release_calls == 0);
    mock.construct_result = RSS_DDC_OK;
    assert(rss_ddc_run_dpcd_candidate_read(1, &callbacks, 0x200, bytes, 8) == RSS_DDC_OK);
    assert(mock.construct_calls == 2 && mock.read_calls == 1 && mock.release_calls == 1);
    assert(mock.address == 0x00200 && mock.length == 8);
    mock.read_result = RSS_DDC_ERROR_DPCD_READ;
    assert(rss_ddc_run_dpcd_candidate_read(1, &callbacks, 0, bytes, 16) == RSS_DDC_ERROR_DPCD_READ);
    assert(mock.construct_calls == 3 && mock.read_calls == 2 && mock.release_calls == 2);
    puts("test_dpcd: passed");
    return 0;
}
