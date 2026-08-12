#ifndef RSS_DDC_MACOS_INTERNAL_H
#define RSS_DDC_MACOS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <IOKit/IOKitLib.h>

#include "rss_ddc.h"

/**
 * The exact fail-closed predicate that rejected a partial registry binding.
 * These values are macOS-private diagnostics, not stable public error codes.
 */
typedef enum {
    RSS_MACOS_CORRELATION_NONE = 0,
    RSS_MACOS_CORRELATION_NO_SELECTED_DISPLAY,
    RSS_MACOS_CORRELATION_NO_DISPLAY_REGISTRY_NODE,
    RSS_MACOS_CORRELATION_NO_SERVICE_PROXY,
    RSS_MACOS_CORRELATION_AMBIGUOUS_SERVICE_PROXY,
    RSS_MACOS_CORRELATION_MISSING_SERVICE_PROVIDER,
    RSS_MACOS_CORRELATION_NOT_EXTERNAL,
    RSS_MACOS_CORRELATION_NO_ACTIVE_BRANCH,
    RSS_MACOS_CORRELATION_MISSING_BRANCH_DEVICE_ID,
    RSS_MACOS_CORRELATION_AMBIGUOUS_ACTIVE_BRANCH,
    RSS_MACOS_CORRELATION_NO_DCPDP_DEVICE_PROXY,
    RSS_MACOS_CORRELATION_AMBIGUOUS_DCPDP_DEVICE_PROXY,
    RSS_MACOS_CORRELATION_UNEXPECTED_EPIC_PARENT,
    RSS_MACOS_CORRELATION_PROVIDER_MISMATCH,
    RSS_MACOS_CORRELATION_ROLE_MISMATCH,
    RSS_MACOS_CORRELATION_UNIT_MISMATCH,
    RSS_MACOS_CORRELATION_UI_UNSUPPORTED,
} RSSMacOSCorrelationFailure;

/**
 * Retained, macOS-private display binding. `service_proxy` follows IOKit Create
 * ownership and must be released exactly once with rss_macos_release_binding.
 */
typedef struct {
    RSSDDCDisplay display;
    io_service_t service_proxy;
    bool dp_safety_gate;
    bool ps190_safety_gate;
    RSSMacOSCorrelationFailure correlation_failure;
} RSSMacOSBinding;

/** Enumerates CoreGraphics displays and reads registry metadata without opening a user client. */
RSSDDCError rss_macos_discover_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count);
/**
 * Resolves one display to a retained DCP service binding and enforces provider
 * correlation before any provider backend is allowed to construct IOAVService.
 */
RSSDDCError rss_macos_resolve_binding(uint32_t list_index, RSSMacOSBinding *binding);
/** Releases the retained service proxy and zeroes the binding; safe on a partial binding. */
void rss_macos_release_binding(RSSMacOSBinding *binding);
/** Returns a static precise diagnostic for a failed partial binding. */
const char *rss_macos_correlation_failure_string(RSSMacOSCorrelationFailure failure);
void rss_macos_diagnostic(const RSSDDCDiagnostics *diagnostics, const char *message);
RSSDDCError rss_macos_ps190_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                     const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_ps190_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                     const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_dp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                  const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_mcdp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                    const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_provider_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                        const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_provider_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                        const RSSDDCDiagnostics *diagnostics);

#endif
