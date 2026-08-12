#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "correlation.h"
#include "set_validation.h"
#include "rss_ddc.h"

static int construct_calls;
static int pre_write_delay_calls;
static int write_calls;
static int release_calls;
static int get_calls;
static int set_calls;
static int settle_calls;
static uint16_t set_values[8];
static size_t set_value_count;
static uint16_t get_current_sequence[16];
static size_t get_sequence_length;
static size_t get_sequence_index;
static RSSDDCError get_fail_error;
static RSSDDCError set_fail_on_call;
static RSSDDCError get_fail_on_call;
static RSSDDCError settle_fail_on_call;

static RSSDDCError mock_construct(void *opaque, void **service_out) {
    (void)opaque;
    ++construct_calls;
    *service_out = (void *)(uintptr_t)0x1;
    return RSS_DDC_OK;
}

static RSSDDCError mock_pre_write_delay(void *opaque) {
    (void)opaque;
    ++pre_write_delay_calls;
    return RSS_DDC_OK;
}

static RSSDDCError mock_write(void *opaque, void *service, uint32_t chip, uint32_t data,
                              const uint8_t *payload, size_t payload_length) {
    (void)opaque;
    (void)service;
    (void)chip;
    (void)data;
    (void)payload;
    (void)payload_length;
    ++write_calls;
    return RSS_DDC_OK;
}

static void mock_release(void *opaque, void *service) {
    (void)opaque;
    (void)service;
    ++release_calls;
}

static RSSDDCError mock_get_vcp(void *opaque, RSSDDCVCPResult *result) {
    (void)opaque;
    ++get_calls;
    if (get_fail_error != RSS_DDC_OK && get_calls == 1) return get_fail_error;
    if (get_fail_on_call != 0 && get_calls == (int)get_fail_on_call) return RSS_DDC_ERROR_REPLY_SOURCE;
    assert(get_sequence_index < get_sequence_length);
    result->vcp_code = RSS_DDC_DCPDP_SERVICE_SET_VALIDATION_VCP;
    result->maximum_value = 100;
    result->current_value = get_current_sequence[get_sequence_index++];
    return RSS_DDC_OK;
}

static RSSDDCError mock_set_vcp(void *opaque, uint16_t value) {
    (void)opaque;
    ++set_calls;
    if (set_fail_on_call != 0 && set_calls == (int)set_fail_on_call) return RSS_DDC_ERROR_WRITE;
    assert(set_value_count < sizeof(set_values) / sizeof(set_values[0]));
    set_values[set_value_count++] = value;
    return RSS_DDC_OK;
}

static RSSDDCError mock_settle(void *opaque) {
    (void)opaque;
    ++settle_calls;
    if (settle_fail_on_call != 0 && settle_calls == (int)settle_fail_on_call) return RSS_DDC_ERROR_SYSTEM;
    return RSS_DDC_OK;
}

static void reset_mocks(void) {
    construct_calls = pre_write_delay_calls = write_calls = release_calls = 0;
    get_calls = set_calls = settle_calls = 0;
    set_value_count = 0;
    get_sequence_index = 0;
    get_sequence_length = 0;
    get_fail_error = RSS_DDC_OK;
    set_fail_on_call = 0;
    get_fail_on_call = 0;
    settle_fail_on_call = 0;
    memset(set_values, 0, sizeof(set_values));
    memset(get_current_sequence, 0, sizeof(get_current_sequence));
}

static void set_get_sequence(const uint16_t *values, size_t count) {
    memcpy(get_current_sequence, values, count * sizeof(uint16_t));
    get_sequence_length = count;
}

static const RSSDDCConventionalSetValidationCallbacks low_level_callbacks = {
    .context = NULL,
    .construct = mock_construct,
    .pre_write_delay = mock_pre_write_delay,
    .write_i2c = mock_write,
    .release = mock_release,
};

static RSSDDCDCPDPServiceReversibleSetValidationCallbacks reversible_callbacks(void) {
    return (RSSDDCDCPDPServiceReversibleSetValidationCallbacks){
        .context = NULL,
        .get_vcp = mock_get_vcp,
        .set_vcp = mock_set_vcp,
        .verification_settle = mock_settle,
        .log = NULL,
    };
}

