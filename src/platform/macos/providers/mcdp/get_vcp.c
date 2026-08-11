#include "macos_internal.h"

RSSDDCError rss_macos_mcdp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result) {
    (void)binding;
    (void)vcp_code;
    (void)result;
    /* MCDP discovery is real; its GET transport is not yet enabled. */
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
