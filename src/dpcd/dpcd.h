#ifndef RSS_DDC_DPCD_H
#define RSS_DDC_DPCD_H

#include "rss_ddc.h"

/** Validates the exact single-read range accepted by the portable DPCD API. */
RSSDDCError rss_ddc_validate_dpcd_request(uint32_t address, const uint8_t *buffer, size_t length);

/** Registry-only candidate outcome; it intentionally says nothing about IODP construction or transport success. */
typedef enum {
    RSS_DDC_DPCD_PATH_UNAVAILABLE = 0,
    RSS_DDC_DPCD_PATH_CANDIDATE,
    RSS_DDC_DPCD_PATH_AMBIGUOUS,
} RSSDDCDPCDPathStatus;

RSSDDCDPCDPathStatus rss_ddc_dpcd_path_status_for_candidate_count(unsigned int candidate_count);

#endif
