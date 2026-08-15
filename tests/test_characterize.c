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
    assert(rss_ddc_characterization_normalize_semantic_id("gamma", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "gamma") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("sharpness", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "sharpness") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("response-time", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "response-time") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("adaptive-sync", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "adaptive-sync") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("energy-saving", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "energy-saving") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("black-stabilizer", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "black-stabilizer") == 0);
    assert(rss_ddc_characterization_normalize_semantic_id("audio-mute", out, sizeof(out)) == RSS_DDC_OK);
    assert(strcmp(out, "audio-mute") == 0);
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

static RSSDDCDisplay slice2_display(void) {
    RSSDDCDisplay display = {
        .list_index = 3,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_DCPDP13,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "Test");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT0");
    return display;
}

static const char *slice2_profile_pack(void) {
    return "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
           "\"packId\":\"slice2-test\",\"profiles\":[{\"id\":\"one\",\"identity\":{"
           "\"productName\":\"Test\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\","
           "\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":["
           "{\"id\":\"brightness\",\"method\":\"vcp\",\"address\":16,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]},"
           "{\"id\":\"input\",\"method\":\"vcp\",\"address\":96,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]},"
           "{\"id\":\"gamma\",\"method\":\"vcp\",\"address\":114,\"readable\":true,"
           "\"writable\":false,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";
}

static const RSSDDCKnowledgeRoute *route_with_semantic(const RSSDDCMonitorKnowledge *knowledge,
                                                       const char *semantic_id) {
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route != NULL && strcmp(route->semantic_id, semantic_id) == 0) {
            return route;
        }
    }
    return NULL;
}

static void test_assemble_identity_without_edid(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    assert(characterization != NULL);
    assert(rss_ddc_characterization_display(characterization) == NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    const RSSDDCDisplay *stored = rss_ddc_characterization_display(characterization);
    assert(stored != NULL);
    assert(stored->list_index == 3);
    assert(stored->external);
    assert(stored->provider == RSS_DDC_PROVIDER_DCPDP13);
    assert(strcmp(stored->product_name, "Test") == 0);
    assert(strcmp(stored->transport, "DCPEXT0") == 0);
    assert(rss_ddc_characterization_edid(characterization) == NULL);
    assert(rss_ddc_characterization_provider_capabilities(characterization) ==
           rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_DCPDP13));
    assert((rss_ddc_characterization_provider_capabilities(characterization) & RSS_DDC_CAP_GET_VCP) != 0);
    assert((rss_ddc_characterization_provider_capabilities(characterization) &
            RSS_DDC_CAP_MCCS_CAPABILITIES) != 0);
    assert(rss_ddc_characterization_profile_status(characterization) ==
           RSS_DDC_CHARACTERIZATION_PROFILE_NONE);
    assert(rss_ddc_characterization_effective_profile(characterization) == NULL);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           0);
    rss_ddc_characterization_destroy(characterization);
}

