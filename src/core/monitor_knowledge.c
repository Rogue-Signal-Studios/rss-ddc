#include "rss_ddc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RSS_DDC_KNOWLEDGE_MAX_ROUTES = 128 };

struct RSSDDCMonitorKnowledge {
    size_t count;
    RSSDDCKnowledgeRoute routes[RSS_DDC_KNOWLEDGE_MAX_ROUTES];
};

/* This owns only its view. Candidate pointers are borrowed from `sources`. */
struct RSSDDCMonitorKnowledgeResolution {
    size_t count;
    const RSSDDCKnowledgeRoute *candidates[RSS_DDC_KNOWLEDGE_MAX_ROUTES];
    const RSSDDCKnowledgeRoute *read;
    const RSSDDCKnowledgeRoute *write;
    RSSDDCKnowledgeResolutionState state;
};

RSSDDCMonitorKnowledge *rss_ddc_monitor_knowledge_create(void) {
    return calloc(1, sizeof(RSSDDCMonitorKnowledge));
}

void rss_ddc_monitor_knowledge_destroy(RSSDDCMonitorKnowledge *knowledge) {
    free(knowledge);
}

static bool terminated(const char *text, size_t length) {
    return memchr(text, '\0', length) != NULL;
}

static bool route_kind_valid(RSSDDCKnowledgeRouteKind kind) {
    return kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP ||
           kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT ||
           kind == RSS_DDC_KNOWLEDGE_ROUTE_PICTURE_MODE ||
           kind == RSS_DDC_KNOWLEDGE_ROUTE_UNSUPPORTED;
}

static bool route_valid(const RSSDDCKnowledgeRoute *route) {
    return route != NULL && terminated(route->semantic_id, sizeof(route->semantic_id)) &&
           terminated(route->route_id, sizeof(route->route_id)) &&
           terminated(route->transport_family, sizeof(route->transport_family)) &&
           terminated(route->command_semantics, sizeof(route->command_semantics)) &&
           terminated(route->applicability, sizeof(route->applicability)) &&
           terminated(route->value.string_value, sizeof(route->value.string_value)) &&
           terminated(route->provenance.source_id, sizeof(route->provenance.source_id)) &&
           terminated(route->provenance.evidence_id, sizeof(route->provenance.evidence_id)) &&
           route->semantic_id[0] != '\0' && route->route_id[0] != '\0' &&
           route->provenance.source_id[0] != '\0' && route_kind_valid(route->kind) &&
           route->value.state <= RSS_DDC_KNOWLEDGE_VALUE_UNSUPPORTED &&
           route->provenance.source <= RSS_DDC_PROFILE_SOURCE_RESEARCH &&
           route->provenance.confidence <= RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED &&
           route->provenance.fact_kind <= RSS_DDC_KNOWLEDGE_FACT_RESOLVED;
}

/* Provenance is deliberately excluded: equal observations remain distinct. */
static bool route_equivalent(const RSSDDCKnowledgeRoute *first,
                             const RSSDDCKnowledgeRoute *second) {
    return strcmp(first->semantic_id, second->semantic_id) == 0 &&
           strcmp(first->route_id, second->route_id) == 0 &&
           first->kind == second->kind && first->address == second->address &&
           first->readable == second->readable && first->writable == second->writable &&
           first->write_authorized == second->write_authorized &&
           strcmp(first->transport_family, second->transport_family) == 0 &&
           strcmp(first->command_semantics, second->command_semantics) == 0 &&
           strcmp(first->applicability, second->applicability) == 0 &&
           first->reported_maximum_present == second->reported_maximum_present &&
           first->reported_maximum == second->reported_maximum &&
           first->value.state == second->value.state &&
           first->value.unsigned_value == second->value.unsigned_value &&
           strcmp(first->value.string_value, second->value.string_value) == 0;
}

/* Only an exact repeated fact from the same provenance is coalesced. */
static bool route_identical(const RSSDDCKnowledgeRoute *first,
                            const RSSDDCKnowledgeRoute *second) {
    return route_equivalent(first, second) &&
           strcmp(first->provenance.source_id, second->provenance.source_id) == 0 &&
           first->provenance.source == second->provenance.source &&
           first->provenance.confidence == second->provenance.confidence &&
           first->provenance.fact_kind == second->provenance.fact_kind &&
           strcmp(first->provenance.evidence_id, second->provenance.evidence_id) == 0;
}

