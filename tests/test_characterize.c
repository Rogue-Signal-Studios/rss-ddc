#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "characterize.h"
#include "input_switch.h"
#include "rss_ddc.h"

/* probe.c references these platform helpers; characterization tests never call them. */
RSSDDCError rss_ddc_get_display(uint32_t index, RSSDDCDisplay *display) {
    (void)index;
    (void)display;
    return RSS_DDC_ERROR_DISCOVERY;
}
RSSDDCError rss_ddc_get_vcp(uint32_t index, uint8_t code, RSSDDCVCPResult *result) {
    (void)index;
    (void)code;
    (void)result;
    return RSS_DDC_ERROR_DISCOVERY;
}
RSSDDCError rss_ddc_get_mccs_capabilities(uint32_t index, RSSDDCMCCSCapabilities *capabilities) {
    (void)index;
    (void)capabilities;
    return RSS_DDC_ERROR_DISCOVERY;
}

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

static RSSDDCDisplay slice3_ps190_display(void) {
    RSSDDCDisplay display = {
        .list_index = 1,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_PS190,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "Studio Display");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "unknown");
    return display;
}

static void test_passive_mccs_requires_transport_capability(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice3_ps190_display();
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(!rss_ddc_characterization_mccs_supported(characterization));
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, "vcp(10 12)", 9) ==
           RSS_DDC_OK);
    assert(rss_ddc_characterization_mccs_status(characterization) ==
           RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(!rss_ddc_characterization_mccs_attempted(characterization));
    assert(rss_ddc_characterization_mccs(characterization) == NULL);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           0);
    rss_ddc_characterization_destroy(characterization);
}

static void test_passive_mccs_declares_known_and_unknown_vcps(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    const char *raw = "prot(monitor)type(lcd)vcp(10 12 15 60(0F 11) 42)";
    const RSSDDCMonitorKnowledge *knowledge = NULL;
    const RSSDDCKnowledgeRoute *brightness = NULL;
    const RSSDDCKnowledgeRoute *contrast = NULL;
    const RSSDDCKnowledgeRoute *picture = NULL;
    const RSSDDCKnowledgeRoute *input = NULL;
    const RSSDDCKnowledgeRoute *unknown = NULL;
    const uint8_t *values = NULL;
    size_t count = 0;
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_mccs_supported(characterization));
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           0);
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, raw, strlen(raw)) ==
           RSS_DDC_OK);
    assert(rss_ddc_characterization_mccs_attempted(characterization));
    assert(rss_ddc_characterization_mccs_status(characterization) == RSS_DDC_OK);
    knowledge = rss_ddc_characterization_knowledge(characterization);
    brightness = route_with_semantic(knowledge, "display.brightness");
    contrast = route_with_semantic(knowledge, "display.contrast");
    picture = route_with_semantic(knowledge, "display.picture_mode");
    input = route_with_semantic(knowledge, "inputs.switching");
    unknown = route_with_semantic(knowledge, "vendor.unknown.vcp.42");
    assert(brightness != NULL && contrast != NULL && picture != NULL && input != NULL &&
           unknown != NULL);
    assert(brightness->address == 0x10);
    assert(contrast->address == 0x12);
    assert(picture->address == 0x15);
    assert(input->address == 0x60);
    assert(unknown->address == 0x42);
    assert(brightness->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_DECLARED);
    assert(!brightness->write_authorized);
    assert(!brightness->writable);
    assert(!input->write_authorized);
    assert(brightness->value.state == RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN);
    assert(rss_ddc_mccs_capabilities_enum_values(rss_ddc_characterization_mccs(characterization), 0x60,
                                                 &values, &count) == RSS_DDC_OK);
    assert(count == 2 && values[0] == 0x0f && values[1] == 0x11);
    rss_ddc_characterization_destroy(characterization);
}

static void test_passive_mccs_preserves_profile_authorization(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    const char *pack = slice2_profile_pack();
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    const RSSDDCMonitorKnowledge *knowledge = NULL;
    size_t declared = 0;
    size_t profile = 0;
    assert(characterization != NULL && store != NULL);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, "vcp(10 12 60(0F 11))",
                                                             strlen("vcp(10 12 60(0F 11))")) == RSS_DDC_OK);
    knowledge = rss_ddc_characterization_knowledge(characterization);
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_DECLARED) {
            ++declared;
            assert(!route->write_authorized);
        } else if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_PROFILE) {
            ++profile;
        }
    }
    assert(declared >= 3 && profile == 3);
    assert(rss_ddc_characterization_resolve(characterization, "display.brightness", &resolution) ==
           RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution)->write_authorized);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution)->provenance.fact_kind ==
           RSS_DDC_KNOWLEDGE_FACT_PROFILE);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_passive_mccs_malformed_and_failed_preserve_state(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    const char *pack = slice2_profile_pack();
    size_t before = 0;
    assert(characterization != NULL && store != NULL);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    before = rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization));
    assert(before == 3);
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, "vcp(10 12", 9) ==
           RSS_DDC_OK);
    assert(rss_ddc_characterization_mccs_attempted(characterization));
    assert(rss_ddc_characterization_mccs_status(characterization) != RSS_DDC_OK);
    assert(rss_ddc_characterization_mccs(characterization) == NULL);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           before);
    assert(rss_ddc_characterization_display(characterization) != NULL);
    assert(rss_ddc_characterization_profile_status(characterization) ==
           RSS_DDC_CHARACTERIZATION_PROFILE_MATCHED);
    assert(rss_ddc_characterization_collect_passive_mccs_failed(characterization, RSS_DDC_ERROR_READ) ==
           RSS_DDC_OK);
    assert(rss_ddc_characterization_mccs_status(characterization) == RSS_DDC_ERROR_READ);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           before);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_passive_mccs_overflow_is_explicit(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *batch = rss_ddc_monitor_knowledge_create();
    RSSDDCDisplay display = slice2_display();
    assert(characterization != NULL && batch != NULL);
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
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, "vcp(10)", 7) ==
           RSS_DDC_ERROR_PROFILE_CONFLICT);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           128);
    assert(rss_ddc_characterization_display(characterization) != NULL);
    rss_ddc_monitor_knowledge_destroy(batch);
    rss_ddc_characterization_destroy(characterization);
}

typedef struct {
    RSSDDCError error;
    RSSDDCVCPResult result;
} Slice4Reply;

typedef struct {
    Slice4Reply replies[RSS_DDC_PROBE_QUICK_CONTROL_COUNT][RSS_DDC_PROBE_QUICK_REPEAT_COUNT];
    unsigned attempts[RSS_DDC_PROBE_QUICK_CONTROL_COUNT];
} Slice4MockGet;

static const uint8_t slice4_quick_codes[RSS_DDC_PROBE_QUICK_CONTROL_COUNT] = {0x10, 0x12, 0x14, 0x16,
                                                                            0x18, 0x1a};

static size_t slice4_code_index(uint8_t code) {
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        if (slice4_quick_codes[index] == code) {
            return index;
        }
    }
    return RSS_DDC_PROBE_QUICK_CONTROL_COUNT;
}

static RSSDDCError slice4_mock_get_vcp(void *context, uint8_t code, RSSDDCVCPResult *result) {
    Slice4MockGet *mock = context;
    size_t index = slice4_code_index(code);
    assert(index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT);
    unsigned attempt = mock->attempts[index]++;
    assert(attempt < RSS_DDC_PROBE_QUICK_REPEAT_COUNT);
    Slice4Reply reply = mock->replies[index][attempt];
    if (reply.error == RSS_DDC_OK) {
        *result = reply.result;
    }
    return reply.error;
}

static void slice4_set_reply(Slice4MockGet *mock, size_t index, unsigned attempt, RSSDDCError error,
                             uint8_t echoed, uint16_t maximum, uint16_t current) {
    mock->replies[index][attempt] = (Slice4Reply){
        .error = error,
        .result = {.vcp_code = echoed, .maximum_value = maximum, .current_value = current},
    };
}

static void slice4_set_stable(Slice4MockGet *mock, size_t index, uint16_t maximum, uint16_t current) {
    slice4_set_reply(mock, index, 0, RSS_DDC_OK, slice4_quick_codes[index], maximum, current);
    slice4_set_reply(mock, index, 1, RSS_DDC_OK, slice4_quick_codes[index], maximum, current);
}

static RSSDDCProbe *slice4_run_quick(Slice4MockGet *mock, const RSSDDCDisplay *display) {
    RSSDDCProbeReadTransport transport = {.context = mock, .get_vcp = slice4_mock_get_vcp};
    RSSDDCProbeTarget target = {.correlation = RSS_DDC_PROBE_CORRELATION_EXACT, .display = *display};
    RSSDDCProbe *probe = NULL;
    assert(rss_ddc_probe_create(&target, &transport, &probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_quick(probe) == RSS_DDC_OK);
    return probe;
}

static size_t count_fact_kind(const RSSDDCMonitorKnowledge *knowledge, const char *semantic_id,
                              RSSDDCKnowledgeFactKind kind) {
    size_t count = 0;
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route != NULL && strcmp(route->semantic_id, semantic_id) == 0 &&
            route->provenance.fact_kind == kind) {
            ++count;
        }
    }
    return count;
}

static const RSSDDCKnowledgeRoute *route_with_kind(const RSSDDCMonitorKnowledge *knowledge,
                                                   const char *semantic_id,
                                                   RSSDDCKnowledgeFactKind kind) {
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route != NULL && strcmp(route->semantic_id, semantic_id) == 0 &&
            route->provenance.fact_kind == kind) {
            return route;
        }
    }
    return NULL;
}

static RSSDDCDisplay slice4_no_get_display(void) {
    RSSDDCDisplay display = {
        .list_index = 2,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_MCDP29XX,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "Internal");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "unknown");
    return display;
}

