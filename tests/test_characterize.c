#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "characterize.h"
#include "rss_ddc.h"

static RSSDDCKnowledgeRoute make_route(const char *semantic_id, const char *source_id,
                                       RSSDDCProfileSource source, RSSDDCProfileConfidence confidence,
                                       RSSDDCKnowledgeFactKind fact_kind, const char *route_id,
                                       uint16_t address, RSSDDCKnowledgeValueState value_state,
                                       unsigned value, bool readable, bool writable,
                                       bool write_authorized) {
    RSSDDCKnowledgeRoute route = {
        .kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP,
        .address = address,
        .readable = readable,
        .writable = writable,
        .write_authorized = write_authorized,
        .value = {.state = value_state, .unsigned_value = (uint16_t)value},
        .provenance = {.source = source, .confidence = confidence, .fact_kind = fact_kind},
    };
    (void)snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", semantic_id);
    (void)snprintf(route.route_id, sizeof(route.route_id), "%s", route_id);
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s", "mccs-vcp");
    (void)snprintf(route.command_semantics, sizeof(route.command_semantics), "%s", "test");
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s", source_id);
    return route;
}

static void test_semantic_normalization(void) {
    char out[RSS_DDC_TEXT_MAX] = {};
    assert(rss_ddc_characterization_normalize_semantic_id("brightness", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "display.brightness") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("display.brightness", out, sizeof(out)) ==
           RSS_DDC_OK);
    assert(strcmp(out, "display.brightness") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("contrast", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "display.contrast") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("picture-mode", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "display.picture_mode") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("picture_mode", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "display.picture_mode") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("input", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "inputs.switching") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("inputs.switching", out, sizeof(out)) ==
           RSS_DDC_OK);
    assert(strcmp(out, "inputs.switching") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("vendor.unknown.vcp.42", out, sizeof(out)) ==
           RSS_DDC_OK);
    assert(strcmp(out, "vendor.unknown.vcp.42") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("not-a-known-alias", out, sizeof(out)) ==
           RSS_DDC_OK);
    assert(strcmp(out, "not-a-known-alias") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("Brightness", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "Brightness") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id(NULL, out, sizeof(out)) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_characterization_normalize_semantic_id("", out, sizeof(out)) == RSS_DDC_ERROR_ARGUMENT);
}

static void test_composition_retains_competing_facts(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *profile = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledge *observed = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute profile_route =
        make_route("brightness", "profile-local", RSS_DDC_PROFILE_SOURCE_LOCAL,
                   RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED, RSS_DDC_KNOWLEDGE_FACT_PROFILE,
                   "vcp-10-profile", 0x10, RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN, 0, true, true, true);
    RSSDDCKnowledgeRoute observed_route =
        make_route("display.brightness", "alien-probe-live-read", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                   RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "vcp-10-live",
                   0x10, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 42, true, false, false);
    RSSDDCKnowledgeRoute competing_write =
        make_route("display.brightness", "alt-method", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                   RSS_DDC_PROFILE_CONFIDENCE_CANDIDATE, RSS_DDC_KNOWLEDGE_FACT_PROFILE, "lg-alt", 0xf4,
                   RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN, 0, false, true, false);
    assert(characterization != NULL && profile != NULL && observed != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(profile, &profile_route) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(observed, &observed_route) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(observed, &competing_write) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, profile) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, observed) == RSS_DDC_OK);
    const RSSDDCMonitorKnowledge *merged = rss_ddc_characterization_knowledge(characterization);
    bool saw_profile = false;
    bool saw_observed = false;
    bool saw_competing = false;
    assert(rss_ddc_monitor_knowledge_route_count(merged) == 3);
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(merged); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(merged, index);
        assert(strcmp(route->semantic_id, "display.brightness") == 0);
        if (strcmp(route->route_id, "vcp-10-profile") == 0) {
            saw_profile = true;
        } else if (strcmp(route->route_id, "vcp-10-live") == 0) {
            saw_observed = true;
        } else if (strcmp(route->route_id, "lg-alt") == 0) {
            saw_competing = true;
        }
    }
    assert(saw_profile && saw_observed && saw_competing);
    rss_ddc_monitor_knowledge_destroy(profile);
    rss_ddc_monitor_knowledge_destroy(observed);
    rss_ddc_characterization_destroy(characterization);
}

