#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "correlation.h"
#include "set_validation.h"
#include "rss_ddc.h"

static int construct_calls = 0;
static int delay_calls = 0;
static int write_calls = 0;
static int release_calls = 0;
static uint32_t last_chip = 0;
static uint32_t last_data = 0;
static uint8_t last_request[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE] = {};

static RSSDDCError mock_construct(void *opaque, void **service_out) {
    (void)opaque;
    ++construct_calls;
    *service_out = (void *)(uintptr_t)0x1;
    return RSS_DDC_OK;
}

static RSSDDCError mock_construct_fail(void *opaque, void **service_out) {
    (void)opaque;
    ++construct_calls;
    *service_out = NULL;
    return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
}

static RSSDDCError mock_delay(void *opaque) {
    (void)opaque;
    ++delay_calls;
    return RSS_DDC_OK;
}

static RSSDDCError mock_write(void *opaque, void *service, uint32_t chip, uint32_t data,
                              const uint8_t *payload, size_t payload_length) {
    (void)opaque;
    (void)service;
    ++write_calls;
    last_chip = chip;
    last_data = data;
    memcpy(last_request, payload, payload_length);
    return RSS_DDC_OK;
}

static RSSDDCError mock_write_fail_first(void *opaque, void *service, uint32_t chip, uint32_t data,
                                         const uint8_t *payload, size_t payload_length) {
    (void)service;
    (void)chip;
    (void)data;
    (void)payload;
    (void)payload_length;
    (void)opaque;
    ++write_calls;
    return RSS_DDC_ERROR_WRITE;
}

static void mock_release(void *opaque, void *service) {
    (void)opaque;
    (void)service;
    ++release_calls;
}

static void reset_counters(void) {
    construct_calls = delay_calls = write_calls = release_calls = 0;
    last_chip = last_data = 0;
    memset(last_request, 0, sizeof(last_request));
}

int main(void) {
    assert(RSS_DDC_DCPDP_SERVICE_SET_VALIDATION_VCP == 0x10u);
    assert(RSS_DDC_DCPDP_SERVICE_SET_WRITE_COUNT == 2u);
    assert(RSS_DDC_DCPDP_SERVICE_SET_PREWRITE_DELAY_US == 10000u);
    assert(rss_ddc_provider_from_registry_class("DCPDPService") == RSS_DDC_PROVIDER_DCPDP_SERVICE);
    assert(rss_ddc_provider_backend(RSS_DDC_PROVIDER_DCPDP_SERVICE) == RSS_DDC_BACKEND_DCPDP_SERVICE);
    assert(rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_DCPDP_SERVICE) ==
           (RSS_DDC_CAP_GET_VCP | RSS_DDC_CAP_READ_DPCD));
    assert((rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_DCPDP_SERVICE) & RSS_DDC_CAP_SET_VCP) == 0);
    assert((rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_DCPDP_SERVICE) & RSS_DDC_CAP_READ_EDID) == 0);

    uint8_t expected_payload[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE];
    rss_ddc_build_conventional_set_vcp(0x10, 62, expected_payload);

    const RSSDDCConventionalSetValidationCallbacks callbacks = {
        .context = NULL,
        .construct = mock_construct,
        .pre_write_delay = mock_delay,
        .write_i2c = mock_write,
        .release = mock_release,
    };

    assert(rss_ddc_run_dcpdpservice_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_PROVIDER_MISMATCH, 62,
                                                   &callbacks) == RSS_DDC_ERROR_SAFETY_GATE);
    assert(construct_calls == 0 && write_calls == 0);

    reset_counters();
    assert(rss_ddc_run_dcpdpservice_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK, 62, &callbacks) ==
           RSS_DDC_OK);
    assert(construct_calls == 1 && delay_calls == 2 && write_calls == 2 && release_calls == 1);
    assert(last_chip == 0x37u && last_data == 0x51u);
    assert(memcmp(last_request, expected_payload, sizeof(last_request)) == 0);

    reset_counters();
    const RSSDDCConventionalSetValidationCallbacks failing_construct = {
        .context = NULL,
        .construct = mock_construct_fail,
        .pre_write_delay = mock_delay,
        .write_i2c = mock_write,
        .release = mock_release,
    };
    assert(rss_ddc_run_dcpdpservice_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK, 62, &failing_construct) ==
           RSS_DDC_ERROR_SERVICE_CONSTRUCTION);
    assert(construct_calls == 1 && delay_calls == 0 && write_calls == 0 && release_calls == 0);

    reset_counters();
    const RSSDDCConventionalSetValidationCallbacks failing_write = {
        .context = NULL,
        .construct = mock_construct,
        .pre_write_delay = mock_delay,
        .write_i2c = mock_write_fail_first,
        .release = mock_release,
    };
    assert(rss_ddc_run_dcpdpservice_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK, 62, &failing_write) ==
           RSS_DDC_ERROR_WRITE);
    assert(construct_calls == 1 && delay_calls == 1 && write_calls == 1 && release_calls == 1);

    puts("test_dcpdpservice_set: passed");
    return 0;
}