RSSDDCError rss_ddc_monitor_knowledge_add_route(RSSDDCMonitorKnowledge *knowledge,
                                                 const RSSDDCKnowledgeRoute *route) {
    if (knowledge == NULL || !route_valid(route)) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    for (size_t index = 0; index < knowledge->count; ++index) {
        if (route_identical(&knowledge->routes[index], route)) {
            return RSS_DDC_OK;
        }
    }
    if (knowledge->count == RSS_DDC_KNOWLEDGE_MAX_ROUTES) {
        return RSS_DDC_ERROR_PROFILE_CONFLICT;
    }
    knowledge->routes[knowledge->count++] = *route;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_monitor_knowledge_add_profile_control(
    RSSDDCMonitorKnowledge *knowledge, const char *semantic_id, const char *source_id,
    const RSSDDCProfileControl *control) {
    if (knowledge == NULL || semantic_id == NULL || semantic_id[0] == '\0' || source_id == NULL ||
        source_id[0] == '\0' || control == NULL || control->id == RSS_DDC_PROFILE_CONTROL_UNKNOWN ||
        control->method == RSS_DDC_PROFILE_METHOD_UNKNOWN) {
        return RSS_DDC_ERROR_ARGUMENT;
    }

    RSSDDCKnowledgeRoute route = {0};
    int semantic_length = snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", semantic_id);
    int route_length = snprintf(route.route_id, sizeof(route.route_id), "profile-control-%u",
                                (unsigned)control->id);
    int source_length = snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s", source_id);
    if (semantic_length < 0 || (size_t)semantic_length >= sizeof(route.semantic_id) || route_length < 0 ||
        (size_t)route_length >= sizeof(route.route_id) || source_length < 0 ||
        (size_t)source_length >= sizeof(route.provenance.source_id)) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    route.kind = control->method == RSS_DDC_PROFILE_METHOD_VCP
                     ? RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP
                     : RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT;
    route.address = control->address;
    route.readable = control->readable;
    route.writable = control->writable;
    route.write_authorized = control->write_authorized;
    route.value.state = RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN;
    route.provenance.source = control->source;
    route.provenance.confidence = control->confidence;
    route.provenance.fact_kind = RSS_DDC_KNOWLEDGE_FACT_PROFILE;
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s",
                   control->method == RSS_DDC_PROFILE_METHOD_VCP ? "mccs-vcp" : "lg-alt-input");
    (void)snprintf(route.command_semantics, sizeof(route.command_semantics), "%s",
                   rss_ddc_profile_control_name(control->id));
    return rss_ddc_monitor_knowledge_add_route(knowledge, &route);
}

size_t rss_ddc_monitor_knowledge_route_count(const RSSDDCMonitorKnowledge *knowledge) {
    return knowledge == NULL ? 0 : knowledge->count;
}

const RSSDDCKnowledgeRoute *rss_ddc_monitor_knowledge_route_at(
    const RSSDDCMonitorKnowledge *knowledge, size_t index) {
    return knowledge != NULL && index < knowledge->count ? &knowledge->routes[index] : NULL;
}

