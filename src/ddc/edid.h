#ifndef RSS_DDC_EDID_H
#define RSS_DDC_EDID_H

#include "rss_ddc.h"

/** Standard E-EDID location for one 128-byte block; segment writes are required only beyond block 1. */
typedef struct {
    uint8_t segment;
    uint8_t offset;
    bool requires_segment_pointer;
} RSSDDCEDIDBlockAddress;

bool rss_ddc_edid_block_checksum_valid(const uint8_t block[RSS_DDC_EDID_BLOCK_SIZE]);
bool rss_ddc_edid_block_address(size_t block_index, RSSDDCEDIDBlockAddress *address);

#endif