static void test_quick_probe_observed_brightness_and_current_value(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    Slice4MockGet mock = {0};
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    const RSSDDCKnowledgeRoute *observed = NULL;
    RSSDDCProbe *probe = NULL;
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        slice4_set_stable(&mock, index, 100, (uint16_t)(10 + index));
    }
    slice4_set_stable(&mock, 0, 100, 42);
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_quick_supported(characterization));
    probe = slice4_run_quick(&mock, &display);
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) == RSS_DDC_OK);
    assert(rss_ddc_characterization_quick_attempted(characterization));
    assert(rss_ddc_characterization_quick_status(characterization) == RSS_DDC_OK);
    observed = route_with_kind(rss_ddc_characterization_knowledge(characterization),
                               "display.brightness", RSS_DDC_KNOWLEDGE_FACT_OBSERVED);
    assert(observed != NULL);
    assert(observed->value.unsigned_value == 42);
    assert(!observed->write_authorized);
    assert(!observed->writable);
    assert(strcmp(observed->provenance.source_id, "alien-probe-live-read") == 0);
    assert(rss_ddc_characterization_current_value(characterization, "brightness", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current == observed);
    assert(rss_ddc_characterization_quick_diagnostics(characterization) != NULL);
    assert(rss_ddc_characterization_quick_diagnostics(characterization)->observation_count ==
           RSS_DDC_PROBE_QUICK_CONTROL_COUNT);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_quick_probe_profile_declared_observed_coexist(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    Slice4MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    const RSSDDCMonitorKnowledge *knowledge = NULL;
    const char *pack = slice2_profile_pack();
    const char *raw = "vcp(10 12 15 60)";
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        slice4_set_stable(&mock, index, 100, 1);
    }
    slice4_set_stable(&mock, 0, 100, 42);
    assert(characterization != NULL && store != NULL);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, raw, strlen(raw)) ==
           RSS_DDC_OK);
    probe = slice4_run_quick(&mock, &display);
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) == RSS_DDC_OK);
    knowledge = rss_ddc_characterization_knowledge(characterization);
    assert(count_fact_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_PROFILE) == 1);
    assert(count_fact_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_DECLARED) == 1);
    assert(count_fact_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 1);
    assert(route_with_kind(knowledge, "display.picture_mode", RSS_DDC_KNOWLEDGE_FACT_DECLARED) != NULL);
    assert(route_with_kind(knowledge, "inputs.switching", RSS_DDC_KNOWLEDGE_FACT_DECLARED) != NULL);
    assert(route_with_kind(knowledge, "display.picture_mode", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == NULL);
    assert(route_with_kind(knowledge, "inputs.switching", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == NULL);
    assert(rss_ddc_characterization_resolve(characterization, "brightness", &resolution) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution)->write_authorized);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_read(resolution)->provenance.fact_kind ==
           RSS_DDC_KNOWLEDGE_FACT_PROFILE);
    assert(rss_ddc_characterization_current_value(characterization, "display.brightness", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED);
    assert(current->value.unsigned_value == 42);
    assert(!current->write_authorized);
    assert(route_with_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_DECLARED)
               ->value.state == RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_probe_destroy(probe);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_quick_probe_failures_remain_diagnostics(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    Slice4MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    const RSSDDCProbeDiagnostics *diagnostics = NULL;
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    const RSSDDCMonitorKnowledge *knowledge = NULL;
    slice4_set_stable(&mock, 0, 10, 18);
    slice4_set_reply(&mock, 1, 0, RSS_DDC_OK, 0x12, 100, 50);
    slice4_set_reply(&mock, 1, 1, RSS_DDC_OK, 0x12, 100, 51);
    slice4_set_reply(&mock, 2, 0, RSS_DDC_ERROR_REPLY_STATUS, 0, 0, 0);
    slice4_set_reply(&mock, 3, 0, RSS_DDC_ERROR_READ, 0, 0, 0);
    slice4_set_reply(&mock, 4, 0, RSS_DDC_ERROR_REPLY_CHECKSUM, 0, 0, 0);
    slice4_set_reply(&mock, 5, 0, RSS_DDC_OK, 0x14, 4, 1);
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    probe = slice4_run_quick(&mock, &display);
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) == RSS_DDC_OK);
    diagnostics = rss_ddc_characterization_quick_diagnostics(characterization);
    assert(diagnostics != NULL);
    assert(diagnostics->controls_protocol_valid == 2);
    assert(diagnostics->controls_stable == 1);
    assert(diagnostics->controls_variable == 1);
    assert(diagnostics->controls_protocol_reported == 1);
    assert(diagnostics->controls_malformed == 2);
    assert(diagnostics->controls_transport_error == 1);
    assert(diagnostics->observations[0].current_exceeds_maximum);
    assert(diagnostics->observations[0].current_value == 18);
    assert(diagnostics->observations[0].maximum_value == 10);
    assert(diagnostics->observations[1].category == RSS_DDC_PROBE_RESULT_VARIABLE);
    assert(diagnostics->observations[2].category == RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED);
    assert(diagnostics->observations[3].category == RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR);
    assert(diagnostics->observations[4].category == RSS_DDC_PROBE_RESULT_MALFORMED);
    assert(diagnostics->observations[5].category == RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH);
    knowledge = rss_ddc_characterization_knowledge(characterization);
    assert(count_fact_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 1);
    assert(route_with_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_OBSERVED)
               ->value.unsigned_value == 18);
    assert(count_fact_kind(knowledge, "display.contrast", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 1);
    assert(route_with_kind(knowledge, "display.contrast", RSS_DDC_KNOWLEDGE_FACT_OBSERVED)
               ->value.unsigned_value == 50);
    assert(count_fact_kind(knowledge, "display.color_preset", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 0);
    assert(count_fact_kind(knowledge, "display.rgb.red_gain", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 0);
    assert(count_fact_kind(knowledge, "display.rgb.green_gain", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 0);
    assert(count_fact_kind(knowledge, "display.rgb.blue_gain", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 0);
    assert(rss_ddc_characterization_current_value(characterization, "display.brightness", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current->value.unsigned_value == 18);
    assert(current->reported_maximum == 10);
    value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    current = NULL;
    assert(rss_ddc_characterization_current_value(characterization, "display.contrast", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current->value.unsigned_value == 50);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_quick_probe_requires_get_capability(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice4_no_get_display();
    Slice4MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    slice4_set_stable(&mock, 0, 100, 42);
    for (size_t index = 1; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        slice4_set_stable(&mock, index, 100, 1);
    }
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(!rss_ddc_characterization_quick_supported(characterization));
    probe = slice4_run_quick(&mock, &display);
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) == RSS_DDC_OK);
    assert(!rss_ddc_characterization_quick_attempted(characterization));
    assert(rss_ddc_characterization_quick_status(characterization) ==
           RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(rss_ddc_characterization_quick_diagnostics(characterization) == NULL);
    assert(count_fact_kind(rss_ddc_characterization_knowledge(characterization), "display.brightness",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 0);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_quick_probe_failure_preserves_prior_knowledge(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    const char *pack = slice2_profile_pack();
    size_t before = 0;
    assert(characterization != NULL && store != NULL);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, "vcp(10 12)",
                                                             strlen("vcp(10 12)")) == RSS_DDC_OK);
    before = rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization));
    assert(before > 0);
    assert(rss_ddc_characterization_collect_quick_probe_failed(characterization, RSS_DDC_ERROR_READ) ==
           RSS_DDC_OK);
    assert(rss_ddc_characterization_quick_attempted(characterization));
    assert(rss_ddc_characterization_quick_status(characterization) == RSS_DDC_ERROR_READ);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           before);
    assert(count_fact_kind(rss_ddc_characterization_knowledge(characterization), "display.brightness",
                           RSS_DDC_KNOWLEDGE_FACT_PROFILE) == 1);
    assert(count_fact_kind(rss_ddc_characterization_knowledge(characterization), "display.brightness",
                           RSS_DDC_KNOWLEDGE_FACT_DECLARED) == 1);
    assert(count_fact_kind(rss_ddc_characterization_knowledge(characterization), "display.brightness",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 0);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_quick_probe_overflow_is_explicit(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *batch = rss_ddc_monitor_knowledge_create();
    RSSDDCDisplay display = slice2_display();
    Slice4MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    for (unsigned index = 0; index < 128; ++index) {
        char route_id[RSS_DDC_PROFILE_ID_MAX] = {};
        (void)snprintf(route_id, sizeof(route_id), "route-%u", index);
        RSSDDCKnowledgeRoute route =
            make_route("display.contrast", "fill", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                       RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, route_id,
                       (uint16_t)index, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, index, true, false, false);
        assert(rss_ddc_monitor_knowledge_add_route(batch, &route) == RSS_DDC_OK);
    }
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        slice4_set_stable(&mock, index, 100, 1);
    }
    assert(characterization != NULL && batch != NULL);
    assert(rss_ddc_characterization_add_knowledge(characterization, batch) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    probe = slice4_run_quick(&mock, &display);
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) ==
           RSS_DDC_ERROR_PROFILE_CONFLICT);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           128);
    assert(rss_ddc_characterization_display(characterization) != NULL);
    rss_ddc_probe_destroy(probe);
    rss_ddc_monitor_knowledge_destroy(batch);
    rss_ddc_characterization_destroy(characterization);
}

static const char *slice5_actionable_pack_dcpdp13(void) {
    return "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
           "\"packId\":\"slice5-dcpdp13\",\"profiles\":[{\"id\":\"one\",\"identity\":{"
           "\"productName\":\"Test\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\","
           "\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":["
           "{\"id\":\"brightness\",\"method\":\"vcp\",\"address\":16,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]},"
           "{\"id\":\"contrast\",\"method\":\"vcp\",\"address\":18,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]},"
           "{\"id\":\"picture-mode\",\"method\":\"vcp\",\"address\":21,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":["
           "{\"id\":\"vivid\",\"name\":\"Vivid\",\"value\":49},"
           "{\"id\":\"reader\",\"name\":\"Reader\",\"value\":1}]},"
           "{\"id\":\"input\",\"method\":\"vcp\",\"address\":96,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";
}

static const char *slice5_actionable_pack_mcdp(void) {
    return "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
           "\"packId\":\"slice5-mcdp\",\"profiles\":[{\"id\":\"one\",\"identity\":{"
           "\"productName\":\"Internal\",\"provider\":\"AppleDCPMCDP29XX\",\"transport\":\"unknown\","
           "\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":["
           "{\"id\":\"brightness\",\"method\":\"vcp\",\"address\":16,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]},"
           "{\"id\":\"contrast\",\"method\":\"vcp\",\"address\":18,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]},"
           "{\"id\":\"picture-mode\",\"method\":\"vcp\",\"address\":21,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":["
           "{\"id\":\"vivid\",\"name\":\"Vivid\",\"value\":49},"
           "{\"id\":\"reader\",\"name\":\"Reader\",\"value\":1}]},"
           "{\"id\":\"input\",\"method\":\"vcp\",\"address\":96,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";
}

static const char *slice5_pack_without_picture_mode(void) {
    return "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
           "\"packId\":\"slice5-no-picture\",\"profiles\":[{\"id\":\"one\",\"identity\":{"
           "\"productName\":\"Test\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\","
           "\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":["
           "{\"id\":\"brightness\",\"method\":\"vcp\",\"address\":16,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]},"
           "{\"id\":\"contrast\",\"method\":\"vcp\",\"address\":18,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]},"
           "{\"id\":\"input\",\"method\":\"vcp\",\"address\":96,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";
}

static RSSDDCCharacterization *slice5_assembled(const RSSDDCDisplay *display, const char *pack) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCProfileStore *store = NULL;
    assert(characterization != NULL);
    if (pack != NULL) {
        store = rss_ddc_profile_store_create();
        assert(store != NULL);
        assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    }
    assert(rss_ddc_characterization_assemble(characterization, display, NULL, store) == RSS_DDC_OK);
    rss_ddc_profile_store_destroy(store);
    return characterization;
}

static RSSDDCProbe *slice5_quick_all_stable(Slice4MockGet *mock, const RSSDDCDisplay *display,
                                            uint16_t brightness) {
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        slice4_set_stable(mock, index, 100, 1);
    }
    slice4_set_stable(mock, 0, 100, brightness);
    return slice4_run_quick(mock, display);
}

static void test_sufficiency_quick_success_is_not_enough(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    Slice4MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    RSSDDCCharacterizationSufficiencyResult result = {0};
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    probe = slice5_quick_all_stable(&mock, &display, 42);
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) == RSS_DDC_OK);
    assert(rss_ddc_characterization_quick_status(characterization) == RSS_DDC_OK);
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT);
    assert((result.reasons & RSS_DDC_CHARACTERIZATION_REASON_UNRESOLVED_METHOD) != 0);
    assert(result.extended_recommended);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_sufficiency_actionable_profile_without_live_values(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice5_actionable_pack_dcpdp13());
    RSSDDCCharacterizationSufficiencyResult result = {0};
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED;
    const RSSDDCKnowledgeRoute *current = (const RSSDDCKnowledgeRoute *)0x1;
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    assert(!result.extended_recommended);
    assert(rss_ddc_characterization_current_value(characterization, "display.brightness", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED);
    rss_ddc_characterization_destroy(characterization);
}

static void test_sufficiency_missing_picture_mode_and_declared_only(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization =
        slice5_assembled(&display, slice5_pack_without_picture_mode());
    RSSDDCCharacterizationSufficiencyResult result = {0};
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, "vcp(15)",
                                                             strlen("vcp(15)")) == RSS_DDC_OK);
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT);
    assert((result.reasons & RSS_DDC_CHARACTERIZATION_REASON_UNRESOLVED_METHOD) != 0);
    assert(result.extended_recommended);
    rss_ddc_characterization_destroy(characterization);
}

