#include "ddc_parser.h"

#define DDC_HOST_READ_ADDRESS 0x50
#define DDC_DISPLAY_SOURCE_ADDRESS 0x6e
#define DDC_GET_VCP_REPLY_LENGTH 0x88
#define DDC_GET_VCP_REPLY_COMMAND 0x02
#define DDC_GET_VCP_SUCCESS 0x00

uint8_t ddcGetVCPReplyChecksum(const uint8_t *bytes, size_t byteCount) {
    uint8_t checksum = DDC_HOST_READ_ADDRESS;

    for (size_t index = 0; index < byteCount; ++index) {
        checksum ^= bytes[index];
    }
    return checksum;
}

DDCGetVCPParseError parseDDCGetVCPReply(const uint8_t *bytes,
                                        size_t byteCount,
                                        uint8_t requestedVCPCode,
                                        DDCGetVCPResponse *response) {
    if (response != NULL) {
        *response = (DDCGetVCPResponse){0};
    }
    if (bytes == NULL || response == NULL) {
        return DDC_GET_VCP_PARSE_NULL_RESPONSE;
    }
    if (byteCount < DDC_GET_VCP_REPLY_SIZE) {
        return DDC_GET_VCP_PARSE_SHORT_RESPONSE;
    }

    response->resultCode = bytes[3];
    response->vcpCode = bytes[4];
    response->vcpType = bytes[5];
    response->maximumValueHigh = bytes[6];
    response->maximumValueLow = bytes[7];
    response->currentValueHigh = bytes[8];
    response->currentValueLow = bytes[9];
    response->maximumValue = ((uint16_t)response->maximumValueHigh << 8) | response->maximumValueLow;
    response->currentValue = ((uint16_t)response->currentValueHigh << 8) | response->currentValueLow;
    response->receivedChecksum = bytes[10];
    response->calculatedChecksum = ddcGetVCPReplyChecksum(bytes, DDC_GET_VCP_REPLY_SIZE - 1);
    response->checksumValid = response->calculatedChecksum == response->receivedChecksum;

    if (bytes[0] != DDC_DISPLAY_SOURCE_ADDRESS) {
        return DDC_GET_VCP_PARSE_SOURCE_ADDRESS;
    }
    if (bytes[1] != DDC_GET_VCP_REPLY_LENGTH) {
        return DDC_GET_VCP_PARSE_LENGTH;
    }
    if (bytes[2] != DDC_GET_VCP_REPLY_COMMAND) {
        return DDC_GET_VCP_PARSE_COMMAND;
    }
    if (response->resultCode != DDC_GET_VCP_SUCCESS) {
        return DDC_GET_VCP_PARSE_RESULT;
    }
    if (response->vcpCode != requestedVCPCode) {
        return DDC_GET_VCP_PARSE_VCP_CODE;
    }
    if (!response->checksumValid) {
        return DDC_GET_VCP_PARSE_CHECKSUM;
    }
    return DDC_GET_VCP_PARSE_OK;
}

const char *ddcGetVCPParseErrorString(DDCGetVCPParseError error) {
    switch (error) {
        case DDC_GET_VCP_PARSE_OK: return "valid response";
        case DDC_GET_VCP_PARSE_NULL_RESPONSE: return "missing response buffer";
        case DDC_GET_VCP_PARSE_SHORT_RESPONSE: return "short response";
        case DDC_GET_VCP_PARSE_SOURCE_ADDRESS: return "unexpected source address";
        case DDC_GET_VCP_PARSE_LENGTH: return "unexpected response length";
        case DDC_GET_VCP_PARSE_COMMAND: return "unexpected DDC/CI command";
        case DDC_GET_VCP_PARSE_RESULT: return "non-success DDC/CI result";
        case DDC_GET_VCP_PARSE_VCP_CODE: return "returned VCP code does not match request";
        case DDC_GET_VCP_PARSE_CHECKSUM: return "checksum mismatch";
    }
    return "unknown parser error";
}
