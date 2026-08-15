#ifndef _DDC_PARSER_H
#define _DDC_PARSER_H

/*
 * Research diagnostic Get VCP parser migrated from m1ddc-rss.
 * Production-equivalent strict parsing lives in src/ddc/protocol.c;
 * this copy populates diagnostic fields on failure for lab dumps.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DDC_GET_VCP_REPLY_SIZE 11

/** Reasons a raw DDC/CI Get VCP Feature reply cannot be used safely. */
typedef enum {
    DDC_GET_VCP_PARSE_OK = 0,
    DDC_GET_VCP_PARSE_NULL_RESPONSE,
    DDC_GET_VCP_PARSE_SHORT_RESPONSE,
    DDC_GET_VCP_PARSE_SOURCE_ADDRESS,
    DDC_GET_VCP_PARSE_LENGTH,
    DDC_GET_VCP_PARSE_COMMAND,
    DDC_GET_VCP_PARSE_RESULT,
    DDC_GET_VCP_PARSE_VCP_CODE,
    DDC_GET_VCP_PARSE_CHECKSUM,
} DDCGetVCPParseError;

/** Decoded fields from a standard DDC/CI Get VCP Feature reply. */
typedef struct {
    uint8_t resultCode;
    uint8_t vcpCode;
    uint8_t vcpType;
    uint8_t maximumValueHigh;
    uint8_t maximumValueLow;
    uint8_t currentValueHigh;
    uint8_t currentValueLow;
    uint16_t maximumValue;
    uint16_t currentValue;
    uint8_t calculatedChecksum;
    uint8_t receivedChecksum;
    bool checksumValid;
} DDCGetVCPResponse;

/**
 * Calculates a DDC/CI reply checksum. The byte sequence must start with the
 * display source address (normally 0x6e); the host's read address is included
 * as required by the DDC/CI checksum definition.
 */
uint8_t ddcGetVCPReplyChecksum(const uint8_t *bytes, size_t byteCount);

/**
 * Parses and validates a standard 11-byte DDC/CI Get VCP Feature reply.
 *
 * The parser validates the reply framing, success result, requested VCP code,
 * and checksum before returning DDC_GET_VCP_PARSE_OK. `response` is populated
 * when the reply is long enough to contain all Get VCP fields, even if a later
 * validation fails, so diagnostics can report the received values.
 */
DDCGetVCPParseError parseDDCGetVCPReply(const uint8_t *bytes,
                                        size_t byteCount,
                                        uint8_t requestedVCPCode,
                                        DDCGetVCPResponse *response);

/** Returns a stable, human-readable description for a parser result. */
const char *ddcGetVCPParseErrorString(DDCGetVCPParseError error);

#endif
