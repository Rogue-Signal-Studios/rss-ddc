#include "protocol.h"

enum {
    /* DDC/CI addressing/framing constants, not IOKit subaddresses. */
    RSS_DDC_DESTINATION_ADDRESS = 0x6e,
    RSS_DDC_SOURCE_ADDRESS = 0x51,
    RSS_DDC_REPLY_SOURCE = 0x6e,
    RSS_DDC_GET_LENGTH = 0x82,
    RSS_DDC_GET_COMMAND = 0x01,
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

void rss_ddc_build_raw_get_vcp(uint8_t vcp_code, uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE]) {
    /* PS190 sends 0x51 inline; it is not supplied as the IOAV data argument. */
    request[0] = RSS_DDC_SOURCE_ADDRESS;
    request[1] = RSS_DDC_GET_LENGTH;
    request[2] = RSS_DDC_GET_COMMAND;
    request[3] = vcp_code;
    request[4] = rss_ddc_request_checksum(request, RSS_DDC_GET_VCP_REQUEST_SIZE - 1);
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
