#ifndef _DDC_REQUEST_H
#define _DDC_REQUEST_H

/*
 * Research request builders migrated from m1ddc-rss. Byte-equivalent
 * production builders live in src/ddc/protocol.c and will be preferred
 * where the lab can share them without changing experimental dumps.
 */

#include <stddef.h>
#include <stdint.h>

#define DDC_GET_VCP_REQUEST_SIZE 4
#define DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE 5

/**
 * Constructs a complete DDC/CI Get VCP Feature request from its VCP code.
 * The result is independent of the prior contents of `request`.
 */
void buildDDCGetVCPRequest(uint8_t vcpCode,
                           uint8_t request[DDC_GET_VCP_REQUEST_SIZE]);

/**
 * Calculates the DDC/CI destination checksum for raw bytes that include the
 * host source address as their first byte.
 */
uint8_t ddcRawFramedRequestChecksum(const uint8_t *bytes, size_t byteCount);

/**
 * Constructs a raw-framed DDC/CI Get VCP request.  The result includes the
 * host source address (0x51) and is intended for a no-offset I2C transport.
 */
void buildRawFramedDDCGetVCPRequest(uint8_t vcpCode,
                                    uint8_t request[DDC_RAW_FRAMED_GET_VCP_REQUEST_SIZE]);

#endif
