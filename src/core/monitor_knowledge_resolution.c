#include "rss_ddc.h"

#include <stdlib.h>
#include <string.h>

struct RSSDDCMonitorKnowledgeResolution {
    char *semantic_id;
    RSSDDCAvailability availability;
    RSSDDCConfidence confidence;
    bool write_authorized;
    bool conflict;
    RSSDDCResolutionReason reason;
    const RSSDDCMonitorKnowledgeMethod *preferred_read;
    const RSSDDCMonitorKnowledgeMethod *preferred_write;
    const RSSDDCMonitorKnowledgeMethod **methods;
    size_t method_count;
    const char **conditions;
    size_t condition_count;
};

struct RSSDDCMonitorKnowledgeValueResolution {
    char *id;
    const RSSDDCMonitorKnowledgeValue **values;
    size_t value_count;
    const RSSDDCMonitorKnowledgeValue *preferred_read;
    const RSSDDCMonitorKnowledgeValue *preferred_write;
    bool write_authorized;
    bool conflict;
};

struct RSSDDCMonitorKnowledgeRangeResolution {
    RSSDDCRange advertised, observed, validated, write_range;
    bool has_advertised, has_observed, has_validated, has_write_range, conflict;
};

struct RSSDDCInputRouteResolution {
    RSSDDCInputRoute *routes;
    size_t route_count;
    const RSSDDCInputRoute *preferred_read;
    const RSSDDCInputRoute *preferred_switch;
    size_t preferred_read_index;
    size_t preferred_switch_index;
    bool switch_authorized;
    bool conflict;
};

