#include "ddc_request.h"

#define DDC_DISPLAY_WRITE_ADDRESS 0x6e
#define DDC_HOST_SOURCE_ADDRESS 0x51
#define DDC_GET_VCP_COMMAND 0x01
#define DDC_GET_VCP_PAYLOAD_LENGTH 0x82

void buildDDCGetVCPRequest(uint8_t vcpCode,
                           uint8_t request[DDC_GET_VCP_REQUEST_SIZE]) {
    request[0] = DDC_GET_VCP_PAYLOAD_LENGTH;
    request[1] = DDC_GET_VCP_COMMAND;
    request[2] = vcpCode;
    request[3] = DDC_DISPLAY_WRITE_ADDRESS ^ request[0] ^ request[1] ^ request[2];
}

uint8_t ddcRawFramedRequestChecksum(const uint8_t *bytes, size_t byteCount) {
    uint8_t checksum = DDC_DISPLAY_WRITE_ADDRESS;
    for (size_t index = 0; index < byteCount; ++index) checksum ^= bytes[index];
    return checksum;
}

void buildRawFramedDDCGetVCPRequest(uint8_t vcpCode,
                                    uint8_t request[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE]) {
    request[0] = DDC_HOST_SOURCE_ADDRESS;
    request[1] = DDC_GET_VCP_PAYLOAD_LENGTH;
    request[2] = DDC_GET_VCP_COMMAND;
    request[3] = vcpCode;
    request[4] = ddcRawFramedRequestChecksum(request, DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE - 1);
}
