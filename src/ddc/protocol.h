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
    RSS_DDC_RAW_CAPABILITIES_REQUEST_SIZE = 6,
    /** MCCS carries a 3-byte E3 header plus at most 32 string bytes per reply. */
    RSS_DDC_CAPABILITIES_REPLY_MAX_DATA_BYTES = 35,
    /** IOAV omits the implicit host-read address: 0x6e + length + 35 data + checksum. */
    RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE = 38,
};

/** A validated, transient view into one MCCS Capabilities Reply packet. */
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
/** Builds the conventional Service payload for one MCCS Capabilities Request (F3). */
void rss_ddc_build_conventional_capabilities_request(
    uint16_t offset, uint8_t request[RSS_DDC_CONVENTIONAL_CAPABILITIES_REQUEST_SIZE]);
/** Builds the inline-0x51 form used only by a future evidence-backed raw transport. */
void rss_ddc_build_raw_capabilities_request(uint16_t offset,
                                            uint8_t request[RSS_DDC_RAW_CAPABILITIES_REQUEST_SIZE]);
/**
 * Strictly parses exactly one 11-byte Get VCP reply. On failure, `result`
 * remains untouched and the returned error identifies the rejected field.
 */
RSSDDCError rss_ddc_parse_get_vcp_reply(const uint8_t *reply, size_t byte_count,
                                        uint8_t requested_vcp, RSSDDCVCPResult *result);
/**
 * Strictly validates one E3 reply and returns its offset and string fragment.
 * The returned bytes alias `reply`; callers must copy them before reusing the
 * input buffer. This does not retrieve a monitor capability string itself.
 */
RSSDDCError rss_ddc_parse_capabilities_reply(const uint8_t *reply, size_t byte_count,
                                             RSSDDCCapabilitiesFragment *fragment);
/**
 * Derives the exact received frame size from a bounded IOAV reply window.
 * IOAVServiceReadI2C exposes no actual-byte-count out parameter, so callers
 * must validate this value before handing only that prefix to the strict parser.
 */
RSSDDCError rss_ddc_capabilities_reply_frame_size(const uint8_t *reply, size_t available_bytes,
                                                  size_t *frame_size);
/** Rejects a syntactically valid fragment whose monitor-echoed offset is unexpected. */
RSSDDCError rss_ddc_validate_capabilities_fragment_offset(const RSSDDCCapabilitiesFragment *fragment,
                                                          uint16_t requested_offset);

#endif
