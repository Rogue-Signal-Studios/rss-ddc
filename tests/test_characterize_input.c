#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "characterize.h"
#include "input_switch.h"
#include "rss_ddc.h"

/*
 * Architectural tripwire: the semantic SET has no input-method parameter.
 * Adding RSSDDCInputSwitchMethod to this signature must fail to compile.
 */
static RSSDDCError (*const semantic_input_set)(const RSSDDCCharacterization *, uint16_t) =
    rss_ddc_characterization_set_input;

static unsigned int standard_calls;
static unsigned int alternate_calls;
static unsigned int production_write_calls;
static unsigned int get_display_calls;
static uint8_t standard_vcp;
static uint16_t standard_value;
static uint16_t alternate_value;
static RSSDDCError standard_result = RSS_DDC_OK;
static RSSDDCError alternate_result = RSS_DDC_OK;
static RSSDDCError get_display_result = RSS_DDC_OK;
static RSSDDCDisplay live_display;
static bool live_display_ready;

static void reset_transport(void) {
    standard_calls = 0;
    alternate_calls = 0;
    production_write_calls = 0;
    get_display_calls = 0;
    standard_vcp = 0;
    standard_value = 0;
    alternate_value = 0;
    standard_result = RSS_DDC_OK;
    alternate_result = RSS_DDC_OK;
    get_display_result = RSS_DDC_OK;
    live_display = (RSSDDCDisplay){0};
    live_display_ready = false;
}

static void bind_live_display(const RSSDDCCharacterization *characterization) {
    const RSSDDCDisplay *display = rss_ddc_characterization_display(characterization);
    assert(display != NULL);
    live_display = *display;
    live_display_ready = true;
    get_display_result = RSS_DDC_OK;
}