static void test_sufficiency_validated_input_without_quick_0x60(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice5_actionable_pack_dcpdp13());
    Slice4MockGet mock = {0};
    RSSDDCProbe *probe = slice5_quick_all_stable(&mock, &display, 42);
    RSSDDCCharacterizationSufficiencyResult result = {0};
    const RSSDDCProbeDiagnostics *diagnostics = NULL;
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) == RSS_DDC_OK);
    diagnostics = rss_ddc_characterization_quick_diagnostics(characterization);
    assert(diagnostics != NULL);
    assert(diagnostics->observations[0].requested_vcp == 0x10);
    assert(route_with_kind(rss_ddc_characterization_knowledge(characterization), "inputs.switching",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == NULL);
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    assert(!result.extended_recommended);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_sufficiency_vendor_unknown_does_not_block(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice5_actionable_pack_dcpdp13());
    RSSDDCCharacterizationSufficiencyResult result = {0};
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, "vcp(10 12 15 60 42)",
                                                             strlen("vcp(10 12 15 60 42)")) == RSS_DDC_OK);
    assert(route_with_semantic(rss_ddc_characterization_knowledge(characterization),
                               "vendor.unknown.vcp.42") != NULL);
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    assert(!result.extended_recommended);
    rss_ddc_characterization_destroy(characterization);
}

static void test_sufficiency_no_get_with_and_without_profile(void) {
    RSSDDCDisplay display = slice4_no_get_display();
    RSSDDCCharacterization *with_profile = slice5_assembled(&display, slice5_actionable_pack_mcdp());
    RSSDDCCharacterization *without_profile = slice5_assembled(&display, NULL);
    RSSDDCCharacterizationSufficiencyResult result = {0};
    assert(!rss_ddc_characterization_quick_supported(with_profile));
    assert(rss_ddc_characterization_sufficiency(with_profile, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    assert(!result.extended_recommended);
    assert((result.reasons & RSS_DDC_CHARACTERIZATION_REASON_NO_GET_SUPPORT) != 0);
    result = (RSSDDCCharacterizationSufficiencyResult){0};
    assert(rss_ddc_characterization_sufficiency(without_profile, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT);
    assert(!result.extended_recommended);
    assert((result.reasons & RSS_DDC_CHARACTERIZATION_REASON_NO_GET_SUPPORT) != 0);
    rss_ddc_characterization_destroy(with_profile);
    rss_ddc_characterization_destroy(without_profile);
}

static void test_sufficiency_profile_conflict(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCCharacterizationSufficiencyResult result = {0};
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
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_CONFLICT);
    assert((result.reasons & RSS_DDC_CHARACTERIZATION_REASON_PROFILE_CONFLICT) != 0);
    assert(!result.extended_recommended);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_characterization_destroy(characterization);
}

static void test_sufficiency_variable_is_not_stable_current(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice5_actionable_pack_dcpdp13());
    Slice4MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    RSSDDCCharacterizationSufficiencyResult result = {0};
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        slice4_set_stable(&mock, index, 100, 1);
    }
    slice4_set_reply(&mock, 0, 0, RSS_DDC_OK, 0x10, 100, 50);
    slice4_set_reply(&mock, 0, 1, RSS_DDC_OK, 0x10, 100, 51);
    probe = slice4_run_quick(&mock, &display);
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) == RSS_DDC_OK);
    assert(rss_ddc_characterization_quick_diagnostics(characterization)->observations[0].category ==
           RSS_DDC_PROBE_RESULT_VARIABLE);
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    assert((result.reasons & RSS_DDC_CHARACTERIZATION_REASON_VARIABLE_OBSERVATION) != 0);
    assert(!result.extended_recommended);
    assert(rss_ddc_characterization_current_value(characterization, "display.brightness", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current->value.unsigned_value == 50);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_sufficiency_stable_quick_observation(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice5_actionable_pack_dcpdp13());
    Slice4MockGet mock = {0};
    RSSDDCProbe *probe = slice5_quick_all_stable(&mock, &display, 42);
    RSSDDCCharacterizationSufficiencyResult result = {0};
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) == RSS_DDC_OK);
    assert(rss_ddc_characterization_quick_diagnostics(characterization)->observations[0].category ==
           RSS_DDC_PROBE_RESULT_STABLE);
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    assert((result.reasons & RSS_DDC_CHARACTERIZATION_REASON_VARIABLE_OBSERVATION) == 0);
    assert(!result.extended_recommended);
    assert(rss_ddc_characterization_current_value(characterization, "display.brightness", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current->value.unsigned_value == 42);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

typedef struct {
    RSSDDCError error;
    RSSDDCVCPResult result;
} Slice6Reply;

typedef struct {
    Slice6Reply replies[RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT][RSS_DDC_PROBE_EXTENDED_REPEAT_COUNT];
    unsigned attempts[RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT];
} Slice6MockGet;

static RSSDDCError slice6_mock_get_vcp(void *context, uint8_t code, RSSDDCVCPResult *result) {
    Slice6MockGet *mock = context;
    unsigned attempt = mock->attempts[code]++;
    assert(attempt < RSS_DDC_PROBE_EXTENDED_REPEAT_COUNT);
    Slice6Reply reply = mock->replies[code][attempt];
    if (reply.error == RSS_DDC_OK) {
        *result = reply.result;
    }
    return reply.error;
}

static void slice6_set_reply(Slice6MockGet *mock, uint8_t code, unsigned attempt, RSSDDCError error,
                             uint8_t echoed, uint16_t maximum, uint16_t current) {
    mock->replies[code][attempt] = (Slice6Reply){
        .error = error,
        .result = {.vcp_code = echoed, .maximum_value = maximum, .current_value = current},
    };
}

static void slice6_set_stable(Slice6MockGet *mock, uint8_t code, uint16_t maximum, uint16_t current) {
    slice6_set_reply(mock, code, 0, RSS_DDC_OK, code, maximum, current);
    slice6_set_reply(mock, code, 1, RSS_DDC_OK, code, maximum, current);
}

static void slice6_fill_protocol_reported(Slice6MockGet *mock) {
    for (uint16_t code = 0; code < RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT; ++code) {
        slice6_set_reply(mock, (uint8_t)code, 0, RSS_DDC_ERROR_REPLY_STATUS, 0, 0, 0);
    }
}

static RSSDDCProbe *slice6_run_extended(Slice6MockGet *mock, const RSSDDCDisplay *display) {
    RSSDDCProbeReadTransport transport = {.context = mock, .get_vcp = slice6_mock_get_vcp};
    RSSDDCProbeTarget target = {.correlation = RSS_DDC_PROBE_CORRELATION_EXACT, .display = *display};
    RSSDDCProbe *probe = NULL;
    assert(rss_ddc_probe_create(&target, &transport, &probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_extended(probe) == RSS_DDC_OK);
    return probe;
}

static const char *slice6_pack_brightness_contrast(void) {
    return "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
           "\"packId\":\"slice6-bc\",\"profiles\":[{\"id\":\"one\",\"identity\":{"
           "\"productName\":\"Test\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\","
           "\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":["
           "{\"id\":\"brightness\",\"method\":\"vcp\",\"address\":16,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]},"
           "{\"id\":\"contrast\",\"method\":\"vcp\",\"address\":18,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";
}

static void test_extended_not_recommended_is_not_run(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice5_actionable_pack_dcpdp13());
    size_t before = rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization));
    RSSDDCCharacterizationSufficiencyResult sufficiency = {0};
    assert(rss_ddc_characterization_sufficiency(characterization, &sufficiency) == RSS_DDC_OK);
    assert(sufficiency.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    assert(!sufficiency.extended_recommended);
    assert(!rss_ddc_characterization_extended_attempted(characterization));
    assert(rss_ddc_characterization_extended_diagnostics(characterization) == NULL);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           before);
    rss_ddc_characterization_destroy(characterization);
}

