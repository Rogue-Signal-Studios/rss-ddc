#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rss_ddc.h"

extern void rss_ddc_probe_test_fail_next_allocation(void);

typedef struct {
    RSSDDCError error;
    RSSDDCVCPResult result;
} Reply;

typedef struct {
    Reply replies[RSS_DDC_PROBE_QUICK_CONTROL_COUNT][RSS_DDC_PROBE_QUICK_REPEAT_COUNT];
    unsigned attempts[RSS_DDC_PROBE_QUICK_CONTROL_COUNT];
    RSSDDCError mccs_error;
    const char *mccs_raw;
    size_t reads;
    size_t mccs_reads;
    size_t writes;
} MockReadTransport;

static const uint8_t codes[RSS_DDC_PROBE_QUICK_CONTROL_COUNT] = {0x10, 0x12, 0x14, 0x16, 0x18, 0x1a};

static size_t code_index(uint8_t code) {
    for (size_t index = 0; index < sizeof(codes); ++index) {
        if (codes[index] == code) return index;
    }
    return sizeof(codes);
}

static RSSDDCError mock_get_vcp(void *context, uint8_t code, RSSDDCVCPResult *result) {
    MockReadTransport *mock = context;
    size_t index = code_index(code);
    assert(index < sizeof(codes));
    unsigned attempt = mock->attempts[index]++;
    assert(attempt < RSS_DDC_PROBE_QUICK_REPEAT_COUNT);
    ++mock->reads;
    Reply reply = mock->replies[index][attempt];
    if (reply.error == RSS_DDC_OK) *result = reply.result;
    return reply.error;
}

static RSSDDCError mock_get_mccs(void *context, RSSDDCMCCSCapabilities *capabilities) {
    MockReadTransport *mock = context;
    ++mock->mccs_reads;
    if (mock->mccs_error != RSS_DDC_OK) return mock->mccs_error;
    return rss_ddc_parse_mccs_capabilities(mock->mccs_raw, strlen(mock->mccs_raw), capabilities);
}

/* The injected tests never use the live convenience entry point. */
RSSDDCError rss_ddc_get_display(uint32_t index, RSSDDCDisplay *display) {
    (void)index; (void)display; return RSS_DDC_ERROR_DISCOVERY;
}
RSSDDCError rss_ddc_get_vcp(uint32_t index, uint8_t code, RSSDDCVCPResult *result) {
    (void)index; (void)code; (void)result; return RSS_DDC_ERROR_DISCOVERY;
}
RSSDDCError rss_ddc_get_mccs_capabilities(uint32_t index, RSSDDCMCCSCapabilities *capabilities) {
    (void)index; (void)capabilities; return RSS_DDC_ERROR_DISCOVERY;
}

static void set_reply(MockReadTransport *mock, size_t index, unsigned attempt, RSSDDCError error,
                      uint8_t echoed, uint16_t maximum, uint16_t current) {
    mock->replies[index][attempt] = (Reply){.error = error,
                                             .result = {.vcp_code = echoed, .maximum_value = maximum,
                                                        .current_value = current}};
}

static void set_stable(MockReadTransport *mock, size_t index, uint16_t maximum, uint16_t current) {
    set_reply(mock, index, 0, RSS_DDC_OK, codes[index], maximum, current);
    set_reply(mock, index, 1, RSS_DDC_OK, codes[index], maximum, current);
}

static RSSDDCMonitorKnowledge *profile_knowledge(void) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    RSSDDCKnowledgeRoute route = {.kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP,
                                  .address = 0x10,
                                  .provenance = {.source = RSS_DDC_PROFILE_SOURCE_LOCAL,
                                                 .confidence = RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED,
                                                 .fact_kind = RSS_DDC_KNOWLEDGE_FACT_PROFILE}};
    snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", "display.brightness");
    snprintf(route.route_id, sizeof(route.route_id), "%s", "profile-vcp-10");
    snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s", "profile-fixture");
    assert(knowledge != NULL);
    assert(rss_ddc_monitor_knowledge_add_route(knowledge, &route) == RSS_DDC_OK);
    return knowledge;
}

static RSSDDCProbeTarget target(bool mccs, const RSSDDCMonitorKnowledge *profile) {
    RSSDDCProbeTarget target = {.correlation = RSS_DDC_PROBE_CORRELATION_EXACT, .profile_knowledge = profile};
    target.display.list_index = 7;
    target.display.external = true;
    target.display.provider = RSS_DDC_PROVIDER_DCPDP13;
    target.display.capabilities = mccs ? RSS_DDC_CAP_MCCS_CAPABILITIES : RSS_DDC_CAP_NONE;
    snprintf(target.display.product_name, sizeof(target.display.product_name), "%s", "LG HDR QHD");
    snprintf(target.display.transport, sizeof(target.display.transport), "%s", "DCPEXT0");
    return target;
}

