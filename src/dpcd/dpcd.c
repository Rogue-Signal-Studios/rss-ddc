#include "dpcd.h"

RSSDDCError rss_ddc_validate_dpcd_request(uint32_t address, const uint8_t *buffer, size_t length) {
    if (buffer == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (length == 0 || length > RSS_DDC_DPCD_MAX_READ_BYTES) return RSS_DDC_ERROR_DPCD_LENGTH;
    if (address > RSS_DDC_DPCD_MAX_ADDRESS || length - 1 > RSS_DDC_DPCD_MAX_ADDRESS - address) {
        return RSS_DDC_ERROR_DPCD_RANGE;
    }
    return RSS_DDC_OK;
}

RSSDDCDPCDPathStatus rss_ddc_dpcd_path_status_for_candidate_count(unsigned int candidate_count) {
    if (candidate_count == 0) return RSS_DDC_DPCD_PATH_UNAVAILABLE;
    if (candidate_count == 1) return RSS_DDC_DPCD_PATH_CANDIDATE;
    return RSS_DDC_DPCD_PATH_AMBIGUOUS;
}

static const char *link_rate_name(uint8_t raw) {
    switch (raw) {
        case 0x06: return "RBR (1.62 Gbps/lane)";
        case 0x0a: return "HBR (2.70 Gbps/lane)";
        case 0x14: return "HBR2 (5.40 Gbps/lane)";
        case 0x1e: return "HBR3 (8.10 Gbps/lane)";
        default: return "unknown";
    }
}

RSSDDCError rss_ddc_decode_dpcd_capabilities(uint32_t address, const uint8_t *bytes, size_t length,
                                              RSSDDCDPCDCapabilities *capabilities) {
    if (bytes == NULL || capabilities == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (address != 0 || length < 6) return RSS_DDC_ERROR_DPCD_LENGTH;
    *capabilities = (RSSDDCDPCDCapabilities){
        .revision = bytes[0],
        .max_link_rate_raw = bytes[1],
        .max_link_rate_name = link_rate_name(bytes[1]),
        .max_lane_count = (uint8_t)(bytes[2] & 0x1f),
        .enhanced_framing = (bytes[2] & 0x80) != 0,
        .downstream_port_present = (bytes[5] & 0x01) != 0,
    };
    return RSS_DDC_OK;
}
