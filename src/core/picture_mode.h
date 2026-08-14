#ifndef RSS_DDC_PICTURE_MODE_H
#define RSS_DDC_PICTURE_MODE_H

#include <stdbool.h>
#include <stdint.h>

#include "rss_ddc.h"

/** Exact historical profile predicate; this does not infer support for other LG displays. */
bool rss_ddc_lg_hdr_qhd_picture_mode_profile_matches(const RSSDDCDisplay *display);
/** Returns the profile-owned capability bit, never a generic provider capability. */
uint32_t rss_ddc_picture_mode_profile_capabilities(const RSSDDCDisplay *display);
/** Applies the profile predicate plus the already-resolved platform safety gate. */
RSSDDCError rss_ddc_validate_lg_hdr_qhd_picture_mode_target(const RSSDDCDisplay *display, bool dp_safety_gate);
/** Maps only directly hardware-validated semantic modes to their VCP 0x15 values. */
RSSDDCError rss_ddc_lg_hdr_qhd_picture_mode_raw_value(RSSDDCPictureMode mode, uint16_t *raw_value);

#endif
