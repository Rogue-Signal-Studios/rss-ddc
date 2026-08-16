#ifndef RSS_DDC_CLI_PROFILE_UPDATE_H
#define RSS_DDC_CLI_PROFILE_UPDATE_H

#include <stdbool.h>
#include <stdio.h>

#include "output_settings.h"
#include "rss_ddc.h"

/** Returns a static uppercase status label for CLI reports. */
const char *rss_ddc_cli_profile_update_status_name(RSSDDCCharacterizationProfileUpdateStatus status);

/**
 * CREATED and UPDATED persist a LOCAL overlay. UNCHANGED, UNSUPPORTED, and
 * CONFLICT must not rewrite or create a profile file.
 */
bool rss_ddc_cli_profile_update_should_save(RSSDDCCharacterizationProfileUpdateStatus status);

/**
 * Writes LOCAL overlay JSON only when `should_save` is true. Sets `*written`
 * when a file is actually saved. Does not contact the monitor.
 */
RSSDDCError rss_ddc_cli_profile_update_save_if_needed(const RSSDDCProfileStore *store,
                                                      RSSDDCCharacterizationProfileUpdateStatus status,
                                                      const char *path, bool *written);

/**
 * Compact profile-update report. Presentation only: it does not characterize,
 * mutate a store, or write files. `effective` may be NULL. `saved_path` is
 * printed only when non-NULL.
 */
void rss_ddc_cli_render_profile_update(FILE *stream, const RSSDDCCharacterization *characterization,
                                       const RSSDDCCharacterizationProfileUpdateResult *update,
                                       const RSSDDCEffectiveProfile *effective, const char *saved_path,
                                       const RSSDDCCliEffectiveOutput *output);

#endif
