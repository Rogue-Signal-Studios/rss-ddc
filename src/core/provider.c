#include <string.h>

#include "rss_ddc.h"
#include "correlation.h"

const char *rss_ddc_error_string(RSSDDCError error) {
    switch (error) {
        case RSS_DDC_OK: return "success";
        case RSS_DDC_ERROR_ARGUMENT: return "invalid argument";
        case RSS_DDC_ERROR_NOT_FOUND: return "display not found";
        case RSS_DDC_ERROR_UNSUPPORTED_PROVIDER: return "unsupported display provider";
        case RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY: return "unsupported capability";
        case RSS_DDC_ERROR_DISCOVERY: return "display discovery failed";
        case RSS_DDC_ERROR_SAFETY_GATE: return "provider safety correlation failed";
        case RSS_DDC_ERROR_SERVICE_CONSTRUCTION: return "IOAVService construction failed";
        case RSS_DDC_ERROR_WRITE: return "DDC/CI write failed";
        case RSS_DDC_ERROR_READ: return "DDC/CI read failed";
        case RSS_DDC_ERROR_REPLY_LENGTH: return "DDC/CI reply has an invalid length";
        case RSS_DDC_ERROR_REPLY_SOURCE: return "DDC/CI reply has an invalid source/framing";
        case RSS_DDC_ERROR_REPLY_COMMAND: return "DDC/CI reply has an invalid command";
        case RSS_DDC_ERROR_REPLY_STATUS: return "DDC/CI reply reports failure status";
        case RSS_DDC_ERROR_REPLY_VCP: return "DDC/CI reply VCP code does not match request";
        case RSS_DDC_ERROR_REPLY_CHECKSUM: return "DDC/CI reply checksum is invalid";
        case RSS_DDC_ERROR_EDID_LENGTH: return "EDID length is invalid";
        case RSS_DDC_ERROR_EDID_HEADER: return "EDID header or manufacturer encoding is invalid";
        case RSS_DDC_ERROR_EDID_CHECKSUM: return "EDID block checksum is invalid";
        case RSS_DDC_ERROR_DPCD_LENGTH: return "DPCD read length is unsupported";
        case RSS_DDC_ERROR_DPCD_RANGE: return "DPCD address range is invalid";
        case RSS_DDC_ERROR_DPCD_READ: return "DPCD read failed";
        case RSS_DDC_ERROR_VERIFY_MISMATCH: return "verification value did not match after all attempts";
        case RSS_DDC_ERROR_VERIFY_RETRY_EXHAUSTED: return "verification retries exhausted after transient GET failures";
        case RSS_DDC_ERROR_VERIFY_UNAVAILABLE: return "SET completed but the original display is unavailable for safe verification";
        case RSS_DDC_ERROR_CAPABILITIES_MALFORMED: return "MCCS capabilities data is malformed or incomplete";
        case RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE: return "MCCS capabilities data exceeds the supported bound";
        case RSS_DDC_ERROR_SYSTEM: return "macOS system error";
    }
    return "unknown error";
}

const char *rss_ddc_provider_string(RSSDDCProvider provider) {
    switch (provider) {
        case RSS_DDC_PROVIDER_DCPDP13: return "DCPDP13Service";
        case RSS_DDC_PROVIDER_DCPDP_SERVICE: return "DCPDPService";
        case RSS_DDC_PROVIDER_MCDP29XX: return "AppleDCPMCDP29XX";
        case RSS_DDC_PROVIDER_PS190: return "AppleDCPPS190";
        case RSS_DDC_PROVIDER_UNKNOWN: return "unknown";
    }
    return "unknown";
}

RSSDDCProvider rss_ddc_provider_from_registry_class(const char *provider_class) {
    if (provider_class == NULL) return RSS_DDC_PROVIDER_UNKNOWN;
    if (strcmp(provider_class, "DCPDP13Service") == 0) return RSS_DDC_PROVIDER_DCPDP13;
    if (strcmp(provider_class, RSS_DDC_REGISTRY_CLASS_DCPDP_SERVICE) == 0) return RSS_DDC_PROVIDER_DCPDP_SERVICE;
    if (strcmp(provider_class, "AppleDCPMCDP29XX") == 0) return RSS_DDC_PROVIDER_MCDP29XX;
    if (strcmp(provider_class, "AppleDCPPS190") == 0) return RSS_DDC_PROVIDER_PS190;
    return RSS_DDC_PROVIDER_UNKNOWN;
}

RSSDDCBackend rss_ddc_provider_backend(RSSDDCProvider provider) {
    switch (provider) {
        case RSS_DDC_PROVIDER_DCPDP13: return RSS_DDC_BACKEND_DCPDP13;
        case RSS_DDC_PROVIDER_DCPDP_SERVICE: return RSS_DDC_BACKEND_DCPDP_SERVICE;
        case RSS_DDC_PROVIDER_MCDP29XX: return RSS_DDC_BACKEND_MCDP29XX;
        case RSS_DDC_PROVIDER_PS190: return RSS_DDC_BACKEND_PS190;
        case RSS_DDC_PROVIDER_UNKNOWN: return RSS_DDC_BACKEND_UNSUPPORTED;
    }
    return RSS_DDC_BACKEND_UNSUPPORTED;
}

const char *rss_ddc_backend_name(RSSDDCBackend backend) {
    switch (backend) {
        case RSS_DDC_BACKEND_DCPDP13: return "DCPDP13Service";
        case RSS_DDC_BACKEND_DCPDP_SERVICE: return "DCPDPService";
        case RSS_DDC_BACKEND_MCDP29XX: return "AppleDCPMCDP29XX";
        case RSS_DDC_BACKEND_PS190: return "AppleDCPPS190";
        case RSS_DDC_BACKEND_UNSUPPORTED: return "unsupported";
    }
    return "unsupported";
}

uint32_t rss_ddc_provider_capabilities(RSSDDCProvider provider) {
    switch (provider) {
        case RSS_DDC_PROVIDER_DCPDP13:
            return RSS_DDC_CAP_GET_VCP | RSS_DDC_CAP_SET_VCP | RSS_DDC_CAP_READ_DPCD;
        case RSS_DDC_PROVIDER_DCPDP_SERVICE:
            return RSS_DDC_CAP_GET_VCP | RSS_DDC_CAP_SET_VCP | RSS_DDC_CAP_READ_DPCD;
        case RSS_DDC_PROVIDER_PS190:
            return RSS_DDC_CAP_GET_VCP | RSS_DDC_CAP_SET_VCP | RSS_DDC_CAP_READ_EDID | RSS_DDC_CAP_READ_DPCD;
        case RSS_DDC_PROVIDER_UNKNOWN:
        case RSS_DDC_PROVIDER_MCDP29XX:
            return RSS_DDC_CAP_NONE;
    }
    return RSS_DDC_CAP_NONE;
}