static char *copy_text(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

static bool empty(const char *value) { return value == NULL || value[0] == '\0'; }
static bool compatible_field(const char *left, const char *right) {
    return empty(left) || empty(right) || strcmp(left, right) == 0;
}

static bool identities_compatible(const RSSDDCMonitorIdentity *left, const RSSDDCMonitorIdentity *right) {
    if (!compatible_field(left->manufacturer, right->manufacturer) ||
        !compatible_field(left->model, right->model) ||
        !compatible_field(left->edid_manufacturer, right->edid_manufacturer) ||
        !compatible_field(left->provider, right->provider) ||
        !compatible_field(left->transport, right->transport) ||
        !compatible_field(left->branch, right->branch) ||
        !compatible_field(left->family_hint, right->family_hint)) return false;
    return !left->edid_product_code_present || !right->edid_product_code_present ||
           left->edid_product_code == right->edid_product_code;
}

static unsigned int specificity(const RSSDDCMonitorIdentity *identity) {
    unsigned int score = 0;
    score += !empty(identity->manufacturer);
    score += !empty(identity->model) * 2u;
    score += !empty(identity->provider);
    score += !empty(identity->transport);
    score += !empty(identity->branch);
    score += identity->edid_product_code_present * 3u;
    score += !empty(identity->serial) * 4u;
    return score;
}

static unsigned int evidence_authority(const RSSDDCEvidence *evidence, size_t count) {
    unsigned int best = 0;
    for (size_t index = 0; index < count; ++index) {
        unsigned int value = 0;
        switch (evidence[index].type) {
            case RSS_DDC_EVIDENCE_SET_CONFIRMED: value = 8; break;
            case RSS_DDC_EVIDENCE_LOCAL_VALIDATED: value = 7; break;
            case RSS_DDC_EVIDENCE_ROGUE_VALIDATED_PROFILE: value = 7; break;
            case RSS_DDC_EVIDENCE_PROFILE_KNOWN: value = 5; break;
            case RSS_DDC_EVIDENCE_STABLE_GET: value = 4; break;
            case RSS_DDC_EVIDENCE_MCCS_ADVERTISED: value = 3; break;
            case RSS_DDC_EVIDENCE_STANDARD_DEFINED: value = 2; break;
            case RSS_DDC_EVIDENCE_EXTERNAL_CANDIDATE:
            case RSS_DDC_EVIDENCE_MANUFACTURER_FAMILY_HINT:
            case RSS_DDC_EVIDENCE_MODEL_FAMILY_HINT: value = 1; break;
            default: value = 2; break;
        }
        if (value > best) best = value;
    }
    return best;
}

static bool has_strong_write_evidence(const RSSDDCEvidence *evidence, size_t count) {
    for (size_t index = 0; index < count; ++index)
        if (evidence[index].type == RSS_DDC_EVIDENCE_SET_CONFIRMED ||
            evidence[index].type == RSS_DDC_EVIDENCE_LOCAL_VALIDATED ||
            evidence[index].type == RSS_DDC_EVIDENCE_ROGUE_VALIDATED_PROFILE) return true;
    return false;
}

static bool external_only(const RSSDDCEvidence *evidence, size_t count) {
    if (count == 0) return false;
    for (size_t index = 0; index < count; ++index)
        if (evidence[index].type != RSS_DDC_EVIDENCE_EXTERNAL_CANDIDATE &&
            evidence[index].type != RSS_DDC_EVIDENCE_MANUFACTURER_FAMILY_HINT &&
            evidence[index].type != RSS_DDC_EVIDENCE_MODEL_FAMILY_HINT &&
            evidence[index].type != RSS_DDC_EVIDENCE_OSD_CORRELATED) return false;
    return true;
}

static bool same_method(const RSSDDCMonitorKnowledgeMethod *left, const RSSDDCMonitorKnowledgeMethod *right) {
    return left->type == right->type && left->vcp_code == right->vcp_code &&
           ((!left->protocol_id && !right->protocol_id) || (left->protocol_id && right->protocol_id && strcmp(left->protocol_id, right->protocol_id) == 0)) &&
           ((!left->address && !right->address) || (left->address && right->address && strcmp(left->address, right->address) == 0));
}
static bool raw_equal(const RSSDDCRawValue *left, const RSSDDCRawValue *right) {
    if (left->type != right->type) return false;
    if (left->type == RSS_DDC_RAW_UNSIGNED) return left->unsigned_value == right->unsigned_value;
    if (left->type == RSS_DDC_RAW_SIGNED) return left->signed_value == right->signed_value;
    return left->data_length == right->data_length &&
           (left->data_length == 0 || memcmp(left->data, right->data, left->data_length) == 0);
}
static bool range_equal(const RSSDDCRange *left, const RSSDDCRange *right) {
    return left->minimum == right->minimum && left->maximum == right->maximum && left->step == right->step &&
           compatible_field(left->units, right->units);
}
static unsigned int capability_score(const RSSDDCMonitorIdentity *identity, const RSSDDCMonitorKnowledgeCapability *capability) {
    return specificity(identity) * 10000u + evidence_authority(capability->evidence, capability->evidence_count) * 100u +
           (unsigned int)capability->confidence * 10u;
}
static bool value_write_evidence(const RSSDDCMonitorKnowledgeValue *value) {
    return value->validation == RSS_DDC_VALIDATION_SET_CONFIRMED || value->validation == RSS_DDC_VALIDATION_HARDWARE_VALIDATED ||
           has_strong_write_evidence(value->evidence, value->evidence_count);
}

static unsigned int read_risk(RSSDDCRisk risk) {
    return risk == RSS_DDC_RISK_READ_STANDARD ? 3u : risk == RSS_DDC_RISK_READ_EXTENDED ? 2u : risk == RSS_DDC_RISK_GUIDED_READ ? 1u : 0u;
}

static unsigned int score(const RSSDDCMonitorIdentity *identity, const RSSDDCMonitorKnowledgeCapability *capability,
                          const RSSDDCMonitorKnowledgeMethod *method, bool write) {
    unsigned int authority = evidence_authority(method->evidence, method->evidence_count);
    if (authority == 0) authority = evidence_authority(capability->evidence, capability->evidence_count);
    return specificity(identity) * 10000u + authority * 100u + (unsigned int)method->confidence * 10u +
           (write ? 0u : read_risk(method->risk));
}

static bool write_allowed(const RSSDDCMonitorKnowledgeCapability *capability, const RSSDDCMonitorKnowledgeMethod *method,
                          RSSDDCResolutionReason *reason) {
    if (!method->writable) return false;
    if (method->risk == RSS_DDC_RISK_HIGH_RISK_DENIED) { *reason = RSS_DDC_RESOLUTION_REASON_RISK_DENIED; return false; }
    if (method->risk == RSS_DDC_RISK_VENDOR_EXPERIMENTAL_SET) { *reason = RSS_DDC_RESOLUTION_REASON_EXPERIMENTAL_ONLY; return false; }
    bool strong = has_strong_write_evidence(method->evidence, method->evidence_count) ||
                  has_strong_write_evidence(capability->evidence, capability->evidence_count);
    if (!strong) {
        *reason = external_only(method->evidence, method->evidence_count) || external_only(capability->evidence, capability->evidence_count)
                      ? RSS_DDC_RESOLUTION_REASON_EXTERNAL_CANDIDATE_ONLY : RSS_DDC_RESOLUTION_REASON_NO_WRITE_EVIDENCE;
        return false;
    }
    return true;
}

RSSDDCError rss_ddc_monitor_knowledge_resolve_capability(const RSSDDCMonitorKnowledge *const *sources,
                                                          size_t source_count, const char *semantic_id,
                                                          RSSDDCMonitorKnowledgeResolution **output) {
    if (sources == NULL || source_count == 0 || semantic_id == NULL || output == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *output = NULL;
    RSSDDCMonitorKnowledgeResolution *result = calloc(1, sizeof(*result));
    if (result == NULL) return RSS_DDC_ERROR_SYSTEM;
    result->semantic_id = copy_text(semantic_id);
    if (result->semantic_id == NULL) { free(result); return RSS_DDC_ERROR_SYSTEM; }
    RSSDDCMonitorIdentity anchor = {};
    bool have_anchor = false, found = false;
    unsigned int read_score = 0, write_score = 0;
    for (size_t source_index = 0; source_index < source_count; ++source_index) {
        if (sources[source_index] == NULL) continue;
        RSSDDCMonitorIdentity identity = {};
        if (rss_ddc_monitor_knowledge_identity(sources[source_index], &identity) != RSS_DDC_OK) continue;
        if (!have_anchor) { anchor = identity; have_anchor = true; }
        else if (!identities_compatible(&anchor, &identity)) { result->conflict = true; result->reason = RSS_DDC_RESOLUTION_REASON_IDENTITY_INCOMPATIBLE; continue; }
        RSSDDCMonitorKnowledgeCapability capability = {};
        if (rss_ddc_monitor_knowledge_find_capability(sources[source_index], semantic_id, &capability) != RSS_DDC_OK) continue;
        found = true;
        if (capability.confidence > result->confidence) result->confidence = capability.confidence;
        if (capability.availability > result->availability) result->availability = capability.availability;
        if (!empty(capability.conditions)) {
            bool duplicate = false;
            for (size_t condition_index = 0; condition_index < result->condition_count; ++condition_index)
                if (strcmp(result->conditions[condition_index], capability.conditions) == 0) duplicate = true;
            if (!duplicate) {
                const char **conditions = realloc(result->conditions, (result->condition_count + 1) * sizeof(*conditions));
                if (conditions == NULL) { rss_ddc_monitor_knowledge_resolution_destroy(result); return RSS_DDC_ERROR_SYSTEM; }
                result->conditions = conditions;
                result->conditions[result->condition_count++] = capability.conditions;
            }
        }
        for (size_t method_index = 0; method_index < capability.method_count; ++method_index) {
            const RSSDDCMonitorKnowledgeMethod *method = &capability.methods[method_index];
            bool duplicate = false;
            for (size_t existing = 0; existing < result->method_count; ++existing) if (same_method(method, result->methods[existing])) { duplicate = true; break; }
            if (!duplicate) {
                const RSSDDCMonitorKnowledgeMethod **methods = realloc(result->methods, (result->method_count + 1) * sizeof(*methods));
                if (methods == NULL) { rss_ddc_monitor_knowledge_resolution_destroy(result); return RSS_DDC_ERROR_SYSTEM; }
                result->methods = methods;
                result->methods[result->method_count++] = method;
            }
            if (method->readable) {
                unsigned int candidate_score = score(&identity, &capability, method, false);
                if (result->preferred_read != NULL && candidate_score == read_score && !same_method(method, result->preferred_read)) result->conflict = true;
                if (result->preferred_read == NULL || candidate_score > read_score) { result->preferred_read = method; read_score = candidate_score; }
            }
            RSSDDCResolutionReason denied = RSS_DDC_RESOLUTION_REASON_NO_WRITE_EVIDENCE;
            if (write_allowed(&capability, method, &denied)) {
                unsigned int candidate_score = score(&identity, &capability, method, true);
                if (result->preferred_write != NULL && candidate_score == write_score && !same_method(method, result->preferred_write)) {
                    result->conflict = true; result->preferred_write = NULL; result->write_authorized = false;
                    result->reason = RSS_DDC_RESOLUTION_REASON_EQUAL_AUTHORITY_CONFLICT;
                } else if (result->preferred_write == NULL || candidate_score > write_score) {
                    result->preferred_write = method; write_score = candidate_score; result->write_authorized = true;
                }
            } else if (result->reason == RSS_DDC_RESOLUTION_REASON_NONE) result->reason = denied;
        }
    }
    if (!found) { rss_ddc_monitor_knowledge_resolution_destroy(result); return RSS_DDC_ERROR_NOT_FOUND; }
    if (result->preferred_read == NULL && result->reason == RSS_DDC_RESOLUTION_REASON_NONE) result->reason = RSS_DDC_RESOLUTION_REASON_NO_READ_METHOD;
    if (result->conflict && result->preferred_write == NULL) result->write_authorized = false;
    *output = result;
    return RSS_DDC_OK;
}

void rss_ddc_monitor_knowledge_resolution_destroy(RSSDDCMonitorKnowledgeResolution *resolution) { if (resolution != NULL) { free(resolution->semantic_id); free(resolution->methods); free(resolution->conditions); free(resolution); } }
const char *rss_ddc_monitor_knowledge_resolution_semantic_id(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution == NULL ? NULL : resolution->semantic_id; }
RSSDDCAvailability rss_ddc_monitor_knowledge_resolution_availability(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution == NULL ? RSS_DDC_AVAILABILITY_UNKNOWN : resolution->availability; }
RSSDDCConfidence rss_ddc_monitor_knowledge_resolution_confidence(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution == NULL ? RSS_DDC_CONFIDENCE_UNKNOWN : resolution->confidence; }
bool rss_ddc_monitor_knowledge_resolution_write_authorized(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution != NULL && resolution->write_authorized; }
bool rss_ddc_monitor_knowledge_resolution_has_conflict(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution != NULL && resolution->conflict; }
RSSDDCResolutionReason rss_ddc_monitor_knowledge_resolution_reason(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution == NULL ? RSS_DDC_RESOLUTION_REASON_NONE : resolution->reason; }
const RSSDDCMonitorKnowledgeMethod *rss_ddc_monitor_knowledge_resolution_preferred_read(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution == NULL ? NULL : resolution->preferred_read; }
const RSSDDCMonitorKnowledgeMethod *rss_ddc_monitor_knowledge_resolution_preferred_write(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution == NULL ? NULL : resolution->preferred_write; }
size_t rss_ddc_monitor_knowledge_resolution_method_count(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution == NULL ? 0 : resolution->method_count; }
RSSDDCError rss_ddc_monitor_knowledge_resolution_method(const RSSDDCMonitorKnowledgeResolution *resolution, size_t index, const RSSDDCMonitorKnowledgeMethod **method) { if (resolution == NULL || method == NULL) return RSS_DDC_ERROR_ARGUMENT; if (index >= resolution->method_count) return RSS_DDC_ERROR_NOT_FOUND; *method = resolution->methods[index]; return RSS_DDC_OK; }
size_t rss_ddc_monitor_knowledge_resolution_condition_count(const RSSDDCMonitorKnowledgeResolution *resolution) { return resolution == NULL ? 0 : resolution->condition_count; }
RSSDDCError rss_ddc_monitor_knowledge_resolution_condition(const RSSDDCMonitorKnowledgeResolution *resolution, size_t index, const char **condition) { if (resolution == NULL || condition == NULL) return RSS_DDC_ERROR_ARGUMENT; if (index >= resolution->condition_count) return RSS_DDC_ERROR_NOT_FOUND; *condition = resolution->conditions[index]; return RSS_DDC_OK; }

RSSDDCError rss_ddc_monitor_knowledge_resolve_value(const RSSDDCMonitorKnowledge *const *sources, size_t source_count,
                                                     const char *semantic_id, const char *value_id,
                                                     RSSDDCMonitorKnowledgeValueResolution **output) {
    if (sources == NULL || source_count == 0 || semantic_id == NULL || value_id == NULL || output == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *output = NULL;
    RSSDDCMonitorKnowledgeResolution *capability_resolution = NULL;
    RSSDDCError error = rss_ddc_monitor_knowledge_resolve_capability(sources, source_count, semantic_id, &capability_resolution);
    if (error != RSS_DDC_OK) return error;
    RSSDDCMonitorKnowledgeValueResolution *result = calloc(1, sizeof(*result));
    if (result == NULL) { rss_ddc_monitor_knowledge_resolution_destroy(capability_resolution); return RSS_DDC_ERROR_SYSTEM; }
    result->id = copy_text(value_id);
    if (result->id == NULL) { free(result); rss_ddc_monitor_knowledge_resolution_destroy(capability_resolution); return RSS_DDC_ERROR_SYSTEM; }
    RSSDDCMonitorIdentity anchor = {};
    bool have_anchor = false, found = false;
    unsigned int read_score = 0, write_score = 0;
    for (size_t source_index = 0; source_index < source_count; ++source_index) {
        RSSDDCMonitorIdentity identity = {};
        RSSDDCMonitorKnowledgeCapability capability = {};
        if (sources[source_index] == NULL || rss_ddc_monitor_knowledge_identity(sources[source_index], &identity) != RSS_DDC_OK ||
            rss_ddc_monitor_knowledge_find_capability(sources[source_index], semantic_id, &capability) != RSS_DDC_OK) continue;
        if (!have_anchor) { anchor = identity; have_anchor = true; }
        if (!identities_compatible(&anchor, &identity)) { result->conflict = true; continue; }
        unsigned int source_score = capability_score(&identity, &capability);
        for (size_t index = 0; index < capability.value_count; ++index) {
            const RSSDDCMonitorKnowledgeValue *value = &capability.values[index];
            if (strcmp(value->id, value_id) != 0) continue;
            found = true;
            bool duplicate = false;
            for (size_t existing = 0; existing < result->value_count; ++existing)
                if (raw_equal(&value->raw, &result->values[existing]->raw)) duplicate = true;
            if (!duplicate) {
                const RSSDDCMonitorKnowledgeValue **values = realloc(result->values, (result->value_count + 1) * sizeof(*values));
                if (values == NULL) { rss_ddc_monitor_knowledge_value_resolution_destroy(result); rss_ddc_monitor_knowledge_resolution_destroy(capability_resolution); return RSS_DDC_ERROR_SYSTEM; }
                result->values = values; result->values[result->value_count++] = value;
            }
            unsigned int value_score = source_score + evidence_authority(value->evidence, value->evidence_count);
            if (value->readable && (result->preferred_read == NULL || value_score > read_score)) { result->preferred_read = value; read_score = value_score; }
            bool allowed = rss_ddc_monitor_knowledge_resolution_write_authorized(capability_resolution) && value->writable && value_write_evidence(value);
            if (allowed) {
                if (result->preferred_write != NULL && value_score == write_score && !raw_equal(&value->raw, &result->preferred_write->raw)) {
                    result->conflict = true; result->preferred_write = NULL; result->write_authorized = false;
                } else if (result->preferred_write == NULL || value_score > write_score) {
                    result->preferred_write = value; write_score = value_score; result->write_authorized = true;
                }
            }
        }
    }
    rss_ddc_monitor_knowledge_resolution_destroy(capability_resolution);
    if (!found) { rss_ddc_monitor_knowledge_value_resolution_destroy(result); return RSS_DDC_ERROR_NOT_FOUND; }
    *output = result;
    return RSS_DDC_OK;
}
void rss_ddc_monitor_knowledge_value_resolution_destroy(RSSDDCMonitorKnowledgeValueResolution *resolution) { if (resolution != NULL) { free(resolution->id); free(resolution->values); free(resolution); } }
const char *rss_ddc_monitor_knowledge_value_resolution_id(const RSSDDCMonitorKnowledgeValueResolution *resolution) { return resolution == NULL ? NULL : resolution->id; }
size_t rss_ddc_monitor_knowledge_value_resolution_candidate_count(const RSSDDCMonitorKnowledgeValueResolution *resolution) { return resolution == NULL ? 0 : resolution->value_count; }
RSSDDCError rss_ddc_monitor_knowledge_value_resolution_candidate(const RSSDDCMonitorKnowledgeValueResolution *resolution, size_t index, const RSSDDCMonitorKnowledgeValue **value) { if (resolution == NULL || value == NULL) return RSS_DDC_ERROR_ARGUMENT; if (index >= resolution->value_count) return RSS_DDC_ERROR_NOT_FOUND; *value = resolution->values[index]; return RSS_DDC_OK; }
const RSSDDCMonitorKnowledgeValue *rss_ddc_monitor_knowledge_value_resolution_preferred_read(const RSSDDCMonitorKnowledgeValueResolution *resolution) { return resolution == NULL ? NULL : resolution->preferred_read; }
const RSSDDCMonitorKnowledgeValue *rss_ddc_monitor_knowledge_value_resolution_preferred_write(const RSSDDCMonitorKnowledgeValueResolution *resolution) { return resolution == NULL ? NULL : resolution->preferred_write; }
bool rss_ddc_monitor_knowledge_value_resolution_write_authorized(const RSSDDCMonitorKnowledgeValueResolution *resolution) { return resolution != NULL && resolution->write_authorized; }
bool rss_ddc_monitor_knowledge_value_resolution_has_conflict(const RSSDDCMonitorKnowledgeValueResolution *resolution) { return resolution != NULL && resolution->conflict; }

static void select_range(RSSDDCRange *target, bool *present, const RSSDDCRange *candidate, unsigned int candidate_score,
                         unsigned int *selected_score, bool *conflict) {
    if (!candidate->present) return;
    if (!*present || candidate_score > *selected_score) { *target = *candidate; *present = true; *selected_score = candidate_score; return; }
    if (candidate_score == *selected_score && !range_equal(target, candidate)) *conflict = true;
}

RSSDDCError rss_ddc_monitor_knowledge_resolve_range(const RSSDDCMonitorKnowledge *const *sources, size_t source_count,
                                                     const char *semantic_id, RSSDDCMonitorKnowledgeRangeResolution **output) {
    if (sources == NULL || source_count == 0 || semantic_id == NULL || output == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *output = NULL;
    RSSDDCMonitorKnowledgeRangeResolution *result = calloc(1, sizeof(*result));
    if (result == NULL) return RSS_DDC_ERROR_SYSTEM;
    RSSDDCMonitorIdentity anchor = {}; bool have_anchor = false, found = false;
    unsigned int advertised_score = 0, observed_score = 0, validated_score = 0;
    for (size_t index = 0; index < source_count; ++index) {
        RSSDDCMonitorIdentity identity = {}; RSSDDCMonitorKnowledgeCapability capability = {};
        if (sources[index] == NULL || rss_ddc_monitor_knowledge_identity(sources[index], &identity) != RSS_DDC_OK ||
            rss_ddc_monitor_knowledge_find_capability(sources[index], semantic_id, &capability) != RSS_DDC_OK) continue;
        if (!have_anchor) { anchor = identity; have_anchor = true; }
        if (!identities_compatible(&anchor, &identity)) { result->conflict = true; continue; }
        found = true; unsigned int source_score = capability_score(&identity, &capability);
        select_range(&result->advertised, &result->has_advertised, &capability.advertised_range, source_score, &advertised_score, &result->conflict);
        select_range(&result->observed, &result->has_observed, &capability.observed_range, source_score, &observed_score, &result->conflict);
        select_range(&result->validated, &result->has_validated, &capability.validated_range, source_score, &validated_score, &result->conflict);
    }
    if (!found) { free(result); return RSS_DDC_ERROR_NOT_FOUND; }
    if (result->has_validated && !result->conflict) { result->write_range = result->validated; result->has_write_range = true; }
    *output = result; return RSS_DDC_OK;
}
void rss_ddc_monitor_knowledge_range_resolution_destroy(RSSDDCMonitorKnowledgeRangeResolution *resolution) { free(resolution); }
bool rss_ddc_monitor_knowledge_range_resolution_advertised(const RSSDDCMonitorKnowledgeRangeResolution *r, RSSDDCRange *out) { if (!r || !out || !r->has_advertised) return false; *out = r->advertised; return true; }
bool rss_ddc_monitor_knowledge_range_resolution_observed(const RSSDDCMonitorKnowledgeRangeResolution *r, RSSDDCRange *out) { if (!r || !out || !r->has_observed) return false; *out = r->observed; return true; }
bool rss_ddc_monitor_knowledge_range_resolution_validated(const RSSDDCMonitorKnowledgeRangeResolution *r, RSSDDCRange *out) { if (!r || !out || !r->has_validated) return false; *out = r->validated; return true; }
bool rss_ddc_monitor_knowledge_range_resolution_write_range(const RSSDDCMonitorKnowledgeRangeResolution *r, RSSDDCRange *out) { if (!r || !out || !r->has_write_range) return false; *out = r->write_range; return true; }
bool rss_ddc_monitor_knowledge_range_resolution_has_conflict(const RSSDDCMonitorKnowledgeRangeResolution *r) { return r != NULL && r->conflict; }

RSSDDCError rss_ddc_monitor_knowledge_resolve_input_route(const RSSDDCMonitorKnowledge *const *sources, size_t source_count,
                                                           const char *route_id, RSSDDCInputRouteResolution **output) {
    if (sources == NULL || source_count == 0 || route_id == NULL || output == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *output = NULL;
    RSSDDCMonitorKnowledgeResolution *capability = NULL;
    RSSDDCError error = rss_ddc_monitor_knowledge_resolve_capability(sources, source_count, "inputs.switching", &capability);
    bool method_authorized = error == RSS_DDC_OK && rss_ddc_monitor_knowledge_resolution_write_authorized(capability);
    RSSDDCInputRouteResolution *result = calloc(1, sizeof(*result));
    if (result == NULL) { rss_ddc_monitor_knowledge_resolution_destroy(capability); return RSS_DDC_ERROR_SYSTEM; }
    RSSDDCMonitorIdentity anchor = {}; bool have_anchor = false, found = false;
    unsigned int read_score = 0, switch_score = 0;
    for (size_t source_index = 0; source_index < source_count; ++source_index) {
        RSSDDCMonitorIdentity identity = {};
        if (sources[source_index] == NULL || rss_ddc_monitor_knowledge_identity(sources[source_index], &identity) != RSS_DDC_OK) continue;
        if (!have_anchor) { anchor = identity; have_anchor = true; }
        if (!identities_compatible(&anchor, &identity)) { result->conflict = true; continue; }
        unsigned int identity_score = specificity(&identity) * 10000u;
        size_t route_count = rss_ddc_monitor_knowledge_input_route_count(sources[source_index]);
        for (size_t index = 0; index < route_count; ++index) {
            RSSDDCInputRoute route = {};
            if (rss_ddc_monitor_knowledge_input_route(sources[source_index], index, &route) != RSS_DDC_OK || strcmp(route.id, route_id) != 0) continue;
            found = true;
            bool duplicate = false;
            for (size_t existing = 0; existing < result->route_count; ++existing)
                if (raw_equal(&route.switch_value, &result->routes[existing].switch_value)) duplicate = true;
            if (!duplicate) {
                RSSDDCInputRoute *routes = realloc(result->routes, (result->route_count + 1) * sizeof(*routes));
                if (!routes) { rss_ddc_input_route_resolution_destroy(result); rss_ddc_monitor_knowledge_resolution_destroy(capability); return RSS_DDC_ERROR_SYSTEM; }
                result->routes = routes; result->routes[result->route_count++] = route;
            }
            unsigned int route_score = identity_score + evidence_authority(route.evidence, route.evidence_count) * 100u + route.confidence;
            if (route.current_readable && (result->preferred_read == NULL || route_score > read_score)) { result->preferred_read = (const RSSDDCInputRoute *)1; result->preferred_read_index = result->route_count - 1; read_score = route_score; }
            bool allowed = method_authorized && route.switching_supported && has_strong_write_evidence(route.evidence, route.evidence_count);
            if (allowed && (result->preferred_switch == NULL || route_score > switch_score)) { result->preferred_switch = (const RSSDDCInputRoute *)1; result->preferred_switch_index = result->route_count - 1; switch_score = route_score; result->switch_authorized = true; }
        }
    }
    rss_ddc_monitor_knowledge_resolution_destroy(capability);
    if (!found) { rss_ddc_input_route_resolution_destroy(result); return RSS_DDC_ERROR_NOT_FOUND; }
    if (result->preferred_read != NULL) result->preferred_read = &result->routes[result->preferred_read_index];
    if (result->preferred_switch != NULL) result->preferred_switch = &result->routes[result->preferred_switch_index];
    *output = result; return RSS_DDC_OK;
}
void rss_ddc_input_route_resolution_destroy(RSSDDCInputRouteResolution *resolution) { if (resolution) { free(resolution->routes); free(resolution); } }
size_t rss_ddc_input_route_resolution_candidate_count(const RSSDDCInputRouteResolution *resolution) { return resolution ? resolution->route_count : 0; }
RSSDDCError rss_ddc_input_route_resolution_candidate(const RSSDDCInputRouteResolution *resolution, size_t index, const RSSDDCInputRoute **route) { if (!resolution || !route) return RSS_DDC_ERROR_ARGUMENT; if (index >= resolution->route_count) return RSS_DDC_ERROR_NOT_FOUND; *route = &resolution->routes[index]; return RSS_DDC_OK; }
const RSSDDCInputRoute *rss_ddc_input_route_resolution_preferred_read(const RSSDDCInputRouteResolution *resolution) { return resolution ? resolution->preferred_read : NULL; }
const RSSDDCInputRoute *rss_ddc_input_route_resolution_preferred_switch(const RSSDDCInputRouteResolution *resolution) { return resolution ? resolution->preferred_switch : NULL; }
bool rss_ddc_input_route_resolution_switch_authorized(const RSSDDCInputRouteResolution *resolution) { return resolution && resolution->switch_authorized; }
bool rss_ddc_input_route_resolution_has_conflict(const RSSDDCInputRouteResolution *resolution) { return resolution && resolution->conflict; }
