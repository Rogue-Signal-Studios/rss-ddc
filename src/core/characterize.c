#include "characterize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSSDDCCharacterization {
    RSSDDCMonitorKnowledge *knowledge;
    bool has_display;
    RSSDDCDisplay display;
    bool has_edid;
    RSSDDCEDIDInfo edid;
    bool has_profile_identity;
    RSSDDCProfileIdentity profile_identity;
    RSSDDCCharacterizationProfileStatus profile_status;
    RSSDDCEffectiveProfile effective_profile;
    uint32_t provider_capabilities;
    bool mccs_attempted;
    RSSDDCError mccs_status;
    RSSDDCMCCSCapabilities *mccs;
    bool quick_attempted;
    bool has_quick_diagnostics;
    RSSDDCError quick_status;
    RSSDDCProbeDiagnostics quick_diagnostics;
    RSSDDCProbeObservation quick_observations[RSS_DDC_PROBE_QUICK_CONTROL_COUNT];
    bool extended_attempted;
    bool has_extended_diagnostics;
    RSSDDCError extended_status;
    RSSDDCProbeExtendedDiagnostics extended_diagnostics;
    RSSDDCProbeExtendedObservation *extended_observations;
    RSSDDCCharacterizationPromotionSummary extended_promotion;
};

typedef struct {
    const char *alias;
    const char *canonical;
} RSSDDCSemanticAlias;

/*
 * Aliases that exist in current profile packs or documented schema prose.
 * Canonical spellings match the historical registry / current probe IDs.
 */
static const RSSDDCSemanticAlias semantic_aliases[] = {
    {"brightness", "display.brightness"},
    {"contrast", "display.contrast"},
    {"color-preset", "display.color_preset"},
    {"picture-mode", "display.picture_mode"},
    {"picture_mode", "display.picture_mode"},
    {"input", "inputs.switching"},
    {"input.current", "inputs.switching"},
    {"inputs.current", "inputs.switching"},
};

static bool copy_terminated(const char *source, char *out, size_t capacity) {
    int written = snprintf(out, capacity, "%s", source);
    return written >= 0 && (size_t)written < capacity;
}

RSSDDCCharacterization *rss_ddc_characterization_create(void) {
    RSSDDCCharacterization *characterization = calloc(1, sizeof(*characterization));
    if (characterization == NULL) {
        return NULL;
    }
    characterization->knowledge = rss_ddc_monitor_knowledge_create();
    if (characterization->knowledge == NULL) {
        free(characterization);
        return NULL;
    }
    characterization->mccs_status = RSS_DDC_OK;
    characterization->quick_status = RSS_DDC_OK;
    characterization->extended_status = RSS_DDC_OK;
    return characterization;
}

void rss_ddc_characterization_destroy(RSSDDCCharacterization *characterization) {
    if (characterization == NULL) {
        return;
    }
    rss_ddc_monitor_knowledge_destroy(characterization->knowledge);
    free(characterization->mccs);
    free(characterization->extended_observations);
    free(characterization);
}

RSSDDCError rss_ddc_characterization_normalize_semantic_id(const char *semantic_id, char *out,
                                                           size_t capacity) {
    if (semantic_id == NULL || semantic_id[0] == '\0' || out == NULL || capacity == 0) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    const char *canonical = semantic_id;
    for (size_t index = 0; index < sizeof(semantic_aliases) / sizeof(semantic_aliases[0]); ++index) {
        if (strcmp(semantic_id, semantic_aliases[index].alias) == 0) {
            canonical = semantic_aliases[index].canonical;
            break;
        }
    }
    if (!copy_terminated(canonical, out, capacity)) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    return RSS_DDC_OK;
}