RSSDDCError rss_ddc_get_display(uint32_t index, RSSDDCDisplay *display) {
    ++get_display_calls;
    if (display == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (get_display_result != RSS_DDC_OK) {
        return get_display_result;
    }
    if (!live_display_ready || live_display.list_index != index) {
        return RSS_DDC_ERROR_NOT_FOUND;
    }
    *display = live_display;
    return RSS_DDC_OK;
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

RSSDDCError rss_ddc_set_vcp_with_diagnostics(uint32_t index, uint8_t vcp, uint16_t value,
                                              const RSSDDCDiagnostics *diagnostics) {
    (void)index;
    (void)diagnostics;
    ++standard_calls;
    standard_vcp = vcp;
    standard_value = value;
    return standard_result;
}

static RSSDDCError production_construct(void *context, void **service_out) {
    (void)context;
    *service_out = (void *)1;
    return RSS_DDC_OK;
}
static RSSDDCError production_delay(void *context) {
    (void)context;
    return RSS_DDC_OK;
}
static RSSDDCError production_write(void *context, void *service, uint32_t chip, uint32_t data,
                                    const uint8_t *payload, size_t length) {
    (void)context;
    (void)service;
    (void)chip;
    (void)data;
    (void)payload;
    (void)length;
    ++production_write_calls;
    return RSS_DDC_OK;
}
static void production_release(void *context, void *service) {
    (void)context;
    (void)service;
}

RSSDDCError rss_macos_set_lg_alt_input_snapshot(uint32_t index, uint16_t value,
                                                const RSSDDCDiagnostics *diagnostics) {
    (void)index;
    (void)diagnostics;
    ++alternate_calls;
    alternate_value = value;
    if (alternate_result != RSS_DDC_OK) {
        return alternate_result;
    }
    RSSDDCLGAltInputCallbacks callbacks = {
        .construct = production_construct,
        .prewrite_delay = production_delay,
        .write_i2c = production_write,
        .release = production_release,
    };
    return rss_ddc_run_lg_alt_input(RSS_DDC_PROVIDER_DCPDP13, value, &callbacks);
}

static RSSDDCDisplay lg_display(void) {
    RSSDDCDisplay display = {
        .list_index = 2,
        .cg_display_id = 42,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_DCPDP13,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "LG HDR QHD");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT0");
    (void)snprintf(display.serial, sizeof(display.serial), "%s", "lg-serial");
    return display;
}

static RSSDDCDisplay odyssey_display(void) {
    RSSDDCDisplay display = {
        .list_index = 1,
        .cg_display_id = 7,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_PS190,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "Odyssey G75F");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT1");
    return display;
}

static RSSDDCDisplay standard_display(void) {
    RSSDDCDisplay display = {
        .list_index = 4,
        .cg_display_id = 99,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_DCPDP_SERVICE,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "Studio Monitor");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT2");
    (void)snprintf(display.serial, sizeof(display.serial), "%s", "std-serial");
    return display;
}

static const char *standard_input_pack(void) {
    return "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
           "\"packId\":\"standard-input\",\"profiles\":[{\"id\":\"studio\",\"identity\":{"
           "\"productName\":\"Studio Monitor\",\"provider\":\"DCPDPService\",\"transport\":\"DCPEXT2\","
           "\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":["
           "{\"id\":\"input\",\"method\":\"vcp\",\"address\":96,\"readable\":true,"
           "\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":["
           "{\"id\":\"hdmi-1\",\"name\":\"HDMI 1\",\"value\":17},"
           "{\"id\":\"hdmi-2\",\"name\":\"HDMI 2\",\"value\":18}]}]}]}";
}

static RSSDDCKnowledgeRoute writable_unauthorized_input_route(void) {
    RSSDDCKnowledgeRoute route = {
        .kind = RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT,
        .address = 0xf4,
        .readable = false,
        .writable = true,
        .write_authorized = false,
        .value = {.state = RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN},
        .provenance = {.source = RSS_DDC_PROFILE_SOURCE_RESEARCH,
                       .confidence = RSS_DDC_PROFILE_CONFIDENCE_CANDIDATE,
                       .fact_kind = RSS_DDC_KNOWLEDGE_FACT_PROFILE},
    };
    (void)snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", "inputs.switching");
    (void)snprintf(route.route_id, sizeof(route.route_id), "%s", "profile-control-input");
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s", "lg-alt-input");
    (void)snprintf(route.command_semantics, sizeof(route.command_semantics), "%s", "input");
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s",
                   "research-candidate");
    return route;
}

static RSSDDCKnowledgeRoute observed_input_route(uint16_t current) {
    RSSDDCKnowledgeRoute route = {
        .kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP,
        .address = 0x60,
        .readable = true,
        .writable = false,
        .write_authorized = false,
        .value = {.state = RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, .unsigned_value = current},
        .provenance = {.source = RSS_DDC_PROFILE_SOURCE_RESEARCH,
                       .confidence = RSS_DDC_PROFILE_CONFIDENCE_OBSERVED,
                       .fact_kind = RSS_DDC_KNOWLEDGE_FACT_OBSERVED},
    };
    (void)snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", "inputs.switching");
    (void)snprintf(route.route_id, sizeof(route.route_id), "%s", "mccs-vcp-60");
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s", "mccs-vcp");
    (void)snprintf(route.command_semantics, sizeof(route.command_semantics), "%s", "observed-get");
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s",
                   "alien-probe-quick");
    (void)snprintf(route.provenance.evidence_id, sizeof(route.provenance.evidence_id), "%s",
                   "stable-get");
    return route;
}

static RSSDDCCharacterization *assembled(const RSSDDCDisplay *display, RSSDDCProfileStore *store) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_augment_with_prior(characterization) == RSS_DDC_OK);
    return characterization;
}

static RSSDDCProfileStore *builtin_store(void) {
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    assert(store != NULL);
    assert(rss_ddc_profile_store_load_builtin(store) == RSS_DDC_OK);
    return store;
}

static RSSDDCProfileStore *pack_store(const char *pack) {
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    assert(store != NULL);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    return store;
}

static void assert_no_transport(void) {
    assert(standard_calls == 0);
    assert(alternate_calls == 0);
    assert(production_write_calls == 0);
}

static void test_null_and_unassembled_are_argument_errors(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    reset_transport();
    assert(rss_ddc_characterization_set_input(NULL, 0x90) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_characterization_set_input(characterization, 0x90) == RSS_DDC_ERROR_ARGUMENT);
    assert_no_transport();
    assert(get_display_calls == 0);
    rss_ddc_characterization_destroy(characterization);
}

