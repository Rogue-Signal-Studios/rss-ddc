#include "rss_ddc.h"
#include "macos_internal.h"

RSSDDCError rss_ddc_list_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count) {
    return rss_macos_discover_displays(displays, capacity, count);
}

RSSDDCError rss_ddc_get_display(uint32_t list_index, RSSDDCDisplay *display) {
    if (display == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error == RSS_DDC_OK) *display = binding.display;
    rss_macos_release_binding(&binding);
    return error;
}

RSSDDCError rss_ddc_get_vcp(uint32_t list_index, uint8_t vcp_code, RSSDDCVCPResult *result) {
    if (result == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error == RSS_DDC_OK) error = rss_macos_provider_get_vcp(&binding, vcp_code, result);
    rss_macos_release_binding(&binding);
    return error;
}

RSSDDCError rss_ddc_set_vcp(uint32_t list_index, uint8_t vcp_code, uint16_t value) {
    (void)list_index;
    (void)vcp_code;
    (void)value;
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
