#ifndef RSS_DDC_EDID_H
#define RSS_DDC_EDID_H

#include "rss_ddc.h"

bool rss_ddc_edid_block_checksum_valid(const uint8_t block[RSS_DDC_EDID_BLOCK_SIZE]);

#endif