static void test_lg_alt_dispatches_without_method_enum(void) {
    RSSDDCDisplay display = lg_display();
    RSSDDCProfileStore *store = builtin_store();
    RSSDDCCharacterization *characterization = assembled(&display, store);
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    const RSSDDCKnowledgeRoute *write = NULL;
    reset_transport();
    bind_live_display(characterization);
    assert(rss_ddc_characterization_resolve(characterization, "inputs.switching", &resolution) ==
           RSS_DDC_OK);
    write = rss_ddc_monitor_knowledge_resolution_preferred_write(resolution);
    assert(write != NULL && write->kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT);
    assert(rss_ddc_monitor_knowledge_resolution_write_authorized(resolution));
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    assert(semantic_input_set(characterization, 0x90) == RSS_DDC_OK);
    assert(alternate_calls == 1 && alternate_value == 0x90 && production_write_calls == 1);
    assert(standard_calls == 0);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
}

static void test_standard_dispatches_without_method_enum(void) {
    RSSDDCDisplay display = standard_display();
    RSSDDCProfileStore *store = pack_store(standard_input_pack());
    RSSDDCCharacterization *characterization = assembled(&display, store);
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    const RSSDDCKnowledgeRoute *write = NULL;
    reset_transport();
    bind_live_display(characterization);
    assert(rss_ddc_characterization_resolve(characterization, "inputs.switching", &resolution) ==
           RSS_DDC_OK);
    write = rss_ddc_monitor_knowledge_resolution_preferred_write(resolution);
    assert(write != NULL && write->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP);
    assert(write->address == 0x60);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    assert(rss_ddc_characterization_set_input(characterization, 0x11) == RSS_DDC_OK);
    assert(standard_calls == 1 && standard_vcp == 0x60 && standard_value == 0x11);
    assert(alternate_calls == 0);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
}

static void test_no_write_method_fails_before_transport(void) {
    RSSDDCDisplay display = odyssey_display();
    RSSDDCProfileStore *store = builtin_store();
    RSSDDCCharacterization *characterization = assembled(&display, store);
    reset_transport();
    bind_live_display(characterization);
    assert(rss_ddc_characterization_set_input(characterization, 0x11) ==
           RSS_DDC_ERROR_NOT_FOUND);
    assert_no_transport();
    assert(get_display_calls == 0);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
}

static void test_write_not_authorized_fails_before_transport(void) {
    RSSDDCDisplay display = lg_display();
    RSSDDCCharacterization *characterization = assembled(&display, NULL);
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute route = writable_unauthorized_input_route();
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &route) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, knowledge) == RSS_DDC_OK);
    assert(rss_ddc_characterization_resolve(characterization, "inputs.switching", &resolution) ==
           RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_write(resolution) != NULL);
    assert(!rss_ddc_monitor_knowledge_resolution_write_authorized(resolution));
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    reset_transport();
    bind_live_display(characterization);
    assert(rss_ddc_characterization_set_input(characterization, 0x90) == RSS_DDC_ERROR_PROFILE_UNSAFE);
    assert_no_transport();
    assert(get_display_calls == 0);
    rss_ddc_monitor_knowledge_destroy(knowledge);
    rss_ddc_characterization_destroy(characterization);
}

static void test_readable_vcp60_only_fails_before_transport(void) {
    RSSDDCDisplay display = lg_display();
    RSSDDCCharacterization *characterization = assembled(&display, NULL);
    RSSDDCMonitorKnowledge *observed = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute route = observed_input_route(0);
    assert(observed != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(observed, &route) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, observed) == RSS_DDC_OK);
    reset_transport();
    bind_live_display(characterization);
    assert(rss_ddc_characterization_set_input(characterization, 0x90) ==
           RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert_no_transport();
    assert(get_display_calls == 0);
    rss_ddc_monitor_knowledge_destroy(observed);
    rss_ddc_characterization_destroy(characterization);
}

