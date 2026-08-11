#include <string.h>

#include "rss_ddc.h"

const char *rss_ddc_error_string(RSSDDCError error) {
    switch (error) {
        case RSS_DDC_OK: return "success";
        case RSS_DDC_ERROR_ARGUMENT: return "invalid argument";
        case RSS_DDC_ERROR_NOT_FOUND: return "display not found";
        case RSS_DDC_ERROR_UNSUPPORTED_PROVIDER: return "unsupported display provider";
        case RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY: return "unsupported capability";
        case RSS_DDC_ERROR_DISCOVERY: return "display discovery failed";
        case RSS_DDC_ERROR_SAFETY_GATE: return "provider safety correlation failed";
        case RSS_DDC_ERROR_TRANSPORT: return "DDC transport failed";
        case RSS_DDC_ERROR_PROTOCOL: return "invalid DDC/CI response";
        case RSS_DDC_ERROR_SYSTEM: return "macOS system error";
    }
    return "unknown error";
}

const char *rss_ddc_provider_string(RSSDDCProvider provider) {
    switch (provider) {
        case RSS_DDC_PROVIDER_DCPDP13: return "DCPDP13Service";
        case RSS_DDC_PROVIDER_MCDP29XX: return "AppleDCPMCDP29XX";
        case RSS_DDC_PROVIDER_PS190: return "AppleDCPPS190";
        case RSS_DDC_PROVIDER_UNKNOWN: return "unknown";
    }
    return "unknown";
}

RSSDDCProvider rss_ddc_provider_from_registry_class(const char *provider_class) {
    if (provider_class == NULL) return RSS_DDC_PROVIDER_UNKNOWN;
    if (strcmp(provider_class, "DCPDP13Service") == 0) return RSS_DDC_PROVIDER_DCPDP13;
    if (strcmp(provider_class, "AppleDCPMCDP29XX") == 0) return RSS_DDC_PROVIDER_MCDP29XX;
    if (strcmp(provider_class, "AppleDCPPS190") == 0) return RSS_DDC_PROVIDER_PS190;
    return RSS_DDC_PROVIDER_UNKNOWN;
}

uint32_t rss_ddc_provider_capabilities(RSSDDCProvider provider) {
    return provider == RSS_DDC_PROVIDER_PS190 ? RSS_DDC_CAP_GET_VCP : RSS_DDC_CAP_NONE;
}
