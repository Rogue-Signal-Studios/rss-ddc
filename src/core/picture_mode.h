#ifndef RSS_DDC_PICTURE_MODE_H
#define RSS_DDC_PICTURE_MODE_H

#include <stdbool.h>
#include <stdint.h>

#include "rss_ddc.h"

/* Internal profile evidence and raw mapping; never installed as part of the public API. */
bool rss_ddc_lg_hdr_qhd_picture_mode_profile_matches(const RSSDDCDisplay *display);
uint32_t rss_ddc_picture_mode_profile_capabilities(const RSSDDCDisplay *display);
RSSDDCError rss_ddc_lg_hdr_qhd_picture_mode_raw_value(RSSDDCPictureMode mode, uint16_t *raw_value);
RSSDDCPictureMode rss_ddc_lg_hdr_qhd_picture_mode_from_raw(uint16_t raw_value);

#endif
