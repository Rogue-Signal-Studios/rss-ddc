#include "rss_ddc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
    uint8_t vcp;
    const char *semantic_id;
} RSSDDCQuickControl;

static const RSSDDCQuickControl quick_controls[RSS_DDC_PROBE_QUICK_CONTROL_COUNT] = {
    {0x10, "display.brightness"}, {0x12, "display.contrast"},
    {0x14, "display.color_preset"}, {0x16, "display.rgb.red_gain"},
    {0x18, "display.rgb.green_gain"}, {0x1a, "display.rgb.blue_gain"},
};

#ifdef RSS_DDC_TESTING
static bool fail_next_allocation;
void rss_ddc_probe_test_fail_next_allocation(void) {
    fail_next_allocation = true;
}
static void *probe_calloc(size_t count, size_t size) {
    if (fail_next_allocation) {
        fail_next_allocation = false;
        return NULL;
    }
    return calloc(count, size);
}
#else
#define probe_calloc calloc
#endif

struct RSSDDCProbe {
    RSSDDCProbeTarget target;
    RSSDDCProbeReadTransport transport;
    RSSDDCMonitorKnowledge *knowledge;
    RSSDDCMCCSCapabilities *mccs;
    RSSDDCProbeObservation observations[RSS_DDC_PROBE_QUICK_CONTROL_COUNT];
    RSSDDCProbeExtendedObservation *extended_observations;
    RSSDDCProbeDiagnostics diagnostics;
    RSSDDCProbeExtendedDiagnostics extended_diagnostics;
    bool owns_transport_context;
};

static uint64_t probe_monotonic_milliseconds(void) {
    struct timeval now = {};
    gettimeofday(&now, NULL);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_usec / 1000u;
}

static bool reply_error_is_malformed(RSSDDCError error) {
    return error == RSS_DDC_ERROR_REPLY_LENGTH || error == RSS_DDC_ERROR_REPLY_SOURCE ||
           error == RSS_DDC_ERROR_REPLY_COMMAND || error == RSS_DDC_ERROR_REPLY_CHECKSUM;
}

static RSSDDCProbeResultCategory category_for_error(RSSDDCError error) {
    if (error == RSS_DDC_ERROR_REPLY_STATUS) {
        return RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED;
    }
    if (error == RSS_DDC_ERROR_REPLY_VCP) {
        return RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH;
    }
    return reply_error_is_malformed(error) ? RSS_DDC_PROBE_RESULT_MALFORMED
                                           : RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR;
}

static RSSDDCProbeTransportState transport_for_error(RSSDDCError error) {
    if (error == RSS_DDC_OK || error == RSS_DDC_ERROR_REPLY_STATUS ||
        error == RSS_DDC_ERROR_REPLY_VCP || reply_error_is_malformed(error)) {
        return RSS_DDC_PROBE_TRANSPORT_SUCCEEDED;
    }
    return RSS_DDC_PROBE_TRANSPORT_FAILED;
}

static bool transport_failure(RSSDDCError error) {
    return error != RSS_DDC_OK && error != RSS_DDC_ERROR_REPLY_STATUS && error != RSS_DDC_ERROR_REPLY_VCP &&
           !reply_error_is_malformed(error);
}

static const char *known_semantic_id(uint8_t vcp) {
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        if (quick_controls[index].vcp == vcp) {
            return quick_controls[index].semantic_id;
        }
    }
    return NULL;
}

static void assign_semantic_id(char *buffer, size_t capacity, uint8_t vcp, const char **semantic_id) {
    const char *known = known_semantic_id(vcp);
    if (known != NULL) {
        *semantic_id = known;
        return;
    }
    (void)snprintf(buffer, capacity, "vendor.unknown.vcp.%02x", vcp);
    *semantic_id = buffer;
}

