#include <string.h>

#include "input_switch.h"

bool rss_ddc_lg_alt_input_value_is_supported(uint16_t value) {
    return value == 0x90u || value == 0x91u || value == 0xd0u;
}

bool rss_ddc_lg_alt_input_write_count_is_supported(unsigned int write_count) {
    return write_count == RSS_DDC_LG_ALT_INPUT_WRITE_COUNT ||
        write_count == RSS_DDC_LG_ALT_INPUT_TEST_TWO_WRITE_COUNT;
}

RSSDDCError rss_ddc_validate_lg_alt_input_target(RSSDDCProvider provider, bool dp_safety_gate,
                                                 const char *product_name, const char *transport) {
    if (provider != RSS_DDC_PROVIDER_DCPDP13) {
        return provider == RSS_DDC_PROVIDER_UNKNOWN ? RSS_DDC_ERROR_UNSUPPORTED_PROVIDER :
            RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    if (!dp_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    if (product_name == NULL || transport == NULL || strcmp(product_name, "LG HDR QHD") != 0 ||
        strcmp(transport, "DCPEXT0") != 0) return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_prepare_lg_alt_input(uint16_t value, RSSDDCLGAltInputPlan *plan_out) {
    if (plan_out == NULL || !rss_ddc_lg_alt_input_value_is_supported(value)) return RSS_DDC_ERROR_ARGUMENT;
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
    /* The alternate IOAV data address is part of the DDC/CI checksum convention. */
    plan.payload[5] = rss_ddc_request_checksum((const uint8_t[]){(uint8_t)plan.data,
        plan.payload[0], plan.payload[1], plan.payload[2], plan.payload[3], plan.payload[4]}, 6);
    *plan_out = plan;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_run_lg_alt_input(RSSDDCProvider provider, uint16_t value,
                                     const RSSDDCLGAltInputCallbacks *callbacks) {
    return rss_ddc_run_lg_alt_input_with_write_count(provider, value, RSS_DDC_LG_ALT_INPUT_WRITE_COUNT,
                                                      callbacks);
}

RSSDDCError rss_ddc_run_lg_alt_input_with_write_count(RSSDDCProvider provider, uint16_t value,
                                                       unsigned int write_count,
                                                       const RSSDDCLGAltInputCallbacks *callbacks) {
    if (callbacks == NULL || callbacks->construct == NULL || callbacks->prewrite_delay == NULL ||
        callbacks->write_i2c == NULL || callbacks->release == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (provider != RSS_DDC_PROVIDER_DCPDP13) return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    if (!rss_ddc_lg_alt_input_write_count_is_supported(write_count)) return RSS_DDC_ERROR_ARGUMENT;

    RSSDDCLGAltInputPlan plan = {};
    RSSDDCError error = rss_ddc_prepare_lg_alt_input(value, &plan);
    if (error != RSS_DDC_OK) return error;
    plan.write_count = write_count;
    void *service = NULL;
    error = callbacks->construct(callbacks->context, &service);
    if (error != RSS_DDC_OK) return error;
    if (service == NULL) return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    for (unsigned int index = 0; index < plan.write_count; ++index) {
        error = callbacks->prewrite_delay(callbacks->context);
        if (error == RSS_DDC_OK) error = callbacks->write_i2c(callbacks->context, service, plan.chip, plan.data,
                                                               plan.payload, plan.payload_length);
        if (error != RSS_DDC_OK) {
            callbacks->release(callbacks->context, service);
            return error;
        }
    }
    callbacks->release(callbacks->context, service);
    return RSS_DDC_OK;
}
