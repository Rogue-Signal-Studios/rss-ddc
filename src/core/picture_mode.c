#include "picture_mode.h"

#include <stdio.h>

#include "macos_internal.h"
#include "profile_store.h"

bool rss_ddc_lg_hdr_qhd_picture_mode_profile_matches(const RSSDDCDisplay *display) {
    RSSDDCProfileIdentity identity = {};
    RSSDDCEffectiveProfile effective = {};
    rss_ddc_profile_identity_from_display(display, &identity);
    return rss_ddc_profile_store_resolve_builtin(&identity, &effective) == RSS_DDC_OK;
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
    RSSDDCProfileIdentity identity = {.external = true, .provider = RSS_DDC_PROVIDER_DCPDP13};
    snprintf(identity.product_name, sizeof(identity.product_name), "LG HDR QHD");
    snprintf(identity.transport, sizeof(identity.transport), "DCPEXT0");
    RSSDDCEffectiveProfile effective = {};
    RSSDDCError error = rss_ddc_profile_store_resolve_builtin(&identity, &effective);
    return error == RSS_DDC_OK ? rss_ddc_profile_picture_mode_raw(&effective, mode, raw_value) : error;
}

RSSDDCPictureMode rss_ddc_lg_hdr_qhd_picture_mode_from_raw(uint16_t raw_value) {
    RSSDDCProfileIdentity identity = {.external = true, .provider = RSS_DDC_PROVIDER_DCPDP13};
    snprintf(identity.product_name, sizeof(identity.product_name), "LG HDR QHD");
    snprintf(identity.transport, sizeof(identity.transport), "DCPEXT0");
    RSSDDCEffectiveProfile effective = {};
    return rss_ddc_profile_store_resolve_builtin(&identity, &effective) == RSS_DDC_OK ?
        rss_ddc_profile_picture_mode_from_raw(&effective, raw_value) : RSS_DDC_PICTURE_MODE_UNKNOWN;
}

static RSSDDCError picture_mode_binding(uint32_t list_index, const RSSDDCProfileStore *store, RSSMacOSBinding *binding,
                                         RSSDDCEffectiveProfile *effective, const RSSDDCDiagnostics *diagnostics) {
    RSSDDCError error = rss_macos_resolve_binding(list_index, binding);
    if (error != RSS_DDC_OK) return error;
    RSSDDCProfileIdentity identity = {};
    rss_ddc_profile_identity_from_display(&binding->display, &identity);
    error = store == NULL ? rss_ddc_profile_store_resolve_builtin(&identity, effective) :
        rss_ddc_profile_store_resolve(store, &identity, effective);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, "operation=PictureMode status=unsupported; no validated matching monitor profile");
        return binding->display.provider == RSS_DDC_PROVIDER_UNKNOWN ? RSS_DDC_ERROR_UNSUPPORTED_PROVIDER : RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    return RSS_DDC_OK;
}

static const RSSDDCProfileControl *picture_control(const RSSDDCEffectiveProfile *effective) {
    for (size_t index = 0; index < effective->control_count; ++index)
        if (effective->controls[index].id == RSS_DDC_PROFILE_CONTROL_PICTURE_MODE) return &effective->controls[index];
    return NULL;
}

