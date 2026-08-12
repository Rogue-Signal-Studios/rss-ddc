#include "verify.h"

#include <stdio.h>

RSSDDCVerifyPolicy rss_ddc_default_verify_policy(void) {
    return (RSSDDCVerifyPolicy){.settle_ms = 100, .retry_count = 3, .retry_delay_ms = 250};
}

bool rss_ddc_verify_policy_is_valid(const RSSDDCVerifyPolicy *policy) {
    return policy != NULL && policy->retry_count <= RSS_DDC_VERIFY_MAX_RETRIES &&
        policy->settle_ms <= RSS_DDC_VERIFY_MAX_DELAY_MS &&
        policy->retry_delay_ms <= RSS_DDC_VERIFY_MAX_DELAY_MS;
}

/* Only post-SET response failures that can settle naturally are retried. */
bool rss_ddc_verify_error_is_retryable(RSSDDCError error) {
    switch (error) {
        case RSS_DDC_ERROR_SERVICE_CONSTRUCTION:
        case RSS_DDC_ERROR_READ:
        case RSS_DDC_ERROR_REPLY_LENGTH:
        case RSS_DDC_ERROR_REPLY_SOURCE:
        case RSS_DDC_ERROR_REPLY_COMMAND:
        case RSS_DDC_ERROR_REPLY_STATUS:
        case RSS_DDC_ERROR_REPLY_VCP:
        case RSS_DDC_ERROR_REPLY_CHECKSUM:
        case RSS_DDC_ERROR_SYSTEM:
            return true;
        default:
            return false;
    }
}

static void verify_diagnostic(const RSSDDCDiagnostics *diagnostics, const char *message) {
    if (diagnostics != NULL && diagnostics->callback != NULL) diagnostics->callback(diagnostics->context, message);
}

RSSDDCError rss_ddc_orchestrate_set_vcp_and_verify(uint8_t vcp_code, uint16_t value,
                                                    const RSSDDCVerifyPolicy *policy,
                                                    RSSDDCVCPResult *result,
                                                    const RSSDDCVerifyOperations *operations) {
    if (vcp_code == 0 || result == NULL || operations == NULL || operations->set_vcp == NULL ||
        operations->get_vcp == NULL || operations->sleep_ms == NULL || !rss_ddc_verify_policy_is_valid(policy)) {
        return RSS_DDC_ERROR_ARGUMENT;
    }

    char message[160] = {};
    snprintf(message, sizeof(message), "verify policy settle_ms=%u retry_count=%u retry_delay_ms=%u",
             policy->settle_ms, policy->retry_count, policy->retry_delay_ms);
    verify_diagnostic(operations->diagnostics, message);

    RSSDDCError error = operations->set_vcp(operations->context, vcp_code, value);
    if (error != RSS_DDC_OK) {
        snprintf(message, sizeof(message), "verify set-result=%s; verification-not-started", rss_ddc_error_string(error));
        verify_diagnostic(operations->diagnostics, message);
        return error;
    }
    verify_diagnostic(operations->diagnostics, "verify set-result=success");

    if (policy->settle_ms != 0) operations->sleep_ms(operations->context, policy->settle_ms);
    for (uint32_t attempt = 0; attempt <= policy->retry_count; ++attempt) {
        snprintf(message, sizeof(message), "verify attempt=%u/%u", attempt + 1, policy->retry_count + 1);
        verify_diagnostic(operations->diagnostics, message);

        RSSDDCVCPResult candidate = {};
        error = operations->get_vcp(operations->context, vcp_code, &candidate);
        if (error == RSS_DDC_OK) {
            snprintf(message, sizeof(message), "verify expected=%u returned=%u", value, candidate.current_value);
            verify_diagnostic(operations->diagnostics, message);
            if (candidate.current_value == value) {
                *result = candidate;
                verify_diagnostic(operations->diagnostics, "verify result=verified");
                return RSS_DDC_OK;
            }
            error = RSS_DDC_ERROR_VERIFY_MISMATCH;
        } else {
            snprintf(message, sizeof(message), "verify get-result=%s", rss_ddc_error_string(error));
            verify_diagnostic(operations->diagnostics, message);
        }

        if (error == RSS_DDC_ERROR_VERIFY_UNAVAILABLE ||
            (!rss_ddc_verify_error_is_retryable(error) && error != RSS_DDC_ERROR_VERIFY_MISMATCH)) {
            verify_diagnostic(operations->diagnostics, "verify result=not-verified");
            return error;
        }
        if (attempt == policy->retry_count) {
            RSSDDCError final_error = error == RSS_DDC_ERROR_VERIFY_MISMATCH ?
                RSS_DDC_ERROR_VERIFY_MISMATCH : RSS_DDC_ERROR_VERIFY_RETRY_EXHAUSTED;
            snprintf(message, sizeof(message), "verify result=%s", rss_ddc_error_string(final_error));
            verify_diagnostic(operations->diagnostics, message);
            return final_error;
        }
        snprintf(message, sizeof(message), "verify retry=yes delay_ms=%u", policy->retry_delay_ms);
        verify_diagnostic(operations->diagnostics, message);
        if (policy->retry_delay_ms != 0) operations->sleep_ms(operations->context, policy->retry_delay_ms);
    }
    return RSS_DDC_ERROR_SYSTEM;
}
