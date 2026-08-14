#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rss_ddc.h"

static RSSDDCKnowledgeRoute make_route(const char *source_id, RSSDDCProfileSource source,
                                       RSSDDCProfileConfidence confidence, const char *route_id,
                                       uint16_t address, unsigned value) {
    RSSDDCKnowledgeRoute route = {
        .kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP,
        .address = address,
        .readable = true,
        .writable = true,
        .write_authorized = confidence == RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED,
        .value = {.state = RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, .unsigned_value = (uint16_t)value},
        .provenance = {.source = source,
                       .confidence = confidence,
                       .fact_kind = RSS_DDC_KNOWLEDGE_FACT_OBSERVED},
    };
    (void)snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", "inputs.switching");
    (void)snprintf(route.route_id, sizeof(route.route_id), "%s", route_id);
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s", "mccs-vcp");
    (void)snprintf(route.command_semantics, sizeof(route.command_semantics), "%s", "input selection");
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s", source_id);
    return route;
}

static void test_empty_and_invalid(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute invalid = {0};
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_route_count(knowledge) == 0);
    assert(rss_ddc_monitor_knowledge_route_at(knowledge, 0) == NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &invalid) == RSS_DDC_ERROR_ARGUMENT);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_single_source(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute route = make_route("one-source", RSS_DDC_PROFILE_SOURCE_LOCAL,
                                            RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, "vcp-10", 0x10, 42);
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    const RSSDDCMonitorKnowledge *sources[] = {knowledge};
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &route) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolve(sources, 1, "inputs.switching", &resolution) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution)->address == 0x10);
    assert(!rss_ddc_monitor_knowledge_resolution_write_authorized(resolution));
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_lossless_merge_and_provenance(void) {
    RSSDDCMonitorKnowledge *first = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledge *second = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledge *merged = NULL;
    RSSDDCKnowledgeRoute local = make_route("local-observation", RSS_DDC_PROFILE_SOURCE_LOCAL,
                                             RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED, "vcp-60", 0x60, 0x11);
    RSSDDCKnowledgeRoute declared = local;
    (void)snprintf(declared.provenance.source_id, sizeof(declared.provenance.source_id), "%s", "declared-mccs");
    declared.provenance.source = RSS_DDC_PROFILE_SOURCE_BUILTIN;
    declared.provenance.confidence = RSS_DDC_PROFILE_CONFIDENCE_OBSERVED;
    declared.provenance.fact_kind = RSS_DDC_KNOWLEDGE_FACT_DECLARED;
    (void)snprintf(declared.provenance.evidence_id, sizeof(declared.provenance.evidence_id), "%s", "mccs-caps");

    assert(first != NULL && second != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(first, &local) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(first, &local) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(second, &declared) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_merge(first, second, &merged) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_route_count(merged) == 2);
    assert(strcmp(rss_ddc_monitor_knowledge_route_at(merged, 1)->provenance.source_id, "declared-mccs") == 0);
    assert(rss_ddc_monitor_knowledge_route_at(merged, 1)->provenance.fact_kind ==
           RSS_DDC_KNOWLEDGE_FACT_DECLARED);

    /* a905c4b regression: merged facts are copied, not borrowed from inputs. */
    rss_ddc_monitor_knowledge_destroy(first);
    rss_ddc_monitor_knowledge_destroy(second);
    assert(rss_ddc_monitor_knowledge_route_count(merged) == 2);
    assert(strcmp(rss_ddc_monitor_knowledge_route_at(merged, 0)->provenance.source_id,
                  "local-observation") == 0);
    rss_ddc_monitor_knowledge_destroy(merged);
}

static void test_explicit_value_states_and_routes(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute unknown = make_route("candidate", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                                               RSS_DDC_PROFILE_CONFIDENCE_CANDIDATE, "unknown", 0x60, 0);
    RSSDDCKnowledgeRoute unsupported = unknown;
    unknown.value.state = RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN;
    unsupported.value.state = RSS_DDC_KNOWLEDGE_VALUE_UNSUPPORTED;
    unsupported.kind = RSS_DDC_KNOWLEDGE_ROUTE_UNSUPPORTED;
    (void)snprintf(unsupported.route_id, sizeof(unsupported.route_id), "%s", "unsupported");
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &unknown) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &unsupported) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_route_count(knowledge) == 2);
    assert(rss_ddc_monitor_knowledge_route_at(knowledge, 0)->value.state == RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN);
    assert(rss_ddc_monitor_knowledge_route_at(knowledge, 1)->value.state == RSS_DDC_KNOWLEDGE_VALUE_UNSUPPORTED);
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