RSSDDCError rss_ddc_get_picture_mode(uint32_t list_index, RSSDDCPictureMode *mode) { return rss_ddc_get_picture_mode_with_diagnostics(list_index, mode, NULL); }
RSSDDCError rss_ddc_get_picture_mode_with_profile_store(uint32_t list_index, const RSSDDCProfileStore *store, RSSDDCPictureMode *mode) {
    if (mode == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *mode = RSS_DDC_PICTURE_MODE_UNKNOWN;
    RSSMacOSBinding binding = {}; RSSDDCEffectiveProfile effective = {};
    RSSDDCError error = picture_mode_binding(list_index, store, &binding, &effective, NULL);
    const RSSDDCProfileControl *control = error == RSS_DDC_OK ? picture_control(&effective) : NULL;
    if (control == NULL && error == RSS_DDC_OK) error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    if (error == RSS_DDC_OK && (!control->readable || control->method != RSS_DDC_PROFILE_METHOD_VCP)) error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    if (error == RSS_DDC_OK) { RSSDDCVCPResult result = {}; error = rss_macos_provider_get_vcp(&binding, (uint8_t)control->address, &result, NULL); if (error == RSS_DDC_OK) *mode = rss_ddc_profile_picture_mode_from_raw(&effective, result.current_value); }
    rss_macos_release_binding(&binding); return error;
}
RSSDDCError rss_ddc_get_picture_mode_with_diagnostics(uint32_t list_index, RSSDDCPictureMode *mode, const RSSDDCDiagnostics *diagnostics) {
    if (mode == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *mode = RSS_DDC_PICTURE_MODE_UNKNOWN;
    RSSMacOSBinding binding = {}; RSSDDCEffectiveProfile effective = {};
    RSSDDCError error = picture_mode_binding(list_index, NULL, &binding, &effective, diagnostics);
    const RSSDDCProfileControl *control = error == RSS_DDC_OK ? picture_control(&effective) : NULL;
    if (control == NULL && error == RSS_DDC_OK) error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    if (error == RSS_DDC_OK && (!control->readable || control->method != RSS_DDC_PROFILE_METHOD_VCP)) error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    if (error == RSS_DDC_OK) { RSSDDCVCPResult result = {}; error = rss_macos_provider_get_vcp(&binding, (uint8_t)control->address, &result, diagnostics); if (error == RSS_DDC_OK) *mode = rss_ddc_profile_picture_mode_from_raw(&effective, result.current_value); }
    rss_macos_release_binding(&binding); return error;
}

RSSDDCError rss_ddc_set_picture_mode(uint32_t list_index, RSSDDCPictureMode mode) { return rss_ddc_set_picture_mode_with_diagnostics(list_index, mode, NULL); }
RSSDDCError rss_ddc_set_picture_mode_with_profile_store(uint32_t list_index, const RSSDDCProfileStore *store, RSSDDCPictureMode mode) {
    RSSMacOSBinding binding = {}; RSSDDCEffectiveProfile effective = {}; uint16_t raw = 0;
    RSSDDCError error = picture_mode_binding(list_index, store, &binding, &effective, NULL);
    const RSSDDCProfileControl *control = error == RSS_DDC_OK ? picture_control(&effective) : NULL;
    if (control == NULL && error == RSS_DDC_OK) error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    if (error == RSS_DDC_OK) error = rss_ddc_profile_picture_mode_raw(&effective, mode, &raw);
    if (error == RSS_DDC_OK) error = rss_macos_provider_set_vcp(&binding, (uint8_t)control->address, raw, NULL);
    rss_macos_release_binding(&binding); return error;
}
RSSDDCError rss_ddc_set_picture_mode_with_diagnostics(uint32_t list_index, RSSDDCPictureMode mode, const RSSDDCDiagnostics *diagnostics) {
    RSSMacOSBinding binding = {}; RSSDDCEffectiveProfile effective = {}; uint16_t raw = 0;
    RSSDDCError error = picture_mode_binding(list_index, NULL, &binding, &effective, diagnostics);
    const RSSDDCProfileControl *control = error == RSS_DDC_OK ? picture_control(&effective) : NULL;
    if (control == NULL && error == RSS_DDC_OK) error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    if (error == RSS_DDC_OK) error = rss_ddc_profile_picture_mode_raw(&effective, mode, &raw);
    if (error == RSS_DDC_OK) { char message[128] = {}; snprintf(message, sizeof(message), "operation=PictureMode mode=%s", rss_ddc_picture_mode_name(mode)); rss_macos_diagnostic(diagnostics, message); error = rss_macos_provider_set_vcp(&binding, (uint8_t)control->address, raw, diagnostics); }
    rss_macos_release_binding(&binding); return error;
}
