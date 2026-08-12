#include "macos_internal.h"

/**
 * Provider boundary: only a recognized provider implementation receives a
 * private binding. Unknown providers fail closed instead of using a fallback.
 */
RSSDDCError rss_macos_provider_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                        const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL) return RSS_DDC_ERROR_ARGUMENT;
    switch (rss_ddc_provider_backend(binding->display.provider)) {
        case RSS_DDC_BACKEND_PS190:
            return rss_macos_ps190_get_vcp(binding, vcp_code, result, diagnostics);
        case RSS_DDC_BACKEND_DCPDP13:
            return rss_macos_dp_get_vcp(binding, vcp_code, result, diagnostics);
        case RSS_DDC_BACKEND_MCDP29XX:
            return rss_macos_mcdp_get_vcp(binding, vcp_code, result, diagnostics);
        case RSS_DDC_BACKEND_UNSUPPORTED:
            rss_macos_diagnostic(diagnostics, "backend=unknown capability=GetVCP status=unsupported");
            return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    }
    return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
}

/** SET dispatch stays provider-specific; each enabled backend owns its evidence-backed IOAV transaction shape. */
RSSDDCError rss_macos_provider_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                        const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL) return RSS_DDC_ERROR_ARGUMENT;
    switch (rss_ddc_provider_backend(binding->display.provider)) {
        case RSS_DDC_BACKEND_PS190:
            return rss_macos_ps190_set_vcp(binding, vcp_code, value, diagnostics);
        case RSS_DDC_BACKEND_DCPDP13:
            return rss_macos_dp_set_vcp(binding, vcp_code, value, diagnostics);
        case RSS_DDC_BACKEND_MCDP29XX:
            rss_macos_diagnostic(diagnostics, "operation=SetVCP status=unsupported");
            return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
        case RSS_DDC_BACKEND_UNSUPPORTED:
            rss_macos_diagnostic(diagnostics, "backend=unknown operation=SetVCP status=unsupported");
            return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    }
    return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
}

/** EDID remains an independent capability; unsupported providers never borrow another backend's path. */
RSSDDCError rss_macos_provider_read_edid(RSSMacOSBinding *binding, RSSDDCEDID *edid,
                                         const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || edid == NULL) return RSS_DDC_ERROR_ARGUMENT;
    switch (rss_ddc_provider_backend(binding->display.provider)) {
        case RSS_DDC_BACKEND_PS190:
            return rss_macos_ps190_read_edid(binding, edid, diagnostics);
        case RSS_DDC_BACKEND_DCPDP13:
        case RSS_DDC_BACKEND_MCDP29XX:
            rss_macos_diagnostic(diagnostics, "operation=ReadEDID status=unsupported");
            return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
        case RSS_DDC_BACKEND_UNSUPPORTED:
            rss_macos_diagnostic(diagnostics, "backend=unknown operation=ReadEDID status=unsupported");
            return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    }
    return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
}

/** DPCD is independently dispatched; DP/MCDP must never borrow PS190's IODP path. */
RSSDDCError rss_macos_provider_read_dpcd(RSSMacOSBinding *binding, uint32_t address, uint8_t *buffer,
                                         size_t length, const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || buffer == NULL) return RSS_DDC_ERROR_ARGUMENT;
    switch (rss_ddc_provider_backend(binding->display.provider)) {
        case RSS_DDC_BACKEND_PS190:
            return rss_macos_ps190_read_dpcd(binding, address, buffer, length, diagnostics);
        case RSS_DDC_BACKEND_DCPDP13:
        case RSS_DDC_BACKEND_MCDP29XX:
            rss_macos_diagnostic(diagnostics, "operation=ReadDPCD status=unsupported");
            return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
        case RSS_DDC_BACKEND_UNSUPPORTED:
            rss_macos_diagnostic(diagnostics, "backend=unknown operation=ReadDPCD status=unsupported");
            return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    }
    return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
}