static RSSDDCError copy_normalized_knowledge(const RSSDDCMonitorKnowledge *source,
                                             RSSDDCMonitorKnowledge **out) {
    RSSDDCMonitorKnowledge *normalized = rss_ddc_monitor_knowledge_create();
    if (normalized == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(source); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(source, index);
        if (route == NULL) {
            rss_ddc_monitor_knowledge_destroy(normalized);
            return RSS_DDC_ERROR_ARGUMENT;
        }
        RSSDDCKnowledgeRoute copy = *route;
        RSSDDCError error = rss_ddc_characterization_normalize_semantic_id(
            route->semantic_id, copy.semantic_id, sizeof(copy.semantic_id));
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(normalized);
            return error;
        }
        error = rss_ddc_monitor_knowledge_add_route(normalized, &copy);
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(normalized);
            return error;
        }
    }
    *out = normalized;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_characterization_add_knowledge(RSSDDCCharacterization *characterization,
                                                   const RSSDDCMonitorKnowledge *knowledge) {
    if (characterization == NULL || characterization->knowledge == NULL || knowledge == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    RSSDDCMonitorKnowledge *normalized = NULL;
    RSSDDCError error = copy_normalized_knowledge(knowledge, &normalized);
    if (error != RSS_DDC_OK) {
        return error;
    }
    RSSDDCMonitorKnowledge *merged = NULL;
    error = rss_ddc_monitor_knowledge_merge(characterization->knowledge, normalized, &merged);
    rss_ddc_monitor_knowledge_destroy(normalized);
    if (error != RSS_DDC_OK) {
        return error;
    }
    rss_ddc_monitor_knowledge_destroy(characterization->knowledge);
    characterization->knowledge = merged;
    return RSS_DDC_OK;
}

const RSSDDCMonitorKnowledge *rss_ddc_characterization_knowledge(
    const RSSDDCCharacterization *characterization) {
    return characterization == NULL ? NULL : characterization->knowledge;
}

RSSDDCError rss_ddc_characterization_resolve(const RSSDDCCharacterization *characterization,
                                             const char *semantic_id,
                                             RSSDDCMonitorKnowledgeResolution **resolution) {
    if (characterization == NULL || characterization->knowledge == NULL || resolution == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    char canonical[RSS_DDC_TEXT_MAX] = {};
    RSSDDCError error = rss_ddc_characterization_normalize_semantic_id(semantic_id, canonical,
                                                                       sizeof(canonical));
    if (error != RSS_DDC_OK) {
        return error;
    }
    const RSSDDCMonitorKnowledge *sources[] = {characterization->knowledge};
    return rss_ddc_monitor_knowledge_resolve(sources, 1, canonical, resolution);
}

static bool observed_current(const RSSDDCKnowledgeRoute *route) {
    return route != NULL && route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED &&
           (route->value.state == RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED ||
            route->value.state == RSS_DDC_KNOWLEDGE_VALUE_STRING);
}

static bool values_equal(const RSSDDCKnowledgeRoute *first, const RSSDDCKnowledgeRoute *second) {
    if (first->value.state != second->value.state) {
        return false;
    }
    if (first->value.state == RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED) {
        return first->value.unsigned_value == second->value.unsigned_value;
    }
    return strcmp(first->value.string_value, second->value.string_value) == 0;
}

RSSDDCError rss_ddc_characterization_current_value(const RSSDDCCharacterization *characterization,
                                                   const char *semantic_id,
                                                   RSSDDCCharacterizationValueState *state,
                                                   const RSSDDCKnowledgeRoute **route) {
    if (characterization == NULL || characterization->knowledge == NULL || state == NULL ||
        route == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    char canonical[RSS_DDC_TEXT_MAX] = {};
    RSSDDCError error = rss_ddc_characterization_normalize_semantic_id(semantic_id, canonical,
                                                                       sizeof(canonical));
    if (error != RSS_DDC_OK) {
        return error;
    }
    *state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    *route = NULL;
    const RSSDDCMonitorKnowledge *knowledge = characterization->knowledge;
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *candidate = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (candidate == NULL || strcmp(candidate->semantic_id, canonical) != 0 ||
            !observed_current(candidate)) {
            continue;
        }
        if (*route == NULL) {
            *route = candidate;
            *state = RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED;
            continue;
        }
        if (!values_equal(*route, candidate)) {
            *route = NULL;
            *state = RSS_DDC_CHARACTERIZATION_VALUE_CONFLICT;
            return RSS_DDC_OK;
        }
        if (strcmp(candidate->provenance.source_id, (*route)->provenance.source_id) < 0) {
            *route = candidate;
        }
    }
    return RSS_DDC_OK;
}

static void profile_source_id(const RSSDDCProfileStore *store, char *out, size_t capacity) {
    RSSDDCProfilePackInfo info = {0};
    if (store != NULL && rss_ddc_profile_store_pack_info(store, &info) == RSS_DDC_OK &&
        info.pack_id[0] != '\0') {
        (void)snprintf(out, capacity, "%s", info.pack_id);
        return;
    }
    (void)snprintf(out, capacity, "%s", "profile-matched");
}

static RSSDDCError add_matched_profile_knowledge(RSSDDCCharacterization *characterization,
                                                 const RSSDDCProfileStore *store,
                                                 const RSSDDCEffectiveProfile *effective) {
    RSSDDCMonitorKnowledge *profile_knowledge = rss_ddc_monitor_knowledge_create();
    char source_id[RSS_DDC_PROFILE_ID_MAX] = {};
    if (profile_knowledge == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    profile_source_id(store, source_id, sizeof(source_id));
    for (size_t index = 0; index < rss_ddc_effective_profile_control_count(effective); ++index) {
        RSSDDCProfileControl control = {0};
        char canonical[RSS_DDC_TEXT_MAX] = {};
        RSSDDCError error = rss_ddc_effective_profile_control(effective, index, &control);
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(profile_knowledge);
            return error;
        }
        error = rss_ddc_characterization_normalize_semantic_id(rss_ddc_profile_control_name(control.id),
                                                               canonical, sizeof(canonical));
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(profile_knowledge);
            return error;
        }
        error = rss_ddc_monitor_knowledge_add_profile_control(profile_knowledge, canonical, source_id,
                                                              &control);
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(profile_knowledge);
            return error;
        }
    }
    RSSDDCError error = rss_ddc_characterization_add_knowledge(characterization, profile_knowledge);
    rss_ddc_monitor_knowledge_destroy(profile_knowledge);
    return error;
}

