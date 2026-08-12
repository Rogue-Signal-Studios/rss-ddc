#ifndef RSS_DDC_VERIFY_H
#define RSS_DDC_VERIFY_H

#include "rss_ddc.h"

/*
 * The portable orchestrator has injected operations so tests can model timing,
 * transient replies, and topology changes without opening a macOS user client.
 * The platform adapter is responsible for proving the target identity before it
 * invokes `get_vcp`.
 */
typedef RSSDDCError (*RSSDDCVerifySetVCP)(void *context, uint8_t vcp_code, uint16_t value);
typedef RSSDDCError (*RSSDDCVerifyGetVCP)(void *context, uint8_t vcp_code, RSSDDCVCPResult *result);
typedef void (*RSSDDCVerifySleep)(void *context, uint32_t milliseconds);

typedef struct {
    void *context;
    RSSDDCVerifySetVCP set_vcp;
    RSSDDCVerifyGetVCP get_vcp;
    RSSDDCVerifySleep sleep_ms;
    const RSSDDCDiagnostics *diagnostics;
} RSSDDCVerifyOperations;

bool rss_ddc_verify_policy_is_valid(const RSSDDCVerifyPolicy *policy);
bool rss_ddc_verify_error_is_retryable(RSSDDCError error);
RSSDDCError rss_ddc_orchestrate_set_vcp_and_verify(uint8_t vcp_code, uint16_t value,
                                                    const RSSDDCVerifyPolicy *policy,
                                                    RSSDDCVCPResult *result,
                                                    const RSSDDCVerifyOperations *operations);

#endif
