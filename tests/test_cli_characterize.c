#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "characterize.h"
#include "output_settings.h"
#include "render.h"
#include "rss_ddc.h"

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

static RSSDDCDisplay dcpdp13_display(void) {
    RSSDDCDisplay display = {
        .list_index = 2,
        .cg_display_id = 9,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_DCPDP13,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "LG HDR QHD");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT0");
    (void)snprintf(display.branch_device_id, sizeof(display.branch_device_id), "%s", "branch-lg");
    return display;
}

static RSSDDCDisplay ps190_display(void) {
    RSSDDCDisplay display = {
        .list_index = 1,
        .cg_display_id = 4,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_PS190,
    };
    (void)snprintf(display.product_name, sizeof(display.product_name), "%s", "Odyssey G75F");
    (void)snprintf(display.transport, sizeof(display.transport), "%s", "DCPEXT1");
    return display;
}

static const char *actionable_pack(void) {
    return "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
           "\"packId\":\"cli-char\",\"profiles\":[{\"id\":\"one\",\"identity\":{"
           "\"productName\":\"LG HDR QHD\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\","
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

static RSSDDCKnowledgeRoute make_route(const char *semantic_id, const char *source_id,
                                       RSSDDCProfileSource source, RSSDDCProfileConfidence confidence,
                                       RSSDDCKnowledgeFactKind fact_kind, const char *route_id, uint16_t address,
                                       RSSDDCKnowledgeValueState value_state, unsigned value, bool readable,
                                       bool writable, bool write_authorized) {
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
    (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s", source_id);
    return route;
}

typedef struct {
    RSSDDCError error;
    RSSDDCVCPResult result;
} MockReply;

typedef struct {
    MockReply quick[RSS_DDC_PROBE_QUICK_CONTROL_COUNT][RSS_DDC_PROBE_QUICK_REPEAT_COUNT];
    unsigned quick_attempts[RSS_DDC_PROBE_QUICK_CONTROL_COUNT];
    MockReply extended[RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT][RSS_DDC_PROBE_EXTENDED_REPEAT_COUNT];
    unsigned extended_attempts[RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT];
} MockGet;

static const uint8_t quick_codes[RSS_DDC_PROBE_QUICK_CONTROL_COUNT] = {0x10, 0x12, 0x14, 0x16, 0x18, 0x1a};

static size_t quick_index(uint8_t code) {
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        if (quick_codes[index] == code) {
            return index;
        }
    }
    return RSS_DDC_PROBE_QUICK_CONTROL_COUNT;
}

static RSSDDCError mock_quick_get(void *context, uint8_t code, RSSDDCVCPResult *result) {
    MockGet *mock = context;
    size_t index = quick_index(code);
    assert(index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT);
    unsigned attempt = mock->quick_attempts[index]++;
    MockReply reply = mock->quick[index][attempt];
    if (reply.error == RSS_DDC_OK) {
        *result = reply.result;
    }
    return reply.error;
}

static RSSDDCError mock_extended_get(void *context, uint8_t code, RSSDDCVCPResult *result) {
    MockGet *mock = context;
    unsigned attempt = mock->extended_attempts[code]++;
    MockReply reply = mock->extended[code][attempt];
    if (reply.error == RSS_DDC_OK) {
        *result = reply.result;
    }
    return reply.error;
}

static void set_quick_stable(MockGet *mock, size_t index, uint16_t current) {
    mock->quick[index][0] = (MockReply){.result = {.vcp_code = quick_codes[index], .maximum_value = 100,
                                                   .current_value = current}};
    mock->quick[index][1] = mock->quick[index][0];
}

static RSSDDCProbe *run_quick(MockGet *mock, const RSSDDCDisplay *display) {
    RSSDDCProbeReadTransport transport = {.context = mock, .get_vcp = mock_quick_get};
    RSSDDCProbeTarget target = {.correlation = RSS_DDC_PROBE_CORRELATION_EXACT, .display = *display};
    RSSDDCProbe *probe = NULL;
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        set_quick_stable(mock, index, 1);
    }
    set_quick_stable(mock, 0, 42);
    assert(rss_ddc_probe_create(&target, &transport, &probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_quick(probe) == RSS_DDC_OK);
    return probe;
}

static RSSDDCProbe *run_extended(MockGet *mock, const RSSDDCDisplay *display) {
    RSSDDCProbeReadTransport transport = {.context = mock, .get_vcp = mock_extended_get};
    RSSDDCProbeTarget target = {.correlation = RSS_DDC_PROBE_CORRELATION_EXACT, .display = *display};
    RSSDDCProbe *probe = NULL;
    for (uint16_t code = 0; code < RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT; ++code) {
        mock->extended[code][0] = (MockReply){.error = RSS_DDC_ERROR_REPLY_STATUS};
        mock->extended[code][1] = mock->extended[code][0];
    }
    mock->extended[0x15][0] =
        (MockReply){.result = {.vcp_code = 0x15, .maximum_value = 255, .current_value = 0x31}};
    mock->extended[0x15][1] = mock->extended[0x15][0];
    mock->extended[0x60][0] =
        (MockReply){.result = {.vcp_code = 0x60, .maximum_value = 18, .current_value = 0x11}};
    mock->extended[0x60][1] = mock->extended[0x60][0];
    assert(rss_ddc_probe_create(&target, &transport, &probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_extended(probe) == RSS_DDC_OK);
    return probe;
}

static RSSDDCProfileStore *load_pack(const char *pack) {
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    assert(store != NULL);
    assert(rss_ddc_profile_store_load_pack_data(store, pack, strlen(pack)) == RSS_DDC_OK);
    return store;
}

static char *capture_render(void (*render)(FILE *, void *), void *context) {
    char *buffer = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&buffer, &length);
    assert(stream != NULL);
    render(stream, context);
    fclose(stream);
    return buffer;
}

typedef struct {
    const RSSDDCCharacterization *characterization;
    RSSDDCCharacterizeMode mode;
    RSSDDCCliEffectiveOutput output;
} RenderContext;

static void render_capture(FILE *stream, void *context) {
    const RenderContext *ctx = context;
    rss_ddc_cli_render_characterization(stream, ctx->characterization, ctx->mode, &ctx->output);
}

static char *render_text(const RSSDDCCharacterization *characterization, RSSDDCCharacterizeMode mode, bool table) {
    RenderContext ctx = {
        .characterization = characterization,
        .mode = mode,
        .output = {.color = false, .table = table, .unicode = false},
    };
    return capture_render(render_capture, &ctx);
}

static void test_public_api_path_is_presentation_only(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = ps190_display();
    char *plain = NULL;
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    plain = render_text(characterization, RSS_DDC_CHARACTERIZE_MODE_PASSIVE, false);
    assert(strstr(plain, "mode=passive") != NULL);
    assert(strstr(plain, "read-only=yes") != NULL);
    rss_ddc_characterization_destroy(characterization);
    free(plain);
}

static void test_sufficient_and_extended_omitted(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = ps190_display();
    MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    RSSDDCCharacterizationSufficiencyResult sufficiency = {0};
    char *plain = NULL;
    char *table = NULL;
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    probe = run_quick(&mock, &display);
    assert(rss_ddc_characterization_collect_quick_probe(characterization, probe) == RSS_DDC_OK);
    assert(rss_ddc_characterization_sufficiency(characterization, &sufficiency) == RSS_DDC_OK);
    assert(sufficiency.status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT);
    plain = render_text(characterization, RSS_DDC_CHARACTERIZE_MODE_DEFAULT, false);
    table = render_text(characterization, RSS_DDC_CHARACTERIZE_MODE_DEFAULT, true);
    assert(strstr(plain, "sufficiency=SUFFICIENT") != NULL);
    assert(strstr(plain, "extended=not-needed") != NULL);
    assert(strstr(plain, "ALIEN PROBE EXTENDED SUMMARY") == NULL);
    assert(strstr(plain, "current=42") != NULL);
    assert(strstr(plain, "id=display.brightness") != NULL);
    assert(strstr(plain, "ALIEN PROBE QUICK SUMMARY") != NULL);
    assert(strstr(plain, "stable=") != NULL);
    assert(strstr(table, "SUFFICIENT") != NULL);
    assert(strstr(table, "Brightness") != NULL);
    assert(strstr(plain, "vcp=0x") == NULL);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(characterization);
    free(plain);
    free(table);
}

static void test_insufficient_conflict_and_controls(void) {
    RSSDDCCharacterization *insufficient = rss_ddc_characterization_create();
    RSSDDCCharacterization *conflicted = rss_ddc_characterization_create();
    RSSDDCDisplay display = dcpdp13_display();
    MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    RSSDDCProfileStore *store = load_pack(actionable_pack());
    RSSDDCProfileStore *conflicts = rss_ddc_profile_store_create();
    const char *first =
        "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
        "\"packId\":\"a\",\"profiles\":[{\"id\":\"one\",\"identity\":{\"productName\":\"LG HDR QHD\","
        "\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},"
        "\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"brightness\",\"method\":\"vcp\","
        "\"address\":16,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\","
        "\"enums\":[]}]}]}";
    const char *second =
        "{\"schemaVersion\":1,\"databaseVersion\":\"x\",\"minimumRSSDDCVersion\":\"0.1.0\","
        "\"packId\":\"b\",\"profiles\":[{\"id\":\"two\",\"identity\":{\"productName\":\"LG HDR QHD\","
        "\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},"
        "\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"brightness\",\"method\":\"vcp\","
        "\"address\":18,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\","
        "\"enums\":[]}]}]}";
    char *plain = NULL;
    char *conflict_plain = NULL;
    assert(insufficient != NULL && conflicted != NULL && conflicts != NULL);
    assert(rss_ddc_characterization_assemble(insufficient, &display, NULL, NULL) == RSS_DDC_OK);
    probe = run_quick(&mock, &display);
    assert(rss_ddc_characterization_collect_quick_probe(insufficient, probe) == RSS_DDC_OK);
    plain = render_text(insufficient, RSS_DDC_CHARACTERIZE_MODE_DEFAULT, false);
    assert(strstr(plain, "sufficiency=INSUFFICIENT") != NULL);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(insufficient);
    free(plain);

    insufficient = rss_ddc_characterization_create();
    mock = (MockGet){0};
    assert(insufficient != NULL);
    assert(rss_ddc_characterization_assemble(insufficient, &display, NULL, store) == RSS_DDC_OK);
    assert(rss_ddc_characterization_collect_passive_mccs_raw(insufficient, "vcp(10 12 15 60)",
                                                             strlen("vcp(10 12 15 60)")) == RSS_DDC_OK);
    probe = run_quick(&mock, &display);
    assert(rss_ddc_characterization_collect_quick_probe(insufficient, probe) == RSS_DDC_OK);
    plain = render_text(insufficient, RSS_DDC_CHARACTERIZE_MODE_DEFAULT, false);
    assert(strstr(plain, "product=LG HDR QHD") != NULL);
    assert(strstr(plain, "evidence=profile,declared,observed") != NULL);
    assert(strstr(plain, "authorized=yes") != NULL);
    assert(strstr(plain, "current=42") != NULL);
    assert(strstr(plain, "write=vcp:0x10") != NULL);
    assert(strstr(plain, "MCCS SUMMARY") != NULL);
    assert(strstr(plain, "advertised=display.brightness") != NULL);
    assert(rss_ddc_profile_store_load_local_data(conflicts, first, strlen(first)) == RSS_DDC_OK);
    assert(rss_ddc_profile_store_load_local_data(conflicts, second, strlen(second)) == RSS_DDC_OK);
    assert(rss_ddc_characterization_assemble(conflicted, &display, NULL, conflicts) ==
           RSS_DDC_ERROR_PROFILE_CONFLICT);
    conflict_plain = render_text(conflicted, RSS_DDC_CHARACTERIZE_MODE_PASSIVE, false);
    assert(strstr(conflict_plain, "sufficiency=CONFLICT") != NULL);
    assert(strstr(conflict_plain, "profile=conflict") != NULL);
    rss_ddc_probe_destroy(probe);
    rss_ddc_profile_store_destroy(store);
    rss_ddc_profile_store_destroy(conflicts);
    rss_ddc_characterization_destroy(insufficient);
    rss_ddc_characterization_destroy(conflicted);
    free(plain);
    free(conflict_plain);
}

static void test_extended_summary_and_deep_not_attempted(void) {
    RSSDDCCharacterization *with_extended = rss_ddc_characterization_create();
    RSSDDCCharacterization *deep_no_get = rss_ddc_characterization_create();
    RSSDDCDisplay display = dcpdp13_display();
    RSSDDCDisplay mcdp = {
        .list_index = 3,
        .online = true,
        .external = true,
        .provider = RSS_DDC_PROVIDER_MCDP29XX,
    };
    MockGet mock = {0};
    RSSDDCProbe *probe = NULL;
    char *plain = NULL;
    char *deep = NULL;
    (void)snprintf(mcdp.product_name, sizeof(mcdp.product_name), "%s", "Internal");
    (void)snprintf(mcdp.transport, sizeof(mcdp.transport), "%s", "unknown");
    assert(with_extended != NULL && deep_no_get != NULL);
    assert(rss_ddc_characterization_assemble(with_extended, &display, NULL, NULL) == RSS_DDC_OK);
    probe = run_extended(&mock, &display);
    assert(rss_ddc_characterization_collect_extended_probe(with_extended, probe) == RSS_DDC_OK);
    plain = render_text(with_extended, RSS_DDC_CHARACTERIZE_MODE_DEFAULT, false);
    assert(strstr(plain, "ALIEN PROBE EXTENDED SUMMARY") != NULL);
    assert(strstr(plain, "promoted=") != NULL);
    assert(strstr(plain, "skipped-capacity=") != NULL);
    assert(rss_ddc_characterization_assemble(deep_no_get, &mcdp, NULL, NULL) == RSS_DDC_OK);
    deep = render_text(deep_no_get, RSS_DDC_CHARACTERIZE_MODE_DEEP, false);
    assert(strstr(deep, "ALIEN PROBE EXTENDED SUMMARY") != NULL);
    assert(strstr(deep, "extended=not-attempted") != NULL);
    assert(strstr(deep, "extended=not-run (GET unavailable)") != NULL);
    rss_ddc_probe_destroy(probe);
    rss_ddc_characterization_destroy(with_extended);
    rss_ddc_characterization_destroy(deep_no_get);
    free(plain);
    free(deep);
}

static void test_passive_omits_extended_and_write_is_not_implied(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = dcpdp13_display();
    RSSDDCMonitorKnowledge *observed = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute observed_route =
        make_route("display.brightness", "alien-probe-live-read", RSS_DDC_PROFILE_SOURCE_RESEARCH,
                   RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_OBSERVED, "vcp-10-live", 0x10,
                   RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED, 42, true, false, false);
    char *plain = NULL;
    assert(characterization != NULL && observed != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_add_route(observed, &observed_route) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_knowledge(characterization, observed) == RSS_DDC_OK);
    plain = render_text(characterization, RSS_DDC_CHARACTERIZE_MODE_PASSIVE, false);
    assert(strstr(plain, "mode=passive") != NULL);
    assert(strstr(plain, "extended=not-run (passive)") != NULL);
    assert(strstr(plain, "ALIEN PROBE EXTENDED SUMMARY") == NULL);
    assert(strstr(plain, "current=42") != NULL);
    assert(strstr(plain, "authorized=yes") == NULL);
    assert(strstr(plain, "authorized=-") != NULL || strstr(plain, "authorized=no") != NULL);
    rss_ddc_monitor_knowledge_destroy(observed);
    rss_ddc_characterization_destroy(characterization);
    free(plain);
}

static void test_lg_alt_write_label_from_production_method(void) {
    RSSDDCCharacterization *characterization = rss_ddc_characterization_create();
    RSSDDCDisplay display = dcpdp13_display();
    char *plain = NULL;
    assert(characterization != NULL);
    assert(rss_ddc_characterization_assemble(characterization, &display, NULL, NULL) == RSS_DDC_OK);
    assert(rss_ddc_characterization_add_production_methods(characterization) == RSS_DDC_OK);
    plain = render_text(characterization, RSS_DDC_CHARACTERIZE_MODE_PASSIVE, false);
    assert(strstr(plain, "id=inputs.switching") != NULL);
    assert(strstr(plain, "write=LG_ALT") != NULL);
    assert(strstr(plain, "authorized=yes") != NULL);
    rss_ddc_characterization_destroy(characterization);
    free(plain);
}

int main(void) {
    test_public_api_path_is_presentation_only();
    test_sufficient_and_extended_omitted();
    test_insufficient_conflict_and_controls();
    test_extended_summary_and_deep_not_attempted();
    test_passive_omits_extended_and_write_is_not_implied();
    test_lg_alt_write_label_from_production_method();
    puts("test_cli_characterize: passed");
    return 0;
}
