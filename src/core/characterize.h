#ifndef RSS_DDC_CHARACTERIZE_H
#define RSS_DDC_CHARACTERIZE_H

#include <stddef.h>
#include <stdint.h>

#include "rss_ddc.h"

/*
 * Characterization orchestration. Internal, and hardware-free except
 * rss_ddc_characterization_prepare, which uses existing display/EDID lookup.
 * It does not restore monitor-knowledge/v0.1 JSON, retrieve MCCS, probe, or
 * call GET/SET VCP.
 */

typedef struct RSSDDCCharacterization RSSDDCCharacterization;

typedef enum {
    RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED = 0,
    RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED,
    RSS_DDC_CHARACTERIZATION_VALUE_CONFLICT
} RSSDDCCharacterizationValueState;

typedef enum {
    RSS_DDC_CHARACTERIZATION_PROFILE_NONE = 0,
    RSS_DDC_CHARACTERIZATION_PROFILE_MATCHED,
    RSS_DDC_CHARACTERIZATION_PROFILE_CONFLICT
} RSSDDCCharacterizationProfileStatus;

/** Allocates empty orchestration state that owns an empty knowledge object. */
RSSDDCCharacterization *rss_ddc_characterization_create(void);
void rss_ddc_characterization_destroy(RSSDDCCharacterization *characterization);

/**
 * Copies `semantic_id` into `out`, replacing a known profile/schema alias with
 * its canonical dotted ID. Matching is exact and case-sensitive. Unknown IDs
 * are copied unchanged. `out` is NUL-terminated on success.
 */
RSSDDCError rss_ddc_characterization_normalize_semantic_id(const char *semantic_id, char *out,
                                                           size_t capacity);

/**
 * Merges a copy of `knowledge` into the accumulated object after normalizing
 * each route's semantic ID. Competing facts are retained. On capacity overflow
 * the accumulated object is unchanged and RSS_DDC_ERROR_PROFILE_CONFLICT is
 * returned (the existing bounded-knowledge overflow status).
 */
RSSDDCError rss_ddc_characterization_add_knowledge(RSSDDCCharacterization *characterization,
                                                   const RSSDDCMonitorKnowledge *knowledge);

/** Borrowed accumulated knowledge; valid until the next mutating call or destroy. */
const RSSDDCMonitorKnowledge *rss_ddc_characterization_knowledge(
    const RSSDDCCharacterization *characterization);

/**
 * Resolves effective read/write methods for `semantic_id` (after normalization)
 * using the existing knowledge resolver. This is method authority, not current
 * value.
 */
RSSDDCError rss_ddc_characterization_resolve(const RSSDDCCharacterization *characterization,
                                             const char *semantic_id,
                                             RSSDDCMonitorKnowledgeResolution **resolution);

/**
 * Selects a live current value independently of method resolution. Slice 1
 * considers only OBSERVED routes with UNSIGNED or STRING values. UNKNOWN never
 * wins. Disagreeing observed values yield CONFLICT and no selected route.
 */
RSSDDCError rss_ddc_characterization_current_value(const RSSDDCCharacterization *characterization,
                                                   const char *semantic_id,
                                                   RSSDDCCharacterizationValueState *state,
                                                   const RSSDDCKnowledgeRoute **route);

/**
 * Pure Slice 2 assembly from an already-resolved display snapshot.
 * Copies identity, optional EDID, and provider capability bits; matches
 * `store` if non-NULL; normalizes profile control names and merges PROFILE
 * facts. Does not call list/get display, EDID read, MCCS, or probe.
 * `edid` and `store` may be NULL. Profile absence is non-fatal. Profile
 * resolve conflict returns RSS_DDC_ERROR_PROFILE_CONFLICT after identity
 * and transport bits are stored. Knowledge overflow returns the same status
 * without committing the new PROFILE facts.
 */
RSSDDCError rss_ddc_characterization_assemble(RSSDDCCharacterization *characterization,
                                              const RSSDDCDisplay *display,
                                              const RSSDDCEDIDInfo *edid,
                                              const RSSDDCProfileStore *store);

/**
 * Platform Slice 2 entry: resolves `list_index` with rss_ddc_get_display,
 * attaches EDID via rss_ddc_read_edid / rss_ddc_parse_edid when that path
 * succeeds, then calls assemble. Display resolution failure is fatal and
 * leaves characterization unchanged. EDID failure is non-fatal. Does not
 * retrieve MCCS or issue GET/SET VCP.
 */
RSSDDCError rss_ddc_characterization_prepare(RSSDDCCharacterization *characterization,
                                             uint32_t list_index, const RSSDDCProfileStore *store);

/** Copied display snapshot, or NULL before a successful assemble/prepare. */
const RSSDDCDisplay *rss_ddc_characterization_display(const RSSDDCCharacterization *characterization);

/** Copied EDID decode, or NULL when EDID was not supplied or not parsed. */
const RSSDDCEDIDInfo *rss_ddc_characterization_edid(const RSSDDCCharacterization *characterization);

/**
 * Bits from rss_ddc_provider_capabilities for the selected display's provider.
 * This is transport/platform capability, not monitor-advertised DECLARED
 * knowledge.
 */
uint32_t rss_ddc_characterization_provider_capabilities(const RSSDDCCharacterization *characterization);

/** Profile match outcome from the last assemble/prepare. */
RSSDDCCharacterizationProfileStatus rss_ddc_characterization_profile_status(
    const RSSDDCCharacterization *characterization);

/**
 * Identity derived from the selected display via
 * rss_ddc_profile_identity_from_display, or NULL before assemble/prepare.
 */
const RSSDDCProfileIdentity *rss_ddc_characterization_profile_identity(
    const RSSDDCCharacterization *characterization);

/** Effective matched profile, or NULL unless status is MATCHED. */
const RSSDDCEffectiveProfile *rss_ddc_characterization_effective_profile(
    const RSSDDCCharacterization *characterization);

#endif
