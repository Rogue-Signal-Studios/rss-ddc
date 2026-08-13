#include "picture_mode.h"

#include <stdio.h>
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

const char *rss_ddc_picture_mode_name(RSSDDCPictureMode mode) {
    switch (mode) {
        case RSS_DDC_PICTURE_MODE_CUSTOM: return "Custom";
        case RSS_DDC_PICTURE_MODE_VIVID: return "Vivid";
        case RSS_DDC_PICTURE_MODE_HDR_EFFECT: return "HDR Effect";
        case RSS_DDC_PICTURE_MODE_CINEMA: return "Cinema";
        case RSS_DDC_PICTURE_MODE_FPS: return "FPS";
        case RSS_DDC_PICTURE_MODE_RTS: return "RTS";
        case RSS_DDC_PICTURE_MODE_COLOR_WEAKNESS: return "Color Weakness";
        case RSS_DDC_PICTURE_MODE_READER: return "Reader";
        case RSS_DDC_PICTURE_MODE_UNKNOWN: return "Unknown";
    }
    return "Unknown";
}

RSSDDCError rss_ddc_lg_hdr_qhd_picture_mode_raw_value(RSSDDCPictureMode mode, uint16_t *raw_value) {
    if (raw_value == NULL) return RSS_DDC_ERROR_ARGUMENT;
    switch (mode) {
        case RSS_DDC_PICTURE_MODE_CUSTOM: *raw_value = 0x0b; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_VIVID: *raw_value = 0x31; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_HDR_EFFECT: *raw_value = 0x27; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_CINEMA: *raw_value = 0x30; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_FPS: *raw_value = 0x1e; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_RTS: *raw_value = 0x1f; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_COLOR_WEAKNESS: *raw_value = 0x06; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_READER: *raw_value = 0x01; return RSS_DDC_OK;
        case RSS_DDC_PICTURE_MODE_UNKNOWN: return RSS_DDC_ERROR_ARGUMENT;
    }
    return RSS_DDC_ERROR_ARGUMENT;
}

RSSDDCPictureMode rss_ddc_lg_hdr_qhd_picture_mode_from_raw(uint16_t raw_value) {
    switch (raw_value) {
        case 0x0b: return RSS_DDC_PICTURE_MODE_CUSTOM;
        case 0x31: return RSS_DDC_PICTURE_MODE_VIVID;
        case 0x27: return RSS_DDC_PICTURE_MODE_HDR_EFFECT;
        case 0x30: return RSS_DDC_PICTURE_MODE_CINEMA;
        case 0x1e: return RSS_DDC_PICTURE_MODE_FPS;
        case 0x1f: return RSS_DDC_PICTURE_MODE_RTS;
        case 0x06: return RSS_DDC_PICTURE_MODE_COLOR_WEAKNESS;
        case 0x01: return RSS_DDC_PICTURE_MODE_READER;
        default: return RSS_DDC_PICTURE_MODE_UNKNOWN;
    }
}

static RSSDDCError picture_mode_binding(uint32_t list_index, RSSMacOSBinding *binding,
                                         const RSSDDCDiagnostics *diagnostics) {
    RSSDDCError error = rss_macos_resolve_binding(list_index, binding);
    if (error != RSS_DDC_OK) return error;
    if (!rss_ddc_lg_hdr_qhd_picture_mode_profile_matches(&binding->display)) {
        rss_macos_diagnostic(diagnostics, "operation=PictureMode status=unsupported; validated LG HDR QHD/DCPEXT0 profile required");
        return binding->display.provider == RSS_DDC_PROVIDER_UNKNOWN ?
            RSS_DDC_ERROR_UNSUPPORTED_PROVIDER : RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_get_picture_mode(uint32_t list_index, RSSDDCPictureMode *mode) {
    return rss_ddc_get_picture_mode_with_diagnostics(list_index, mode, NULL);
}

RSSDDCError rss_ddc_get_picture_mode_with_diagnostics(uint32_t list_index, RSSDDCPictureMode *mode,
                                                       const RSSDDCDiagnostics *diagnostics) {
    if (mode == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *mode = RSS_DDC_PICTURE_MODE_UNKNOWN;
    RSSMacOSBinding binding = {0};
    RSSDDCError error = picture_mode_binding(list_index, &binding, diagnostics);
    if (error == RSS_DDC_OK) {
        RSSDDCVCPResult result = {};
        error = rss_macos_provider_get_vcp(&binding, RSS_DDC_LG_HDR_QHD_PICTURE_MODE_VCP, &result, diagnostics);
        if (error == RSS_DDC_OK) *mode = rss_ddc_lg_hdr_qhd_picture_mode_from_raw(result.current_value);
    }
    rss_macos_release_binding(&binding);
    return error;
}

RSSDDCError rss_ddc_set_picture_mode(uint32_t list_index, RSSDDCPictureMode mode) {
    return rss_ddc_set_picture_mode_with_diagnostics(list_index, mode, NULL);
}

RSSDDCError rss_ddc_set_picture_mode_with_diagnostics(uint32_t list_index, RSSDDCPictureMode mode,
                                                       const RSSDDCDiagnostics *diagnostics) {
    uint16_t raw_value = 0;
    RSSDDCError error = rss_ddc_lg_hdr_qhd_picture_mode_raw_value(mode, &raw_value);
    if (error != RSS_DDC_OK) return error;
    RSSMacOSBinding binding = {0};
    error = picture_mode_binding(list_index, &binding, diagnostics);
    if (error == RSS_DDC_OK) {
        char message[128] = {};
        snprintf(message, sizeof(message), "operation=PictureMode mode=%s", rss_ddc_picture_mode_name(mode));
        rss_macos_diagnostic(diagnostics, message);
        error = rss_macos_provider_set_vcp(&binding, RSS_DDC_LG_HDR_QHD_PICTURE_MODE_VCP, raw_value, diagnostics);
    }
    rss_macos_release_binding(&binding);
    return error;
}
