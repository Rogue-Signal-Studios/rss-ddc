#include "macos_internal.h"

RSSDDCError rss_macos_provider_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result) {
    if (binding == NULL) return RSS_DDC_ERROR_ARGUMENT;
    switch (binding->display.provider) {
        case RSS_DDC_PROVIDER_PS190:
            return rss_macos_ps190_get_vcp(binding, vcp_code, result);
        case RSS_DDC_PROVIDER_DCPDP13:
            return rss_macos_dp_get_vcp(binding, vcp_code, result);
        case RSS_DDC_PROVIDER_MCDP29XX:
            return rss_macos_mcdp_get_vcp(binding, vcp_code, result);
        case RSS_DDC_PROVIDER_UNKNOWN:
            return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    }
    return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
}