static RSSDDCProbeKnowledgeState profile_knows_quick(const RSSDDCProbe *probe,
                                                     const RSSDDCQuickControl *control) {
    const RSSDDCMonitorKnowledge *knowledge = probe->target.profile_knowledge;
    if (knowledge == NULL) {
        return RSS_DDC_PROBE_KNOWLEDGE_UNKNOWN;
    }
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route != NULL && route->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP &&
            route->address == control->vcp && strcmp(route->semantic_id, control->semantic_id) == 0) {
            return RSS_DDC_PROBE_KNOWLEDGE_YES;
        }
    }
    return RSS_DDC_PROBE_KNOWLEDGE_NO;
}

static RSSDDCProbeKnowledgeState profile_knows_vcp(const RSSDDCProbe *probe, uint8_t vcp) {
    const RSSDDCMonitorKnowledge *knowledge = probe->target.profile_knowledge;
    if (knowledge == NULL) {
        return RSS_DDC_PROBE_KNOWLEDGE_UNKNOWN;
    }
    for (size_t index = 0; index < rss_ddc_monitor_knowledge_route_count(knowledge); ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route != NULL && route->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP && route->address == vcp) {
            return RSS_DDC_PROBE_KNOWLEDGE_YES;
        }
    }
    return RSS_DDC_PROBE_KNOWLEDGE_NO;
}

static bool mccs_advertises(const RSSDDCProbe *probe, uint8_t vcp) {
    return probe->mccs != NULL && rss_ddc_mccs_capabilities_has_vcp(probe->mccs, vcp);
}

static RSSDDCProbeKnowledgeState advertised_state(const RSSDDCProbe *probe, bool mccs_available, uint8_t vcp) {
    if (!mccs_available) {
        return RSS_DDC_PROBE_KNOWLEDGE_UNKNOWN;
    }
    return mccs_advertises(probe, vcp) ? RSS_DDC_PROBE_KNOWLEDGE_YES : RSS_DDC_PROBE_KNOWLEDGE_NO;
}

static void probe_delay(const RSSDDCProbeReadTransport *transport, uint32_t delay_ms) {
    if (delay_ms > 0 && transport->delay != NULL) {
        transport->delay(transport->context, delay_ms);
    }
}

static void reset_observation(RSSDDCProbeObservation *observation) {
    memset(observation, 0, sizeof(*observation));
    observation->first_error = RSS_DDC_ERROR_NOT_FOUND;
}

static void observe_vcp_pair(const RSSDDCProbeReadTransport *transport, uint8_t vcp, uint32_t repeat_delay_ms,
                             RSSDDCProbeObservation *observation) {
    reset_observation(observation);
    observation->requested_vcp = vcp;
    RSSDDCVCPResult first = {0};
    observation->first_error = transport->get_vcp(transport->context, vcp, &first);
    observation->transport = transport_for_error(observation->first_error);
    if (observation->first_error != RSS_DDC_OK) {
        observation->category = category_for_error(observation->first_error);
        return;
    }
    if (first.vcp_code != vcp) {
        observation->category = RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH;
        observation->transport = RSS_DDC_PROBE_TRANSPORT_SUCCEEDED;
        observation->semantic_request_match = false;
        return;
    }
    observation->protocol_valid = true;
    observation->semantic_request_match = true;
    observation->current_value = first.current_value;
    observation->maximum_value = first.maximum_value;
    observation->current_exceeds_maximum = first.current_value > first.maximum_value;
    probe_delay(transport, repeat_delay_ms);
    RSSDDCVCPResult repeat = {0};
    observation->repeat_attempted = true;
    observation->repeat_error = transport->get_vcp(transport->context, vcp, &repeat);
    observation->stable = observation->repeat_error == RSS_DDC_OK && repeat.vcp_code == vcp &&
                          repeat.current_value == first.current_value && repeat.maximum_value == first.maximum_value;
    observation->category =
        observation->stable ? RSS_DDC_PROBE_RESULT_STABLE : RSS_DDC_PROBE_RESULT_VARIABLE;
}

static void count_quick_first_failure(RSSDDCProbe *probe, RSSDDCProbeResultCategory category) {
    if (category == RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED) {
        ++probe->diagnostics.controls_protocol_reported;
    } else if (category == RSS_DDC_PROBE_RESULT_MALFORMED ||
               category == RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH) {
        ++probe->diagnostics.controls_malformed;
    } else {
        ++probe->diagnostics.controls_transport_error;
    }
}