static void test_extended_promotes_input_and_picture_mode(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    Slice6MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    RSSDDCCharacterizationSufficiencyResult before = {0};
    RSSDDCCharacterizationSufficiencyResult after = {0};
    const RSSDDCKnowledgeRoute *input = NULL;
    const RSSDDCKnowledgeRoute *picture = NULL;
    slice6_fill_protocol_reported(&mock);
    slice6_set_stable(&mock, 0x10, 100, 42);
    slice6_set_stable(&mock, 0x12, 100, 50);
    slice6_set_stable(&mock, 0x14, 4, 1);
    slice6_set_stable(&mock, 0x15, 255, 0x31);
    slice6_set_stable(&mock, 0x60, 18, 0x11);
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_sufficiency(characterization, &before) == RSS_DDC_OK);
    assert(before.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT);
    assert(before.extended_recommended);
    probe = slice6_run_extended(&mock, &display);
    assert(rss_ddc_characterization_collect_extended_probe(characterization, probe) == RSS_DDC_OK);
    assert(rss_ddc_characterization_extended_attempted(characterization));
    assert(rss_ddc_characterization_extended_diagnostics(characterization) != NULL);
    assert(rss_ddc_characterization_extended_diagnostics(characterization)->observation_count ==
           RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT);
    input = route_with_kind(rss_ddc_characterization_knowledge(characterization), "inputs.switching",
                            RSS_DDC_KNOWLEDGE_FACT_OBSERVED);
    picture = route_with_kind(rss_ddc_characterization_knowledge(characterization), "display.picture_mode",
                              RSS_DDC_KNOWLEDGE_FACT_OBSERVED);
    assert(input != NULL && picture != NULL);
    assert(input->address == 0x60);
    assert(picture->address == 0x15);
    assert(!input->write_authorized && !picture->write_authorized);
    assert(rss_ddc_characterization_sufficiency(characterization, &after) == RSS_DDC_OK);
    assert(after.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    assert(!after.extended_recommended);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_extended_coexists_and_does_not_authorize_write(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice5_pack_without_picture_mode());
    Slice4MockGet quick_mock = {0};
    Slice6MockGet extended_mock = {0};
    RSSDDCProbe *quick = NULL;
    RSSDDCProbe *extended = NULL;
    const RSSDDCMonitorKnowledge *knowledge = NULL;
    RSSDDCCharacterizationSufficiencyResult result = {0};
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, "vcp(10 15)",
                                                             strlen("vcp(10 15)")) == RSS_DDC_OK);
    quick = slice5_quick_all_stable(&quick_mock, &display, 42);
    assert(rss_ddc_characterization_collect_quick_probe(characterization, quick) == RSS_DDC_OK);
    slice6_fill_protocol_reported(&extended_mock);
    slice6_set_stable(&extended_mock, 0x10, 100, 42);
    slice6_set_stable(&extended_mock, 0x15, 255, 1);
    extended = slice6_run_extended(&extended_mock, &display);
    assert(rss_ddc_characterization_collect_extended_probe(characterization, extended) == RSS_DDC_OK);
    knowledge = rss_ddc_characterization_knowledge(characterization);
    assert(count_fact_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_PROFILE) == 1);
    assert(count_fact_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_DECLARED) == 1);
    assert(count_fact_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 1);
    assert(count_fact_kind(knowledge, "display.picture_mode", RSS_DDC_KNOWLEDGE_FACT_DECLARED) == 1);
    assert(count_fact_kind(knowledge, "display.picture_mode", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 1);
    assert(!route_with_kind(knowledge, "display.picture_mode", RSS_DDC_KNOWLEDGE_FACT_OBSERVED)
                ->write_authorized);
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    rss_ddc_probe_destroy(quick);
    rss_ddc_probe_destroy(extended);
    rss_ddc_characterization_destroy(characterization);
}

static void test_extended_failures_remain_diagnostics(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = slice2_display();
    Slice6MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    const RSSDDCProbeExtendedDiagnostics *diagnostics = NULL;
    const RSSDDCMonitorKnowledge *knowledge = NULL;
    slice6_fill_protocol_reported(&mock);
    slice6_set_stable(&mock, 0x10, 10, 18);
    slice6_set_reply(&mock, 0x12, 0, RSS_DDC_OK, 0x12, 100, 50);
    slice6_set_reply(&mock, 0x12, 1, RSS_DDC_OK, 0x12, 100, 51);
    slice6_set_reply(&mock, 0x61, 0, RSS_DDC_ERROR_REPLY_CHECKSUM, 0, 0, 0);
    slice6_set_reply(&mock, 0x62, 0, RSS_DDC_ERROR_READ, 0, 0, 0);
    slice6_set_reply(&mock, 0x63, 0, RSS_DDC_OK, 0x14, 1, 1);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    probe = slice6_run_extended(&mock, &display);
    assert(rss_ddc_characterization_collect_extended_probe(characterization, probe) == RSS_DDC_OK);
    diagnostics = rss_ddc_characterization_extended_diagnostics(characterization);
    assert(diagnostics->observations[0x10].observation.current_exceeds_maximum);
    assert(diagnostics->observations[0x10].observation.current_value == 18);
    assert(diagnostics->observations[0x12].observation.category == RSS_DDC_PROBE_RESULT_VARIABLE);
    assert(diagnostics->observations[0x61].observation.category == RSS_DDC_PROBE_RESULT_MALFORMED);
    assert(diagnostics->observations[0x62].observation.category == RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR);
    assert(diagnostics->observations[0x63].observation.category == RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH);
    knowledge = rss_ddc_characterization_knowledge(characterization);
    assert(route_with_kind(knowledge, "display.brightness", RSS_DDC_KNOWLEDGE_FACT_OBSERVED)
               ->value.unsigned_value == 18);
    assert(route_with_kind(knowledge, "display.contrast", RSS_DDC_KNOWLEDGE_FACT_OBSERVED)
               ->value.unsigned_value == 50);
    assert(count_fact_kind(knowledge, "display.rgb.red_gain", RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 0);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_extended_priority_beats_capacity(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice6_pack_brightness_contrast());
    RSSDDCMonitorKnowledge *batch = rss_ddc_monitor_knowledge_create();
    Slice6MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    const RSSDDCCharacterizationPromotionSummary *promotion = NULL;
    size_t before = 0;
    for (unsigned index = 0; index < 124; ++index) {
        char route_id[RSS_DDC_PROFILE_ID_MAX] = {};
        (void)snprintf(route_id, sizeof(route_id), "fill-%u", index);
        RSSDDCKnowledgeRoute route =
            make_route("display.contrast", "fill", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                       RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, route_id,
                       (uint16_t)index, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, index, true, false, false);
        assert(rss_ddc_monitor_knowledge_add_route(batch, &route) == RSS_DDC_OK);
    }
    assert(rss_ddc_characterization_add_knowledge(characterization, batch) == RSS_DDC_OK);
    before = rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization));
    assert(before == 126);
    slice6_fill_protocol_reported(&mock);
    slice6_set_stable(&mock, 0x15, 255, 1);
    slice6_set_stable(&mock, 0x42, 4, 99);
    slice6_set_stable(&mock, 0x43, 4, 7);
    slice6_set_stable(&mock, 0x60, 18, 0x11);
    probe = slice6_run_extended(&mock, &display);
    assert(rss_ddc_characterization_collect_extended_probe(characterization, probe) == RSS_DDC_OK);
    promotion = rss_ddc_characterization_extended_promotion(characterization);
    assert(promotion != NULL);
    assert(promotion->considered == RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT);
    assert(promotion->skipped_capacity >= 2);
    assert(rss_ddc_characterization_extended_diagnostics(characterization)->observation_count ==
           RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           128);
    assert(route_with_kind(rss_ddc_characterization_knowledge(characterization), "inputs.switching",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) != NULL);
    assert(route_with_kind(rss_ddc_characterization_knowledge(characterization), "display.picture_mode",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) != NULL);
    assert(route_with_semantic(rss_ddc_characterization_knowledge(characterization),
                               "vendor.unknown.vcp.42") == NULL);
    rss_ddc_probe_destroy(probe);
    rss_ddc_monitor_knowledge_destroy(batch);
    rss_ddc_characterization_destroy(characterization);
}

static void test_extended_advertised_before_unknown_and_still_insufficient(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice6_pack_brightness_contrast());
    RSSDDCMonitorKnowledge *batch = rss_ddc_monitor_knowledge_create();
    Slice6MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    RSSDDCCharacterizationSufficiencyResult result = {0};
    const RSSDDCCharacterizationPromotionSummary *promotion = NULL;
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, "vcp(15 42)",
                                                             strlen("vcp(15 42)")) == RSS_DDC_OK);
    for (unsigned index = 0; index < 123; ++index) {
        char route_id[RSS_DDC_PROFILE_ID_MAX] = {};
        (void)snprintf(route_id, sizeof(route_id), "fill-%u", index);
        RSSDDCKnowledgeRoute route =
            make_route("display.contrast", "fill", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                       RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, route_id,
                       (uint16_t)index, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, index, true, false, false);
        assert(rss_ddc_monitor_knowledge_add_route(batch, &route) == RSS_DDC_OK);
    }
    assert(rss_ddc_characterization_add_knowledge(characterization, batch) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           127);
    slice6_fill_protocol_reported(&mock);
    slice6_set_stable(&mock, 0x42, 4, 1);
    slice6_set_stable(&mock, 0x99, 4, 2);
    probe = slice6_run_extended(&mock, &display);
    assert(rss_ddc_characterization_collect_extended_probe(characterization, probe) == RSS_DDC_OK);
    promotion = rss_ddc_characterization_extended_promotion(characterization);
    assert(promotion->promoted >= 1);
    assert(route_with_kind(rss_ddc_characterization_knowledge(characterization), "vendor.unknown.vcp.42",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) != NULL);
    assert(route_with_semantic(rss_ddc_characterization_knowledge(characterization),
                               "vendor.unknown.vcp.99") == NULL);
    assert(rss_ddc_characterization_sufficiency(characterization, &result) == RSS_DDC_OK);
    assert(result.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT);
    assert(route_with_kind(rss_ddc_characterization_knowledge(characterization), "display.picture_mode",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == NULL);
    assert(route_with_kind(rss_ddc_characterization_knowledge(characterization), "display.picture_mode",
                           RSS_DDC_KNOWLEDGE_FACT_DECLARED) != NULL);
    rss_ddc_probe_destroy(probe);
    rss_ddc_monitor_knowledge_destroy(batch);
    rss_ddc_characterization_destroy(characterization);
}