static void test_mccs_advertised_only_fails_before_transport(void) {
    RSSDDCDisplay display = lg_display();
    RSSDDCCharacterization *characterization = assembled(&display, NULL);
    const char *raw = "vcp(60(11 12 0f 00))";
    assert(rss_ddc_characterization_collect_passive_mccs_raw(characterization, raw, strlen(raw)) ==
           RSS_DDC_OK);
    reset_transport();
    bind_live_display(characterization);
    assert(rss_ddc_characterization_set_input(characterization, 0x11) ==
           RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert_no_transport();
    assert(get_display_calls == 0);
    rss_ddc_characterization_destroy(characterization);
}

static void test_value_outside_authorized_domain_fails_before_transport(void) {
    RSSDDCDisplay display = lg_display();
    RSSDDCProfileStore *store = builtin_store();
    RSSDDCCharacterization *characterization = assembled(&display, store);
    reset_transport();
    bind_live_display(characterization);
    assert(rss_ddc_characterization_set_input(characterization, 0x11) == RSS_DDC_ERROR_ARGUMENT);
    assert_no_transport();
    assert(get_display_calls == 0);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
}

static void test_current_value_is_not_authorized_domain(void) {
    RSSDDCDisplay display = lg_display();
    RSSDDCProfileStore *store = builtin_store();
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCMonitorKnowledge *observed = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute route = observed_input_route(0x11);
    RSSDDCCharacterizationValueState state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    assert(characterization != NULL && observed != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(observed, &route) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, observed) == RSS_DDC_OK);
    assert(rss_ddc_characterization_augment_with_prior(characterization) == RSS_DDC_OK);
    assert(rss_ddc_characterization_current_value(characterization, "inputs.switching", &state,
                                                  &current) == RSS_DDC_OK);
    assert(state == RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED);
    assert(current != NULL && current->value.unsigned_value == 0x11);
    reset_transport();
    bind_live_display(characterization);
    assert(rss_ddc_characterization_set_input(characterization, 0x11) == RSS_DDC_ERROR_ARGUMENT);
    assert_no_transport();
    assert(rss_ddc_characterization_set_input(characterization, 0xd0) == RSS_DDC_OK);
    assert(alternate_calls == 1 && alternate_value == 0xd0);
    rss_ddc_monitor_knowledge_destroy(observed);
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
}

static void test_route_comes_from_effective_knowledge_not_model_name(void) {
    RSSDDCDisplay lg = lg_display();
    RSSDDCDisplay studio = standard_display();
    RSSDDCProfileStore *builtin = builtin_store();
    RSSDDCProfileStore *standard = pack_store(standard_input_pack());
    RSSDDCCharacterization *lg_named_without_profile = assembled(&lg, NULL);
    RSSDDCCharacterization *studio_with_standard = assembled(&studio, standard);
    RSSDDCCharacterization *lg_with_profile = assembled(&lg, builtin);
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;

    reset_transport();
    bind_live_display(lg_named_without_profile);
    assert(rss_ddc_characterization_set_input(lg_named_without_profile, 0x90) ==
           RSS_DDC_ERROR_NOT_FOUND);
    assert_no_transport();

    reset_transport();
    bind_live_display(studio_with_standard);
    assert(rss_ddc_characterization_resolve(studio_with_standard, "inputs.switching", &resolution) ==
           RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_write(resolution)->kind ==
           RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    assert(rss_ddc_characterization_set_input(studio_with_standard, 0x12) == RSS_DDC_OK);
    assert(standard_calls == 1 && standard_value == 0x12 && alternate_calls == 0);

    reset_transport();
    bind_live_display(lg_with_profile);
    assert(rss_ddc_characterization_resolve(lg_with_profile, "inputs.switching", &resolution) ==
           RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_write(resolution)->kind ==
           RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT);
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    assert(rss_ddc_characterization_set_input(lg_with_profile, 0x91) == RSS_DDC_OK);
    assert(alternate_calls == 1 && alternate_value == 0x91 && standard_calls == 0);

    rss_ddc_characterization_destroy(lg_named_without_profile);
    rss_ddc_characterization_destroy(studio_with_standard);
    rss_ddc_characterization_destroy(lg_with_profile);
    rss_ddc_profile_store_destroy(builtin);
    rss_ddc_profile_store_destroy(standard);
}

static void test_lower_level_set_input_still_requires_method(void) {
    reset_transport();
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_STANDARD, 0x11) == RSS_DDC_OK);
    assert(standard_calls == 1 && standard_vcp == 0x60 && standard_value == 0x11);
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_LG_ALT, 0x11) == RSS_DDC_ERROR_ARGUMENT);
    assert(alternate_calls == 0);
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_LG_ALT, 0x90) == RSS_DDC_OK);
    assert(alternate_calls == 1 && alternate_value == 0x90);
}