int main(void) {
    uint16_t target = 0;
    assert(RSS_DDC_DCPDP_SERVICE_SET_VERIFY_SETTLE_MS == 250u);
    assert(rss_ddc_dcpdpservice_set_validation_adjacent_target(62, 100, &target) == RSS_DDC_OK);
    assert(target == 61);
    assert(rss_ddc_dcpdpservice_set_validation_adjacent_target(0, 100, &target) == RSS_DDC_OK);
    assert(target == 1);
    assert(rss_ddc_dcpdpservice_set_validation_adjacent_target(100, 100, &target) == RSS_DDC_OK);
    assert(target == 99);
    assert(rss_ddc_dcpdpservice_set_validation_adjacent_target(0, 0, &target) == RSS_DDC_ERROR_ARGUMENT);

    assert(rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_DCPDP_SERVICE) ==
           (RSS_DDC_CAP_GET_VCP | RSS_DDC_CAP_READ_DPCD));
    assert((rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_DCPDP_SERVICE) & RSS_DDC_CAP_SET_VCP) == 0);

    reset_mocks();
    uint8_t expected_payload[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE];
    rss_ddc_build_conventional_set_vcp(0x10, 61, expected_payload);
    assert(rss_ddc_run_dcpdpservice_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK, 61, &low_level_callbacks) ==
           RSS_DDC_OK);
    assert(construct_calls == 1 && pre_write_delay_calls == 2 && write_calls == 2 && release_calls == 1);

    RSSDDCDCPDPServiceReversibleSetReport report = {};
    RSSDDCDCPDPServiceReversibleSetValidationCallbacks callbacks = reversible_callbacks();

    reset_mocks();
    set_get_sequence((const uint16_t[]){62, 61, 62}, 3);
    assert(rss_ddc_run_dcpdpservice_reversible_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK,
                                                              &callbacks, &report) == RSS_DDC_OK);
    assert(report.outcome == RSS_DDC_REVERSIBLE_SET_OUTCOME_OK);
    assert(report.original_value == 62 && report.target_value == 61);
    assert(get_calls == 3 && set_calls == 2 && settle_calls == 2);
    assert(set_value_count == 2 && set_values[0] == 61 && set_values[1] == 62);
    assert(report.target_writes_completed && report.restore_attempted && report.restore_writes_completed);

    reset_mocks();
    set_get_sequence((const uint16_t[]){0, 1, 0}, 3);
    assert(rss_ddc_run_dcpdpservice_reversible_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK,
                                                              &callbacks, &report) == RSS_DDC_OK);
    assert(report.original_value == 0 && report.target_value == 1);

    reset_mocks();
    get_fail_error = RSS_DDC_ERROR_READ;
    assert(rss_ddc_run_dcpdpservice_reversible_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK,
                                                              &callbacks, &report) == RSS_DDC_ERROR_READ);
    assert(report.outcome == RSS_DDC_REVERSIBLE_SET_OUTCOME_PRE_GET_FAILED);
    assert(set_calls == 0 && settle_calls == 0);

    reset_mocks();
    get_fail_error = RSS_DDC_OK;
    set_get_sequence((const uint16_t[]){62}, 1);
    report = (RSSDDCDCPDPServiceReversibleSetReport){};
    set_fail_on_call = 1;
    assert(rss_ddc_run_dcpdpservice_reversible_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK,
                                                              &callbacks, &report) == RSS_DDC_ERROR_WRITE);
    assert(report.outcome == RSS_DDC_REVERSIBLE_SET_OUTCOME_TARGET_WRITE_FAILED);
    assert(!report.target_writes_completed && !report.restore_attempted);

    reset_mocks();
    set_get_sequence((const uint16_t[]){62, 62, 62}, 3);
    set_fail_on_call = 0;
    get_fail_on_call = 2;
    assert(rss_ddc_run_dcpdpservice_reversible_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK,
                                                              &callbacks, &report) ==
           RSS_DDC_ERROR_REPLY_SOURCE);
    assert(report.outcome == RSS_DDC_REVERSIBLE_SET_OUTCOME_TARGET_VERIFY_FAILED);
    assert(report.target_writes_completed && report.restore_attempted && report.restore_writes_completed);
    assert(set_value_count == 2 && set_values[1] == 62);

    reset_mocks();
    set_get_sequence((const uint16_t[]){62, 62, 62}, 3);
    get_fail_on_call = 0;
    assert(rss_ddc_run_dcpdpservice_reversible_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK,
                                                              &callbacks, &report) ==
           RSS_DDC_ERROR_VERIFY_MISMATCH);
    assert(report.target_verify_current == 62);

    reset_mocks();
    set_get_sequence((const uint16_t[]){62, 61, 62}, 3);
    set_fail_on_call = 2;
    assert(rss_ddc_run_dcpdpservice_reversible_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK,
                                                              &callbacks, &report) ==
           RSS_DDC_ERROR_WRITE);
    assert(report.outcome == RSS_DDC_REVERSIBLE_SET_OUTCOME_RESTORE_WRITE_FAILED);
    assert(report.target_writes_completed && report.restore_attempted && !report.restore_writes_completed);

    reset_mocks();
    set_get_sequence((const uint16_t[]){62, 61, 55}, 3);
    set_fail_on_call = 0;
    assert(rss_ddc_run_dcpdpservice_reversible_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK,
                                                              &callbacks, &report) ==
           RSS_DDC_ERROR_VERIFY_MISMATCH);
    assert(report.outcome == RSS_DDC_REVERSIBLE_SET_OUTCOME_RESTORE_VERIFY_FAILED);
    assert(report.restore_verify_current == 55);

    reset_mocks();
    set_get_sequence((const uint16_t[]){62, 61, 62}, 3);
    settle_fail_on_call = 1;
    assert(rss_ddc_run_dcpdpservice_reversible_set_validation(RSS_DDC_DCPDP_SERVICE_CORRELATION_OK,
                                                              &callbacks, &report) ==
           RSS_DDC_ERROR_SYSTEM);
    assert(report.restore_attempted);

    puts("test_dcpdpservice_set: passed");
    return 0;
}