static bool observation_strict_valid(const RSSDDCProbeObservation *observation) {
    return observation->protocol_valid && observation->semantic_request_match;
}

static void set_extended_interpretation(RSSDDCProbeExtendedObservation *extended) {
    const RSSDDCProbeObservation *observation = &extended->observation;
    if (!observation_strict_valid(observation)) {
        extended->interpretation = RSS_DDC_PROBE_INTERPRETATION_UNKNOWN;
        return;
    }
    if (observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_YES) {
        extended->interpretation = RSS_DDC_PROBE_INTERPRETATION_OBSERVED_ADVERTISED;
    } else if (observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_NO) {
        extended->interpretation = RSS_DDC_PROBE_INTERPRETATION_OBSERVED_UNADVERTISED;
    } else {
        extended->interpretation = RSS_DDC_PROBE_INTERPRETATION_OBSERVED_PROTOCOL_VALID;
    }
}

static void set_enum_correlation(const RSSDDCProbe *probe, RSSDDCProbeExtendedObservation *extended) {
    extended->enum_list_present = false;
    extended->current_in_declared_enum = false;
    if (probe->mccs == NULL) {
        return;
    }
    const uint8_t *values = NULL;
    size_t count = 0;
    if (rss_ddc_mccs_capabilities_enum_values(probe->mccs, extended->observation.requested_vcp, &values,
                                              &count) != RSS_DDC_OK || count == 0) {
        return;
    }
    extended->enum_list_present = true;
    for (size_t index = 0; index < count; ++index) {
        if (values[index] == (uint8_t)extended->observation.current_value) {
            extended->current_in_declared_enum = true;
            return;
        }
    }
}

static void count_extended_result(RSSDDCProbe *probe, const RSSDDCProbeObservation *observation) {
    switch (observation->category) {
    case RSS_DDC_PROBE_RESULT_STABLE:
    case RSS_DDC_PROBE_RESULT_VARIABLE:
        break;
    case RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED:
        ++probe->extended_diagnostics.protocol_reported;
        break;
    case RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH:
        ++probe->extended_diagnostics.semantic_mismatch;
        break;
    case RSS_DDC_PROBE_RESULT_MALFORMED:
        ++probe->extended_diagnostics.malformed;
        break;
    case RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR:
        ++probe->extended_diagnostics.transport_errors;
        break;
    case RSS_DDC_PROBE_RESULT_UNATTEMPTED:
        break;
    }
    if (observation_strict_valid(observation)) {
        ++probe->extended_diagnostics.strict_valid;
        if (observation->category == RSS_DDC_PROBE_RESULT_STABLE) {
            ++probe->extended_diagnostics.stable_valid;
        } else if (observation->category == RSS_DDC_PROBE_RESULT_VARIABLE) {
            ++probe->extended_diagnostics.variable_valid;
        }
        if (observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_YES) {
            ++probe->extended_diagnostics.advertised_valid;
        } else if (observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_NO) {
            ++probe->extended_diagnostics.unadvertised_valid;
        }
    }
}