static RSSDDCProbe *run(MockReadTransport *mock, bool mccs, const RSSDDCMonitorKnowledge *profile) {
    RSSDDCProbeReadTransport transport = {.context = mock, .get_vcp = mock_get_vcp,
                                          .get_mccs_capabilities = mock_get_mccs};
    RSSDDCProbe *probe = NULL;
    RSSDDCProbeTarget selected = target(mccs, profile);
    assert(rss_ddc_probe_create(&selected, &transport, &probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_quick(probe) == RSS_DDC_OK);
    return probe;
}

static void test_final_observation_semantics(void) {
    MockReadTransport mock = {.mccs_error = RSS_DDC_OK, .mccs_raw = "vcp(10 14)"};
    RSSDDCMonitorKnowledge *profile = profile_knowledge();
    set_stable(&mock, 0, 10, 18); /* unusual, retained; not a scalar-range claim */
    set_reply(&mock, 1, 0, RSS_DDC_OK, 0x12, 100, 50);
    set_reply(&mock, 1, 1, RSS_DDC_OK, 0x12, 100, 51);
    set_reply(&mock, 2, 0, RSS_DDC_ERROR_REPLY_STATUS, 0, 0, 0);
    set_reply(&mock, 3, 0, RSS_DDC_ERROR_READ, 0, 0, 0);
    set_reply(&mock, 4, 0, RSS_DDC_ERROR_REPLY_CHECKSUM, 0, 0, 0);
    set_reply(&mock, 5, 0, RSS_DDC_OK, 0x14, 4, 1);

    RSSDDCProbe *probe = run(&mock, true, profile);
    RSSDDCProbeDiagnostics diagnostics = {0};
    assert(rss_ddc_probe_diagnostics(probe, &diagnostics) == RSS_DDC_OK);
    assert(diagnostics.observation_count == RSS_DDC_PROBE_QUICK_CONTROL_COUNT);
    assert(diagnostics.controls_attempted == 6 && diagnostics.controls_protocol_valid == 2);
    assert(diagnostics.controls_stable == 1 && diagnostics.controls_variable == 1);
    assert(diagnostics.controls_protocol_reported == 1 && diagnostics.controls_malformed == 2 &&
           diagnostics.controls_transport_error == 1);
    assert(mock.reads == 8 && mock.mccs_reads == 1 && mock.writes == 0);

    const RSSDDCProbeObservation *observations = diagnostics.observations;
    assert(observations[0].requested_vcp == 0x10 && observations[0].category == RSS_DDC_PROBE_RESULT_STABLE);
    assert(observations[0].protocol_valid && observations[0].semantic_request_match &&
           observations[0].current_exceeds_maximum && observations[0].advertised == RSS_DDC_PROBE_KNOWLEDGE_YES &&
           observations[0].profile_known == RSS_DDC_PROBE_KNOWLEDGE_YES);
    assert(observations[1].category == RSS_DDC_PROBE_RESULT_VARIABLE && observations[1].advertised == RSS_DDC_PROBE_KNOWLEDGE_NO);
    assert(observations[2].category == RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED &&
           observations[2].transport == RSS_DDC_PROBE_TRANSPORT_SUCCEEDED && !observations[2].protocol_valid);
    assert(!observations[2].repeat_attempted);
    assert(strcmp(rss_ddc_probe_repeat_error_name(&observations[2]), "not-attempted") == 0);
    assert(observations[0].repeat_attempted);
    assert(observations[3].category == RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR &&
           observations[3].transport == RSS_DDC_PROBE_TRANSPORT_FAILED);
    assert(observations[4].category == RSS_DDC_PROBE_RESULT_MALFORMED);
    assert(observations[5].category == RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH &&
           !observations[5].semantic_request_match);

    const RSSDDCMCCSCapabilities *mccs = NULL;
    assert(rss_ddc_probe_mccs_capabilities(probe, &mccs) == RSS_DDC_OK && rss_ddc_mccs_capabilities_has_vcp(mccs, 0x10));
    const RSSDDCMonitorKnowledge *knowledge = NULL;
    assert(rss_ddc_probe_knowledge(probe, &knowledge) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_route_count(knowledge) == 4);
    const RSSDDCKnowledgeRoute *live = rss_ddc_monitor_knowledge_route_at(knowledge, 0);
    assert(live->value.unsigned_value == 18 && live->reported_maximum_present && live->reported_maximum == 10 &&
           live->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED && !live->writable && !live->write_authorized);
    const RSSDDCMonitorKnowledge *sources[] = {knowledge};
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    assert(rss_ddc_monitor_knowledge_resolve(sources, 1, "display.brightness", &resolution) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_write(resolution) == NULL &&
           !rss_ddc_monitor_knowledge_resolution_write_authorized(resolution));
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_probe_destroy(probe);
    rss_ddc_monitor_knowledge_destroy(profile);
}

static void test_determinism_and_cleanup(void) {
    MockReadTransport first = {.mccs_error = RSS_DDC_ERROR_READ};
    MockReadTransport second = {.mccs_error = RSS_DDC_ERROR_READ};
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        set_stable(&first, index, 100, (uint16_t)(10 + index));
        set_stable(&second, index, 100, (uint16_t)(10 + index));
    }
    RSSDDCProbe *left = run(&first, false, NULL);
    RSSDDCProbe *right = run(&second, false, NULL);
    RSSDDCProbeDiagnostics a = {0}, b = {0};
    assert(rss_ddc_probe_diagnostics(left, &a) == RSS_DDC_OK && rss_ddc_probe_diagnostics(right, &b) == RSS_DDC_OK);
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        assert(a.observations[index].requested_vcp == codes[index]);
        assert(a.observations[index].requested_vcp == b.observations[index].requested_vcp);
        assert(a.observations[index].current_value == b.observations[index].current_value);
    }
    rss_ddc_probe_destroy(left);
    rss_ddc_probe_destroy(right);
}

