#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "correlation.h"
#include "reader.h"

static int construct_calls = 0;
static int read_calls = 0;
static int release_calls = 0;

static RSSDDCError mock_construct(void *opaque, void **device_out) {
    (void)opaque;
    ++construct_calls;
    *device_out = (void *)(uintptr_t)0x1;
    return RSS_DDC_OK;
}

static RSSDDCError mock_read(void *opaque, void *device, uint32_t address, uint8_t *bytes, size_t length) {
    (void)opaque;
    (void)device;
    assert(address == RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_ADDRESS);
    assert(length == RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_LENGTH);
    ++read_calls;
    memset(bytes, 0x11, length);
    return RSS_DDC_OK;
}

static void mock_release(void *opaque, void *device) {
    (void)opaque;
    (void)device;
    ++release_calls;
}

static RSSDDCError mock_construct_fail(void *opaque, void **device_out) {
    (void)opaque;
    ++construct_calls;
    *device_out = NULL;
    return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
}

int main(void) {
    assert(strcmp(RSS_DDC_REGISTRY_CLASS_DCPDP_SERVICE, "DCPDPService") == 0);
    assert(RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_ADDRESS == 0x00000u);
    assert(RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_LENGTH == 16u);

    uint8_t bytes[RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_LENGTH] = {};
    const RSSDDCDPCDReadCallbacks callbacks = {
        .context = NULL,
        .construct = mock_construct,
        .read = mock_read,
        .release = mock_release,
    };
    assert(rss_ddc_run_dpcd_candidate_read(0, &callbacks, RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_ADDRESS, bytes,
                                           RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_LENGTH) == RSS_DDC_ERROR_SAFETY_GATE);
    assert(construct_calls == 0 && read_calls == 0 && release_calls == 0);

    assert(rss_ddc_run_dpcd_candidate_read(2, &callbacks, RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_ADDRESS, bytes,
                                           RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_LENGTH) == RSS_DDC_ERROR_SAFETY_GATE);
    assert(construct_calls == 0 && read_calls == 0 && release_calls == 0);

    assert(rss_ddc_run_dpcd_candidate_read(1, &callbacks, RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_ADDRESS, bytes,
                                           RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_LENGTH) == RSS_DDC_OK);
    assert(construct_calls == 1 && read_calls == 1 && release_calls == 1);
    assert(bytes[0] == 0x11);

    construct_calls = read_calls = release_calls = 0;
    const RSSDDCDPCDReadCallbacks failing_callbacks = {
        .context = NULL,
        .construct = mock_construct_fail,
        .read = mock_read,
        .release = mock_release,
    };
    assert(rss_ddc_run_dpcd_candidate_read(1, &failing_callbacks, RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_ADDRESS,
                                           bytes, RSS_DDC_DCPDP_SERVICE_DPCD_VALIDATION_LENGTH) ==
           RSS_DDC_ERROR_SERVICE_CONSTRUCTION);
    assert(construct_calls == 1 && read_calls == 0 && release_calls == 0);

    puts("test_dcpdpservice: passed");
    return 0;
}
