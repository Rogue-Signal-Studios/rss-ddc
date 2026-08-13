#include "rss_ddc.h"
#include "macos_internal.h"

RSSDDCError rss_ddc_set_input(uint32_t list_index, RSSDDCInputSwitchMethod method, uint16_t value) {
    return rss_ddc_set_input_with_diagnostics(list_index, method, value, NULL);
}

RSSDDCError rss_ddc_set_input_with_diagnostics(uint32_t list_index, RSSDDCInputSwitchMethod method,
                                                uint16_t value, const RSSDDCDiagnostics *diagnostics) {
    if (method == RSS_DDC_INPUT_SWITCH_STANDARD) {
        /* Keep the ordinary input path byte-for-byte under the existing SetVCP dispatch. */
        return rss_ddc_set_vcp_with_diagnostics(list_index, 0x60, value, diagnostics);
    }
    if (method != RSS_DDC_INPUT_SWITCH_LG_ALT || value > UINT8_MAX) return RSS_DDC_ERROR_ARGUMENT;

    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error == RSS_DDC_OK) {
        error = rss_macos_provider_set_lg_alt_input(&binding, value, diagnostics);
    } else {
        rss_macos_diagnostic(diagnostics, rss_macos_correlation_failure_string(binding.correlation_failure));
        const char *detail = rss_macos_correlation_detail_string(&binding);
        if (detail != NULL) rss_macos_diagnostic(diagnostics, detail);
    }
    rss_macos_release_binding(&binding);
    return error;
}
