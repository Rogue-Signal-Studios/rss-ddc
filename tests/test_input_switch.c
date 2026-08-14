#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "input_switch.h"

static unsigned int construct_calls, delay_calls, write_calls, release_calls;
static uint32_t observed_chip, observed_data;
static uint8_t observed_payload[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE];
static size_t observed_length;
static RSSDDCError write_result;

static RSSDDCError construct(void *context, void **service_out) {
    (void)context;
    ++construct_calls;
    *service_out = (void *)1;
    return RSS_DDC_OK;
}
static RSSDDCError delay(void *context) { (void)context; ++delay_calls; return RSS_DDC_OK; }
static RSSDDCError write_i2c(void *context, void *service, uint32_t chip, uint32_t data,
                             const uint8_t *payload, size_t length) {
    (void)context; (void)service;
    ++write_calls;
    observed_chip = chip;
    observed_data = data;
    observed_length = length;
    memcpy(observed_payload, payload, length);
    return write_result;
}
static void release(void *context, void *service) { (void)context; (void)service; ++release_calls; }
static const RSSDDCLGAltInputCallbacks callbacks = {
    .construct = construct, .prewrite_delay = delay, .write_i2c = write_i2c, .release = release,
};
static void reset(void) {
    construct_calls = delay_calls = write_calls = release_calls = 0;
    observed_chip = observed_data = 0;
    observed_length = 0;
    write_result = RSS_DDC_OK;
    memset(observed_payload, 0, sizeof(observed_payload));
}

int main(void) {
    RSSDDCLGAltInputPlan plan = {};
    const uint8_t hdmi_1[] = {0x84, 0x03, 0xf4, 0x00, 0x90, 0xdd};
    const uint8_t hdmi_2[] = {0x84, 0x03, 0xf4, 0x00, 0x91, 0xdc};
    const uint8_t displayport_1[] = {0x84, 0x03, 0xf4, 0x00, 0xd0, 0x9d};
    assert(rss_ddc_lg_alt_input_value_is_supported(0x90));
    assert(rss_ddc_lg_alt_input_value_is_supported(0x91));
    assert(rss_ddc_lg_alt_input_value_is_supported(0xd0));
    assert(!rss_ddc_lg_alt_input_value_is_supported(0x11)); /* MCCS VCP 0x60 evidence, not LG_ALT framing. */
    assert(!rss_ddc_lg_alt_input_value_is_supported(0x100));
    assert(rss_ddc_prepare_lg_alt_input(0x90, &plan) == RSS_DDC_OK);
    assert(plan.chip == 0x37 && plan.data == 0x50 && plan.payload_length == sizeof(hdmi_1));
    assert(plan.write_count == 1 && plan.prewrite_delay_us == 10000);
    assert(memcmp(plan.payload, hdmi_1, sizeof(hdmi_1)) == 0);
    assert(rss_ddc_prepare_lg_alt_input(0x91, &plan) == RSS_DDC_OK &&
           memcmp(plan.payload, hdmi_2, sizeof(hdmi_2)) == 0);
    assert(rss_ddc_prepare_lg_alt_input(0xd0, &plan) == RSS_DDC_OK &&
           memcmp(plan.payload, displayport_1, sizeof(displayport_1)) == 0);
    assert(rss_ddc_prepare_lg_alt_input(0x11, &plan) == RSS_DDC_ERROR_ARGUMENT);

    assert(rss_ddc_validate_lg_alt_input_target(RSS_DDC_PROVIDER_DCPDP13, true, "LG HDR QHD", "DCPEXT0") == RSS_DDC_OK);
    assert(rss_ddc_validate_lg_alt_input_target(RSS_DDC_PROVIDER_DCPDP_SERVICE, true, "LG HDR QHD", "DCPEXT0") == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(rss_ddc_validate_lg_alt_input_target(RSS_DDC_PROVIDER_DCPDP13, false, "LG HDR QHD", "DCPEXT0") == RSS_DDC_ERROR_SAFETY_GATE);
    assert(rss_ddc_validate_lg_alt_input_target(RSS_DDC_PROVIDER_DCPDP13, true, "Another LG", "DCPEXT0") == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(rss_ddc_validate_lg_alt_input_target(RSS_DDC_PROVIDER_DCPDP13, true, "LG HDR QHD", "DCPEXT1") == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);

    reset();
    /* Production runner uses the hardware-validated single F4 write. */
    assert(rss_ddc_run_lg_alt_input(RSS_DDC_PROVIDER_DCPDP13, 0x90, &callbacks) == RSS_DDC_OK);
    assert(construct_calls == 1 && delay_calls == 1 && write_calls == 1 && release_calls == 1);
    assert(observed_chip == 0x37 && observed_data == 0x50 && observed_length == sizeof(hdmi_1));
    assert(memcmp(observed_payload, hdmi_1, sizeof(hdmi_1)) == 0);

    uint8_t one_write_payload[sizeof(hdmi_1)] = {};
    reset();
    assert(rss_ddc_run_lg_alt_input_with_write_count(RSS_DDC_PROVIDER_DCPDP13, 0x90,
                                                      RSS_DDC_LG_ALT_INPUT_WRITE_COUNT, &callbacks) == RSS_DDC_OK);
    assert(construct_calls == 1 && delay_calls == 1 && write_calls == 1 && release_calls == 1);
    assert(observed_chip == 0x37 && observed_data == 0x50 && observed_length == sizeof(hdmi_1));
    memcpy(one_write_payload, observed_payload, sizeof(one_write_payload));
    reset();
    assert(rss_ddc_run_lg_alt_input_with_write_count(RSS_DDC_PROVIDER_DCPDP13, 0x90,
                                                      RSS_DDC_LG_ALT_INPUT_TEST_TWO_WRITE_COUNT, &callbacks) == RSS_DDC_OK);
    assert(construct_calls == 1 && delay_calls == 2 && write_calls == 2 && release_calls == 1);
    assert(memcmp(one_write_payload, observed_payload, sizeof(one_write_payload)) == 0);
    reset();
    assert(rss_ddc_run_lg_alt_input_with_write_count(RSS_DDC_PROVIDER_DCPDP13, 0x90, 0, &callbacks) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_run_lg_alt_input_with_write_count(RSS_DDC_PROVIDER_DCPDP13, 0x90, 3, &callbacks) == RSS_DDC_ERROR_ARGUMENT);
    assert(construct_calls == 0 && delay_calls == 0 && write_calls == 0 && release_calls == 0);
    reset();
    write_result = RSS_DDC_ERROR_WRITE;
    assert(rss_ddc_run_lg_alt_input(RSS_DDC_PROVIDER_DCPDP13, 0x90, &callbacks) == RSS_DDC_ERROR_WRITE);
    assert(construct_calls == 1 && delay_calls == 1 && write_calls == 1 && release_calls == 1);
    reset();
    assert(rss_ddc_run_lg_alt_input(RSS_DDC_PROVIDER_DCPDP_SERVICE, 0x90, &callbacks) == RSS_DDC_ERROR_UNSUPPORTED_PROVIDER);
    assert(construct_calls == 0 && delay_calls == 0 && write_calls == 0 && release_calls == 0);
    assert(rss_ddc_run_lg_alt_input(RSS_DDC_PROVIDER_DCPDP13, 0x11, &callbacks) == RSS_DDC_ERROR_ARGUMENT);
    assert(construct_calls == 0 && delay_calls == 0 && write_calls == 0 && release_calls == 0);
    puts("test_input_switch: passed");
    return 0;
}