static RSSDDCError load_mccs(RSSDDCProbe *probe) {
    probe->diagnostics.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    probe->diagnostics.mccs_available = false;
    probe->extended_diagnostics.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    probe->extended_diagnostics.mccs_available = false;
    if ((probe->target.display.capabilities & RSS_DDC_CAP_MCCS_CAPABILITIES) == 0 ||
        probe->transport.get_mccs_capabilities == NULL) {
        return RSS_DDC_OK;
    }
    probe->mccs = probe_calloc(1, sizeof(*probe->mccs));
    if (probe->mccs == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    RSSDDCError error = probe->transport.get_mccs_capabilities(probe->transport.context, probe->mccs);
    probe->diagnostics.mccs_error = error;
    probe->extended_diagnostics.mccs_error = error;
    if (error == RSS_DDC_OK) {
        probe->diagnostics.mccs_available = true;
        probe->extended_diagnostics.mccs_available = true;
        return RSS_DDC_OK;
    }
    free(probe->mccs);
    probe->mccs = NULL;
    return RSS_DDC_OK;
}

static RSSDDCError add_knowledge_fact(RSSDDCMonitorKnowledge *knowledge,
                                      const RSSDDCProbeObservation *observation,
                                      const char *semantic_id, bool declared) {
    RSSDDCKnowledgeRoute *route = probe_calloc(1, sizeof(*route));
    if (route == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    (void)snprintf(route->semantic_id, sizeof(route->semantic_id), "%s", semantic_id);
    (void)snprintf(route->route_id, sizeof(route->route_id), "mccs-vcp-%02x", observation->requested_vcp);
    route->kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP;
    route->address = observation->requested_vcp;
    (void)snprintf(route->transport_family, sizeof(route->transport_family), "%s", "mccs-vcp");
    (void)snprintf(route->command_semantics, sizeof(route->command_semantics), "%s",
                   declared ? "monitor-declared-mccs" : "read-only-get-vcp");
    route->provenance.source = RSS_DDC_PROFILE_SOURCE_RESEARCH;
    route->provenance.confidence = RSS_DDC_PROFILE_CONFIDENCE_OBSERVED;
    route->provenance.fact_kind = declared ? RSS_DDC_KNOWLEDGE_FACT_DECLARED : RSS_DDC_KNOWLEDGE_FACT_OBSERVED;
    (void)snprintf(route->provenance.source_id, sizeof(route->provenance.source_id), "%s",
                   declared ? "mccs-capabilities" : "alien-probe-live-read");
    (void)snprintf(route->provenance.evidence_id, sizeof(route->provenance.evidence_id), "%s",
                   declared ? "mccs-advertised" : observation->stable ? "stable-get" : "variable-get");
    if (declared) {
        route->value.state = RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN;
    } else {
        route->readable = true;
        route->value.state = RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED;
        route->value.unsigned_value = observation->current_value;
        route->reported_maximum_present = true;
        route->reported_maximum = observation->maximum_value;
    }
    route->writable = false;
    route->write_authorized = false;
    RSSDDCError error = rss_ddc_monitor_knowledge_add_route(knowledge, route);
    free(route);
    return error;
}

static RSSDDCError build_knowledge_from_observations(RSSDDCProbe *probe, const RSSDDCProbeObservation *observations,
                                                     size_t count,
                                                     const char *const *semantic_ids) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    if (knowledge == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    for (size_t index = 0; index < count; ++index) {
        const RSSDDCProbeObservation *observation = &observations[index];
        RSSDDCError error = RSS_DDC_OK;
        if (observation_strict_valid(observation)) {
            error = add_knowledge_fact(knowledge, observation, semantic_ids[index], false);
        }
        if (error == RSS_DDC_OK && observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_YES) {
            error = add_knowledge_fact(knowledge, observation, semantic_ids[index], true);
        }
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(knowledge);
            return error;
        }
    }
    probe->knowledge = knowledge;
    return RSS_DDC_OK;
}

static RSSDDCError build_extended_knowledge(RSSDDCProbe *probe) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    if (knowledge == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    for (size_t index = 0; index < RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT; ++index) {
        const RSSDDCProbeExtendedObservation *extended = &probe->extended_observations[index];
        const RSSDDCProbeObservation *observation = &extended->observation;
        RSSDDCError error = RSS_DDC_OK;
        if (observation_strict_valid(observation)) {
            error = add_knowledge_fact(knowledge, observation, extended->observation.semantic_id, false);
        }
        if (error == RSS_DDC_OK && observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_YES) {
            error = add_knowledge_fact(knowledge, observation, extended->observation.semantic_id, true);
        }
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(knowledge);
            return error;
        }
    }
    probe->knowledge = knowledge;
    return RSS_DDC_OK;
}

static bool extended_provider_supported(RSSDDCProvider provider) {
    return provider == RSS_DDC_PROVIDER_DCPDP13 || provider == RSS_DDC_PROVIDER_DCPDP_SERVICE ||
           provider == RSS_DDC_PROVIDER_PS190;
}