RSSDDCError rss_ddc_characterization_assemble(RSSDDCCharacterization *characterization,
                                              const RSSDDCDisplay *display,
                                              const RSSDDCEDIDInfo *edid,
                                              const RSSDDCProfileStore *store) {
    if (characterization == NULL || characterization->knowledge == NULL || display == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }

    characterization->display = *display;
    characterization->has_display = true;
    if (edid != NULL) {
        characterization->edid = *edid;
        characterization->has_edid = true;
    } else {
        characterization->edid = (RSSDDCEDIDInfo){0};
        characterization->has_edid = false;
    }
    characterization->provider_capabilities = rss_ddc_provider_capabilities(display->provider);
    rss_ddc_profile_identity_from_display(display, &characterization->profile_identity);
    characterization->has_profile_identity = true;
    characterization->profile_status = RSS_DDC_CHARACTERIZATION_PROFILE_NONE;
    characterization->effective_profile = (RSSDDCEffectiveProfile){0};

    if (store == NULL) {
        return RSS_DDC_OK;
    }

    RSSDDCEffectiveProfile effective = {0};
    RSSDDCError error =
        rss_ddc_profile_store_resolve(store, &characterization->profile_identity, &effective);
    if (error == RSS_DDC_ERROR_NOT_FOUND) {
        return RSS_DDC_OK;
    }
    if (error == RSS_DDC_ERROR_PROFILE_CONFLICT) {
        characterization->profile_status = RSS_DDC_CHARACTERIZATION_PROFILE_CONFLICT;
        return error;
    }
    if (error != RSS_DDC_OK) {
        return error;
    }

    error = add_matched_profile_knowledge(characterization, store, &effective);
    if (error != RSS_DDC_OK) {
        return error;
    }
    characterization->effective_profile = effective;
    characterization->profile_status = RSS_DDC_CHARACTERIZATION_PROFILE_MATCHED;
    return RSS_DDC_OK;
}

const RSSDDCDisplay *rss_ddc_characterization_display(const RSSDDCCharacterization *characterization) {
    return characterization != NULL && characterization->has_display ? &characterization->display : NULL;
}

const RSSDDCEDIDInfo *rss_ddc_characterization_edid(const RSSDDCCharacterization *characterization) {
    return characterization != NULL && characterization->has_edid ? &characterization->edid : NULL;
}

uint32_t rss_ddc_characterization_provider_capabilities(const RSSDDCCharacterization *characterization) {
    return characterization == NULL ? RSS_DDC_CAP_NONE : characterization->provider_capabilities;
}

RSSDDCCharacterizationProfileStatus rss_ddc_characterization_profile_status(
    const RSSDDCCharacterization *characterization) {
    return characterization == NULL ? RSS_DDC_CHARACTERIZATION_PROFILE_NONE
                                    : characterization->profile_status;
}

const RSSDDCProfileIdentity *rss_ddc_characterization_profile_identity(
    const RSSDDCCharacterization *characterization) {
    return characterization != NULL && characterization->has_profile_identity
               ? &characterization->profile_identity
               : NULL;
}

const RSSDDCEffectiveProfile *rss_ddc_characterization_effective_profile(
    const RSSDDCCharacterization *characterization) {
    return characterization != NULL &&
                   characterization->profile_status == RSS_DDC_CHARACTERIZATION_PROFILE_MATCHED
               ? &characterization->effective_profile
               : NULL;
}

static bool mccs_retrieval_supported(const RSSDDCCharacterization *characterization) {
    return characterization != NULL &&
           (characterization->provider_capabilities & RSS_DDC_CAP_MCCS_CAPABILITIES) != 0;
}

static const char *declared_semantic_id(uint8_t vcp, char *unknown, size_t unknown_capacity) {
    static const struct {
        uint8_t vcp;
        const char *semantic_id;
    } known[] = {
        {0x10, "display.brightness"},     {0x12, "display.contrast"},
        {0x14, "display.color_preset"},   {0x15, "display.picture_mode"},
        {0x16, "display.rgb.red_gain"},   {0x18, "display.rgb.green_gain"},
        {0x1a, "display.rgb.blue_gain"},  {0x60, "inputs.switching"},
    };
    for (size_t index = 0; index < sizeof(known) / sizeof(known[0]); ++index) {
        if (known[index].vcp == vcp) {
            return known[index].semantic_id;
        }
    }
    (void)snprintf(unknown, unknown_capacity, "vendor.unknown.vcp.%02x", vcp);
    return unknown;
}

