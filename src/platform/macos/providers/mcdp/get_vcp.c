#include "macos_internal.h"

/** MCDP is classified separately but has no validated GET transport in this milestone. */
RSSDDCError rss_macos_mcdp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                    const RSSDDCDiagnostics *diagnostics) {
    (void)binding;
    (void)vcp_code;
    (void)result;
    rss_macos_diagnostic(diagnostics, "backend=AppleDCPMCDP29XX capability=GetVCP status=unsupported");
    /* MCDP discovery is real; its GET transport is not yet enabled. */
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
