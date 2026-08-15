#ifndef RSS_DDC_CHARACTERIZE_H
#define RSS_DDC_CHARACTERIZE_H

#include <stddef.h>

#include "rss_ddc.h"

/*
 * Slice 1 characterization orchestration. Internal, offline, and hardware-free.
 * It composes existing RSSDDCMonitorKnowledge objects; it does not restore
 * monitor-knowledge/v0.1 JSON or call GET/SET.
 */

typedef struct RSSDDCCharacterization RSSDDCCharacterization;

typedef enum {
    RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED = 0,
    RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED,
    RSS_DDC_CHARACTERIZATION_VALUE_CONFLICT
} RSSDDCCharacterizationValueState;

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

#endif
