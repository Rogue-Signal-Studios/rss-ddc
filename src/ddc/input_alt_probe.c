#include "input_alt_probe.h"

#include <string.h>

const char *rss_ddc_input_alt_probe_variant_name(RSSDDCInputAltProbeVariant variant) {
    switch (variant) {
        case RSS_DDC_INPUT_ALT_PROBE_CONVENTIONAL: return "conventional";
        case RSS_DDC_INPUT_ALT_PROBE_INLINE_UNSUPPORTED: return "inline";
    }
    return "unknown";
}

RSSDDCError rss_ddc_input_alt_probe_variant_from_string(const char *text, RSSDDCInputAltProbeVariant *variant_out) {
    if (text == NULL || variant_out == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (strcmp(text, "conventional") == 0) { *variant_out = RSS_DDC_INPUT_ALT_PROBE_CONVENTIONAL; return RSS_DDC_OK; }
    if (strcmp(text, "inline") == 0) { *variant_out = RSS_DDC_INPUT_ALT_PROBE_INLINE_UNSUPPORTED; return RSS_DDC_OK; }
    return RSS_DDC_ERROR_ARGUMENT;
}

RSSDDCError rss_ddc_prepare_dcpdp13_input_alt_probe(RSSDDCInputAltProbeVariant variant, uint8_t value,
                                                     RSSDDCInputAltProbePlan *plan_out) {
    if (plan_out == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *plan_out = (RSSDDCInputAltProbePlan){0};
    if (variant == RSS_DDC_INPUT_ALT_PROBE_INLINE_UNSUPPORTED) return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    if (variant != RSS_DDC_INPUT_ALT_PROBE_CONVENTIONAL) return RSS_DDC_ERROR_ARGUMENT;

    RSSDDCInputAltProbePlan plan = {
        .chip = RSS_DDC_INPUT_ALT_PROBE_CHIP,
        .data = RSS_DDC_INPUT_ALT_PROBE_DATA,
        .payload_length = RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE,
        .write_count = RSS_DDC_INPUT_ALT_PROBE_WRITE_COUNT,
        .prewrite_delay_us = RSS_DDC_INPUT_ALT_PROBE_PREWRITE_DELAY_US,
    };
    plan.payload[0] = 0x84;
    plan.payload[1] = 0x03;
    plan.payload[2] = RSS_DDC_INPUT_ALT_PROBE_VCP;
    plan.payload[3] = 0x00;
    plan.payload[4] = value;
    /* Upstream m1ddc PR #52 established that alternate IOAV data address 0x50
     * is also the write checksum source address; it remains outside this payload. */
    plan.payload[5] = rss_ddc_request_checksum((const uint8_t[]){(uint8_t)plan.data,
        plan.payload[0], plan.payload[1], plan.payload[2], plan.payload[3], plan.payload[4]}, 6);
    *plan_out = plan;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_run_dcpdp13_input_alt_probe(RSSDDCProvider provider, RSSDDCInputAltProbeVariant variant,
                                                 uint8_t value, const RSSDDCInputAltProbeCallbacks *callbacks) {
    if (callbacks == NULL || callbacks->construct == NULL || callbacks->prewrite_delay == NULL ||
        callbacks->write_i2c == NULL || callbacks->release == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (provider != RSS_DDC_PROVIDER_DCPDP13) return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;

    RSSDDCInputAltProbePlan plan = {};
    RSSDDCError error = rss_ddc_prepare_dcpdp13_input_alt_probe(variant, value, &plan);
    if (error != RSS_DDC_OK) return error;
    void *service = NULL;
    error = callbacks->construct(callbacks->context, &service);
    if (error != RSS_DDC_OK) return error;
    if (service == NULL) return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    for (unsigned int index = 0; index < plan.write_count; ++index) {
        error = callbacks->prewrite_delay(callbacks->context);
        if (error == RSS_DDC_OK) error = callbacks->write_i2c(callbacks->context, service, plan.chip, plan.data,
                                                               plan.payload, plan.payload_length);
        if (error != RSS_DDC_OK) { callbacks->release(callbacks->context, service); return error; }
    }
    callbacks->release(callbacks->context, service);
    return RSS_DDC_OK;
}
