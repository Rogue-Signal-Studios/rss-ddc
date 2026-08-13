#include "input_switch.h"

RSSDDCError rss_ddc_prepare_lg_alt_input(uint16_t value, RSSDDCLGAltInputPlan *plan_out) {
    if (plan_out == NULL || value > UINT8_MAX) return RSS_DDC_ERROR_ARGUMENT;
    RSSDDCLGAltInputPlan plan = {
        .chip = RSS_DDC_LG_ALT_INPUT_CHIP,
        .data = RSS_DDC_LG_ALT_INPUT_DATA,
        .payload_length = RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE,
        .write_count = RSS_DDC_LG_ALT_INPUT_WRITE_COUNT,
        .prewrite_delay_us = RSS_DDC_LG_ALT_INPUT_PREWRITE_DELAY_US,
    };
    plan.payload[0] = 0x84;
    plan.payload[1] = 0x03;
    plan.payload[2] = RSS_DDC_LG_ALT_INPUT_VCP;
    plan.payload[3] = 0x00;
    plan.payload[4] = (uint8_t)value;
    /* m1ddc PR #52: the alternate IOAV data address is also the checksum address. */
    plan.payload[5] = rss_ddc_request_checksum((const uint8_t[]){(uint8_t)plan.data,
        plan.payload[0], plan.payload[1], plan.payload[2], plan.payload[3], plan.payload[4]}, 6);
    *plan_out = plan;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_run_lg_alt_input(RSSDDCProvider provider, uint16_t value,
                                     const RSSDDCLGAltInputCallbacks *callbacks) {
    if (callbacks == NULL || callbacks->construct == NULL || callbacks->prewrite_delay == NULL ||
        callbacks->write_i2c == NULL || callbacks->release == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (provider != RSS_DDC_PROVIDER_DCPDP13) return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;

    RSSDDCLGAltInputPlan plan = {};
    RSSDDCError error = rss_ddc_prepare_lg_alt_input(value, &plan);
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
