#ifndef RSS_DDC_PROTOCOL_H
#define RSS_DDC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "rss_ddc.h"

enum {
    RSS_DDC_GET_VCP_REQUEST_SIZE = 5,
    RSS_DDC_GET_VCP_REPLY_SIZE = 11,
};

uint8_t rss_ddc_raw_request_checksum(const uint8_t *bytes, size_t byte_count);
void rss_ddc_build_raw_get_vcp(uint8_t vcp_code, uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE]);
RSSDDCError rss_ddc_parse_get_vcp_reply(const uint8_t *reply, size_t byte_count,
                                        uint8_t requested_vcp, RSSDDCVCPResult *result);

#endif
