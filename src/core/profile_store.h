#ifndef RSS_DDC_PROFILE_STORE_H
#define RSS_DDC_PROFILE_STORE_H

#include "rss_ddc.h"

void rss_ddc_profile_identity_from_display(const RSSDDCDisplay *display, RSSDDCProfileIdentity *identity);
RSSDDCError rss_ddc_profile_store_resolve_builtin(const RSSDDCProfileIdentity *identity,
                                                  RSSDDCEffectiveProfile *effective);
RSSDDCError rss_ddc_profile_picture_mode_raw(const RSSDDCEffectiveProfile *effective, RSSDDCPictureMode mode,
                                             uint16_t *raw_value);
RSSDDCPictureMode rss_ddc_profile_picture_mode_from_raw(const RSSDDCEffectiveProfile *effective, uint16_t raw_value);

#endif
