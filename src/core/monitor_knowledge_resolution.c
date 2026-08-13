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

void rss_ddc_monitor_knowledge_resolution_destroy(RSSDDCMonitorKnowledgeResolution *resolution) { if (resolution != NULL) { free(resolution->semantic_id); free(resolution->methods); free(resolution); } }
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
