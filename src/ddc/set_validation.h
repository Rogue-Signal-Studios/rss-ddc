#ifndef RSS_DDC_SET_VALIDATION_H
#define RSS_DDC_SET_VALIDATION_H

#include "correlation.h"
#include "protocol.h"

enum {
    RSS_DDC_DCPDP_SERVICE_SET_VALIDATION_VCP = 0x10u,
    RSS_DDC_DCPDP_SERVICE_SET_WRITE_COUNT = 2u,
    RSS_DDC_DCPDP_SERVICE_SET_PREWRITE_DELAY_US = 10000u,
    /*
     * Harness-only post-SET delay before a verification GET. Chosen to match the
     * 250 ms retry interval that succeeded on the documented LG/DCPDP13 path
     * after an immediate malformed post-SET reply; it is not a universal rule.
     */
    RSS_DDC_DCPDP_SERVICE_SET_VERIFY_SETTLE_MS = 250u,
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

typedef enum {
    RSS_DDC_REVERSIBLE_SET_OUTCOME_OK = 0,
    RSS_DDC_REVERSIBLE_SET_OUTCOME_PRE_GET_FAILED,
    RSS_DDC_REVERSIBLE_SET_OUTCOME_NO_ADJACENT_TARGET,
    RSS_DDC_REVERSIBLE_SET_OUTCOME_TARGET_WRITE_FAILED,
    RSS_DDC_REVERSIBLE_SET_OUTCOME_TARGET_VERIFY_FAILED,
    RSS_DDC_REVERSIBLE_SET_OUTCOME_RESTORE_WRITE_FAILED,
    RSS_DDC_REVERSIBLE_SET_OUTCOME_RESTORE_VERIFY_FAILED,
} RSSDDCDCPDPServiceReversibleSetOutcome;

typedef struct {
    RSSDDCDCPDPServiceReversibleSetOutcome outcome;
    uint16_t original_value;
    uint16_t target_value;
    uint16_t maximum_value;
    bool target_writes_completed;
    bool restore_attempted;
    bool restore_writes_completed;
    RSSDDCError target_verify_error;
    RSSDDCError restore_error;
    uint16_t target_verify_current;
    uint16_t restore_verify_current;
} RSSDDCDCPDPServiceReversibleSetReport;

typedef struct {
    void *context;
    RSSDDCError (*get_vcp)(void *context, RSSDDCVCPResult *result);
    RSSDDCError (*set_vcp)(void *context, uint16_t value);
    RSSDDCError (*verification_settle)(void *context);
    void (*log)(void *context, const char *message);
} RSSDDCDCPDPServiceReversibleSetValidationCallbacks;

RSSDDCError rss_ddc_run_dcpdpservice_set_validation(RSSDDCDCPDPServiceCorrelationResult correlation,
                                                    uint16_t value,
                                                    const RSSDDCConventionalSetValidationCallbacks *callbacks);

/**
 * Chooses one adjacent brightness step so the harness can prove a reversible
 * state change without jumping far across the range. Prefer original - 1 when
 * possible; otherwise original + 1 when below maximum.
 */
RSSDDCError rss_ddc_dcpdpservice_set_validation_adjacent_target(uint16_t original, uint16_t maximum,
                                                                uint16_t *target_out);

RSSDDCError rss_ddc_run_dcpdpservice_reversible_set_validation(
    RSSDDCDCPDPServiceCorrelationResult correlation,
    const RSSDDCDCPDPServiceReversibleSetValidationCallbacks *callbacks,
    RSSDDCDCPDPServiceReversibleSetReport *report);

#endif
