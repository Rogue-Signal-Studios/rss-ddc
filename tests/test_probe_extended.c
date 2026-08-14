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
    Reply replies[RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT][RSS_DDC_PROBE_EXTENDED_REPEAT_COUNT];
    unsigned attempts[RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT];
    RSSDDCError mccs_error;
    const char *mccs_raw;
    size_t reads;
    size_t mccs_reads;
    size_t writes;
    size_t delays;
    uint32_t last_delay_ms;
} MockExtendedTransport;

static RSSDDCError mock_get_vcp(void *context, uint8_t code, RSSDDCVCPResult *result) {
    MockExtendedTransport *mock = context;
    assert((unsigned)code < RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT);
    unsigned attempt = mock->attempts[code]++;
    assert(attempt < RSS_DDC_PROBE_EXTENDED_REPEAT_COUNT);
    ++mock->reads;
    Reply reply = mock->replies[code][attempt];
    if (reply.error == RSS_DDC_OK) {
        *result = reply.result;
    }
    return reply.error;
}

static RSSDDCError mock_get_mccs(void *context, RSSDDCMCCSCapabilities *capabilities) {
    MockExtendedTransport *mock = context;
    ++mock->mccs_reads;
    if (mock->mccs_error != RSS_DDC_OK) {
        return mock->mccs_error;
    }
    return rss_ddc_parse_mccs_capabilities(mock->mccs_raw, strlen(mock->mccs_raw), capabilities);
}

static void mock_delay(void *context, uint32_t milliseconds) {
    MockExtendedTransport *mock = context;
    ++mock->delays;
    mock->last_delay_ms = milliseconds;
}

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

static void set_reply(MockExtendedTransport *mock, uint8_t code, unsigned attempt, RSSDDCError error,
                      uint8_t echoed, uint16_t maximum, uint16_t current) {
    mock->replies[code][attempt] = (Reply){.error = error,
                                            .result = {.vcp_code = echoed, .maximum_value = maximum,
                                                       .current_value = current}};
}

static void set_stable(MockExtendedTransport *mock, uint8_t code, uint16_t maximum, uint16_t current) {
    set_reply(mock, code, 0, RSS_DDC_OK, code, maximum, current);
    set_reply(mock, code, 1, RSS_DDC_OK, code, maximum, current);
}

static RSSDDCProbeTarget target(bool mccs, RSSDDCProvider provider) {
    RSSDDCProbeTarget selected = {.correlation = RSS_DDC_PROBE_CORRELATION_EXACT};
    selected.display.list_index = 3;
    selected.display.external = true;
    selected.display.provider = provider;
    selected.display.capabilities = mccs ? RSS_DDC_CAP_MCCS_CAPABILITIES : RSS_DDC_CAP_NONE;
    snprintf(selected.display.product_name, sizeof(selected.display.product_name), "%s", "LG HDR QHD");
    snprintf(selected.display.transport, sizeof(selected.display.transport), "%s", "DCPEXT0");
    return selected;
}

