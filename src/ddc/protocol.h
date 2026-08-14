#ifndef RSS_DDC_PROTOCOL_H
#define RSS_DDC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "rss_ddc.h"

enum {
    RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE = 4,
    RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE = 6,
    RSS_DDC_GET_VCP_REQUEST_SIZE = 5,
    RSS_DDC_GET_VCP_REPLY_SIZE = 11,
    RSS_DDC_CONVENTIONAL_CAPABILITIES_REQUEST_SIZE = 5,
    /** E3 data includes command and two offset bytes before text. */
    RSS_DDC_CAPABILITIES_REPLY_MAX_DATA_BYTES = 35,
    /** 0x6e, length, up to 35 data bytes, checksum; IOAV omits host-read address. */
    RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE = 38,
    /** 128 nonempty 32-byte fragments plus one explicit empty terminator. */
    RSS_DDC_CAPABILITIES_MAX_REQUESTS = 129,
};

/** A transient, strictly bounded view of one validated MCCS E3 text fragment. */
typedef struct {
    uint16_t offset;
    const uint8_t *bytes;
    size_t length;
} RSSDDCCapabilitiesFragment;

/**
 * XORs the DDC destination seed (0x6e) with a provider-specific request
 * representation. PS190 includes inline 0x51; conventional DP does not.
 */
uint8_t rss_ddc_request_checksum(const uint8_t *bytes, size_t byte_count);
/** Builds a four-byte DCPDP13 payload; the IOAV data argument carries 0x51 separately. */
void rss_ddc_build_conventional_get_vcp(
    uint8_t vcp_code, uint8_t request[RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE]);
/**
 * Builds the conventional six-byte Set VCP payload. The caller supplies the
 * DDC source address (normally 0x51) as IOAV's data/subaddress argument, and
 * therefore includes it in the checksum but not in this payload.
 */
void rss_ddc_build_conventional_set_vcp(
    uint8_t vcp_code, uint16_t value, uint8_t request[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE]);
/** Builds the complete five-byte raw-framed Get VCP request for the PS190 transport. */
void rss_ddc_build_raw_get_vcp(uint8_t vcp_code, uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE]);
/** Builds DCPDP13's conventional F3 capabilities request for one text offset. */
void rss_ddc_build_conventional_capabilities_request(
    uint16_t offset, uint8_t request[RSS_DDC_CONVENTIONAL_CAPABILITIES_REQUEST_SIZE]);
/**
 * Strictly parses exactly one 11-byte Get VCP reply. On failure, `result`
 * remains untouched and the returned error identifies the rejected field.
 */
RSSDDCError rss_ddc_parse_get_vcp_reply(const uint8_t *reply, size_t byte_count,
                                        uint8_t requested_vcp, RSSDDCVCPResult *result);
/** Derives the exact E3 frame size from the bounded receive prefix. */
RSSDDCError rss_ddc_capabilities_reply_frame_size(const uint8_t *reply, size_t available_bytes,
                                                  size_t *frame_size);
/** Strictly validates one exact-size E3 frame and exposes only its text payload. */
RSSDDCError rss_ddc_parse_capabilities_reply(const uint8_t *reply, size_t byte_count,
                                             RSSDDCCapabilitiesFragment *fragment);
/** Rejects an otherwise valid fragment when its echoed offset differs from the request. */
RSSDDCError rss_ddc_validate_capabilities_fragment_offset(const RSSDDCCapabilitiesFragment *fragment,
                                                          uint16_t requested_offset);

#endif
