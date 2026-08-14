#include "protocol.h"

enum {
    /* DDC/CI addressing/framing constants, not IOKit subaddresses. */
    RSS_DDC_DESTINATION_ADDRESS = 0x6e,
    RSS_DDC_SOURCE_ADDRESS = 0x51,
    RSS_DDC_REPLY_SOURCE = 0x6e,
    RSS_DDC_GET_LENGTH = 0x82,
    RSS_DDC_GET_COMMAND = 0x01,
    RSS_DDC_SET_LENGTH = 0x84,
    RSS_DDC_SET_COMMAND = 0x03,
    RSS_DDC_CAPABILITIES_REQUEST_COMMAND = 0xf3,
    RSS_DDC_CAPABILITIES_REPLY_COMMAND = 0xe3,
    RSS_DDC_REPLY_LENGTH = 0x88,
    RSS_DDC_REPLY_COMMAND = 0x02,
};

uint8_t rss_ddc_request_checksum(const uint8_t *bytes, size_t byte_count) {
    uint8_t checksum = RSS_DDC_DESTINATION_ADDRESS;
    for (size_t index = 0; index < byte_count; ++index) checksum ^= bytes[index];
    return checksum;
}

void rss_ddc_build_conventional_get_vcp(
    uint8_t vcp_code, uint8_t request[RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE]) {
    request[0] = RSS_DDC_GET_LENGTH;
    request[1] = RSS_DDC_GET_COMMAND;
    request[2] = vcp_code;
    /* The 0x51 source address is passed as IOAV's subaddress, not in this payload. */
    request[3] = rss_ddc_request_checksum(request, 3);
}

void rss_ddc_build_conventional_set_vcp(
    uint8_t vcp_code, uint16_t value, uint8_t request[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE]) {
    request[0] = RSS_DDC_SET_LENGTH;
    request[1] = RSS_DDC_SET_COMMAND;
    request[2] = vcp_code;
    request[3] = (uint8_t)(value >> 8);
    request[4] = (uint8_t)value;
    /* The conventional IOAV form represents 0x51 out-of-band, but its DDC checksum is still required. */
    const uint8_t source_address = RSS_DDC_SOURCE_ADDRESS;
    uint8_t checksum = rss_ddc_request_checksum(&source_address, 1);
    for (size_t index = 0; index < RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE - 1; ++index) {
        checksum ^= request[index];
    }
    request[5] = checksum;
}

void rss_ddc_build_raw_get_vcp(uint8_t vcp_code, uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE]) {
    /* PS190 sends 0x51 inline; it is not supplied as the IOAV data argument. */
    request[0] = RSS_DDC_SOURCE_ADDRESS;
    request[1] = RSS_DDC_GET_LENGTH;
    request[2] = RSS_DDC_GET_COMMAND;
    request[3] = vcp_code;
    request[4] = rss_ddc_request_checksum(request, RSS_DDC_GET_VCP_REQUEST_SIZE - 1);
}

void rss_ddc_build_conventional_capabilities_request(
    uint16_t offset, uint8_t request[RSS_DDC_CONVENTIONAL_CAPABILITIES_REQUEST_SIZE]) {
    request[0] = 0x83;
    request[1] = RSS_DDC_CAPABILITIES_REQUEST_COMMAND;
    request[2] = (uint8_t)(offset >> 8);
    request[3] = (uint8_t)offset;
    request[4] = rss_ddc_request_checksum(request, RSS_DDC_CONVENTIONAL_CAPABILITIES_REQUEST_SIZE - 1);
}

RSSDDCError rss_ddc_parse_get_vcp_reply(const uint8_t *reply, size_t byte_count,
                                        uint8_t requested_vcp, RSSDDCVCPResult *result) {
    if (reply == NULL || result == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (byte_count != RSS_DDC_GET_VCP_REPLY_SIZE) return RSS_DDC_ERROR_REPLY_LENGTH;
    if (reply[0] != RSS_DDC_REPLY_SOURCE || reply[1] != RSS_DDC_REPLY_LENGTH) return RSS_DDC_ERROR_REPLY_SOURCE;
    if (reply[2] != RSS_DDC_REPLY_COMMAND) return RSS_DDC_ERROR_REPLY_COMMAND;
    if (reply[3] != 0) return RSS_DDC_ERROR_REPLY_STATUS;
    if (reply[4] != requested_vcp) return RSS_DDC_ERROR_REPLY_VCP;
    /* Reply checksums use the host read address (0x50), unlike raw write requests. */
    uint8_t checksum = 0x50;
    for (size_t index = 0; index < RSS_DDC_GET_VCP_REPLY_SIZE - 1; ++index) checksum ^= reply[index];
    if (checksum != reply[10]) return RSS_DDC_ERROR_REPLY_CHECKSUM;
    result->vcp_code = reply[4];
    result->maximum_value = ((uint16_t)reply[6] << 8) | reply[7];
    result->current_value = ((uint16_t)reply[8] << 8) | reply[9];
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_capabilities_reply_frame_size(const uint8_t *reply, size_t available_bytes,
                                                  size_t *frame_size) {
    if (reply == NULL || frame_size == NULL || available_bytes < 2) return RSS_DDC_ERROR_ARGUMENT;
    if (reply[0] != RSS_DDC_REPLY_SOURCE || (reply[1] & 0x80u) == 0) {
        return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    }
    size_t data_length = reply[1] & 0x7fu;
    size_t declared_frame_size = data_length + 3;
    if (data_length < 3 || data_length > RSS_DDC_CAPABILITIES_REPLY_MAX_DATA_BYTES ||
        declared_frame_size > available_bytes || declared_frame_size > RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE) {
        return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    }
    *frame_size = declared_frame_size;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_parse_capabilities_reply(const uint8_t *reply, size_t byte_count,
                                             RSSDDCCapabilitiesFragment *fragment) {
    if (reply == NULL || fragment == NULL) return RSS_DDC_ERROR_ARGUMENT;
    size_t frame_size = 0;
    RSSDDCError error = rss_ddc_capabilities_reply_frame_size(reply, byte_count, &frame_size);
    if (error != RSS_DDC_OK || frame_size != byte_count) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    if (reply[2] != RSS_DDC_CAPABILITIES_REPLY_COMMAND) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    uint8_t checksum = 0x50;
    for (size_t index = 0; index + 1 < byte_count; ++index) checksum ^= reply[index];
    if (checksum != reply[byte_count - 1]) return RSS_DDC_ERROR_REPLY_CHECKSUM;
    *fragment = (RSSDDCCapabilitiesFragment){
        .offset = ((uint16_t)reply[3] << 8) | reply[4], .bytes = reply + 5, .length = (reply[1] & 0x7fu) - 3};
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_validate_capabilities_fragment_offset(const RSSDDCCapabilitiesFragment *fragment,
                                                          uint16_t requested_offset) {
    if (fragment == NULL) return RSS_DDC_ERROR_ARGUMENT;
    return fragment->offset == requested_offset ? RSS_DDC_OK : RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
}