RSSDDCError rss_ddc_probe_create(const RSSDDCProbeTarget *target, const RSSDDCProbeReadTransport *transport,
                                 RSSDDCProbe **probe) {
    if (target == NULL || transport == NULL || transport->get_vcp == NULL || probe == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    *probe = NULL;
    if (target->correlation != RSS_DDC_PROBE_CORRELATION_EXACT) {
        return RSS_DDC_ERROR_DISCOVERY;
    }
    RSSDDCProbe *result = probe_calloc(1, sizeof(*result));
    if (result == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    result->target = *target;
    result->transport = *transport;
    result->diagnostics.display = target->display;
    result->diagnostics.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    result->diagnostics.observation_count = RSS_DDC_PROBE_QUICK_CONTROL_COUNT;
    result->diagnostics.observations = result->observations;
    result->extended_diagnostics.display = target->display;
    result->extended_diagnostics.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    *probe = result;
    return RSS_DDC_OK;
}

void rss_ddc_probe_destroy(RSSDDCProbe *probe) {
    if (probe == NULL) {
        return;
    }
    rss_ddc_monitor_knowledge_destroy(probe->knowledge);
    free(probe->mccs);
    free(probe->extended_observations);
    if (probe->owns_transport_context) {
        free(probe->transport.context);
    }
    free(probe);
}

RSSDDCError rss_ddc_probe_quick(RSSDDCProbe *probe) {
    if (probe == NULL || probe->transport.get_vcp == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    rss_ddc_monitor_knowledge_destroy(probe->knowledge);
    probe->knowledge = NULL;
    free(probe->mccs);
    probe->mccs = NULL;
    memset(probe->observations, 0, sizeof(probe->observations));
    probe->diagnostics.controls_attempted = 0;
    probe->diagnostics.controls_protocol_valid = 0;
    probe->diagnostics.controls_stable = 0;
    probe->diagnostics.controls_variable = 0;
    probe->diagnostics.controls_protocol_reported = 0;
    probe->diagnostics.controls_malformed = 0;
    probe->diagnostics.controls_transport_error = 0;
    probe->diagnostics.mccs_available = false;
    probe->diagnostics.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;

    RSSDDCError error = load_mccs(probe);
    if (error != RSS_DDC_OK) {
        return error;
    }

    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        const RSSDDCQuickControl *control = &quick_controls[index];
        RSSDDCProbeObservation *observation = &probe->observations[index];
        ++probe->diagnostics.controls_attempted;
        observe_vcp_pair(&probe->transport, control->vcp, RSS_DDC_PROBE_QUICK_REPEAT_DELAY_MS, observation);
        observation->semantic_id = control->semantic_id;
        observation->requested_vcp = control->vcp;
        observation->advertised =
            advertised_state(probe, probe->diagnostics.mccs_available, control->vcp);
        observation->profile_known = profile_knows_quick(probe, control);
        if (observation->first_error != RSS_DDC_OK) {
            count_quick_first_failure(probe, observation->category);
            continue;
        }
        if (!observation->semantic_request_match) {
            ++probe->diagnostics.controls_malformed;
            continue;
        }
        ++probe->diagnostics.controls_protocol_valid;
        if (observation->stable) {
            ++probe->diagnostics.controls_stable;
        } else {
            ++probe->diagnostics.controls_variable;
        }
    }

    const char *semantic_ids[RSS_DDC_PROBE_QUICK_CONTROL_COUNT];
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        semantic_ids[index] = probe->observations[index].semantic_id;
    }
    return build_knowledge_from_observations(probe, probe->observations, RSS_DDC_PROBE_QUICK_CONTROL_COUNT,
                                             semantic_ids);
}

RSSDDCError rss_ddc_probe_extended(RSSDDCProbe *probe) {
    if (probe == NULL || probe->transport.get_vcp == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (!extended_provider_supported(probe->target.display.provider)) {
        return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    }

    uint64_t started = probe_monotonic_milliseconds();
    rss_ddc_monitor_knowledge_destroy(probe->knowledge);
    probe->knowledge = NULL;
    free(probe->mccs);
    probe->mccs = NULL;
    free(probe->extended_observations);
    probe->extended_observations = probe_calloc(RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT,
                                                sizeof(*probe->extended_observations));
    if (probe->extended_observations == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }

    memset(&probe->extended_diagnostics, 0, sizeof(probe->extended_diagnostics));
    probe->extended_diagnostics.display = probe->target.display;
    probe->extended_diagnostics.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    probe->extended_diagnostics.requested = RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT;
    probe->extended_diagnostics.observation_count = RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT;
    probe->extended_diagnostics.observations = probe->extended_observations;

    RSSDDCError error = load_mccs(probe);
    if (error != RSS_DDC_OK) {
        return error;
    }

    size_t consecutive_transport_failures = 0;
    for (uint16_t vcp = 0; vcp < RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT; ++vcp) {
        if (probe->extended_diagnostics.aborted) {
            RSSDDCProbeExtendedObservation *extended = &probe->extended_observations[vcp];
            extended->observation.requested_vcp = (uint8_t)vcp;
            extended->observation.category = RSS_DDC_PROBE_RESULT_UNATTEMPTED;
            assign_semantic_id(extended->semantic_id_buffer, sizeof(extended->semantic_id_buffer), (uint8_t)vcp,
                               &extended->observation.semantic_id);
            continue;
        }
        if (vcp > 0) {
            probe_delay(&probe->transport, RSS_DDC_PROBE_EXTENDED_INTER_ADDRESS_DELAY_MS);
        }
        RSSDDCProbeExtendedObservation *extended = &probe->extended_observations[vcp];
        ++probe->extended_diagnostics.attempted;
        observe_vcp_pair(&probe->transport, (uint8_t)vcp, RSS_DDC_PROBE_EXTENDED_REPEAT_DELAY_MS,
                         &extended->observation);
        assign_semantic_id(extended->semantic_id_buffer, sizeof(extended->semantic_id_buffer), (uint8_t)vcp,
                           &extended->observation.semantic_id);
        extended->observation.requested_vcp = (uint8_t)vcp;
        extended->observation.advertised =
            advertised_state(probe, probe->extended_diagnostics.mccs_available, (uint8_t)vcp);
        extended->observation.profile_known = profile_knows_vcp(probe, (uint8_t)vcp);
        if (transport_failure(extended->observation.first_error)) {
            ++consecutive_transport_failures;
            if (consecutive_transport_failures >= RSS_DDC_PROBE_EXTENDED_TRANSPORT_FAILURE_LIMIT) {
                probe->extended_diagnostics.aborted = true;
            }
        } else {
            consecutive_transport_failures = 0;
        }
        set_extended_interpretation(extended);
        set_enum_correlation(probe, extended);
        count_extended_result(probe, &extended->observation);
    }

    error = build_extended_knowledge(probe);
    probe->extended_diagnostics.duration_ms = probe_monotonic_milliseconds() - started;
    return error;
}

RSSDDCError rss_ddc_probe_knowledge(const RSSDDCProbe *probe, const RSSDDCMonitorKnowledge **knowledge) {
    if (probe == NULL || knowledge == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (probe->knowledge == NULL) {
        return RSS_DDC_ERROR_NOT_FOUND;
    }
    *knowledge = probe->knowledge;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_probe_diagnostics(const RSSDDCProbe *probe, RSSDDCProbeDiagnostics *diagnostics) {
    if (probe == NULL || diagnostics == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    *diagnostics = probe->diagnostics;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_probe_extended_diagnostics(const RSSDDCProbe *probe,
                                               RSSDDCProbeExtendedDiagnostics *diagnostics) {
    if (probe == NULL || diagnostics == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    *diagnostics = probe->extended_diagnostics;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_probe_mccs_capabilities(const RSSDDCProbe *probe,
                                            const RSSDDCMCCSCapabilities **capabilities) {
    if (probe == NULL || capabilities == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (probe->mccs == NULL) {
        return RSS_DDC_ERROR_NOT_FOUND;
    }
    *capabilities = probe->mccs;
    return RSS_DDC_OK;
}

static RSSDDCError system_get_vcp(void *context, uint8_t vcp, RSSDDCVCPResult *result) {
    return rss_ddc_get_vcp(*(const uint32_t *)context, vcp, result);
}

static RSSDDCError system_get_mccs(void *context, RSSDDCMCCSCapabilities *capabilities) {
    return rss_ddc_get_mccs_capabilities(*(const uint32_t *)context, capabilities);
}

static void system_delay(void *context, uint32_t milliseconds) {
    (void)context;
    if (milliseconds > 0) {
        usleep((useconds_t)milliseconds * 1000u);
    }
}

static RSSDDCError probe_for_display(uint32_t list_index, bool extended, RSSDDCProbe **probe) {
    if (probe == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    *probe = NULL;
    RSSDDCDisplay *display = probe_calloc(1, sizeof(*display));
    RSSDDCProbeTarget *target = probe_calloc(1, sizeof(*target));
    uint32_t *context = probe_calloc(1, sizeof(*context));
    if (display == NULL || target == NULL || context == NULL) {
        free(context);
        free(target);
        free(display);
        return RSS_DDC_ERROR_SYSTEM;
    }
    RSSDDCError error = rss_ddc_get_display(list_index, display);
    if (error == RSS_DDC_OK) {
        target->display = *display;
        target->correlation = RSS_DDC_PROBE_CORRELATION_EXACT;
        *context = list_index;
        RSSDDCProbeReadTransport transport = {.context = context,
                                               .get_vcp = system_get_vcp,
                                               .get_mccs_capabilities = system_get_mccs,
                                               .delay = extended ? system_delay : NULL};
        error = rss_ddc_probe_create(target, &transport, probe);
        if (error == RSS_DDC_OK) {
            (*probe)->transport.context = context;
            (*probe)->owns_transport_context = true;
            context = NULL;
            error = extended ? rss_ddc_probe_extended(*probe) : rss_ddc_probe_quick(*probe);
            if (error != RSS_DDC_OK) {
                rss_ddc_probe_destroy(*probe);
                *probe = NULL;
            }
        }
    }
    free(context);
    free(target);
    free(display);
    return error;
}

RSSDDCError rss_ddc_probe_quick_for_display(uint32_t list_index, RSSDDCProbe **probe) {
    return probe_for_display(list_index, false, probe);
}

RSSDDCError rss_ddc_probe_extended_for_display(uint32_t list_index, RSSDDCProbe **probe) {
    return probe_for_display(list_index, true, probe);
}

const char *rss_ddc_probe_result_category_name(RSSDDCProbeResultCategory category) {
    switch (category) {
    case RSS_DDC_PROBE_RESULT_STABLE: return "stable";
    case RSS_DDC_PROBE_RESULT_VARIABLE: return "variable";
    case RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED: return "protocol-reported";
    case RSS_DDC_PROBE_RESULT_MALFORMED: return "malformed";
    case RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH: return "semantic-mismatch";
    case RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR: return "transport-error";
    case RSS_DDC_PROBE_RESULT_UNATTEMPTED: return "unattempted";
    }
    return "unknown";
}

const char *rss_ddc_probe_interpretation_name(RSSDDCProbeInterpretationConfidence interpretation) {
    switch (interpretation) {
    case RSS_DDC_PROBE_INTERPRETATION_OBSERVED_PROTOCOL_VALID: return "observed-protocol-valid";
    case RSS_DDC_PROBE_INTERPRETATION_OBSERVED_ADVERTISED: return "observed-advertised";
    case RSS_DDC_PROBE_INTERPRETATION_OBSERVED_UNADVERTISED: return "observed-unadvertised";
    case RSS_DDC_PROBE_INTERPRETATION_UNKNOWN: return "unknown";
    }
    return "unknown";
}

const char *rss_ddc_probe_repeat_error_name(const RSSDDCProbeObservation *observation) {
    if (observation == NULL || !observation->repeat_attempted) {
        return "not-attempted";
    }
    return rss_ddc_error_string(observation->repeat_error);
}
