#include "set_validation.h"

enum {
    RSS_DDC_CONVENTIONAL_SET_CHIP = 0x37u,
    RSS_DDC_CONVENTIONAL_SET_DATA = 0x51u,
};

RSSDDCError rss_ddc_run_dcpdpservice_set_validation(RSSDDCDCPDPServiceCorrelationResult correlation,
                                                    uint16_t current_value,
                                                    const RSSDDCConventionalSetValidationCallbacks *callbacks) {
    if (callbacks == NULL || callbacks->construct == NULL || callbacks->pre_write_delay == NULL ||
        callbacks->write_i2c == NULL || callbacks->release == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (correlation != RSS_DDC_DCPDP_SERVICE_CORRELATION_OK) return RSS_DDC_ERROR_SAFETY_GATE;

    void *service = NULL;
    RSSDDCError error = callbacks->construct(callbacks->context, &service);
    if (error != RSS_DDC_OK) return error;
    if (service == NULL) return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;

    uint8_t request[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE];
    rss_ddc_build_conventional_set_vcp(RSS_DDC_DCPDP_SERVICE_SET_VALIDATION_VCP, current_value, request);
    for (unsigned int write_index = 0; write_index < RSS_DDC_DCPDP_SERVICE_SET_WRITE_COUNT; ++write_index) {
        error = callbacks->pre_write_delay(callbacks->context);
        if (error != RSS_DDC_OK) {
            callbacks->release(callbacks->context, service);
            return error;
        }
        error = callbacks->write_i2c(callbacks->context, service, RSS_DDC_CONVENTIONAL_SET_CHIP,
                                     RSS_DDC_CONVENTIONAL_SET_DATA, request, sizeof(request));
        if (error != RSS_DDC_OK) {
            callbacks->release(callbacks->context, service);
            return error;
        }
    }
    callbacks->release(callbacks->context, service);
    return RSS_DDC_OK;
}