RSSDDCError rss_ddc_characterization_collect_passive_mccs_failed(
    RSSDDCCharacterization *characterization, RSSDDCError status) {
    if (characterization == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (!mccs_retrieval_supported(characterization)) {
        characterization->mccs_attempted = false;
        characterization->mccs_status = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
        return RSS_DDC_OK;
    }
    characterization->mccs_attempted = true;
    characterization->mccs_status = status == RSS_DDC_OK ? RSS_DDC_ERROR_READ : status;
    return RSS_DDC_OK;
}

static RSSDDCError add_declared_mccs_route(RSSDDCMonitorKnowledge *knowledge,
                                           const RSSDDCMCCSVcpCapability *feature) {
    RSSDDCKnowledgeRoute route = {0};
    char unknown[RSS_DDC_TEXT_MAX] = {};
    const char *semantic_id = declared_semantic_id(feature->vcp_code, unknown, sizeof(unknown));
    RSSDDCError error =
        rss_ddc_characterization_normalize_semantic_id(semantic_id, route.semantic_id,
                                                       sizeof(route.semantic_id));
    if (error != RSS_DDC_OK) {
        return error;
    }
    (void)snprintf(route.route_id, sizeof(route.route_id), "mccs-vcp-%02x", feature->vcp_code);
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s", "mccs-vcp");
    (void)snprintf(route.command_semantics, sizeof(route.command_semantics), "%s",
                   "monitor-declared-mccs");
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s",
                   "mccs-capabilities");
    (void)snprintf(route.provenance.evidence_id, sizeof(route.provenance.evidence_id), "%s",
                   "mccs-advertised");
    route.kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP;
    route.address = feature->vcp_code;
    route.readable = false;
    route.writable = false;
    route.write_authorized = false;
    route.value.state = RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN;
    route.provenance.source = RSS_DDC_PROFILE_SOURCE_RESEARCH;
    route.provenance.confidence = RSS_DDC_PROFILE_CONFIDENCE_OBSERVED;
    route.provenance.fact_kind = RSS_DDC_KNOWLEDGE_FACT_DECLARED;
    return rss_ddc_monitor_knowledge_add_route(knowledge, &route);
}

