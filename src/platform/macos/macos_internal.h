#ifndef RSS_DDC_MACOS_INTERNAL_H
#define RSS_DDC_MACOS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <IOKit/IOKitLib.h>

#include "rss_ddc.h"

#include "correlation.h"

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
 * Private evidence retained only for one Set-and-Verify sequence. A display
 * list index can reorder, so verification needs a stronger match before it may
 * address the current entry at that index. The UUID is derived from the
 * CoreGraphics display through ColorSync; the remaining fields corroborate the
 * selected provider/transport binding.
 */
typedef struct {
    bool valid;
    uint32_t cg_display_id;
    RSSDDCProvider provider;
    char display_uuid[RSS_DDC_TEXT_MAX];
    char product_name[RSS_DDC_TEXT_MAX];
    char branch_device_id[RSS_DDC_TEXT_MAX];
    char transport[RSS_DDC_TEXT_MAX];
} RSSMacOSDisplayIdentity;

/**
 * Retained, macOS-private display binding. `service_proxy` follows IOKit Create
 * ownership. When PS190 DPCD safety correlation succeeds, `dcpdp_device_proxy`
 * is the unique branch-matched native-DP proxy. Both are released exactly once
 * with rss_macos_release_binding.
 */
typedef struct {
    RSSDDCDisplay display;
    RSSMacOSDisplayIdentity identity;
    io_service_t service_proxy;
    io_service_t dcpdp_device_proxy;
    bool dp_safety_gate;
    bool ps190_safety_gate;
    RSSMacOSCorrelationFailure correlation_failure;
    char correlation_detail[RSS_DDC_TEXT_MAX * 2];
} RSSMacOSBinding;

/** Enumerates CoreGraphics displays and reads registry metadata without opening a user client. */
RSSDDCError rss_macos_discover_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count);
/**
 * Resolves one display to a retained DCP service binding and enforces provider
 * correlation before any provider backend is allowed to construct IOAVService.
 */
RSSDDCError rss_macos_resolve_binding(uint32_t list_index, RSSMacOSBinding *binding);
/** Captures optional identity evidence for Set-and-Verify without affecting plain GET/SET resolution. */
bool rss_macos_capture_binding_identity(RSSMacOSBinding *binding);
/** True only when a freshly correlated binding still proves the original display identity. */
bool rss_macos_binding_matches_identity(const RSSMacOSBinding *binding,
                                        const RSSMacOSDisplayIdentity *identity);
/** Releases the retained service proxy and zeroes the binding; safe on a partial binding. */
void rss_macos_release_binding(RSSMacOSBinding *binding);
/** Returns a static precise diagnostic for a failed partial binding. */
const char *rss_macos_correlation_failure_string(RSSMacOSCorrelationFailure failure);
/** Returns an optional pointer-free detail string for a partial binding. */
const char *rss_macos_correlation_detail_string(const RSSMacOSBinding *binding);
void rss_macos_diagnostic(const RSSDDCDiagnostics *diagnostics, const char *message);
RSSDDCError rss_macos_ps190_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                     const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_ps190_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                     const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_ps190_read_edid(RSSMacOSBinding *binding, RSSDDCEDID *edid,
                                      const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_ps190_read_dpcd(RSSMacOSBinding *binding, uint32_t address, uint8_t *buffer,
                                      size_t length, const RSSDDCDiagnostics *diagnostics);
/** Reads DPCD through the one same-role scoped DCPDPDeviceProxy correlation. */
RSSDDCError rss_macos_dp_read_dpcd(RSSMacOSBinding *binding, uint32_t address, uint8_t *buffer,
                                   size_t length, const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_dp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                  const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_dcpdpservice_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                            const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_dp_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                 const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_mcdp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                    const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_provider_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                        const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_provider_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                        const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_provider_read_edid(RSSMacOSBinding *binding, RSSDDCEDID *edid,
                                         const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_provider_read_dpcd(RSSMacOSBinding *binding, uint32_t address, uint8_t *buffer,
                                         size_t length, const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_provider_get_mccs_capabilities(RSSMacOSBinding *binding,
                                                     RSSDDCMCCSCapabilities *capabilities,
                                                     const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_dp_get_mccs_capabilities(RSSMacOSBinding *binding,
                                               RSSDDCMCCSCapabilities *capabilities,
                                               const RSSDDCDiagnostics *diagnostics);
/** Registry-only conventional-DP candidate reporting; it never creates IODP/IOAV objects. */
RSSDDCError rss_macos_probe_dpcd_path(uint32_t list_index, const RSSDDCDiagnostics *diagnostics);
/**
 * Developer-only DCPDP13 experiment: one MCCS F3 offset-zero write and one
 * bounded E3 read. It is not a public runtime capabilities API.
 */
RSSDDCError rss_macos_probe_mccs_capabilities(uint32_t list_index, const RSSDDCDiagnostics *diagnostics);
/** Hardware-derived follow-up: repeat only LG's offset-zero F3 with a 16-byte IOAV read window. */
RSSDDCError rss_macos_probe_mccs_capabilities_exact_first_frame(uint32_t list_index,
                                                                 const RSSDDCDiagnostics *diagnostics);
/** Hardware-derived follow-up: one LG offset-0x000a F3 request and one bounded read. */
RSSDDCError rss_macos_probe_mccs_capabilities_next_fragment(uint32_t list_index,
                                                             const RSSDDCDiagnostics *diagnostics);
/** Bounded LG-only developer harness; it is not a public runtime capabilities API. */
RSSDDCError rss_macos_probe_mccs_capabilities_full(uint32_t list_index, const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_dcpdp13_probe_mccs_capabilities(RSSMacOSBinding *binding,
                                                       const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_dcpdp13_probe_mccs_capabilities_exact_first_frame(
    RSSMacOSBinding *binding, const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_dcpdp13_probe_mccs_capabilities_next_fragment(
    RSSMacOSBinding *binding, const RSSDDCDiagnostics *diagnostics);
RSSDDCError rss_macos_dcpdp13_probe_mccs_capabilities_full(
    RSSMacOSBinding *binding, const RSSDDCDiagnostics *diagnostics);

#endif