static RSSDDCProbe *run_extended(MockExtendedTransport *mock, bool mccs, RSSDDCProvider provider) {
    RSSDDCProbeReadTransport transport = {.context = mock,
                                          .get_vcp = mock_get_vcp,
                                          .get_mccs_capabilities = mock_get_mccs,
                                          .delay = mock_delay};
    RSSDDCProbe *probe = NULL;
    RSSDDCProbeTarget selected = target(mccs, provider);
    assert(rss_ddc_probe_create(&selected, &transport, &probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_extended(probe) == RSS_DDC_OK);
    return probe;
}

static void fill_default_replies(MockExtendedTransport *mock) {
    for (uint16_t code = 0; code < RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT; ++code) {
        set_reply(mock, (uint8_t)code, 0, RSS_DDC_ERROR_REPLY_STATUS, 0, 0, 0);
    }
}

static void test_bounds_ordering_and_request_limits(void) {
    MockExtendedTransport mock = {.mccs_error = RSS_DDC_ERROR_READ};
    fill_default_replies(&mock);
    RSSDDCProbe *probe = run_extended(&mock, false, RSS_DDC_PROVIDER_DCPDP13);
    RSSDDCProbeExtendedDiagnostics diagnostics = {0};
    assert(rss_ddc_probe_extended_diagnostics(probe, &diagnostics) == RSS_DDC_OK);
    assert(diagnostics.requested == 256 && diagnostics.attempted == 256);
    assert(diagnostics.observation_count == 256);
    assert(mock.reads == 256 && mock.mccs_reads == 0 && mock.writes == 0);
    assert(mock.delays == 255);
    assert(mock.last_delay_ms == RSS_DDC_PROBE_EXTENDED_REPEAT_DELAY_MS);
    for (size_t index = 0; index < diagnostics.observation_count; ++index) {
        assert(diagnostics.observations[index].observation.requested_vcp == (uint8_t)index);
    }
    rss_ddc_probe_destroy(probe);
}

static void test_classification_matrix_and_unadvertised_regression(void) {
    MockExtendedTransport mock = {.mccs_error = RSS_DDC_OK, .mccs_raw = "vcp(10(01 02))"};
    fill_default_replies(&mock);
    set_stable(&mock, 0x10, 100, 1);
    set_stable(&mock, 0x42, 4, 99);
    set_reply(&mock, 0x43, 0, RSS_DDC_OK, 0x43, 100, 50);
    set_reply(&mock, 0x43, 1, RSS_DDC_OK, 0x43, 100, 51);
    set_reply(&mock, 0x61, 0, RSS_DDC_ERROR_REPLY_CHECKSUM, 0, 0, 0);
    set_reply(&mock, 0x62, 0, RSS_DDC_ERROR_READ, 0, 0, 0);
    set_reply(&mock, 0x63, 0, RSS_DDC_OK, 0x14, 1, 1);

    RSSDDCProbe *probe = run_extended(&mock, true, RSS_DDC_PROVIDER_DCPDP13);
    RSSDDCProbeExtendedDiagnostics diagnostics = {0};
    assert(rss_ddc_probe_extended_diagnostics(probe, &diagnostics) == RSS_DDC_OK);
    assert(diagnostics.mccs_available);
    assert(diagnostics.observations[0x10].observation.advertised == RSS_DDC_PROBE_KNOWLEDGE_YES);
    assert(diagnostics.strict_valid == 3);
    assert(diagnostics.stable_valid == 2);
    assert(diagnostics.variable_valid == 1);
    assert(diagnostics.protocol_reported == 250);
    assert(diagnostics.malformed == 1);
    assert(diagnostics.transport_errors == 1);
    assert(diagnostics.semantic_mismatch == 1);
    assert(diagnostics.advertised_valid == 1);
    assert(diagnostics.unadvertised_valid == 2);

    const RSSDDCProbeExtendedObservation *advertised = &diagnostics.observations[0x10];
    assert(advertised->observation.category == RSS_DDC_PROBE_RESULT_STABLE);
    assert(advertised->observation.advertised == RSS_DDC_PROBE_KNOWLEDGE_YES);
    assert(advertised->interpretation == RSS_DDC_PROBE_INTERPRETATION_OBSERVED_ADVERTISED);
    assert(advertised->enum_list_present && advertised->current_in_declared_enum);

    const RSSDDCProbeExtendedObservation *unadvertised = &diagnostics.observations[0x42];
    assert(unadvertised->observation.category == RSS_DDC_PROBE_RESULT_STABLE);
    assert(unadvertised->observation.advertised == RSS_DDC_PROBE_KNOWLEDGE_NO);
    assert(unadvertised->observation.current_exceeds_maximum);
    assert(unadvertised->interpretation == RSS_DDC_PROBE_INTERPRETATION_OBSERVED_UNADVERTISED);
    assert(strcmp(unadvertised->observation.semantic_id, "vendor.unknown.vcp.42") == 0);

    const RSSDDCMonitorKnowledge *knowledge = NULL;
    assert(rss_ddc_probe_knowledge(probe, &knowledge) == RSS_DDC_OK);
    bool found_unadvertised = false;
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route->address == 0x42 && route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED) {
            found_unadvertised = true;
            assert(!route->writable && !route->write_authorized);
            assert(route->value.unsigned_value == 99);
        }
    }
    assert(found_unadvertised);
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    assert(rss_ddc_monitor_knowledge_resolve(&knowledge, 1, "vendor.unknown.vcp.42", &resolution) == RSS_DDC_OK);
    assert(rss_ddc_monitor_knowledge_resolution_preferred_write(resolution) == NULL);
    assert(!rss_ddc_monitor_knowledge_resolution_write_authorized(resolution));
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    rss_ddc_probe_destroy(probe);
}

