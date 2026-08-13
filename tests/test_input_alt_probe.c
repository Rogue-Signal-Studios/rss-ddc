#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "input_alt_probe.h"

static int construct_calls, delay_calls, write_calls, release_calls;
static uint32_t observed_chip, observed_data;
static uint8_t observed_payload[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE];
static size_t observed_payload_length;
static RSSDDCError write_result;

static RSSDDCError construct(void *context, void **service_out) { (void)context; ++construct_calls; *service_out = (void *)1; return RSS_DDC_OK; }
static RSSDDCError delay(void *context) { (void)context; ++delay_calls; return RSS_DDC_OK; }
static RSSDDCError write_i2c(void *context, void *service, uint32_t chip, uint32_t data, const uint8_t *payload, size_t length) {
    (void)context; (void)service; ++write_calls; observed_chip = chip; observed_data = data; observed_payload_length = length;
    memcpy(observed_payload, payload, length); return write_result;
}
static void release(void *context, void *service) { (void)context; (void)service; ++release_calls; }
static const RSSDDCInputAltProbeCallbacks callbacks = {.construct = construct, .prewrite_delay = delay, .write_i2c = write_i2c, .release = release};
static void reset(void) {
    construct_calls = delay_calls = write_calls = release_calls = 0;
    observed_chip = observed_data = 0; observed_payload_length = 0; write_result = RSS_DDC_OK;
    memset(observed_payload, 0, sizeof(observed_payload));
}

int main(void) {
    RSSDDCInputAltProbePlan plan = {};
    RSSDDCInputAltProbeVariant variant = RSS_DDC_INPUT_ALT_PROBE_INLINE_UNSUPPORTED;
    assert(rss_ddc_input_alt_probe_variant_from_string("conventional", &variant) == RSS_DDC_OK && variant == RSS_DDC_INPUT_ALT_PROBE_CONVENTIONAL);
    assert(rss_ddc_input_alt_probe_variant_from_string("lg-alt", &variant) == RSS_DDC_OK && variant == RSS_DDC_INPUT_ALT_PROBE_LG_ALT);
    assert(rss_ddc_input_alt_probe_variant_from_string("other", &variant) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_prepare_dcpdp13_input_alt_probe(RSS_DDC_INPUT_ALT_PROBE_CONVENTIONAL, 0x11, &plan) == RSS_DDC_OK);
    const uint8_t expected_11[] = {0x84, 0x03, 0x60, 0x00, 0x11, 0xc8};
    assert(plan.chip == 0x37 && plan.data == 0x50 && plan.payload_length == 6 && plan.write_count == 2 && plan.prewrite_delay_us == 10000);
    assert(memcmp(plan.payload, expected_11, sizeof(expected_11)) == 0);
    assert(rss_ddc_prepare_dcpdp13_input_alt_probe(RSS_DDC_INPUT_ALT_PROBE_CONVENTIONAL, 0x12, &plan) == RSS_DDC_OK);
    assert(plan.payload[5] == 0xcb);
    assert(rss_ddc_prepare_dcpdp13_input_alt_probe(RSS_DDC_INPUT_ALT_PROBE_LG_ALT, 0x90, &plan) == RSS_DDC_OK);
    const uint8_t expected_lg_hdmi_1[] = {0x84, 0x03, 0xf4, 0x00, 0x90, 0xdd};
    assert(plan.chip == 0x37 && plan.data == 0x50 && plan.payload_length == 6 && plan.write_count == 2 && plan.prewrite_delay_us == 10000);
    assert(memcmp(plan.payload, expected_lg_hdmi_1, sizeof(expected_lg_hdmi_1)) == 0);
    assert(rss_ddc_prepare_dcpdp13_input_alt_probe(RSS_DDC_INPUT_ALT_PROBE_LG_ALT, 0x91, &plan) == RSS_DDC_OK);
    const uint8_t expected_lg_hdmi_2[] = {0x84, 0x03, 0xf4, 0x00, 0x91, 0xdc};
    assert(memcmp(plan.payload, expected_lg_hdmi_2, sizeof(expected_lg_hdmi_2)) == 0);
    assert(rss_ddc_prepare_dcpdp13_input_alt_probe(RSS_DDC_INPUT_ALT_PROBE_LG_ALT, 0xd0, &plan) == RSS_DDC_OK);
    const uint8_t expected_lg_displayport_1[] = {0x84, 0x03, 0xf4, 0x00, 0xd0, 0x9d};
    assert(memcmp(plan.payload, expected_lg_displayport_1, sizeof(expected_lg_displayport_1)) == 0);
    assert(rss_ddc_prepare_dcpdp13_input_alt_probe(RSS_DDC_INPUT_ALT_PROBE_INLINE_UNSUPPORTED, 0x11, &plan) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);

    reset();
    assert(rss_ddc_run_dcpdp13_input_alt_probe(RSS_DDC_PROVIDER_DCPDP13, RSS_DDC_INPUT_ALT_PROBE_CONVENTIONAL, 0x11, &callbacks) == RSS_DDC_OK);
    assert(construct_calls == 1 && delay_calls == 2 && write_calls == 2 && release_calls == 1);
    assert(observed_chip == 0x37 && observed_data == 0x50 && observed_payload_length == 6);
    assert(memcmp(observed_payload, expected_11, sizeof(expected_11)) == 0);
    /* The runner exposes no GET, verify, restore, or retry callback: it makes
     * exactly the planned two upstream writes and returns immediately on error. */
    reset();
    assert(rss_ddc_run_dcpdp13_input_alt_probe(RSS_DDC_PROVIDER_DCPDP13, RSS_DDC_INPUT_ALT_PROBE_LG_ALT, 0x90, &callbacks) == RSS_DDC_OK);
    assert(construct_calls == 1 && delay_calls == 2 && write_calls == 2 && release_calls == 1);
    assert(observed_chip == 0x37 && observed_data == 0x50 && observed_payload_length == 6);
    assert(memcmp(observed_payload, expected_lg_hdmi_1, sizeof(expected_lg_hdmi_1)) == 0);
    reset();
    write_result = RSS_DDC_ERROR_WRITE;
    assert(rss_ddc_run_dcpdp13_input_alt_probe(RSS_DDC_PROVIDER_DCPDP13, RSS_DDC_INPUT_ALT_PROBE_LG_ALT, 0x90, &callbacks) == RSS_DDC_ERROR_WRITE);
    assert(construct_calls == 1 && delay_calls == 1 && write_calls == 1 && release_calls == 1);
    reset();
    assert(rss_ddc_run_dcpdp13_input_alt_probe(RSS_DDC_PROVIDER_DCPDP_SERVICE, RSS_DDC_INPUT_ALT_PROBE_LG_ALT, 0x90, &callbacks) == RSS_DDC_ERROR_UNSUPPORTED_PROVIDER);
    assert(construct_calls == 0 && delay_calls == 0 && write_calls == 0 && release_calls == 0);
    reset();
    assert(rss_ddc_run_dcpdp13_input_alt_probe(RSS_DDC_PROVIDER_DCPDP13, RSS_DDC_INPUT_ALT_PROBE_INLINE_UNSUPPORTED, 0x11, &callbacks) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(construct_calls == 0 && delay_calls == 0 && write_calls == 0 && release_calls == 0);
    puts("test_input_alt_probe: passed");
    return 0;
}
