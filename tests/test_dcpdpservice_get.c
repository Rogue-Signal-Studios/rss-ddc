#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "correlation.h"
#include "get_validation.h"
#include "rss_ddc.h"

static int construct_calls = 0;
static int write_calls = 0;
static int delay_calls = 0;
static int read_calls = 0;
static int release_calls = 0;
static uint32_t last_chip = 0;
static uint32_t last_data = 0;
static uint8_t last_request[RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE] = {};
static size_t last_request_length = 0;

static const uint8_t valid_reply_10[] = {0x6e, 0x88, 0x02, 0x00, 0x10, 0x00, 0x00, 0x64, 0x00, 0x64, 0xa4};

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

static RSSDDCError mock_write(void *opaque, void *service, uint32_t chip, uint32_t data,
                              const uint8_t *payload, size_t payload_length) {
    (void)opaque;
    (void)service;
    ++write_calls;
    last_chip = chip;
    last_data = data;
    last_request_length = payload_length;
    memcpy(last_request, payload, payload_length);
    return RSS_DDC_OK;
}

static RSSDDCError mock_write_fail(void *opaque, void *service, uint32_t chip, uint32_t data,
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

static RSSDDCError mock_delay(void *opaque) {
    (void)opaque;
    ++delay_calls;
    return RSS_DDC_OK;
}

static RSSDDCError mock_read(void *opaque, void *service, uint32_t chip, uint32_t data, uint8_t *reply,
                             size_t reply_length) {
    (void)opaque;
    (void)service;
    ++read_calls;
    assert(chip == 0x37u);
    assert(data == 0x51u);
    assert(reply_length == RSS_DDC_GET_VCP_REPLY_SIZE);
    memcpy(reply, valid_reply_10, reply_length);
    return RSS_DDC_OK;
}

static RSSDDCError mock_read_garbage(void *opaque, void *service, uint32_t chip, uint32_t data, uint8_t *reply,
                                     size_t reply_length) {
    (void)opaque;
    (void)service;
    (void)chip;
    (void)data;
    ++read_calls;
    memset(reply, 0, reply_length);
    return RSS_DDC_OK;
}

static void mock_release(void *opaque, void *service) {
    (void)opaque;
    (void)service;
    ++release_calls;
}

static void reset_counters(void) {
    construct_calls = write_calls = delay_calls = read_calls = release_calls = 0;
    last_chip = last_data = 0;
    last_request_length = 0;
    memset(last_request, 0, sizeof(last_request));
}

int main(void) {
    assert(RSS_DDC_DCPDP_SERVICE_GET_VALIDATION_VCP == 0x10u);
    assert(RSS_DDC_DCPDP_SERVICE_GET_VALIDATION_DELAY_US == 50000u);
    assert(rss_ddc_dcpdpservice_get_validation_ready(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK));
    assert(!rss_ddc_dcpdpservice_get_validation_ready(RSS_DDC_DCPDP_SERVICE_CORRELATION_PROVIDER_MISMATCH));
    assert(rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_UNKNOWN) == RSS_DDC_CAP_NONE);
    assert(rss_ddc_provider_from_registry_class("DCPDPService") == RSS_DDC_PROVIDER_UNKNOWN);

    RSSDDCVCPResult result = {};
    const RSSDDCConventionalGetValidationCallbacks callbacks = {
        .context = NULL,
        .construct = mock_construct,
        .write_i2c = mock_write,
        .delay = mock_delay,
        .read_i2c = mock_read,
        .release = mock_release,
    };

    assert(rss_ddc_run_dcpdpservice_get_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_NO_SERVICE, &callbacks,
                                                   &result) == RSS_DDC_ERROR_SAFETY_GATE);
    assert(rss_ddc_run_dcpdpservice_get_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_AMBIGUOUS_SERVICE, &callbacks,
                                                   &result) == RSS_DDC_ERROR_SAFETY_GATE);
    assert(rss_ddc_run_dcpdpservice_get_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_PROVIDER_MISMATCH, &callbacks,
                                                   &result) == RSS_DDC_ERROR_SAFETY_GATE);
    assert(construct_calls == 0 && write_calls == 0 && read_calls == 0);

    reset_counters();
    assert(rss_ddc_run_dcpdpservice_get_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK, &callbacks, &result) ==
           RSS_DDC_OK);
    assert(construct_calls == 1 && write_calls == 1 && delay_calls == 1 && read_calls == 1 && release_calls == 1);
    assert(last_chip == 0x37u && last_data == 0x51u);
    assert(last_request_length == RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE);
    assert(last_request[0] == 0x82 && last_request[1] == 0x01 && last_request[2] == 0x10 && last_request[3] == 0xfd);
    assert(result.vcp_code == 0x10 && result.maximum_value == 100 && result.current_value == 100);

    reset_counters();
    const RSSDDCConventionalGetValidationCallbacks failing_construct = {
        .context = NULL,
        .construct = mock_construct_fail,
        .write_i2c = mock_write,
        .delay = mock_delay,
        .read_i2c = mock_read,
        .release = mock_release,
    };
    assert(rss_ddc_run_dcpdpservice_get_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK, &failing_construct,
                                                   &result) == RSS_DDC_ERROR_SERVICE_CONSTRUCTION);
    assert(construct_calls == 1 && write_calls == 0 && read_calls == 0 && release_calls == 0);

    reset_counters();
    const RSSDDCConventionalGetValidationCallbacks failing_write = {
        .context = NULL,
        .construct = mock_construct,
        .write_i2c = mock_write_fail,
        .delay = mock_delay,
        .read_i2c = mock_read,
        .release = mock_release,
    };
    assert(rss_ddc_run_dcpdpservice_get_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK, &failing_write, &result) ==
           RSS_DDC_ERROR_WRITE);
    assert(construct_calls == 1 && write_calls == 1 && delay_calls == 0 && read_calls == 0 && release_calls == 1);

    reset_counters();
    const RSSDDCConventionalGetValidationCallbacks garbage_read = {
        .context = NULL,
        .construct = mock_construct,
        .write_i2c = mock_write,
        .delay = mock_delay,
        .read_i2c = mock_read_garbage,
        .release = mock_release,
    };
    assert(rss_ddc_run_dcpdpservice_get_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK, &garbage_read, &result) ==
           RSS_DDC_ERROR_REPLY_SOURCE);
    assert(construct_calls == 1 && write_calls == 1 && delay_calls == 1 && read_calls == 1 && release_calls == 1);

    puts("test_dcpdpservice_get: passed");
    return 0;
}
