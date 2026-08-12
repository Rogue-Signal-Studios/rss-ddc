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