RSSDDCError rss_ddc_characterization_collect_passive_mccs(
    RSSDDCCharacterization *characterization, const RSSDDCMCCSCapabilities *capabilities) {
    if (characterization == NULL || characterization->knowledge == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (!mccs_retrieval_supported(characterization)) {
        return rss_ddc_characterization_collect_passive_mccs_failed(
            characterization, RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    }
    if (capabilities == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }

    RSSDDCMonitorKnowledge *declared = rss_ddc_monitor_knowledge_create();
    RSSDDCMCCSCapabilities *copy = NULL;
    if (declared == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    for (size_t index = 0; index < capabilities->feature_count; ++index) {
        RSSDDCError error = add_declared_mccs_route(declared, &capabilities->features[index]);
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(declared);
            characterization->mccs_attempted = true;
            characterization->mccs_status = error;
            return error;
        }
    }
    RSSDDCError error = rss_ddc_characterization_add_knowledge(characterization, declared);
    rss_ddc_monitor_knowledge_destroy(declared);
    if (error != RSS_DDC_OK) {
        characterization->mccs_attempted = true;
        characterization->mccs_status = error;
        return error;
    }

    copy = malloc(sizeof(*copy));
    if (copy == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    *copy = *capabilities;
    free(characterization->mccs);
    characterization->mccs = copy;
    characterization->mccs_attempted = true;
    characterization->mccs_status = RSS_DDC_OK;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_characterization_collect_passive_mccs_raw(
    RSSDDCCharacterization *characterization, const char *raw, size_t raw_length) {
    if (characterization == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (!mccs_retrieval_supported(characterization)) {
        return rss_ddc_characterization_collect_passive_mccs_failed(
            characterization, RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    }
    RSSDDCMCCSCapabilities parsed = {0};
    RSSDDCError error = rss_ddc_parse_mccs_capabilities(raw, raw_length, &parsed);
    if (error != RSS_DDC_OK) {
        return rss_ddc_characterization_collect_passive_mccs_failed(characterization, error);
    }
    return rss_ddc_characterization_collect_passive_mccs(characterization, &parsed);
}

bool rss_ddc_characterization_mccs_supported(const RSSDDCCharacterization *characterization) {
    return mccs_retrieval_supported(characterization);
}

bool rss_ddc_characterization_mccs_attempted(const RSSDDCCharacterization *characterization) {
    return characterization != NULL && characterization->mccs_attempted;
}

RSSDDCError rss_ddc_characterization_mccs_status(const RSSDDCCharacterization *characterization) {
    return characterization == NULL ? RSS_DDC_ERROR_ARGUMENT : characterization->mccs_status;
}

const RSSDDCMCCSCapabilities *rss_ddc_characterization_mccs(
    const RSSDDCCharacterization *characterization) {
    return characterization == NULL ? NULL : characterization->mccs;
}

static bool get_vcp_supported(const RSSDDCCharacterization *characterization) {
    return characterization != NULL &&
           (characterization->provider_capabilities & RSS_DDC_CAP_GET_VCP) != 0;
}

static void copy_quick_diagnostics(RSSDDCCharacterization *characterization,
                                   const RSSDDCProbeDiagnostics *diagnostics) {
    characterization->quick_diagnostics = *diagnostics;
    size_t count = diagnostics->observation_count;
    if (count > RSS_DDC_PROBE_QUICK_CONTROL_COUNT) {
        count = RSS_DDC_PROBE_QUICK_CONTROL_COUNT;
    }
    if (diagnostics->observations != NULL && count > 0) {
        memcpy(characterization->quick_observations, diagnostics->observations,
               count * sizeof(*diagnostics->observations));
    } else {
        memset(characterization->quick_observations, 0, sizeof(characterization->quick_observations));
        count = 0;
    }
    characterization->quick_diagnostics.observations = characterization->quick_observations;
    characterization->quick_diagnostics.observation_count = count;
    characterization->has_quick_diagnostics = true;
}

static RSSDDCError merge_quick_observed_knowledge(RSSDDCCharacterization *characterization,
                                                 const RSSDDCMonitorKnowledge *probe_knowledge) {
    RSSDDCMonitorKnowledge *observed = rss_ddc_monitor_knowledge_create();
    if (observed == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(probe_knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(probe_knowledge, index);
        if (route == NULL) {
            rss_ddc_monitor_knowledge_destroy(observed);
            return RSS_DDC_ERROR_ARGUMENT;
        }
        if (route->provenance.fact_kind != RSS_DDC_KNOWLEDGE_FACT_OBSERVED) {
            continue;
        }
        RSSDDCError error = rss_ddc_monitor_knowledge_add_route(observed, route);
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(observed);
            return error;
        }
    }
    RSSDDCError error = rss_ddc_characterization_add_knowledge(characterization, observed);
    rss_ddc_monitor_knowledge_destroy(observed);
    return error;
}

RSSDDCError rss_ddc_characterization_collect_quick_probe_failed(
    RSSDDCCharacterization *characterization, RSSDDCError status) {
    if (characterization == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (!get_vcp_supported(characterization)) {
        characterization->quick_attempted = false;
        characterization->quick_status = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
        return RSS_DDC_OK;
    }
    characterization->quick_attempted = true;
    characterization->quick_status = status == RSS_DDC_OK ? RSS_DDC_ERROR_READ : status;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_characterization_collect_quick_probe(RSSDDCCharacterization *characterization,
                                                         const RSSDDCProbe *probe) {
    if (characterization == NULL || characterization->knowledge == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (!get_vcp_supported(characterization)) {
        return rss_ddc_characterization_collect_quick_probe_failed(
            characterization, RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    }
    if (probe == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }

    RSSDDCProbeDiagnostics diagnostics = {0};
    RSSDDCError error = rss_ddc_probe_diagnostics(probe, &diagnostics);
    if (error != RSS_DDC_OK) {
        return error;
    }
    copy_quick_diagnostics(characterization, &diagnostics);

    const RSSDDCMonitorKnowledge *probe_knowledge = NULL;
    error = rss_ddc_probe_knowledge(probe, &probe_knowledge);
    if (error == RSS_DDC_ERROR_NOT_FOUND) {
        characterization->quick_attempted = true;
        characterization->quick_status = RSS_DDC_OK;
        return RSS_DDC_OK;
    }
    if (error != RSS_DDC_OK) {
        characterization->quick_attempted = true;
        characterization->quick_status = error;
        return error;
    }

    error = merge_quick_observed_knowledge(characterization, probe_knowledge);
    characterization->quick_attempted = true;
    characterization->quick_status = error;
    return error;
}

bool rss_ddc_characterization_quick_supported(const RSSDDCCharacterization *characterization) {
    return get_vcp_supported(characterization);
}

bool rss_ddc_characterization_quick_attempted(const RSSDDCCharacterization *characterization) {
    return characterization != NULL && characterization->quick_attempted;
}

RSSDDCError rss_ddc_characterization_quick_status(const RSSDDCCharacterization *characterization) {
    return characterization == NULL ? RSS_DDC_ERROR_ARGUMENT : characterization->quick_status;
}

const RSSDDCProbeDiagnostics *rss_ddc_characterization_quick_diagnostics(
    const RSSDDCCharacterization *characterization) {
    return characterization != NULL && characterization->has_quick_diagnostics
               ? &characterization->quick_diagnostics
               : NULL;
}

static bool knowledge_has_semantic(const RSSDDCMonitorKnowledge *knowledge, const char *semantic_id) {
    if (knowledge == NULL || semantic_id == NULL) {
        return false;
    }
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route != NULL && strcmp(route->semantic_id, semantic_id) == 0) {
            return true;
        }
    }
    return false;
}

static bool mccs_has_vcp(const RSSDDCCharacterization *characterization, uint8_t vcp) {
    return characterization->mccs != NULL && rss_ddc_mccs_capabilities_has_vcp(characterization->mccs, vcp);
}

static bool picture_mode_production_gate(const RSSDDCCharacterization *characterization) {
    const RSSDDCDisplay *display = rss_ddc_characterization_display(characterization);
    return display != NULL && (display->capabilities & RSS_DDC_CAP_PICTURE_MODE) != 0;
}

static bool control_in_scope(const RSSDDCCharacterization *characterization, const char *semantic_id) {
    const RSSDDCMonitorKnowledge *knowledge = rss_ddc_characterization_knowledge(characterization);
    if (strcmp(semantic_id, "display.brightness") == 0 || strcmp(semantic_id, "display.contrast") == 0) {
        return true;
    }
    if (strcmp(semantic_id, "display.color_preset") == 0) {
        return knowledge_has_semantic(knowledge, semantic_id) || mccs_has_vcp(characterization, 0x14);
    }
    if (strcmp(semantic_id, "display.picture_mode") == 0) {
        return knowledge_has_semantic(knowledge, semantic_id) || mccs_has_vcp(characterization, 0x15) ||
               picture_mode_production_gate(characterization);
    }
    if (strcmp(semantic_id, "inputs.switching") == 0) {
        return knowledge_has_semantic(knowledge, semantic_id) || mccs_has_vcp(characterization, 0x60) ||
               (characterization->provider_capabilities & RSS_DDC_CAP_ALTERNATE_INPUT) != 0;
    }
    return false;
}

static RSSDDCError control_method_state(const RSSDDCCharacterization *characterization,
                                        const char *semantic_id, bool *usable, bool *conflict) {
    *usable = false;
    *conflict = false;
    if (strcmp(semantic_id, "display.picture_mode") == 0 && picture_mode_production_gate(characterization)) {
        *usable = true;
        return RSS_DDC_OK;
    }

    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    RSSDDCError error = rss_ddc_characterization_resolve(characterization, semantic_id, &resolution);
    if (error == RSS_DDC_ERROR_NOT_FOUND) {
        return RSS_DDC_OK;
    }
    if (error != RSS_DDC_OK) {
        return error;
    }
    if (rss_ddc_monitor_knowledge_resolution_has_conflict(resolution)) {
        *conflict = true;
    } else if (rss_ddc_monitor_knowledge_resolution_state(resolution) ==
                   RSS_DDC_KNOWLEDGE_RESOLUTION_RESOLVED &&
               (rss_ddc_monitor_knowledge_resolution_preferred_read(resolution) != NULL ||
                rss_ddc_monitor_knowledge_resolution_preferred_write(resolution) != NULL)) {
        *usable = true;
    }
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    return RSS_DDC_OK;
}

static bool quick_variable_for(const RSSDDCCharacterization *characterization, const char *semantic_id) {
    const RSSDDCProbeDiagnostics *diagnostics =
        rss_ddc_characterization_quick_diagnostics(characterization);
    if (diagnostics == NULL || diagnostics->observations == NULL) {
        return false;
    }
    for (size_t index = 0; index < diagnostics->observation_count; ++index) {
        const RSSDDCProbeObservation *observation = &diagnostics->observations[index];
        if (observation->semantic_id != NULL && strcmp(observation->semantic_id, semantic_id) == 0 &&
            observation->category == RSS_DDC_PROBE_RESULT_VARIABLE) {
            return true;
        }
    }
    return false;
}

RSSDDCError rss_ddc_characterization_sufficiency(
    const RSSDDCCharacterization *characterization,
    RSSDDCCharacterizationSufficiencyResult *result) {
    if (characterization == NULL || result == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }

    *result = (RSSDDCCharacterizationSufficiencyResult){
        .status = RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT,
        .reasons = RSS_DDC_CHARACTERIZATION_REASON_NONE,
        .extended_recommended = false,
    };

    if (rss_ddc_characterization_display(characterization) == NULL) {
        result->status = RSS_DDC_CHARACTERIZATION_SUFFICIENCY_UNAVAILABLE;
        return RSS_DDC_OK;
    }

    const bool get_supported = get_vcp_supported(characterization);
    if (!get_supported) {
        result->reasons |= RSS_DDC_CHARACTERIZATION_REASON_NO_GET_SUPPORT;
    }

    if (rss_ddc_characterization_profile_status(characterization) ==
        RSS_DDC_CHARACTERIZATION_PROFILE_CONFLICT) {
        result->status = RSS_DDC_CHARACTERIZATION_SUFFICIENCY_CONFLICT;
        result->reasons |= RSS_DDC_CHARACTERIZATION_REASON_PROFILE_CONFLICT;
        return RSS_DDC_OK;
    }

    static const char *const product_controls[] = {
        "display.brightness", "display.contrast", "display.color_preset", "display.picture_mode",
        "inputs.switching",
    };

    bool any_unresolved = false;
    bool any_conflict = false;
    bool probe_helpful = false;
    const RSSDDCMonitorKnowledge *knowledge = rss_ddc_characterization_knowledge(characterization);

    for (size_t index = 0; index < sizeof(product_controls) / sizeof(product_controls[0]); ++index) {
        const char *semantic_id = product_controls[index];
        if (!control_in_scope(characterization, semantic_id)) {
            continue;
        }

        bool usable = false;
        bool conflict = false;
        RSSDDCError error = control_method_state(characterization, semantic_id, &usable, &conflict);
        if (error != RSS_DDC_OK) {
            return error;
        }

        const bool has_route = knowledge_has_semantic(knowledge, semantic_id);
        const bool production_gate =
            strcmp(semantic_id, "display.picture_mode") == 0 && picture_mode_production_gate(characterization);

        if (conflict) {
            any_conflict = true;
            result->reasons |= RSS_DDC_CHARACTERIZATION_REASON_CONFLICTING_METHOD;
        } else if (!usable) {
            any_unresolved = true;
            result->reasons |= RSS_DDC_CHARACTERIZATION_REASON_UNRESOLVED_METHOD;
            if (!has_route && !production_gate) {
                result->reasons |= RSS_DDC_CHARACTERIZATION_REASON_MISSING_CONTROL;
            }
            if (get_supported) {
                probe_helpful = true;
            }
        }

        if ((strcmp(semantic_id, "display.brightness") == 0 ||
             strcmp(semantic_id, "display.contrast") == 0) &&
            quick_variable_for(characterization, semantic_id)) {
            result->reasons |= RSS_DDC_CHARACTERIZATION_REASON_VARIABLE_OBSERVATION;
        }
    }

    if (any_conflict) {
        result->status = RSS_DDC_CHARACTERIZATION_SUFFICIENCY_CONFLICT;
    } else if (any_unresolved) {
        result->status = RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT;
    } else {
        result->status = RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT;
    }

    if (result->status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT && probe_helpful) {
        result->extended_recommended = true;
        result->reasons |= RSS_DDC_CHARACTERIZATION_REASON_PROBE_HELPFUL;
    }
    return RSS_DDC_OK;
}

static bool observation_is_strict_valid(const RSSDDCProbeObservation *observation) {
    return observation != NULL && observation->protocol_valid && observation->semantic_request_match;
}

static bool is_product_relevant_id(const char *semantic_id) {
    return strcmp(semantic_id, "display.brightness") == 0 || strcmp(semantic_id, "display.contrast") == 0 ||
           strcmp(semantic_id, "display.color_preset") == 0 ||
           strcmp(semantic_id, "display.picture_mode") == 0 || strcmp(semantic_id, "inputs.switching") == 0;
}

static bool is_vendor_unknown_id(const char *semantic_id) {
    return strncmp(semantic_id, "vendor.unknown.vcp.", 19) == 0;
}

static bool knowledge_has_observed(const RSSDDCMonitorKnowledge *knowledge, const char *semantic_id) {
    if (knowledge == NULL || semantic_id == NULL) {
        return false;
    }
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route != NULL && strcmp(route->semantic_id, semantic_id) == 0 &&
            route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED) {
            return true;
        }
    }
    return false;
}

static unsigned extended_promotion_priority(RSSDDCCharacterization *characterization,
                                            const RSSDDCProbeObservation *observation,
                                            const char *semantic_id) {
    bool usable = false;
    bool conflict = false;
    (void)control_method_state(characterization, semantic_id, &usable, &conflict);
    const bool advertised = observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_YES ||
                            mccs_has_vcp(characterization, observation->requested_vcp);
    const bool known = !is_vendor_unknown_id(semantic_id);

    if (is_product_relevant_id(semantic_id) && !usable && !conflict) {
        return 0;
    }
    if (advertised && !usable && !conflict) {
        return 1;
    }
    if (known && (knowledge_has_semantic(rss_ddc_characterization_knowledge(characterization), semantic_id) ||
                  advertised) &&
        !knowledge_has_observed(rss_ddc_characterization_knowledge(characterization), semantic_id)) {
        return 2;
    }
    if (known) {
        return 3;
    }
    return 4;
}

typedef struct {
    uint8_t vcp;
    unsigned priority;
} RSSDDCExtendedPromotionItem;

static int compare_extended_promotion(const void *left, const void *right) {
    const RSSDDCExtendedPromotionItem *first = left;
    const RSSDDCExtendedPromotionItem *second = right;
    if (first->priority != second->priority) {
        return first->priority < second->priority ? -1 : 1;
    }
    return (int)first->vcp - (int)second->vcp;
}

static RSSDDCError observed_route_from_extended(const RSSDDCProbeObservation *observation,
                                                const char *semantic_id, RSSDDCKnowledgeRoute *route) {
    memset(route, 0, sizeof(*route));
    if (!copy_terminated(semantic_id, route->semantic_id, sizeof(route->semantic_id))) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    (void)snprintf(route->route_id, sizeof(route->route_id), "mccs-vcp-%02x", observation->requested_vcp);
    route->kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP;
    route->address = observation->requested_vcp;
    (void)snprintf(route->transport_family, sizeof(route->transport_family), "%s", "mccs-vcp");
    (void)snprintf(route->command_semantics, sizeof(route->command_semantics), "%s", "read-only-get-vcp");
    route->readable = true;
    route->writable = false;
    route->write_authorized = false;
    route->value.state = RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED;
    route->value.unsigned_value = observation->current_value;
    route->reported_maximum_present = true;
    route->reported_maximum = observation->maximum_value;
    route->provenance.source = RSS_DDC_PROFILE_SOURCE_RESEARCH;
    route->provenance.confidence = RSS_DDC_PROFILE_CONFIDENCE_OBSERVED;
    route->provenance.fact_kind = RSS_DDC_KNOWLEDGE_FACT_OBSERVED;
    (void)snprintf(route->provenance.source_id, sizeof(route->provenance.source_id), "%s",
                   "alien-probe-live-read");
    (void)snprintf(route->provenance.evidence_id, sizeof(route->provenance.evidence_id), "%s",
                   observation->stable ? "stable-get" : "variable-get");
    return RSS_DDC_OK;
}

static RSSDDCError copy_extended_diagnostics(RSSDDCCharacterization *characterization,
                                             const RSSDDCProbeExtendedDiagnostics *diagnostics) {
    RSSDDCProbeExtendedObservation *copy =
        calloc(RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT, sizeof(*copy));
    if (copy == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    size_t count = diagnostics->observation_count;
    if (count > RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT) {
        count = RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT;
    }
    if (diagnostics->observations != NULL && count > 0) {
        memcpy(copy, diagnostics->observations, count * sizeof(*copy));
        for (size_t index = 0; index < count; ++index) {
            if (copy[index].semantic_id_buffer[0] != '\0') {
                copy[index].observation.semantic_id = copy[index].semantic_id_buffer;
            }
        }
    } else {
        count = 0;
    }
    free(characterization->extended_observations);
    characterization->extended_observations = copy;
    characterization->extended_diagnostics = *diagnostics;
    characterization->extended_diagnostics.observations = copy;
    characterization->extended_diagnostics.observation_count = count;
    characterization->has_extended_diagnostics = true;
    return RSS_DDC_OK;
}

static RSSDDCError promote_extended_observations(RSSDDCCharacterization *characterization) {
    RSSDDCCharacterizationPromotionSummary summary = {0};
    const RSSDDCProbeExtendedDiagnostics *diagnostics = &characterization->extended_diagnostics;
    RSSDDCExtendedPromotionItem items[RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT];
    size_t item_count = 0;

    for (size_t index = 0; index < diagnostics->observation_count; ++index) {
        const RSSDDCProbeExtendedObservation *extended = &diagnostics->observations[index];
        ++summary.considered;
        if (!observation_is_strict_valid(&extended->observation)) {
            ++summary.skipped_nonpromotable;
            continue;
        }
        items[item_count].vcp = extended->observation.requested_vcp;
        char unknown[32] = {};
        const char *semantic_id = declared_semantic_id(extended->observation.requested_vcp, unknown,
                                                       sizeof(unknown));
        items[item_count].priority =
            extended_promotion_priority(characterization, &extended->observation, semantic_id);
        ++item_count;
    }

    qsort(items, item_count, sizeof(items[0]), compare_extended_promotion);

    for (size_t index = 0; index < item_count; ++index) {
        const RSSDDCProbeExtendedObservation *extended =
            &diagnostics->observations[items[index].vcp];
        char unknown[32] = {};
        const char *semantic_id =
            declared_semantic_id(extended->observation.requested_vcp, unknown, sizeof(unknown));
        RSSDDCKnowledgeRoute route = {0};
        RSSDDCError error = observed_route_from_extended(&extended->observation, semantic_id, &route);
        if (error != RSS_DDC_OK) {
            return error;
        }
        RSSDDCMonitorKnowledge *observed = rss_ddc_monitor_knowledge_create();
        if (observed == NULL) {
            return RSS_DDC_ERROR_SYSTEM;
        }
        error = rss_ddc_monitor_knowledge_add_route(observed, &route);
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(observed);
            return error;
        }
        error = rss_ddc_characterization_add_knowledge(characterization, observed);
        rss_ddc_monitor_knowledge_destroy(observed);
        if (error == RSS_DDC_ERROR_PROFILE_CONFLICT) {
            ++summary.skipped_capacity;
            continue;
        }
        if (error != RSS_DDC_OK) {
            return error;
        }
        ++summary.promoted;
    }

    characterization->extended_promotion = summary;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_characterization_collect_extended_probe_failed(
    RSSDDCCharacterization *characterization, RSSDDCError status) {
    if (characterization == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (!get_vcp_supported(characterization)) {
        characterization->extended_attempted = false;
        characterization->extended_status = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
        return RSS_DDC_OK;
    }
    characterization->extended_attempted = true;
    characterization->extended_status = status == RSS_DDC_OK ? RSS_DDC_ERROR_READ : status;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_characterization_collect_extended_probe(RSSDDCCharacterization *characterization,
                                                            const RSSDDCProbe *probe) {
    if (characterization == NULL || characterization->knowledge == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (!get_vcp_supported(characterization)) {
        return rss_ddc_characterization_collect_extended_probe_failed(
            characterization, RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    }
    if (probe == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }

    RSSDDCProbeExtendedDiagnostics diagnostics = {0};
    RSSDDCError error = rss_ddc_probe_extended_diagnostics(probe, &diagnostics);
    if (error != RSS_DDC_OK) {
        return error;
    }
    error = copy_extended_diagnostics(characterization, &diagnostics);
    if (error != RSS_DDC_OK) {
        characterization->extended_attempted = true;
        characterization->extended_status = error;
        return error;
    }
    error = promote_extended_observations(characterization);
    characterization->extended_attempted = true;
    characterization->extended_status = error;
    return error;
}

bool rss_ddc_characterization_extended_attempted(const RSSDDCCharacterization *characterization) {
    return characterization != NULL && characterization->extended_attempted;
}

RSSDDCError rss_ddc_characterization_extended_status(const RSSDDCCharacterization *characterization) {
    return characterization == NULL ? RSS_DDC_ERROR_ARGUMENT : characterization->extended_status;
}

const RSSDDCProbeExtendedDiagnostics *rss_ddc_characterization_extended_diagnostics(
    const RSSDDCCharacterization *characterization) {
    return characterization != NULL && characterization->has_extended_diagnostics
               ? &characterization->extended_diagnostics
               : NULL;
}

const RSSDDCCharacterizationPromotionSummary *rss_ddc_characterization_extended_promotion(
    const RSSDDCCharacterization *characterization) {
    return characterization != NULL && characterization->extended_attempted
               ? &characterization->extended_promotion
               : NULL;
}