static void test_stale_target_fails_closed(void) {
    RSSDDCDisplay display = lg_display();
    RSSDDCProfileStore *store = builtin_store();
    RSSDDCCharacterization *characterization = assembled(&display, store);
    reset_transport();
    bind_live_display(characterization);
    (void)snprintf(live_display.serial, sizeof(live_display.serial), "%s", "different-serial");
    assert(rss_ddc_characterization_set_input(characterization, 0x90) == RSS_DDC_ERROR_SAFETY_GATE);
    assert(get_display_calls == 1);
    assert_no_transport();

    reset_transport();
    bind_live_display(characterization);
    get_display_result = RSS_DDC_ERROR_NOT_FOUND;
    live_display_ready = false;
    assert(rss_ddc_characterization_set_input(characterization, 0x90) == RSS_DDC_ERROR_NOT_FOUND);
    assert(get_display_calls == 1);
    assert_no_transport();
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
}

static void test_transport_failure_propagates(void) {
    RSSDDCDisplay lg = lg_display();
    RSSDDCDisplay studio = standard_display();
    RSSDDCProfileStore *builtin = builtin_store();
    RSSDDCProfileStore *standard = pack_store(standard_input_pack());
    RSSDDCCharacterization *lg_characterization = assembled(&lg, builtin);
    RSSDDCCharacterization *standard_characterization = assembled(&studio, standard);

    reset_transport();
    bind_live_display(lg_characterization);
    alternate_result = RSS_DDC_ERROR_WRITE;
    assert(rss_ddc_characterization_set_input(lg_characterization, 0x90) == RSS_DDC_ERROR_WRITE);
    assert(alternate_calls == 1 && production_write_calls == 0);

    reset_transport();
    bind_live_display(standard_characterization);
    standard_result = RSS_DDC_ERROR_WRITE;
    assert(rss_ddc_characterization_set_input(standard_characterization, 0x11) == RSS_DDC_ERROR_WRITE);
    assert(standard_calls == 1 && alternate_calls == 0);

    rss_ddc_characterization_destroy(lg_characterization);
    rss_ddc_characterization_destroy(standard_characterization);
    rss_ddc_profile_store_destroy(builtin);
    rss_ddc_profile_store_destroy(standard);
}

static void test_wrong_characterization_target_does_not_use_other_display_index(void) {
    RSSDDCDisplay lg = lg_display();
    RSSDDCProfileStore *store = builtin_store();
    RSSDDCCharacterization *characterization = assembled(&lg, store);
    reset_transport();
    bind_live_display(characterization);
    live_display.list_index = 9;
    live_display.cg_display_id = 1000;
    assert(rss_ddc_characterization_set_input(characterization, 0x90) == RSS_DDC_ERROR_NOT_FOUND);
    assert_no_transport();
    rss_ddc_characterization_destroy(characterization);
    rss_ddc_profile_store_destroy(store);
}

int main(void) {
    test_null_and_unassembled_are_argument_errors();
    test_lg_alt_dispatches_without_method_enum();
    test_standard_dispatches_without_method_enum();
    test_no_write_method_fails_before_transport();
    test_write_not_authorized_fails_before_transport();
    test_readable_vcp60_only_fails_before_transport();
    test_mccs_advertised_only_fails_before_transport();
    test_value_outside_authorized_domain_fails_before_transport();
    test_current_value_is_not_authorized_domain();
    test_route_comes_from_effective_knowledge_not_model_name();
    test_lower_level_set_input_still_requires_method();
    test_stale_target_fails_closed();
    test_transport_failure_propagates();
    test_wrong_characterization_target_does_not_use_other_display_index();
    puts("test_characterize_input: passed");
    return 0;
}
