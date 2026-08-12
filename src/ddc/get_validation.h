#ifndef RSS_DDC_GET_VALIDATION_H
#define RSS_DDC_GET_VALIDATION_H

#include "correlation.h"
#include "protocol.h"

/**
 * Internal testable lifecycle for a validation-only conventional Service GET.
 * Callbacks own the private IOAV object; the runner performs one write, one
 * delay, one read, and one strict parse with no retry or fallback.
 */
typedef struct {
    void *context;
    RSSDDCError (*construct)(void *context, void **service);
    RSSDDCError (*write_i2c)(void *context, void *service, uint32_t chip, uint32_t data,
                             const uint8_t *payload, size_t payload_length);
    RSSDDCError (*delay)(void *context);
    RSSDDCError (*read_i2c)(void *context, void *service, uint32_t chip, uint32_t data,
                            uint8_t *reply, size_t reply_length);
    void (*release)(void *context, void *service);
} RSSDDCConventionalGetValidationCallbacks;

RSSDDCError rss_ddc_run_dcpdpservice_get_validation(RSSDDCDCPDPServiceCorrelationResult correlation,
                                                    const RSSDDCConventionalGetValidationCallbacks *callbacks,
                                                    RSSDDCVCPResult *result);

#endif
