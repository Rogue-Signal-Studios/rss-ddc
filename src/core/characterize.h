#ifndef RSS_DDC_CHARACTERIZE_H
#define RSS_DDC_CHARACTERIZE_H

#include <stddef.h>
#include <stdint.h>

#include "rss_ddc.h"

/*
 * Characterization orchestration. Internal, and hardware-free except
 * rss_ddc_characterization_prepare (display/EDID),
 * rss_ddc_characterization_collect_passive (MCCS retrieval), and
 * rss_ddc_characterization_collect_quick (Alien Probe Quick Auto Probe), and
 * rss_ddc_characterization_collect_extended (Alien Probe Extended Auto Probe).
 * It does not restore monitor-knowledge/v0.1 JSON or call SET VCP. Sufficiency
 * is a pure decision over current evidence. Extended runs only when recommended.
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

/**
 * Pure Slice 3 conversion: if provider bits include MCCS retrieval, copy
 * `capabilities` and merge one DECLARED route per advertised VCP. If MCCS
 * retrieval is not supported, returns OK, adds no DECLARED facts, and ignores
 * `capabilities`. DECLARED routes are never write_authorized. Advertised
 * enum lists stay on the retained MCCS model, not as extra knowledge routes.
 */
RSSDDCError rss_ddc_characterization_collect_passive_mccs(
    RSSDDCCharacterization *characterization, const RSSDDCMCCSCapabilities *capabilities);

/**
 * Parses `raw` with rss_ddc_parse_mccs_capabilities then collect_passive_mccs.
 * Parse failure is non-fatal: identity/profile knowledge is preserved, no
 * DECLARED facts are added, and the parse error is stored as MCCS status.
 */
RSSDDCError rss_ddc_characterization_collect_passive_mccs_raw(
    RSSDDCCharacterization *characterization, const char *raw, size_t raw_length);

/**
 * Records a non-fatal MCCS stage failure (unsupported or retrieval/parse
 * error) without adding DECLARED facts or clearing identity/profile state.
 */
RSSDDCError rss_ddc_characterization_collect_passive_mccs_failed(
    RSSDDCCharacterization *characterization, RSSDDCError status);

/**
 * Platform Slice 3 entry: if MCCS retrieval is unsupported, records that and
 * returns OK. Otherwise calls rss_ddc_get_mccs_capabilities for the assembled
 * display and collect_passive_mccs. Retrieval/parse failure is non-fatal.
 * Requires a prior assemble/prepare. Does not GET/SET VCP or probe.
 */
RSSDDCError rss_ddc_characterization_collect_passive(RSSDDCCharacterization *characterization);

/** True when assembled provider bits include RSS_DDC_CAP_MCCS_CAPABILITIES. */
bool rss_ddc_characterization_mccs_supported(const RSSDDCCharacterization *characterization);

/** True when this run attempted to apply or retrieve an MCCS document. */
bool rss_ddc_characterization_mccs_attempted(const RSSDDCCharacterization *characterization);

/** Last MCCS stage status: OK, UNSUPPORTED_CAPABILITY, or a retrieval/parse error. */
RSSDDCError rss_ddc_characterization_mccs_status(const RSSDDCCharacterization *characterization);

/** Borrowed parsed MCCS model, or NULL when none was successfully applied. */
const RSSDDCMCCSCapabilities *rss_ddc_characterization_mccs(
    const RSSDDCCharacterization *characterization);

/**
 * Ingests an already-run Alien Probe Quick Auto Probe (`rss_ddc_probe_quick`).
 * Copies diagnostics and merges probe knowledge through add_knowledge.
 * RESEARCH-tagged OBSERVED facts are treated as production live GET evidence.
 * If GET VCP is unsupported, returns OK with no OBSERVED facts.
 */
RSSDDCError rss_ddc_characterization_collect_quick_probe(RSSDDCCharacterization *characterization,
                                                         const RSSDDCProbe *probe);

/**
 * Records a non-fatal Quick Auto Probe stage failure without adding OBSERVED
 * facts or clearing identity/profile/DECLARED knowledge.
 */
RSSDDCError rss_ddc_characterization_collect_quick_probe_failed(
    RSSDDCCharacterization *characterization, RSSDDCError status);

/**
 * Platform Slice 4 entry: if GET VCP is unsupported, records that and returns
 * OK. Otherwise runs rss_ddc_probe_quick_for_display and
 * collect_quick_probe. Probe failure is non-fatal for prior characterization
 * state. Does not SET, verify, or run Extended Probe.
 */
