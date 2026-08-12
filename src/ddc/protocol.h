#ifndef RSS_DDC_PROTOCOL_H
#define RSS_DDC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "rss_ddc.h"

enum {
    RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE = 4,
    RSS_DDC_GET_VCP_REQUEST_SIZE = 5,
    RSS_DDC_GET_VCP_REPLY_SIZE = 11,
};

/**
 * XORs the DDC destination seed (0x6e) with a provider-specific request
 * representation. PS190 includes inline 0x51; conventional DP does not.
 */
uint8_t rss_ddc_request_checksum(const uint8_t *bytes, size_t byte_count);
/** Builds a four-byte DCPDP13 payload; the IOAV data argument carries 0x51 separately. */
void rss_ddc_build_conventional_get_vcp(
    uint8_t vcp_code, uint8_t request[RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE]);
/** Builds the complete five-byte raw-framed Get VCP request for the PS190 transport. */
void rss_ddc_build_raw_get_vcp(uint8_t vcp_code, uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE]);
/**
 * Strictly parses exactly one 11-byte Get VCP reply. On failure, `result`
 * remains untouched and the returned error identifies the rejected field.
 */
RSSDDCError rss_ddc_parse_get_vcp_reply(const uint8_t *reply, size_t byte_count,
                                        uint8_t requested_vcp, RSSDDCVCPResult *result);

#endif