static void test_deterministic_resolution(void) {
    RSSDDCMonitorKnowledge *first = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledge *second = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute a = make_route("a-source", RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK,
                                         RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED, "vcp-60", 0x60, 0x11);
    RSSDDCKnowledgeRoute b = a;
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    const RSSDDCMonitorKnowledge *forward[] = {first, second};
    const RSSDDCMonitorKnowledge *reverse[] = {second, first};
    (void)snprintf(b.provenance.source_id, sizeof(b.provenance.source_id), "%s", "z-source");
    assert(first != NULL && second != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(first, &a) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(second, &b) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolve(forward, 2, "inputs.switching", &resolution) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_state(resolution) == RSS_DDC_KNOWLEDGE_RESOLUTION_RESOLVED);
    assert(!rss_ddc_monitor_knowledge_resolution_has_conflict(resolution));
    assert(rss_ddc_monitor_knowledge_resolution_candidate_count(resolution) == 2);
    assert(strcmp(rss_ddc_monitor_knowledge_resolution_preferred_write(resolution)->provenance.source_id,
                  "a-source") == 0);
    assert(rss_ddc_monitor_knowledge_resolution_write_authorized(resolution));
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    resolution = NULL;
    assert(rss_ddc_monitor_knowledge_resolve(reverse, 2, "inputs.switching", &resolution) == RSS_DDC_OK);
    assert(strcmp(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution)->provenance.source_id,
                  "a-source") == 0);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_monitor_knowledge_destroy(second);
    rss_ddc_monitor_knowledge_destroy(first);
}

static void test_conflict_and_higher_authority(void) {
    RSSDDCMonitorKnowledge *first = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledge *second = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    RSSDDCKnowledgeRoute local = make_route("local", RSS_DDC_PROFILE_SOURCE_LOCAL,
                                             RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED, "vcp-60", 0x60, 0x11);
    RSSDDCKnowledgeRoute research = make_route("research", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                                                RSS_DDC_PROFILE_CONFIDENCE_CANDIDATE, "vendor-f4", 0xf4, 0x12);
    const RSSDDCMonitorKnowledge *sources[] = {first, second};
    assert(first != NULL && second != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(first, &local) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(second, &research) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolve(sources, 2, "inputs.switching", &resolution) == RSS_DDC_OK);
    assert(!rss_ddc_monitor_knowledge_resolution_has_conflict(resolution));
    assert(rss_ddc_monitor_knowledge_resolution_preferred_write(resolution)->address == 0x60);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);

    research.provenance.confidence = RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED;
    research.provenance.source = RSS_DDC_PROFILE_SOURCE_LOCAL;
    assert(rss_ddc_monitor_knowledge_add_route(second, &research) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolve(sources, 2, "inputs.switching", &resolution) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_has_conflict(resolution));
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution) == NULL);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_write(resolution) == NULL);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_monitor_knowledge_destroy(second);
    rss_ddc_monitor_knowledge_destroy(first);
}

static void test_profile_integration_and_cycles(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCProfileControl control = {.id = RSS_DDC_PROFILE_CONTROL_INPUT,
                                    .method = RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT,
                                    .address = 0xf4,
                                    .writable = true,
                                    .write_authorized = true,
                                    .source = RSS_DDC_PROFILE_SOURCE_LOCAL,
                                    .confidence = RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED};
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_profile_control(knowledge, "inputs.switching", "profile-lg",
                                                          &control) == RSS_DDC_OK);
    const RSSDDCKnowledgeRoute *profile_route = rss_ddc_monitor_knowledge_route_at(knowledge, 0);
    assert(profile_route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_PROFILE);
    assert(profile_route->kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT);
    assert(profile_route->write_authorized);
    for (unsigned index = 0; index < 16; ++index) {
        RSSDDCMonitorKnowledge *copy = NULL;
        assert(rss_ddc_monitor_knowledge_merge(knowledge, knowledge, &copy) == RSS_DDC_OK);
        assert(rss_ddc_monitor_knowledge_route_count(copy) == 1);
        rss_ddc_monitor_knowledge_destroy(copy);
    }
    rss_ddc_monitor_knowledge_destroy(knowledge);
}

int main(void) {
    test_empty_and_invalid();
    test_single_source();
    test_lossless_merge_and_provenance();
    test_explicit_value_states_and_routes();
    test_deterministic_resolution();
    test_conflict_and_higher_authority();
    test_profile_integration_and_cycles();
    puts("test_monitor_knowledge: passed");
}
