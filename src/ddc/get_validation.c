#include "get_validation.h"

#include <string.h>

enum {
    RSS_DDC_CONVENTIONAL_GET_CHIP = 0x37u,
    RSS_DDC_CONVENTIONAL_GET_DATA = 0x51u,
};

RSSDDCError rss_ddc_run_dcpdpservice_get_validation(RSSDDCDCPDPServiceCorrelationResult correlation,
                                                    const RSSDDCConventionalGetValidationCallbacks *callbacks,
                                                    RSSDDCVCPResult *result) {
    if (callbacks == NULL || callbacks->construct == NULL || callbacks->write_i2c == NULL ||
        callbacks->delay == NULL || callbacks->read_i2c == NULL || callbacks->release == NULL ||
        result == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (correlation != RSS_DDC_DCPDP_SERVICE_CORRELATION_OK) return RSS_DDC_ERROR_SAFETY_GATE;

    void *service = NULL;
    RSSDDCError error = callbacks->construct(callbacks->context, &service);
    if (error != RSS_DDC_OK) return error;
    if (service == NULL) return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;

    uint8_t request[RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE];
    uint8_t reply[RSS_DDC_GET_VCP_REPLY_SIZE];
    rss_ddc_build_conventional_get_vcp(RSS_DDC_DCPDP_SERVICE_GET_VALIDATION_VCP, request);
    error = callbacks->write_i2c(callbacks->context, service, RSS_DDC_CONVENTIONAL_GET_CHIP,
                                   RSS_DDC_CONVENTIONAL_GET_DATA, request, sizeof(request));
    if (error != RSS_DDC_OK) {
        callbacks->release(callbacks->context, service);
        return error;
    }

    error = callbacks->delay(callbacks->context);
    if (error != RSS_DDC_OK) {
        callbacks->release(callbacks->context, service);
        return error;
    }

    error = callbacks->read_i2c(callbacks->context, service, RSS_DDC_CONVENTIONAL_GET_CHIP,
                                RSS_DDC_CONVENTIONAL_GET_DATA, reply, sizeof(reply));
    callbacks->release(callbacks->context, service);
    if (error != RSS_DDC_OK) return error;

    return rss_ddc_parse_get_vcp_reply(reply, sizeof(reply), RSS_DDC_DCPDP_SERVICE_GET_VALIDATION_VCP, result);
}
