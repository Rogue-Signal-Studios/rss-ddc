#include "rss_ddc.h"
#include "input_switch.h"
#include "macos_internal.h"

RSSDDCError rss_ddc_set_input(uint32_t list_index, RSSDDCInputSwitchMethod method, uint16_t value) {
    return rss_ddc_set_input_with_diagnostics(list_index, method, value, NULL);
}

RSSDDCError rss_ddc_set_input_with_diagnostics(uint32_t list_index, RSSDDCInputSwitchMethod method,
                                                uint16_t value, const RSSDDCDiagnostics *diagnostics) {
    if (method == RSS_DDC_INPUT_SWITCH_STANDARD) {
        /* Preserve the ordinary VCP 0x60 dispatch without a new transport wrapper. */
        return rss_ddc_set_vcp_with_diagnostics(list_index, 0x60, value, diagnostics);
    }
    if (method != RSS_DDC_INPUT_SWITCH_LG_ALT) return RSS_DDC_ERROR_ARGUMENT;
    /* Reject unsupported alternate values before resolving a display or creating IOAV state. */
    if (!rss_ddc_lg_alt_input_value_is_supported(value)) return RSS_DDC_ERROR_ARGUMENT;
    return rss_macos_set_lg_alt_input_snapshot(list_index, value, diagnostics);
}