static void test_assemble_optional_edid_and_no_profile(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCEDIDInfo edid = {.manufacturer_id = "GSM", .product_code = 0x1234, .monitor_name = "Partial"};
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    assert(characterization != NULL && store != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, &edid, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_edid(characterization) != NULL);
    assert(strcmp(rss_ddc_characterization_edid(characterization)->manufacturer_id, "GSM") == 0);
    assert(rss_ddc_characterization_profile_status(characterization) ==
           RSS_DDC_CHARACTERIZATION_PROFILE_NONE);
    assert(rss_ddc_characterization_profile_identity(characterization) != NULL);
    assert(strcmp(rss_ddc_characterization_profile_identity(characterization)->product_name, "Test") ==
           0);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_assemble_matches_and_normalizes_profile_controls(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    const char *pack = slice2_profile_pack();
    const RSSDDCMonitorKnowledge *knowledge = NULL;
    const RSSDDCKnowledgeRoute *brightness = NULL;
    const RSSDDCKnowledgeRoute *input = NULL;
    const RSSDDCKnowledgeRoute *gamma = NULL;
    assert(characterization != NULL && store != NULL);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_profile_status(characterization) ==
           RSS_DDC_CHARACTERIZATION_PROFILE_MATCHED);
    assert(rss_ddc_characterization_effective_profile(characterization) != NULL);
    assert(rss_ddc_effective_profile_control_count(
               rss_ddc_characterization_effective_profile(characterization)) == 3);
    knowledge = rss_ddc_characterization_knowledge(characterization);
    assert(rss_ddc_monitor_knowledge_route_count(knowledge) == 3);
    brightness = route_with_semantic(knowledge, "display.brightness");
    input = route_with_semantic(knowledge, "inputs.switching");
    gamma = route_with_semantic(knowledge, "gamma");
    assert(brightness != NULL && input != NULL && gamma != NULL);
    assert(route_with_semantic(knowledge, "brightness") == NULL);
    assert(route_with_semantic(knowledge, "input") == NULL);
    assert(brightness->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_PROFILE);
    assert(brightness->provenance.confidence == RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED);
    assert(brightness->write_authorized);
    assert(brightness->address == 0x10);
    assert(strcmp(brightness->provenance.source_id, "slice2-test") == 0);
    assert(input->address == 0x60);
    assert(input->write_authorized);
    assert(gamma->address == 114);
    assert(!gamma->write_authorized);
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        assert(rss_ddc_monitor_knowledge_route_at(knowledge, index)->provenance.fact_kind !=
               RSS_DDC_KNOWLEDGE_FACT_DECLARED);
    }
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_assemble_retains_competing_facts(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *observed = rss_ddc_monitor_knowledge_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    const char *pack = slice2_profile_pack();
    RSSDDCKnowledgeRoute live =
        make_route("display.brightness", "alien-probe-live-read", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                   RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "vcp-10-live",
                   0x10, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 42, true, false, false);
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    assert(characterization != NULL && observed != NULL && store != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(observed, &live) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, observed) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           4);
    assert(rss_ddc_characterization_resolve(characterization, "brightness", &resolution) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution)->write_authorized);
    assert(rss_ddc_characterization_current_value(characterization, "display.brightness", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current->value.unsigned_value == 42);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_monitor_knowledge_destroy(observed);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_assemble_profile_conflict_is_explicit(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    const char *first =
        "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
        "\"packId\":\"a\",\"profiles\":[{\"id\":\"one\",\"identity\":{\"productName\":\"Test\","
        "\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},"
        "\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"brightness\",\"method\":\"vcp\","
        "\"address\":16,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\","
        "\"enums\":[]}]}]}";
    const char *second =
        "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
        "\"packId\":\"b\",\"profiles\":[{\"id\":\"two\",\"identity\":{\"productName\":\"Test\","
        "\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},"
        "\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"brightness\",\"method\":\"vcp\","
        "\"address\":18,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\","
        "\"enums\":[]}]}]}";
    assert(characterization != NULL && store != NULL);
    assert(rss_ddc_profile_store_load_local_data(store, first, strlen(first)) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_load_local_data(store, second, strlen(second)) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) ==
           RSS_DDC_ERROR_PROFILE_CONFLICT);
    assert(rss_ddc_characterization_profile_status(characterization) ==
           RSS_DDC_CHARACTERIZATION_PROFILE_CONFLICT);
    assert(rss_ddc_characterization_display(characterization) != NULL);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           0);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_assemble_profile_merge_overflow_is_explicit(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *batch = rss_ddc_monitor_knowledge_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    const char *pack = slice2_profile_pack();
    assert(characterization != NULL && batch != NULL && store != NULL);
    for (unsigned index = 0; index < 128; ++index) {
        char route_id[RSS_DDC_PROFILE_ID_MAX] = {};
        (void)snprintf(route_id, sizeof(route_id), "route-%u", index);
        RSSDDCKnowledgeRoute route =
            make_route("display.contrast", "fill", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                       RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, route_id,
                       (uint16_t)index, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, index, true, false, false);
        assert(rss_ddc_monitor_knowledge_add_route(batch, &route) == RSS_DDC_OK);
    }
    assert(rss_ddc_characterization_add_knowledge(characterization, batch) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) ==
           RSS_DDC_ERROR_PROFILE_CONFLICT);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           128);
    assert(rss_ddc_characterization_profile_status(characterization) ==
           RSS_DDC_CHARACTERIZATION_PROFILE_NONE);
    assert(rss_ddc_characterization_display(characterization) != NULL);
    rss_ddc_monitor_knowledge_destroy(batch);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_assemble_rejects_unresolved_display(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    assert(rss_ddc_characterization_assemble(NULL, &display, NULL, NULL) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_characterization_assemble(characterization, NULL, NULL, NULL) == RSS_DDC_ERROR_ARGUMENT);
    rss_ddc_characterization_destroy(characterization);
}

int main(void) {
    test_semantic_normalization();
    test_composition_retains_competing_facts();
    test_method_versus_current_value();
    test_unknown_does_not_replace_observed();
    test_conflicting_observed_values();
    test_capacity_overflow_is_explicit();
    test_assemble_identity_without_edid();
    test_assemble_optional_edid_and_no_profile();
    test_assemble_matches_and_normalizes_profile_controls();
    test_assemble_retains_competing_facts();
    test_assemble_profile_conflict_is_explicit();
    test_assemble_profile_merge_overflow_is_explicit();
    test_assemble_rejects_unresolved_display();
    puts("test_characterize: passed");
}