RSSDDCError rss_ddc_monitor_knowledge_merge(const RSSDDCMonitorKnowledge *first,
                                            const RSSDDCMonitorKnowledge *second,
                                            RSSDDCMonitorKnowledge **merged) {
    if (first == NULL || second == NULL || merged == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    *merged = NULL;
    RSSDDCMonitorKnowledge *result = rss_ddc_monitor_knowledge_create();
    if (result == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    for (size_t source = 0; source < 2; ++source) {
        const RSSDDCMonitorKnowledge *knowledge = source == 0 ? first : second;
        for (size_t index = 0; index < knowledge->count; ++index) {
            RSSDDCError error = rss_ddc_monitor_knowledge_add_route(result, &knowledge->routes[index]);
            if (error != RSS_DDC_OK) {
                rss_ddc_monitor_knowledge_destroy(result);
                return error;
            }
        }
    }
    *merged = result;
    return RSS_DDC_OK;
}

static unsigned source_rank(RSSDDCProfileSource source) {
    switch (source) {
    case RSS_DDC_PROFILE_SOURCE_LOCAL:
        return 4;
    case RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK:
        return 3;
    case RSS_DDC_PROFILE_SOURCE_BUILTIN:
        return 2;
    case RSS_DDC_PROFILE_SOURCE_RESEARCH:
        return 1;
    }
    return 0;
}

static unsigned authority(const RSSDDCKnowledgeRoute *route) {
    return (unsigned)route->provenance.confidence * 8U + source_rank(route->provenance.source);
}

static void select_route(const RSSDDCKnowledgeRoute *candidate,
                         const RSSDDCKnowledgeRoute **selected, unsigned *selected_authority,
                         RSSDDCKnowledgeResolutionState *state) {
    unsigned candidate_authority = authority(candidate);
    if (*selected == NULL || candidate_authority > *selected_authority) {
        *selected = candidate;
        *selected_authority = candidate_authority;
        return;
    }
    if (candidate_authority != *selected_authority) {
        return;
    }
    if (!route_equivalent(candidate, *selected)) {
        *state = RSS_DDC_KNOWLEDGE_RESOLUTION_CONFLICT;
        return;
    }
    /* Stable preferred provenance for agreeing equal-authority facts. */
    if (strcmp(candidate->provenance.source_id, (*selected)->provenance.source_id) < 0) {
        *selected = candidate;
    }
}

RSSDDCError rss_ddc_monitor_knowledge_resolve(const RSSDDCMonitorKnowledge *const *sources,
                                              size_t source_count, const char *semantic_id,
                                              RSSDDCMonitorKnowledgeResolution **resolution) {
    if (sources == NULL || source_count == 0 || semantic_id == NULL || semantic_id[0] == '\0' ||
        resolution == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    *resolution = NULL;
    RSSDDCMonitorKnowledgeResolution *result = calloc(1, sizeof(*result));
    if (result == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    unsigned read_authority = 0;
    unsigned write_authority = 0;
    for (size_t source = 0; source < source_count; ++source) {
        if (sources[source] == NULL) {
            free(result);
            return RSS_DDC_ERROR_ARGUMENT;
        }
        for (size_t index = 0; index < sources[source]->count; ++index) {
            const RSSDDCKnowledgeRoute *route = &sources[source]->routes[index];
            if (strcmp(route->semantic_id, semantic_id) != 0) {
                continue;
            }
            if (result->count == RSS_DDC_KNOWLEDGE_MAX_ROUTES) {
                free(result);
                return RSS_DDC_ERROR_PROFILE_CONFLICT;
            }
            result->candidates[result->count++] = route;
            if (route->readable) {
                select_route(route, &result->read, &read_authority, &result->state);
            }
            if (route->writable && route->value.state != RSS_DDC_KNOWLEDGE_VALUE_UNSUPPORTED) {
                select_route(route, &result->write, &write_authority, &result->state);
            }
        }
    }
    if (result->count == 0) {
        free(result);
        return RSS_DDC_ERROR_NOT_FOUND;
    }
    if (result->state == RSS_DDC_KNOWLEDGE_RESOLUTION_CONFLICT) {
        result->read = NULL;
        result->write = NULL;
    } else if (result->read != NULL || result->write != NULL) {
        result->state = RSS_DDC_KNOWLEDGE_RESOLUTION_RESOLVED;
    }
    *resolution = result;
    return RSS_DDC_OK;
}

void rss_ddc_monitor_knowledge_resolution_destroy(RSSDDCMonitorKnowledgeResolution *resolution) {
    free(resolution);
}

RSSDDCKnowledgeResolutionState rss_ddc_monitor_knowledge_resolution_state(
    const RSSDDCMonitorKnowledgeResolution *resolution) {
    return resolution == NULL ? RSS_DDC_KNOWLEDGE_RESOLUTION_UNRESOLVED : resolution->state;
}

bool rss_ddc_monitor_knowledge_resolution_has_conflict(
    const RSSDDCMonitorKnowledgeResolution *resolution) {
    return rss_ddc_monitor_knowledge_resolution_state(resolution) ==
           RSS_DDC_KNOWLEDGE_RESOLUTION_CONFLICT;
}

const RSSDDCKnowledgeRoute *rss_ddc_monitor_knowledge_resolution_preferred_read(
    const RSSDDCMonitorKnowledgeResolution *resolution) {
    return resolution == NULL ? NULL : resolution->read;
}

const RSSDDCKnowledgeRoute *rss_ddc_monitor_knowledge_resolution_preferred_write(
    const RSSDDCMonitorKnowledgeResolution *resolution) {
    return resolution == NULL ? NULL : resolution->write;
}

bool rss_ddc_monitor_knowledge_resolution_write_authorized(
    const RSSDDCMonitorKnowledgeResolution *resolution) {
    return resolution != NULL && resolution->state == RSS_DDC_KNOWLEDGE_RESOLUTION_RESOLVED &&
           resolution->write != NULL && resolution->write->write_authorized;
}

size_t rss_ddc_monitor_knowledge_resolution_candidate_count(
    const RSSDDCMonitorKnowledgeResolution *resolution) {
    return resolution == NULL ? 0 : resolution->count;
}

const RSSDDCKnowledgeRoute *rss_ddc_monitor_knowledge_resolution_candidate_at(
    const RSSDDCMonitorKnowledgeResolution *resolution, size_t index) {
    return resolution != NULL && index < resolution->count ? resolution->candidates[index] : NULL;
}