static void test_method_versus_current_value(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute profile_route =
        make_route("display.brightness", "validated-profile", RSS_DDC_PROFILE_SOURCE_LOCAL,
                   RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED, RSS_DDC_KNOWLEDGE_FACT_PROFILE,
                   "vcp-10-profile", 0x10, RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN, 0, true, true, true);
    RSSDDCKnowledgeRoute observed_route =
        make_route("display.brightness", "alien-probe-live-read", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                   RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "vcp-10-live",
                   0x10, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 42, true, false, false);
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    assert(characterization != NULL && knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &profile_route) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &observed_route) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, knowledge) == RSS_DDC_OK);
    assert(rss_ddc_characterization_resolve(characterization, "brightness", &resolution) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_state(resolution) == RSS_DDC_KNOWLEDGE_RESOLUTION_RESOLVED);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution)->write_authorized);
    assert(strcmp(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution)->provenance.source_id,
                  "validated-profile") == 0);
    assert(rss_ddc_characterization_current_value(characterization, "display.brightness", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current != NULL);
    assert(current->value.unsigned_value == 42);
    assert(current->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED);
    assert(!current->write_authorized);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_monitor_knowledge_destroy(knowledge);
    rss_ddc_characterization_destroy(characterization);
}

static void test_unknown_does_not_replace_observed(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *first = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledge *second = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute observed = make_route(
        "display.brightness", "live", RSS_DDC_PROFILE_SOURCE_RESEARCH, RSS_DDC_PROFILE_CONFIDENCE_OBSERVED,
        RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "live", 0x10, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 17, true, false,
        false);
    RSSDDCKnowledgeRoute unknown = make_route(
        "display.brightness", "profile", RSS_DDC_PROFILE_SOURCE_LOCAL,
        RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED, RSS_DDC_KNOWLEDGE_FACT_PROFILE, "profile", 0x10,
        RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN, 99, true, true, true);
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    assert(rss_ddc_monitor_knowledge_add_route(first, &observed) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(second, &unknown) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, first) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, second) == RSS_DDC_OK);
    assert(rss_ddc_characterization_current_value(characterization, "brightness", &value_state, &current) ==
           RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current->value.unsigned_value == 17);
    rss_ddc_monitor_knowledge_destroy(first);
    rss_ddc_monitor_knowledge_destroy(second);
    rss_ddc_characterization_destroy(characterization);
}

static void test_conflicting_observed_values(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute first = make_route(
        "display.brightness", "a-source", RSS_DDC_PROFILE_SOURCE_RESEARCH, RSS_DDC_PROFILE_CONFIDENCE_OBSERVED,
        RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "live-a", 0x10, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 10, true, false,
        false);
    RSSDDCKnowledgeRoute second = make_route(
        "display.brightness", "b-source", RSS_DDC_PROFILE_SOURCE_RESEARCH, RSS_DDC_PROFILE_CONFIDENCE_OBSERVED,
        RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "live-b", 0x10, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 11, true, false,
        false);
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = (const RSSDDCKnowledgeRoute *)0x1;
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &first) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &second) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, knowledge) == RSS_DDC_OK);
    assert(rss_ddc_characterization_current_value(characterization, "display.brightness", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_CONFLICT);
    assert(current == NULL);
    rss_ddc_monitor_knowledge_destroy(knowledge);
    rss_ddc_characterization_destroy(characterization);
}

static void test_capacity_overflow_is_explicit(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *batch = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledge *extra = rss_ddc_monitor_knowledge_create();
    assert(characterization != NULL && batch != NULL && extra != NULL);
    for (unsigned index = 0; index < 128; ++index) {
        char route_id[RSS_DDC_PROFILE_ID_MAX] = {};
        (void)snprintf(route_id, sizeof(route_id), "route-%u", index);
        RSSDDCKnowledgeRoute route =
            make_route("display.brightness", "fill", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                       RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, route_id,
                       (uint16_t)index, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, index, true, false, false);
        assert(rss_ddc_monitor_knowledge_add_route(batch, &route) == RSS_DDC_OK);
    }
    assert(rss_ddc_characterization_add_knowledge(characterization, batch) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           128);
    RSSDDCKnowledgeRoute overflow =
        make_route("display.brightness", "overflow", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                   RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "overflow", 0x99,
                   RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 1, true, false, false);
    assert(rss_ddc_monitor_knowledge_add_route(extra, &overflow) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, extra) == RSS_DDC_ERROR_PROFILE_CONFLICT);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           128);
    rss_ddc_monitor_knowledge_destroy(batch);
    rss_ddc_monitor_knowledge_destroy(extra);
    rss_ddc_characterization_destroy(characterization);
}

int main(void) {
    test_semantic_normalization();
    test_composition_retains_competing_facts();
    test_method_versus_current_value();
    test_unknown_does_not_replace_observed();
    test_conflicting_observed_values();
    test_capacity_overflow_is_explicit();
    puts("test_characterize: passed");
}
