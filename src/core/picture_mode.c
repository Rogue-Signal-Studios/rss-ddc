#include "picture_mode.h"

#include <string.h>

#include "macos_internal.h"

enum {
    RSS_DDC_LG_HDR_QHD_PICTURE_MODE_VCP = 0x15,
};

bool rss_ddc_lg_hdr_qhd_picture_mode_profile_matches(const RSSDDCDisplay *display) {
    return display != NULL && display->external && display->provider == RSS_DDC_PROVIDER_DCPDP13 &&
        strcmp(display->product_name, "LG HDR QHD") == 0 && strcmp(display->transport, "DCPEXT0") == 0;
}

uint32_t rss_ddc_picture_mode_profile_capabilities(const RSSDDCDisplay *display) {
    return rss_ddc_lg_hdr_qhd_picture_mode_profile_matches(display) ? RSS_DDC_CAP_PICTURE_MODE : RSS_DDC_CAP_NONE;
}

RSSDDCError rss_ddc_validate_lg_hdr_qhd_picture_mode_target(const RSSDDCDisplay *display, bool dp_safety_gate) {
    if (display == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (display->provider == RSS_DDC_PROVIDER_UNKNOWN) return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    if (!rss_ddc_lg_hdr_qhd_picture_mode_profile_matches(display)) return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    return dp_safety_gate ? RSS_DDC_OK : RSS_DDC_ERROR_SAFETY_GATE;
}

const char *rss_ddc_picture_mode_name(RSSDDCPictureMode mode) {
    switch (mode) {
        case RSS_DDC_PICTURE_MODE_VIVID: return "Vivid";
        case RSS_DDC_PICTURE_MODE_READER: return "Reader";
        case RSS_DDC_PICTURE_MODE_UNKNOWN: return "Unknown";
    }
    return "Unknown";
}

RSSDDCError rss_ddc_lg_hdr_qhd_picture_mode_raw_value(RSSDDCPictureMode mode, uint16_t *raw_value) {
    if (raw_value == NULL) return RSS_DDC_ERROR_ARGUMENT;
    switch (mode) {
        case RSS_DDC_PICTURE_MODE_VIVID: *raw_value = 0x31; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_READER: *raw_value = 0x01; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_UNKNOWN: return RSS_DDC_ERROR_ARGUMENT;
    }
    return RSS_DDC_ERROR_ARGUMENT;
}

RSSDDCError rss_ddc_set_picture_mode(uint32_t list_index, RSSDDCPictureMode mode) {
    return rss_ddc_set_picture_mode_with_diagnostics(list_index, mode, NULL);
}

RSSDDCError rss_ddc_set_picture_mode_with_diagnostics(uint32_t list_index, RSSDDCPictureMode mode,
                                                       const RSSDDCDiagnostics *diagnostics) {
    uint16_t raw_value = 0;
    RSSDDCError error = rss_ddc_lg_hdr_qhd_picture_mode_raw_value(mode, &raw_value);
    if (error != RSS_DDC_OK) return error;
    return rss_macos_set_lg_picture_mode_snapshot(list_index, RSS_DDC_LG_HDR_QHD_PICTURE_MODE_VCP,
                                                   raw_value, rss_ddc_picture_mode_name(mode), diagnostics);
}
