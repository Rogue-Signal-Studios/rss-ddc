#ifndef RSS_DDC_SET_VALIDATION_H
#define RSS_DDC_SET_VALIDATION_H

#include "correlation.h"
#include "protocol.h"

enum {
    RSS_DDC_DCPDP_SERVICE_SET_VALIDATION_VCP = 0x10u,
    RSS_DDC_DCPDP_SERVICE_SET_WRITE_COUNT = 2u,
    RSS_DDC_DCPDP_SERVICE_SET_PREWRITE_DELAY_US = 10000u,
};

/**
 * Internal testable lifecycle for a validation-only conventional Service SET.
 * Callbacks own the private IOAV object; the runner performs exactly two
 * identical write-only transactions with one pre-write delay before each write.
 */
typedef struct {
    void *context;
    RSSDDCError (*construct)(void *context, void **service);
    RSSDDCError (*pre_write_delay)(void *context);
    RSSDDCError (*write_i2c)(void *context, void *service, uint32_t chip, uint32_t data,
                             const uint8_t *payload, size_t payload_length);
    void (*release)(void *context, void *service);
} RSSDDCConventionalSetValidationCallbacks;

RSSDDCError rss_ddc_run_dcpdpservice_set_validation(RSSDDCDCPDPServiceCorrelationResult correlation,
                                                    uint16_t current_value,
                                                    const RSSDDCConventionalSetValidationCallbacks *callbacks);

#endif