static void test_quick_control_selection_ignores_product_name(void) {
    MockReadTransport lg = {0};
    MockReadTransport odyssey = {0};
    MockReadTransport alien = {0};
    RSSDDCProbe *lg_probe = NULL;
    RSSDDCProbe *odyssey_probe = NULL;
    RSSDDCProbe *alien_probe = NULL;
    RSSDDCProbeDiagnostics lg_diag = {0};
    RSSDDCProbeDiagnostics odyssey_diag = {0};
    RSSDDCProbeDiagnostics alien_diag = {0};
    RSSDDCProbeReadTransport lg_transport = {.context = &lg, .get_vcp = mock_get_vcp};
    RSSDDCProbeReadTransport odyssey_transport = {.context = &odyssey, .get_vcp = mock_get_vcp};
    RSSDDCProbeReadTransport alien_transport = {.context = &alien, .get_vcp = mock_get_vcp};
    RSSDDCProbeTarget lg_target = target(false, NULL);
    RSSDDCProbeTarget odyssey_target = target(false, NULL);
    RSSDDCProbeTarget alien_target = target(false, NULL);
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        set_stable(&lg, index, 100, 1);
        set_stable(&odyssey, index, 100, 1);
        set_stable(&alien, index, 100, 1);
    }
    snprintf(odyssey_target.display.product_name, sizeof(odyssey_target.display.product_name), "%s",
             "Odyssey G75F");
    snprintf(alien_target.display.product_name, sizeof(alien_target.display.product_name), "%s",
             "Unknown Alien Panel");
    assert(rss_ddc_probe_create(&lg_target, &lg_transport, &lg_probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_create(&odyssey_target, &odyssey_transport, &odyssey_probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_create(&alien_target, &alien_transport, &alien_probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_quick(lg_probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_quick(odyssey_probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_quick(alien_probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_diagnostics(lg_probe, &lg_diag) == RSS_DDC_OK);
    assert(rss_ddc_probe_diagnostics(odyssey_probe, &odyssey_diag) == RSS_DDC_OK);
    assert(rss_ddc_probe_diagnostics(alien_probe, &alien_diag) == RSS_DDC_OK);
    assert(lg_diag.observation_count == RSS_DDC_PROBE_QUICK_CONTROL_COUNT);
    assert(lg.writes == 0 && odyssey.writes == 0 && alien.writes == 0);
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        assert(lg_diag.observations[index].requested_vcp == codes[index]);
        assert(odyssey_diag.observations[index].requested_vcp == codes[index]);
        assert(alien_diag.observations[index].requested_vcp == codes[index]);
    }
    rss_ddc_probe_destroy(lg_probe);
    rss_ddc_probe_destroy(odyssey_probe);
    rss_ddc_probe_destroy(alien_probe);
}

static void test_allocation_failure_and_stack_boundary(void) {
    MockReadTransport mock = {0};
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) set_stable(&mock, index, 100, 1);
    RSSDDCProbeReadTransport transport = {.context = &mock, .get_vcp = mock_get_vcp};
    RSSDDCProbeTarget selected = target(false, NULL);
    struct { uint64_t before; RSSDDCProbe *probe; uint64_t after; } guarded = {
        .before = UINT64_C(0x0123456789abcdef), .after = UINT64_C(0xfedcba9876543210)};
    assert(rss_ddc_probe_create(&selected, &transport, &guarded.probe) == RSS_DDC_OK);
    rss_ddc_probe_test_fail_next_allocation();
    assert(rss_ddc_probe_quick(guarded.probe) == RSS_DDC_ERROR_SYSTEM);
    assert(guarded.before == UINT64_C(0x0123456789abcdef) && guarded.after == UINT64_C(0xfedcba9876543210));
    rss_ddc_probe_destroy(guarded.probe);
}

int main(void) {
    test_final_observation_semantics();
    test_determinism_and_cleanup();
    test_quick_control_selection_ignores_product_name();
    test_allocation_failure_and_stack_boundary();
    puts("test_probe: passed");
}