static void test_no_mccs_advertised_unknown(void) {
    MockExtendedTransport mock = {.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY};
    set_stable(&mock, 0x10, 100, 50);
    for (uint16_t code = 0; code < RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT; ++code) {
        if (code == 0x10) continue;
        set_reply(&mock, (uint8_t)code, 0, RSS_DDC_ERROR_REPLY_STATUS, 0, 0, 0);
    }
    RSSDDCProbe *probe = run_extended(&mock, false, RSS_DDC_PROVIDER_PS190);
    const RSSDDCProbeExtendedObservation *observation = NULL;
    RSSDDCProbeExtendedDiagnostics diagnostics = {0};
    assert(rss_ddc_probe_extended_diagnostics(probe, &diagnostics) == RSS_DDC_OK);
    observation = &diagnostics.observations[0x10];
    assert(observation->observation.advertised == RSS_DDC_PROBE_KNOWLEDGE_UNKNOWN);
    assert(observation->interpretation == RSS_DDC_PROBE_INTERPRETATION_OBSERVED_PROTOCOL_VALID);
    rss_ddc_probe_destroy(probe);
}

static void test_transport_storm_abort(void) {
    MockExtendedTransport mock = {.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY};
    for (uint16_t code = 0; code < RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT; ++code) {
        set_reply(&mock, (uint8_t)code, 0, RSS_DDC_ERROR_READ, 0, 0, 0);
        set_reply(&mock, (uint8_t)code, 1, RSS_DDC_ERROR_READ, 0, 0, 0);
    }
    RSSDDCProbe *probe = run_extended(&mock, false, RSS_DDC_PROVIDER_DCPDP13);
    RSSDDCProbeExtendedDiagnostics diagnostics = {0};
    assert(rss_ddc_probe_extended_diagnostics(probe, &diagnostics) == RSS_DDC_OK);
    assert(diagnostics.aborted);
    assert(diagnostics.attempted == RSS_DDC_PROBE_EXTENDED_TRANSPORT_FAILURE_LIMIT);
    assert(diagnostics.observations[255].observation.category == RSS_DDC_PROBE_RESULT_UNATTEMPTED);
    rss_ddc_probe_destroy(probe);
}

static void test_unsupported_provider(void) {
    MockExtendedTransport mock = {.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY};
    RSSDDCProbeReadTransport transport = {.context = &mock, .get_vcp = mock_get_vcp, .delay = mock_delay};
    RSSDDCProbe *probe = NULL;
    RSSDDCProbeTarget selected = target(false, RSS_DDC_PROVIDER_MCDP29XX);
    assert(rss_ddc_probe_create(&selected, &transport, &probe) == RSS_DDC_OK);
    assert(rss_ddc_probe_extended(probe) == RSS_DDC_ERROR_UNSUPPORTED_PROVIDER);
    rss_ddc_probe_destroy(probe);
}

static void test_allocation_failure_and_stack_boundary(void) {
    MockExtendedTransport mock = {.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY};
    fill_default_replies(&mock);
    RSSDDCProbeReadTransport transport = {.context = &mock, .get_vcp = mock_get_vcp, .delay = mock_delay};
    RSSDDCProbeTarget selected = target(false, RSS_DDC_PROVIDER_DCPDP13);
    struct {
        uint64_t before;
        RSSDDCProbe *probe;
        uint64_t after;
    } guarded = {.before = UINT64_C(0x0123456789abcdef), .after = UINT64_C(0xfedcba9876543210)};
    assert(rss_ddc_probe_create(&selected, &transport, &guarded.probe) == RSS_DDC_OK);
    rss_ddc_probe_test_fail_next_allocation();
    assert(rss_ddc_probe_extended(guarded.probe) == RSS_DDC_ERROR_SYSTEM);
    assert(guarded.before == UINT64_C(0x0123456789abcdef) && guarded.after == UINT64_C(0xfedcba9876543210));
    rss_ddc_probe_destroy(guarded.probe);
}

int main(void) {
    test_bounds_ordering_and_request_limits();
    test_classification_matrix_and_unadvertised_regression();
    test_no_mccs_advertised_unknown();
    test_transport_storm_abort();
    test_unsupported_provider();
    test_allocation_failure_and_stack_boundary();
    puts("test_probe_extended: passed");
}
