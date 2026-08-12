#include "set_validation.h"

#include <stdio.h>

enum {
    RSS_DDC_CONVENTIONAL_SET_CHIP = 0x37u,
    RSS_DDC_CONVENTIONAL_SET_DATA = 0x51u,
};

static void reversible_log(const RSSDDCDCPDPServiceReversibleSetValidationCallbacks *callbacks,
                           const char *message) {
    if (callbacks != NULL && callbacks->log != NULL) callbacks->log(callbacks->context, message);
}

static void init_report(RSSDDCDCPDPServiceReversibleSetReport *report) {
    if (report != NULL) *report = (RSSDDCDCPDPServiceReversibleSetReport){0};
}

RSSDDCError rss_ddc_run_dcpdpservice_set_validation(RSSDDCDCPDPServiceCorrelationResult correlation,
                                                    uint16_t value,
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
    rss_ddc_build_conventional_set_vcp(RSS_DDC_DCPDP_SERVICE_SET_VALIDATION_VCP, value, request);
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

RSSDDCError rss_ddc_dcpdpservice_set_validation_adjacent_target(uint16_t original, uint16_t maximum,
                                                                uint16_t *target_out) {
    if (target_out == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (original > 0) {
        *target_out = (uint16_t)(original - 1u);
        return RSS_DDC_OK;
    }
    if (maximum > 0 && original < maximum) {
        *target_out = (uint16_t)(original + 1u);
        return RSS_DDC_OK;
    }
    return RSS_DDC_ERROR_ARGUMENT;
}

static RSSDDCError verify_current(uint16_t expected, const RSSDDCVCPResult *result) {
    if (result == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (result->current_value != expected) return RSS_DDC_ERROR_VERIFY_MISMATCH;
    return RSS_DDC_OK;
}

/*
 * Once a state-changing SET write sequence completes, the monitor may remain
 * off the original brightness until restoration succeeds. Restoration is
 * mandatory even when verification of the changed value fails.
 */
static RSSDDCError attempt_restoration(uint16_t original,
                                       const RSSDDCDCPDPServiceReversibleSetValidationCallbacks *callbacks,
                                       RSSDDCDCPDPServiceReversibleSetReport *report) {
    report->restore_attempted = true;
    char message[160] = {};
    snprintf(message, sizeof(message), "restore-set vcp=0x%02x value=%u", RSS_DDC_DCPDP_SERVICE_SET_VALIDATION_VCP,
             original);
    reversible_log(callbacks, message);

    RSSDDCError error = callbacks->set_vcp(callbacks->context, original);
    if (error != RSS_DDC_OK) {
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_RESTORE_WRITE_FAILED;
        report->restore_error = error;
        reversible_log(callbacks, "restore-set failed");
        return error;
    }
    report->restore_writes_completed = true;

    error = callbacks->verification_settle(callbacks->context);
    if (error != RSS_DDC_OK) {
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_RESTORE_VERIFY_FAILED;
        report->restore_error = error;
        return error;
    }

    RSSDDCVCPResult restore_get = {};
    error = callbacks->get_vcp(callbacks->context, &restore_get);
    if (error != RSS_DDC_OK) {
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_RESTORE_VERIFY_FAILED;
        report->restore_error = error;
        reversible_log(callbacks, "restore verification GET failed");
        return error;
    }
    report->restore_verify_current = restore_get.current_value;
    error = verify_current(original, &restore_get);
    if (error != RSS_DDC_OK) {
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_RESTORE_VERIFY_FAILED;
        report->restore_error = error;
        snprintf(message, sizeof(message), "restore verification mismatch expected=%u actual=%u", original,
                 restore_get.current_value);
        reversible_log(callbacks, message);
        return error;
    }
    reversible_log(callbacks, "restore verification succeeded");
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_run_dcpdpservice_reversible_set_validation(
    RSSDDCDCPDPServiceCorrelationResult correlation,
    const RSSDDCDCPDPServiceReversibleSetValidationCallbacks *callbacks,
    RSSDDCDCPDPServiceReversibleSetReport *report) {
    if (callbacks == NULL || callbacks->get_vcp == NULL || callbacks->set_vcp == NULL ||
        callbacks->verification_settle == NULL || report == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (correlation != RSS_DDC_DCPDP_SERVICE_CORRELATION_OK) return RSS_DDC_ERROR_SAFETY_GATE;

    init_report(report);
    char message[160] = {};

    RSSDDCVCPResult pre_get = {};
    RSSDDCError error = callbacks->get_vcp(callbacks->context, &pre_get);
    if (error != RSS_DDC_OK) {
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_PRE_GET_FAILED;
        return error;
    }
    report->original_value = pre_get.current_value;
    report->maximum_value = pre_get.maximum_value;
    snprintf(message, sizeof(message), "pre-get vcp=0x%02x maximum=%u current=%u",
             pre_get.vcp_code, pre_get.maximum_value, pre_get.current_value);
    reversible_log(callbacks, message);

    error = rss_ddc_dcpdpservice_set_validation_adjacent_target(pre_get.current_value, pre_get.maximum_value,
                                                                &report->target_value);
    if (error != RSS_DDC_OK) {
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_NO_ADJACENT_TARGET;
        reversible_log(callbacks, "no safe adjacent brightness target");
        return error;
    }
    snprintf(message, sizeof(message), "selected-target=%u", report->target_value);
    reversible_log(callbacks, message);

    snprintf(message, sizeof(message), "target-set vcp=0x%02x value=%u", RSS_DDC_DCPDP_SERVICE_SET_VALIDATION_VCP,
             report->target_value);
    reversible_log(callbacks, message);
    error = callbacks->set_vcp(callbacks->context, report->target_value);
    if (error != RSS_DDC_OK) {
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_TARGET_WRITE_FAILED;
        return error;
    }
    report->target_writes_completed = true;

    error = callbacks->verification_settle(callbacks->context);
    if (error != RSS_DDC_OK) {
        report->target_verify_error = error;
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_TARGET_VERIFY_FAILED;
        (void)attempt_restoration(report->original_value, callbacks, report);
        return error;
    }

    RSSDDCVCPResult target_get = {};
    error = callbacks->get_vcp(callbacks->context, &target_get);
    if (error != RSS_DDC_OK) {
        report->target_verify_error = error;
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_TARGET_VERIFY_FAILED;
        reversible_log(callbacks, "target verification GET failed");
        (void)attempt_restoration(report->original_value, callbacks, report);
        return error;
    }
    report->target_verify_current = target_get.current_value;
    error = verify_current(report->target_value, &target_get);
    if (error != RSS_DDC_OK) {
        report->target_verify_error = error;
        report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_TARGET_VERIFY_FAILED;
        snprintf(message, sizeof(message), "target verification mismatch expected=%u actual=%u", report->target_value,
                 target_get.current_value);
        reversible_log(callbacks, message);
        RSSDDCError restore_error = attempt_restoration(report->original_value, callbacks, report);
        if (restore_error != RSS_DDC_OK) return restore_error;
        return error;
    }
    reversible_log(callbacks, "target verification succeeded");

    RSSDDCError restore_error = attempt_restoration(report->original_value, callbacks, report);
    if (restore_error != RSS_DDC_OK) return restore_error;

    report->outcome = RSS_DDC_REVERSIBLE_SET_OUTCOME_OK;
    return RSS_DDC_OK;
}
