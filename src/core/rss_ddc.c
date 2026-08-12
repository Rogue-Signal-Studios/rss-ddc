#include "rss_ddc.h"
#include "macos_internal.h"

#include <stdio.h>

/** Bridges the portable callback to platform code without exposing platform handles publicly. */
void rss_macos_diagnostic(const RSSDDCDiagnostics *diagnostics, const char *message) {
    if (diagnostics != NULL && diagnostics->callback != NULL) diagnostics->callback(diagnostics->context, message);
}

/** Delegates discovery to macOS while retaining the public API's value-only contract. */
RSSDDCError rss_ddc_list_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count) {
    return rss_macos_discover_displays(displays, capacity, count);
}

/** Resolves and then releases the private binding; callers receive only its public snapshot. */
RSSDDCError rss_ddc_get_display(uint32_t list_index, RSSDDCDisplay *display) {
    if (display == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error == RSS_DDC_OK) *display = binding.display;
    rss_macos_release_binding(&binding);
    return error;
}

/** Keeps the concise API free of diagnostics while sharing the same validation path. */
RSSDDCError rss_ddc_get_vcp(uint32_t list_index, uint8_t vcp_code, RSSDDCVCPResult *result) {
    return rss_ddc_get_vcp_with_diagnostics(list_index, vcp_code, result, NULL);
}

RSSDDCError rss_ddc_get_vcp_with_diagnostics(uint32_t list_index, uint8_t vcp_code, RSSDDCVCPResult *result,
                                              const RSSDDCDiagnostics *diagnostics) {
    if (result == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error == RSS_DDC_OK) {
        /* Print stable operator evidence, never transient registry IDs or pointers. */
        char message[512] = {};
        snprintf(message, sizeof(message),
                 "display=%u product=%s manufacturer=%s serial=%s provider=%s transport=%s branch=%s",
                 binding.display.list_index, binding.display.product_name,
                 binding.display.manufacturer[0] ? binding.display.manufacturer : "<unavailable>",
                 binding.display.serial[0] ? binding.display.serial : "<unavailable>",
                 rss_ddc_provider_string(binding.display.provider), binding.display.transport,
                 binding.display.branch_device_id[0] ? binding.display.branch_device_id : "<unavailable>");
        rss_macos_diagnostic(diagnostics, message);
        error = rss_macos_provider_get_vcp(&binding, vcp_code, result, diagnostics);
    } else {
        rss_macos_diagnostic(diagnostics, rss_ddc_error_string(error));
    }
    rss_macos_release_binding(&binding);
    return error;
}

/** SET deliberately fails closed until a provider has separately validated write semantics. */
RSSDDCError rss_ddc_set_vcp(uint32_t list_index, uint8_t vcp_code, uint16_t value) {
    (void)list_index;
    (void)vcp_code;
    (void)value;
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