static void test_extended_abort_preserves_prior_knowledge(void) {
    RSSDDCDisplay display = slice2_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, slice5_actionable_pack_dcpdp13());
    Slice6MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    size_t before = rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization));
    for (uint16_t code = 0; code < RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT; ++code) {
        slice6_set_reply(&mock, (uint8_t)code, 0, RSS_DDC_ERROR_READ, 0, 0, 0);
        slice6_set_reply(&mock, (uint8_t)code, 1, RSS_DDC_ERROR_READ, 0, 0, 0);
    }
    probe = slice6_run_extended(&mock, &display);
    assert(rss_ddc_characterization_collect_extended_probe(characterization, probe) == RSS_DDC_OK);
    assert(rss_ddc_characterization_extended_diagnostics(characterization)->aborted);
    assert(rss_ddc_monitor_knowledge_route_count(rss_ddc_characterization_knowledge(characterization)) ==
           before);
    assert(rss_ddc_characterization_extended_promotion(characterization)->promoted == 0);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_extended_no_get_is_not_run(void) {
    RSSDDCDisplay display = slice4_no_get_display();
    RSSDDCCharacterization *characterization = slice5_assembled(&display, NULL);
    assert(rss_ddc_characterization_collect_extended_probe(characterization, NULL) == RSS_DDC_OK);
    assert(!rss_ddc_characterization_extended_attempted(characterization));
    assert(rss_ddc_characterization_extended_status(characterization) ==
           RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    rss_ddc_characterization_destroy(characterization);
}

typedef struct {
    RSSDDCDisplay display;
    RSSDDCError display_error;
    RSSDDCError edid_error;
    RSSDDCEDIDInfo edid;
    RSSDDCError mccs_error;
    const char *mccs_raw;
    Slice4MockGet quick;
    RSSDDCError quick_error;
    Slice6MockGet extended;
    RSSDDCError extended_error;
    unsigned get_display_calls;
    unsigned read_edid_calls;
    unsigned get_mccs_calls;
    unsigned quick_calls;
    unsigned extended_calls;
    unsigned set_calls;
} Slice7Harness;

static RSSDDCError slice7_get_display(void *context, uint32_t list_index, RSSDDCDisplay *display) {
    Slice7Harness *harness = context;
    ++harness->get_display_calls;
    (void)list_index;
    if (harness->display_error != RSS_DDC_OK) {
        return harness->display_error;
    }
    *display = harness->display;
    return RSS_DDC_OK;
}

static RSSDDCError slice7_read_edid(void *context, uint32_t list_index, RSSDDCEDID *edid) {
    Slice7Harness *harness = context;
    ++harness->read_edid_calls;
    (void)list_index;
    (void)edid;
    return harness->edid_error == RSS_DDC_OK ? RSS_DDC_OK : harness->edid_error;
}

static RSSDDCError slice7_parse_edid(void *context, const RSSDDCEDID *edid, RSSDDCEDIDInfo *info) {
    Slice7Harness *harness = context;
    (void)edid;
    *info = harness->edid;
    return RSS_DDC_OK;
}

static RSSDDCError slice7_get_mccs(void *context, uint32_t list_index,
                                   RSSDDCMCCSCapabilities *capabilities) {
    Slice7Harness *harness = context;
    ++harness->get_mccs_calls;
    (void)list_index;
    if (harness->mccs_error != RSS_DDC_OK) {
        return harness->mccs_error;
    }
    if (harness->mccs_raw == NULL) {
        return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    return rss_ddc_parse_mccs_capabilities(harness->mccs_raw, strlen(harness->mccs_raw), capabilities);
}

static RSSDDCError slice7_probe_quick(void *context, uint32_t list_index, RSSDDCProbe **probe) {
    Slice7Harness *harness = context;
    ++harness->quick_calls;
    (void)list_index;
    if (harness->quick_error != RSS_DDC_OK) {
        if (probe != NULL) {
            *probe = NULL;
        }
        return harness->quick_error;
    }
    *probe = slice4_run_quick(&harness->quick, &harness->display);
    return RSS_DDC_OK;
}

static RSSDDCError slice7_probe_extended(void *context, uint32_t list_index, RSSDDCProbe **probe) {
    Slice7Harness *harness = context;
    ++harness->extended_calls;
    (void)list_index;
    if (harness->extended_error != RSS_DDC_OK) {
        if (probe != NULL) {
            *probe = NULL;
        }
        return harness->extended_error;
    }
    *probe = slice6_run_extended(&harness->extended, &harness->display);
    return RSS_DDC_OK;
}

static RSSDDCCharacterizationOps slice7_ops(Slice7Harness *harness) {
    RSSDDCCharacterizationOps ops = {
        .context = harness,
        .get_display = slice7_get_display,
        .read_edid = slice7_read_edid,
        .parse_edid = slice7_parse_edid,
        .get_mccs_capabilities = slice7_get_mccs,
        .probe_quick_for_display = slice7_probe_quick,
        .probe_extended_for_display = slice7_probe_extended,
    };
    return ops;
}

static Slice7Harness slice7_harness(RSSDDCDisplay display) {
    Slice7Harness harness = {.display = display};
    slice6_fill_protocol_reported(&harness.extended);
    return harness;
}

static void slice7_stable_quick(Slice7Harness *harness, uint16_t brightness) {
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        slice4_set_stable(&harness->quick, index, 100, 1);
    }
    slice4_set_stable(&harness->quick, 0, 100, brightness);
}

static RSSDDCError slice7_run(Slice7Harness *harness, const RSSDDCProfileStore *profiles,
                              const RSSDDCCharacterizeOptions *options,
                              RSSDDCCharacterization **out) {
    RSSDDCCharacterizationOps ops = slice7_ops(harness);
    RSSDDCError error = rss_ddc_characterization_execute(harness->display.list_index, profiles,
                                                         options, &ops, out);
    assert(harness->set_calls == 0);
    return error;
}

static void test_public_invalid_display_returns_no_object(void) {
    Slice7Harness harness = slice7_harness(slice2_display());
    RSSDDCCharacterization *result = (RSSDDCCharacterization *)0x1;
    RSSDDCCharacterizeOptions options = rss_ddc_default_characterize_options();
    RSSDDCCharacterizationOps ops = slice7_ops(&harness);
    harness.display_error = RSS_DDC_ERROR_NOT_FOUND;
    assert(rss_ddc_characterization_execute(3, NULL, &options, &ops, &result) ==
           RSS_DDC_ERROR_NOT_FOUND);
    assert(result == NULL);
    assert(harness.quick_calls == 0);
    assert(harness.extended_calls == 0);
    assert(harness.set_calls == 0);
    assert(rss_ddc_characterization_execute(3, NULL, &options, &ops, NULL) == RSS_DDC_ERROR_ARGUMENT);
}

static void test_public_passive_skips_probes(void) {
    Slice7Harness harness = slice7_harness(slice2_display());
    RSSDDCCharacterizeOptions options = {.mode = RSS_DDC_CHARACTERIZE_MODE_PASSIVE};
    RSSDDCCharacterization *result = NULL;
    RSSDDCCharacterizationSufficiencyResult sufficiency = {0};
    harness.mccs_raw = "vcp(10 12 15 60)";
    assert(slice7_run(&harness, NULL, &options, &result) == RSS_DDC_OK);
    assert(result != NULL);
    assert(harness.get_mccs_calls == 1);
    assert(harness.quick_calls == 0);
    assert(harness.extended_calls == 0);
    assert(!rss_ddc_characterization_quick_attempted(result));
    assert(!rss_ddc_characterization_extended_attempted(result));
    assert(rss_ddc_characterization_mccs(result) != NULL);
    assert(count_fact_kind(rss_ddc_characterization_knowledge(result), "display.brightness",
                           RSS_DDC_KNOWLEDGE_FACT_DECLARED) == 1);
    assert(rss_ddc_characterization_sufficiency(result, &sufficiency) == RSS_DDC_OK);
    assert(sufficiency.status != RSS_DDC_CHARACTERIZATION_SUFFICIENCY_UNAVAILABLE);
    rss_ddc_characterization_destroy(result);
}

static void test_public_default_sufficient_after_quick_skips_extended(void) {
    Slice7Harness harness = slice7_harness(slice3_ps190_display());
    RSSDDCCharacterization *result = NULL;
    RSSDDCCharacterizationSufficiencyResult sufficiency = {0};
    slice7_stable_quick(&harness, 42);
    assert(slice7_run(&harness, NULL, NULL, &result) == RSS_DDC_OK);
    assert(harness.quick_calls == 1);
    assert(harness.extended_calls == 0);
    assert(rss_ddc_characterization_quick_attempted(result));
    assert(!rss_ddc_characterization_extended_attempted(result));
    assert(rss_ddc_characterization_sufficiency(result, &sufficiency) == RSS_DDC_OK);
    assert(sufficiency.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    assert(!sufficiency.extended_recommended);
    rss_ddc_characterization_destroy(result);
}

static void test_public_deep_forces_extended_when_get_available(void) {
    Slice7Harness harness = slice7_harness(slice3_ps190_display());
    RSSDDCCharacterizeOptions options = {.mode = RSS_DDC_CHARACTERIZE_MODE_DEEP};
    RSSDDCCharacterization *result = NULL;
    RSSDDCCharacterizationSufficiencyResult sufficiency = {0};
    slice7_stable_quick(&harness, 42);
    assert(slice7_run(&harness, NULL, &options, &result) == RSS_DDC_OK);
    assert(harness.quick_calls == 1);
    assert(harness.extended_calls == 1);
    assert(rss_ddc_characterization_extended_attempted(result));
    assert(rss_ddc_characterization_extended_diagnostics(result) != NULL);
    assert(rss_ddc_characterization_sufficiency(result, &sufficiency) == RSS_DDC_OK);
    assert(sufficiency.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    rss_ddc_characterization_destroy(result);
}

static void test_public_deep_without_get_skips_extended(void) {
    Slice7Harness harness = slice7_harness(slice4_no_get_display());
    RSSDDCCharacterizeOptions options = {.mode = RSS_DDC_CHARACTERIZE_MODE_DEEP};
    RSSDDCCharacterization *result = NULL;
    assert(slice7_run(&harness, NULL, &options, &result) == RSS_DDC_OK);
    assert(result != NULL);
    assert(harness.quick_calls == 0);
    assert(harness.extended_calls == 0);
    assert(!rss_ddc_characterization_extended_attempted(result));
    assert(rss_ddc_characterization_display(result) != NULL);
    rss_ddc_characterization_destroy(result);
}

static void test_public_default_extended_when_recommended(void) {
    Slice7Harness harness = slice7_harness(slice2_display());
    RSSDDCCharacterization *result = NULL;
    RSSDDCCharacterizationSufficiencyResult sufficiency = {0};
    const RSSDDCProbeDiagnostics *quick = NULL;
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    const char *pack = slice2_profile_pack();
    harness.edid.manufacturer_id[0] = 'G';
    harness.edid.manufacturer_id[1] = 'S';
    harness.edid.manufacturer_id[2] = 'M';
    harness.edid.manufacturer_id[3] = '\0';
    harness.mccs_raw = "vcp(10 12 15 60)";
    slice7_stable_quick(&harness, 42);
    slice6_set_stable(&harness.extended, 0x15, 255, 0x31);
    slice6_set_stable(&harness.extended, 0x60, 18, 0x11);
    assert(store != NULL);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    assert(slice7_run(&harness, store, NULL, &result) == RSS_DDC_OK);
    assert(harness.quick_calls == 1);
    assert(harness.extended_calls == 1);
    assert(rss_ddc_characterization_edid(result) != NULL);
    assert(count_fact_kind(rss_ddc_characterization_knowledge(result), "display.brightness",
                           RSS_DDC_KNOWLEDGE_FACT_PROFILE) == 1);
    assert(count_fact_kind(rss_ddc_characterization_knowledge(result), "display.brightness",
                           RSS_DDC_KNOWLEDGE_FACT_DECLARED) == 1);
    assert(count_fact_kind(rss_ddc_characterization_knowledge(result), "display.brightness",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 1);
    assert(route_with_kind(rss_ddc_characterization_knowledge(result), "inputs.switching",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) != NULL);
    assert(route_with_kind(rss_ddc_characterization_knowledge(result), "display.picture_mode",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) != NULL);
    assert(rss_ddc_characterization_resolve(result, "brightness", &resolution) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_state(resolution) == RSS_DDC_KNOWLEDGE_RESOLUTION_RESOLVED);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    assert(rss_ddc_characterization_current_value(result, "brightness", &value_state, &current) ==
           RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current->value.unsigned_value == 42);
    quick = rss_ddc_characterization_quick_diagnostics(result);
    assert(quick != NULL);
    assert(quick->observation_count == RSS_DDC_PROBE_QUICK_CONTROL_COUNT);
    assert(rss_ddc_characterization_sufficiency(result, &sufficiency) == RSS_DDC_OK);
    assert(sufficiency.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    rss_ddc_characterization_destroy(result);
    rss_ddc_profile_store_destroy(store);
}

static void test_public_null_store_and_stage_degradation(void) {
    Slice7Harness harness = slice7_harness(slice3_ps190_display());
    RSSDDCCharacterization *result = NULL;
    RSSDDCCharacterizeOptions passive = {.mode = RSS_DDC_CHARACTERIZE_MODE_PASSIVE};
    harness.edid_error = RSS_DDC_ERROR_READ;
    slice7_stable_quick(&harness, 42);
    assert(slice7_run(&harness, NULL, NULL, &result) == RSS_DDC_OK);
    assert(rss_ddc_characterization_edid(result) == NULL);
    assert(rss_ddc_characterization_profile_status(result) == RSS_DDC_CHARACTERIZATION_PROFILE_NONE);
    rss_ddc_characterization_destroy(result);

    harness = slice7_harness(slice2_display());
    harness.mccs_error = RSS_DDC_ERROR_READ;
    assert(slice7_run(&harness, NULL, &passive, &result) == RSS_DDC_OK);
    assert(rss_ddc_characterization_mccs_attempted(result));
    assert(rss_ddc_characterization_mccs_status(result) == RSS_DDC_ERROR_READ);
    assert(rss_ddc_characterization_mccs(result) == NULL);
    assert(!rss_ddc_characterization_quick_attempted(result));
    rss_ddc_characterization_destroy(result);

    harness = slice7_harness(slice2_display());
    harness.quick_error = RSS_DDC_ERROR_READ;
    harness.extended_error = RSS_DDC_ERROR_READ;
    assert(slice7_run(&harness, NULL, NULL, &result) == RSS_DDC_OK);
    assert(rss_ddc_characterization_quick_attempted(result));
    assert(rss_ddc_characterization_quick_status(result) == RSS_DDC_ERROR_READ);
    assert(rss_ddc_characterization_display(result) != NULL);
    rss_ddc_characterization_destroy(result);

    harness = slice7_harness(slice2_display());
    slice7_stable_quick(&harness, 42);
    slice4_set_reply(&harness.quick, 1, 0, RSS_DDC_ERROR_READ, 0, 0, 0);
    slice4_set_reply(&harness.quick, 1, 1, RSS_DDC_ERROR_READ, 0, 0, 0);
    harness.extended_error = RSS_DDC_ERROR_READ;
    assert(slice7_run(&harness, NULL, NULL, &result) == RSS_DDC_OK);
    assert(rss_ddc_characterization_quick_attempted(result));
    assert(rss_ddc_characterization_quick_diagnostics(result) != NULL);
    assert(count_fact_kind(rss_ddc_characterization_knowledge(result), "display.brightness",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == 1);
    assert(rss_ddc_characterization_extended_attempted(result));
    assert(rss_ddc_characterization_extended_status(result) == RSS_DDC_ERROR_READ);
    assert(route_with_kind(rss_ddc_characterization_knowledge(result), "inputs.switching",
                           RSS_DDC_KNOWLEDGE_FACT_OBSERVED) == NULL);
    rss_ddc_characterization_destroy(result);
}

static RSSDDCDisplay lg_hdr_qhd_display(void) {
    RSSDDCDisplay display = {
        .list_index = 2,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_DCPDP13,
        .capabilities = RSS_DDC_CAP_PICTURE_MODE,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "LG HDR QHD");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT0");
    return display;
}

static RSSDDCDisplay odyssey_g75f_display(void) {
    RSSDDCDisplay display = {
        .list_index = 1,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_PS190,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "Odyssey G75F");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT1");
    return display;
}

static const RSSDDCKnowledgeRoute *route_with_kind_and_route_kind(
    const RSSDDCMonitorKnowledge *knowledge, const char *semantic_id, RSSDDCKnowledgeRouteKind kind) {
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route != NULL && strcmp(route->semantic_id, semantic_id) == 0 && route->kind == kind) {
            return route;
        }
    }
    return NULL;
}

static void test_production_lg_alt_injected_only_for_exact_gate(void) {
    RSSDDCCharacterization *matching = rss_ddc_characterization_create();
    RSSDDCCharacterization *wrong_product = rss_ddc_characterization_create();
    RSSDDCCharacterization *wrong_transport = rss_ddc_characterization_create();
    RSSDDCCharacterization *wrong_provider = rss_ddc_characterization_create();
    RSSDDCDisplay lg = lg_hdr_qhd_display();
    RSSDDCDisplay other = lg;
    RSSDDCDisplay transport = lg;
    RSSDDCDisplay provider = lg;
    const RSSDDCKnowledgeRoute *lg_alt = NULL;
    (void)snprintf(other.product_name, sizeof(other.product_name), "%s", "Another LG");
    (void)snprintf(transport.transport, sizeof(transport.transport), "%s", "DCPEXT1");
    provider.provider = RSS_DDC_PROVIDER_DCPDP_SERVICE;
    assert(matching != NULL && wrong_product != NULL && wrong_transport != NULL &&
           wrong_provider != NULL);
    assert(rss_ddc_characterization_assemble(matching, &lg, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(matching) == RSS_DDC_OK);
    lg_alt = route_with_kind_and_route_kind(rss_ddc_characterization_knowledge(matching),
                                            "inputs.switching", RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT);
    assert(lg_alt != NULL);
    assert(!lg_alt->readable);
    assert(lg_alt->writable);
    assert(lg_alt->write_authorized);
    assert(lg_alt->provenance.confidence == RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED);
    assert(lg_alt->provenance.source == RSS_DDC_PROFILE_SOURCE_BUILTIN);
    assert(strcmp(lg_alt->provenance.source_id, "production-lg-alt-input") == 0);
    assert(rss_ddc_validate_lg_alt_input_target(lg.provider, true, lg.product_name, lg.transport) ==
           RSS_DDC_OK);
    assert(rss_ddc_validate_lg_alt_input_target(lg.provider, false, lg.product_name, lg.transport) ==
           RSS_DDC_ERROR_SAFETY_GATE);

    assert(rss_ddc_characterization_assemble(wrong_product, &other, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(wrong_product) == RSS_DDC_OK);
    assert(route_with_kind_and_route_kind(rss_ddc_characterization_knowledge(wrong_product),
                                          "inputs.switching",
                                          RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) == NULL);

    assert(rss_ddc_characterization_assemble(wrong_transport, &transport, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(wrong_transport) == RSS_DDC_OK);
    assert(route_with_kind_and_route_kind(rss_ddc_characterization_knowledge(wrong_transport),
                                          "inputs.switching",
                                          RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) == NULL);

    assert(rss_ddc_characterization_assemble(wrong_provider, &provider, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(wrong_provider) == RSS_DDC_OK);
    assert(route_with_kind_and_route_kind(rss_ddc_characterization_knowledge(wrong_provider),
                                          "inputs.switching",
                                          RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) == NULL);

    rss_ddc_characterization_destroy(matching);
    rss_ddc_characterization_destroy(wrong_product);
    rss_ddc_characterization_destroy(wrong_transport);
    rss_ddc_characterization_destroy(wrong_provider);
}

static void test_production_read_vcp60_write_lg_alt_remain_independent(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = lg_hdr_qhd_display();
    Slice6MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    const RSSDDCKnowledgeRoute *read = NULL;
    const RSSDDCKnowledgeRoute *write = NULL;
    const RSSDDCKnowledgeRoute *observed = NULL;
    slice6_fill_protocol_reported(&mock);
    slice6_set_stable(&mock, 0x60, 18, 0);
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(characterization) == RSS_DDC_OK);
    probe = slice6_run_extended(&mock, &display);
    assert(rss_ddc_characterization_collect_extended_probe(characterization, probe) == RSS_DDC_OK);
    observed = route_with_kind(rss_ddc_characterization_knowledge(characterization), "inputs.switching",
                               RSS_DDC_KNOWLEDGE_FACT_OBSERVED);
    assert(observed != NULL);
    assert(observed->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP);
    assert(observed->address == 0x60);
    assert(observed->readable);
    assert(!observed->writable);
    assert(!observed->write_authorized);
    assert(rss_ddc_characterization_resolve(characterization, "inputs.switching", &resolution) ==
           RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_state(resolution) == RSS_DDC_KNOWLEDGE_RESOLUTION_RESOLVED);
    read = rss_ddc_monitor_knowledge_resolution_preferred_read(resolution);
    write = rss_ddc_monitor_knowledge_resolution_preferred_write(resolution);
    assert(read != NULL && write != NULL);
    assert(read->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP);
    assert(read->address == 0x60);
    assert(!read->write_authorized);
    assert(write->kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT);
    assert(write->writable);
    assert(rss_ddc_monitor_knowledge_resolution_write_authorized(resolution));
    assert(rss_ddc_characterization_current_value(characterization, "inputs.switching", &value_state,
                                                  &current) == RSS_DDC_OK);
    assert(value_state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current->value.unsigned_value == 0);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
}

static void test_production_lg_alt_values_stay_separate_from_mccs_enums(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = lg_hdr_qhd_display();
    const RSSDDCKnowledgeRoute *lg_alt = NULL;
    const RSSDDCKnowledgeRoute *declared = NULL;
    const RSSDDCMCCSCapabilities *mccs = NULL;
    const uint8_t *enums = NULL;
    size_t enum_count = 0;
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    const char *raw = "vcp(60(11 12 0f 00))";
    assert(characterization != NULL);
    assert(rss_ddc_lg_alt_input_value_is_supported(0x90));
    assert(rss_ddc_lg_alt_input_value_is_supported(0x91));
    assert(rss_ddc_lg_alt_input_value_is_supported(0xd0));
    assert(!rss_ddc_lg_alt_input_value_is_supported(0x11));
    assert(!rss_ddc_lg_alt_input_value_is_supported(0x12));
    assert(!rss_ddc_lg_alt_input_value_is_supported(0x0f));
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(characterization) == RSS_DDC_OK);
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, raw, strlen(raw)) ==
           RSS_DDC_OK);
    lg_alt = route_with_kind_and_route_kind(rss_ddc_characterization_knowledge(characterization),
                                            "inputs.switching", RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT);
    declared = route_with_kind(rss_ddc_characterization_knowledge(characterization), "inputs.switching",
                               RSS_DDC_KNOWLEDGE_FACT_DECLARED);
    assert(lg_alt != NULL);
    assert(lg_alt->value.state == RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN);
    assert(lg_alt->write_authorized);
    assert(declared != NULL);
    assert(!declared->write_authorized);
    assert(declared->address == 0x60);
    mccs = rss_ddc_characterization_mccs(characterization);
    assert(mccs != NULL);
    assert(rss_ddc_mccs_capabilities_enum_values(mccs, 0x60, &enums, &enum_count) == RSS_DDC_OK);
    assert(enum_count == 4);
    assert(enums[0] == 0x11 && enums[1] == 0x12 && enums[2] == 0x0f && enums[3] == 0x00);
    assert(rss_ddc_characterization_resolve(characterization, "inputs.switching", &resolution) ==
           RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_write(resolution)->kind ==
           RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_characterization_destroy(characterization);
}

static void test_production_picture_mode_and_odyssey_unchanged(void) {
    RSSDDCCharacterization *lg = rss_ddc_characterization_create();
    RSSDDCCharacterization *odyssey = rss_ddc_characterization_create();
    RSSDDCDisplay lg_display = lg_hdr_qhd_display();
    RSSDDCDisplay odyssey_display = odyssey_g75f_display();
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCMonitorKnowledgeResolution *picture = NULL;
    RSSDDCMonitorKnowledgeResolution *input = NULL;
    const RSSDDCKnowledgeRoute *picture_write = NULL;
    assert(lg != NULL && odyssey != NULL && store != NULL);
    assert(rss_ddc_profile_store_load_builtin(store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(lg, &lg_display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(lg) == RSS_DDC_OK);
    assert(rss_ddc_characterization_resolve(lg, "display.picture_mode", &picture) == RSS_DDC_OK);
    picture_write = rss_ddc_monitor_knowledge_resolution_preferred_write(picture);
    assert(picture_write != NULL);
    assert(picture_write->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP);
    assert(picture_write->address == 0x15);
    assert(rss_ddc_monitor_knowledge_resolution_write_authorized(picture));
    assert(rss_ddc_characterization_resolve(lg, "inputs.switching", &input) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_write(input)->kind ==
           RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT);
    rss_ddc_monitor_knowledge_resolution_destroy(picture);
    rss_ddc_monitor_knowledge_resolution_destroy(input);

    assert(rss_ddc_characterization_assemble(odyssey, &odyssey_display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(odyssey) == RSS_DDC_OK);
    assert(route_with_kind_and_route_kind(rss_ddc_characterization_knowledge(odyssey),
                                          "inputs.switching",
                                          RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) == NULL);
    assert(rss_ddc_characterization_resolve(odyssey, "inputs.switching", &input) ==
           RSS_DDC_ERROR_NOT_FOUND);
    rss_ddc_characterization_destroy(lg);
    rss_ddc_characterization_destroy(odyssey);
    rss_ddc_profile_store_destroy(store);
}

static void test_production_execute_reports_current_sufficiency_policy(void) {
    Slice7Harness harness = slice7_harness(lg_hdr_qhd_display());
    RSSDDCCharacterization *result = NULL;
    RSSDDCCharacterizationSufficiencyResult sufficiency = {0};
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    const RSSDDCKnowledgeRoute *write = NULL;
    harness.mccs_raw = "vcp(10 12 15 60(11 12 0f 00))";
    slice7_stable_quick(&harness, 42);
    slice6_set_stable(&harness.extended, 0x60, 18, 0);
    slice6_set_stable(&harness.extended, 0x15, 255, 0x31);
    assert(store != NULL);
    assert(rss_ddc_profile_store_load_builtin(store) == RSS_DDC_OK);
    assert(slice7_run(&harness, store, NULL, &result) == RSS_DDC_OK);
    assert(harness.set_calls == 0);
    assert(rss_ddc_characterization_resolve(result, "inputs.switching", &resolution) == RSS_DDC_OK);
    write = rss_ddc_monitor_knowledge_resolution_preferred_write(resolution);
    assert(write != NULL);
    assert(write->kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT);
    assert(rss_ddc_monitor_knowledge_resolution_write_authorized(resolution));
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    assert(rss_ddc_characterization_sufficiency(result, &sufficiency) == RSS_DDC_OK);
    if (sufficiency.extended_recommended) {
        assert(harness.extended_calls == 1);
        assert(rss_ddc_characterization_extended_attempted(result));
    } else {
        assert(harness.extended_calls == 0);
        assert(!rss_ddc_characterization_extended_attempted(result));
    }
    rss_ddc_characterization_destroy(result);
    rss_ddc_profile_store_destroy(store);
}

static char *export_store_json(const RSSDDCProfileStore *store) {
    size_t required = 0;
    char *json = NULL;
    assert(rss_ddc_profile_store_export_json(store, NULL, 0, &required) == RSS_DDC_OK);
    json = malloc(required);
    assert(json != NULL);
    assert(rss_ddc_profile_store_export_json(store, json, required, &required) == RSS_DDC_OK);
    return json;
}

static const char *lg_vcp_input_pack(void) {
    return "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
           "\"packId\":\"lg-vcp-input\",\"profiles\":[{\"id\":\"lg-vcp\",\"identity\":{"
           "\"productName\":\"LG HDR QHD\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\","
           "\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":["
           "{\"id\":\"input\",\"method\":\"vcp\",\"address\":96,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[]}]}]}";
}

static void test_profile_update_explicit_and_does_not_mutate_characterize(void) {
    Slice7Harness harness = slice7_harness(lg_hdr_qhd_display());
    RSSDDCCharacterization *result = NULL;
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCCharacterizationProfileUpdateResult update = {0};
    char *before = NULL;
    char *after = NULL;
    assert(store != NULL);
    assert(rss_ddc_profile_store_load_builtin(store) == RSS_DDC_OK);
    before = export_store_json(store);
    slice7_stable_quick(&harness, 42);
    assert(slice7_run(&harness, store, NULL, &result) == RSS_DDC_OK);
    after = export_store_json(store);
    assert(strcmp(before, after) == 0);
    assert(rss_ddc_characterization_update_profile(result, store, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED);
    assert(update.controls_added >= 1);
    free(before);
    before = export_store_json(store);
    assert(strcmp(before, after) != 0);
    rss_ddc_characterization_destroy(result);
    rss_ddc_profile_store_destroy(store);
    free(before);
    free(after);
}

static void test_profile_update_persists_lg_alt_not_vcp60(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = lg_hdr_qhd_display();
    RSSDDCProfileStore *builtin = rss_ddc_profile_store_create();
    RSSDDCProfileStore *target = rss_ddc_profile_store_create();
    RSSDDCCharacterizationProfileUpdateResult update = {0};
    RSSDDCEffectiveProfile effective = {0};
    RSSDDCProfileControl input = {0};
    RSSDDCProfileControl picture = {0};
    RSSDDCProfileEnumValue value = {0};
    char *json = NULL;
    const char *raw = "vcp(10 12 15 60(11 12 0f 00))";
    assert(characterization != NULL && builtin != NULL && target != NULL);
    assert(rss_ddc_profile_store_load_builtin(builtin) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_load_builtin(target) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, builtin) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(characterization) == RSS_DDC_OK);
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, raw, strlen(raw)) ==
           RSS_DDC_OK);
    assert(rss_ddc_characterization_update_profile(characterization, target, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED);
    assert(rss_ddc_profile_store_resolve(target, rss_ddc_characterization_profile_identity(characterization),
                                         &effective) == RSS_DDC_OK);
    assert(rss_ddc_effective_profile_control_count(&effective) == 2);
    assert(rss_ddc_effective_profile_control(&effective, 0, &picture) == RSS_DDC_OK ||
           rss_ddc_effective_profile_control(&effective, 1, &picture) == RSS_DDC_OK);
    for (size_t index = 0; index < rss_ddc_effective_profile_control_count(&effective); ++index) {
        RSSDDCProfileControl control = {0};
        assert(rss_ddc_effective_profile_control(&effective, index, &control) == RSS_DDC_OK);
        if (control.id == RSS_DDC_PROFILE_CONTROL_INPUT) {
            input = control;
        } else if (control.id == RSS_DDC_PROFILE_CONTROL_PICTURE_MODE) {
            picture = control;
        }
    }
    assert(input.method == RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT);
    assert(input.address != 0x60);
    assert(input.writable && input.write_authorized);
    assert(input.enum_value_count == 3);
    assert(rss_ddc_profile_control_enum_value(&input, 0, &value) == RSS_DDC_OK);
    assert(value.raw_value == 0x90);
    assert(rss_ddc_profile_control_enum_value(&input, 1, &value) == RSS_DDC_OK);
    assert(value.raw_value == 0x91);
    assert(rss_ddc_profile_control_enum_value(&input, 2, &value) == RSS_DDC_OK);
    assert(value.raw_value == 0xd0);
    assert(picture.method == RSS_DDC_PROFILE_METHOD_VCP);
    assert(picture.address == 0x15);
    assert(picture.write_authorized);
    json = export_store_json(target);
    assert(strstr(json, "lg-alt-input") != NULL);
    assert(strstr(json, "\"id\":\"lg-hdr-qhd-dcpdp13-dcpext0\"") != NULL);
    assert(strstr(json, "\"value\":17") == NULL);
    assert(strstr(json, "\"value\":18") == NULL);
    assert(strstr(json, "\"value\":15") == NULL);
    assert(strstr(json, "\"value\":144") != NULL);
    free(json);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(builtin);
    rss_ddc_profile_store_destroy(target);
}

static void test_profile_update_observations_do_not_authorize_or_persist_current(void) {
    RSSDDCCharacterization *lg = rss_ddc_characterization_create();
    RSSDDCCharacterization *odyssey = rss_ddc_characterization_create();
    RSSDDCDisplay lg_display = lg_hdr_qhd_display();
    RSSDDCDisplay odyssey_display = odyssey_g75f_display();
    RSSDDCProfileStore *target = rss_ddc_profile_store_create();
    RSSDDCMonitorKnowledge *observed = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute brightness =
        make_route("display.brightness", "alien-probe-live-read", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                   RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "vcp-10-live",
                   0x10, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 50, true, false, false);
    RSSDDCKnowledgeRoute input60 =
        make_route("inputs.switching", "alien-probe-live-read", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                   RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "vcp-60-live",
                   0x60, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 0, true, false, false);
    RSSDDCCharacterizationProfileUpdateResult update = {0};
    RSSDDCEffectiveProfile effective = {0};
    char *json = NULL;
    assert(lg != NULL && odyssey != NULL && target != NULL && observed != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(observed, &brightness) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(observed, &input60) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(lg, &lg_display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(lg) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(lg, observed) == RSS_DDC_OK);
    assert(rss_ddc_characterization_update_profile(lg, target, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CREATED);
    json = export_store_json(target);
    assert(strstr(json, "brightness") == NULL);
    assert(strstr(json, "\"value\":50") == NULL);
    assert(strstr(json, "lg-alt-input") != NULL);
    free(json);

    update = (RSSDDCCharacterizationProfileUpdateResult){0};
    assert(rss_ddc_characterization_assemble(odyssey, &odyssey_display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(odyssey) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(odyssey, observed) == RSS_DDC_OK);
    assert(rss_ddc_characterization_update_profile(odyssey, target, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNSUPPORTED);
    assert(rss_ddc_profile_store_resolve(
               target, rss_ddc_characterization_profile_identity(odyssey), &effective) ==
           RSS_DDC_ERROR_NOT_FOUND);
    rss_ddc_monitor_knowledge_destroy(observed);
    rss_ddc_characterization_destroy(lg);
    rss_ddc_characterization_destroy(odyssey);
    rss_ddc_profile_store_destroy(target);
}

static void test_profile_update_conflict_and_repeat_unchanged(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = lg_hdr_qhd_display();
    RSSDDCProfileStore *builtin = rss_ddc_profile_store_create();
    RSSDDCProfileStore *conflicted = rss_ddc_profile_store_create();
    RSSDDCCharacterizationProfileUpdateResult update = {0};
    const char *pack = lg_vcp_input_pack();
    assert(characterization != NULL && builtin != NULL && conflicted != NULL);
    assert(rss_ddc_profile_store_load_builtin(builtin) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, builtin) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(characterization) == RSS_DDC_OK);
    assert(rss_ddc_characterization_update_profile(characterization, builtin, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED);
    update = (RSSDDCCharacterizationProfileUpdateResult){0};
    assert(rss_ddc_characterization_update_profile(characterization, builtin, &update) == RSS_DDC_OK);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNCHANGED);

    assert(rss_ddc_profile_store_load_pack_data(conflicted, pack, strlen(pack)) == RSS_DDC_OK);
    update = (RSSDDCCharacterizationProfileUpdateResult){0};
    assert(rss_ddc_characterization_update_profile(characterization, conflicted, &update) ==
           RSS_DDC_ERROR_PROFILE_CONFLICT);
    assert(update.status == RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CONFLICT);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(builtin);
    rss_ddc_profile_store_destroy(conflicted);
}

static void test_profile_update_bounds_and_missing_identity(void) {
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCCharacterizationProfileUpdateResult update = {0};
    RSSDDCProfileControl control = {.id = RSS_DDC_PROFILE_CONTROL_BRIGHTNESS,
                                    .method = RSS_DDC_PROFILE_METHOD_VCP,
                                    .address = 0x10,
                                    .readable = true,
                                    .writable = true,
                                    .confidence = RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED};
    assert(store != NULL && characterization != NULL);
    for (unsigned index = 0; index < RSS_DDC_PROFILE_MAX_PROFILES; ++index) {
        RSSDDCProfileIdentity identity = {.external = true, .provider = RSS_DDC_PROVIDER_PS190};
        (void)snprintf(identity.product_name, sizeof(identity.product_name), "Bound %u", index);
        (void)snprintf(identity.transport, sizeof(identity.transport), "%s", "DCPEXT1");
        char id[RSS_DDC_PROFILE_ID_MAX];
        (void)snprintf(id, sizeof(id), "bound-%u", index);
        assert(rss_ddc_profile_store_put_local_profile(store, id, &identity,
                                                       RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED,
                                                       &control, 1) == RSS_DDC_OK);
    }
    {
        RSSDDCProfileIdentity identity = {.external = true, .provider = RSS_DDC_PROVIDER_PS190};
        (void)snprintf(identity.product_name, sizeof(identity.product_name), "%s", "Overflow");
        (void)snprintf(identity.transport, sizeof(identity.transport), "%s", "DCPEXT1");
        assert(rss_ddc_profile_store_put_local_profile(store, "overflow", &identity,
                                                       RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED,
                                                       &control, 1) == RSS_DDC_ERROR_PROFILE_CONFLICT);
    }
    assert(rss_ddc_characterization_update_profile(characterization, store, &update) == RSS_DDC_ERROR_ARGUMENT);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
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
    test_passive_mccs_requires_transport_capability();
    test_passive_mccs_declares_known_and_unknown_vcps();
    test_passive_mccs_preserves_profile_authorization();
    test_passive_mccs_malformed_and_failed_preserve_state();
    test_passive_mccs_overflow_is_explicit();
    test_quick_probe_observed_brightness_and_current_value();
    test_quick_probe_profile_declared_observed_coexist();
    test_quick_probe_failures_remain_diagnostics();
    test_quick_probe_requires_get_capability();
    test_quick_probe_failure_preserves_prior_knowledge();
    test_quick_probe_overflow_is_explicit();
    test_sufficiency_quick_success_is_not_enough();
    test_sufficiency_actionable_profile_without_live_values();
    test_sufficiency_missing_picture_mode_and_declared_only();
    test_sufficiency_validated_input_without_quick_0x60();
    test_sufficiency_vendor_unknown_does_not_block();
    test_sufficiency_no_get_with_and_without_profile();
    test_sufficiency_profile_conflict();
    test_sufficiency_variable_is_not_stable_current();
    test_sufficiency_stable_quick_observation();
    test_extended_not_recommended_is_not_run();
    test_extended_promotes_input_and_picture_mode();
    test_extended_coexists_and_does_not_authorize_write();
    test_extended_failures_remain_diagnostics();
    test_extended_priority_beats_capacity();
    test_extended_advertised_before_unknown_and_still_insufficient();
    test_extended_abort_preserves_prior_knowledge();
    test_extended_no_get_is_not_run();
    test_public_invalid_display_returns_no_object();
    test_public_passive_skips_probes();
    test_public_default_sufficient_after_quick_skips_extended();
    test_public_deep_forces_extended_when_get_available();
    test_public_deep_without_get_skips_extended();
    test_public_default_extended_when_recommended();
    test_public_null_store_and_stage_degradation();
    test_production_lg_alt_injected_only_for_exact_gate();
    test_production_read_vcp60_write_lg_alt_remain_independent();
    test_production_lg_alt_values_stay_separate_from_mccs_enums();
    test_production_picture_mode_and_odyssey_unchanged();
    test_production_execute_reports_current_sufficiency_policy();
    test_profile_update_explicit_and_does_not_mutate_characterize();
    test_profile_update_persists_lg_alt_not_vcp60();
    test_profile_update_observations_do_not_authorize_or_persist_current();
    test_profile_update_conflict_and_repeat_unchanged();
    test_profile_update_bounds_and_missing_identity();
    puts("test_characterize: passed");
}