RSSDDCError rss_ddc_characterization_collect_quick(RSSDDCCharacterization *characterization);

/** True when assembled provider bits include RSS_DDC_CAP_GET_VCP. */
bool rss_ddc_characterization_quick_supported(const RSSDDCCharacterization *characterization);

bool rss_ddc_characterization_quick_attempted(const RSSDDCCharacterization *characterization);

RSSDDCError rss_ddc_characterization_quick_status(const RSSDDCCharacterization *characterization);

/**
 * Borrowed Quick Auto Probe diagnostics; observation pointers are valid until
 * the next Quick collect or destroy. NULL before a Quick stage runs.
 */
const RSSDDCProbeDiagnostics *rss_ddc_characterization_quick_diagnostics(
    const RSSDDCCharacterization *characterization);

typedef enum {
    RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT = 0,
    RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT,
    RSS_DDC_CHARACTERIZATION_SUFFICIENCY_UNAVAILABLE,
    RSS_DDC_CHARACTERIZATION_SUFFICIENCY_CONFLICT
} RSSDDCCharacterizationSufficiency;

enum {
    RSS_DDC_CHARACTERIZATION_REASON_NONE = 0,
    RSS_DDC_CHARACTERIZATION_REASON_MISSING_CONTROL = 1u << 0,
    RSS_DDC_CHARACTERIZATION_REASON_UNRESOLVED_METHOD = 1u << 1,
    RSS_DDC_CHARACTERIZATION_REASON_CONFLICTING_METHOD = 1u << 2,
    RSS_DDC_CHARACTERIZATION_REASON_VARIABLE_OBSERVATION = 1u << 3,
    RSS_DDC_CHARACTERIZATION_REASON_NO_GET_SUPPORT = 1u << 4,
    RSS_DDC_CHARACTERIZATION_REASON_PROFILE_CONFLICT = 1u << 5,
    RSS_DDC_CHARACTERIZATION_REASON_PROBE_HELPFUL = 1u << 6
};

typedef struct {
    RSSDDCCharacterizationSufficiency status;
    uint32_t reasons;
    bool extended_recommended;
} RSSDDCCharacterizationSufficiencyResult;

/**
 * Pure DEFAULT-mode sufficiency over current identity, profile, MCCS, Quick
 * Auto Probe diagnostics, and merged knowledge. Does not run Extended Probe,
 * GET, or SET. Quick success alone is not sufficiency.
 */
RSSDDCError rss_ddc_characterization_sufficiency(
    const RSSDDCCharacterization *characterization,
    RSSDDCCharacterizationSufficiencyResult *result);

typedef struct {
    size_t considered;
    size_t promoted;
    size_t skipped_capacity;
    size_t skipped_nonpromotable;
} RSSDDCCharacterizationPromotionSummary;

/**
 * Records a non-fatal Extended Auto Probe stage failure without promoting
 * OBSERVED facts or clearing prior characterization knowledge.
 */
RSSDDCError rss_ddc_characterization_collect_extended_probe_failed(
    RSSDDCCharacterization *characterization, RSSDDCError status);

/**
 * Ingests an already-run Alien Probe Extended Auto Probe
 * (`rss_ddc_probe_extended`). Copies full diagnostics and selectively
 * promotes protocol-valid OBSERVED facts. Does not SET. If GET VCP is
 * unsupported, returns OK with no promotion.
 */
RSSDDCError rss_ddc_characterization_collect_extended_probe(
    RSSDDCCharacterization *characterization, const RSSDDCProbe *probe);

/**
 * Platform Slice 6 entry: runs Extended only when Slice 5 sufficiency
 * recommends it and GET VCP is available. Otherwise returns OK without
 * execution. Does not SET, verify, or run Guided Discovery.
 */
RSSDDCError rss_ddc_characterization_collect_extended(RSSDDCCharacterization *characterization);

bool rss_ddc_characterization_extended_attempted(const RSSDDCCharacterization *characterization);

RSSDDCError rss_ddc_characterization_extended_status(const RSSDDCCharacterization *characterization);

/**
 * Borrowed Extended Auto Probe diagnostics; observation pointers are valid
 * until the next Extended collect or destroy. NULL before an Extended stage
 * copies diagnostics.
 */
const RSSDDCProbeExtendedDiagnostics *rss_ddc_characterization_extended_diagnostics(
    const RSSDDCCharacterization *characterization);

const RSSDDCCharacterizationPromotionSummary *rss_ddc_characterization_extended_promotion(
    const RSSDDCCharacterization *characterization);

#endif
