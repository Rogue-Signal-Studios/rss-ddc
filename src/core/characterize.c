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
    return characterization;
}

void rss_ddc_characterization_destroy(RSSDDCCharacterization *characterization) {
    if (characterization == NULL) {
        return;
    }
    rss_ddc_monitor_knowledge_destroy(characterization->knowledge);
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
