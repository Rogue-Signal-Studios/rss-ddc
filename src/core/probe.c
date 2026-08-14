#include "rss_ddc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    RSSDDCProbeDiagnostics diagnostics;
    bool owns_transport_context;
};

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

static RSSDDCProbeKnowledgeState profile_knows(const RSSDDCProbe *probe,
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

static bool mccs_advertises(const RSSDDCProbe *probe, uint8_t vcp) {
    return probe->mccs != NULL && rss_ddc_mccs_capabilities_has_vcp(probe->mccs, vcp);
}

static void count_first_failure(RSSDDCProbe *probe, RSSDDCProbeResultCategory category) {
    if (category == RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED) {
        ++probe->diagnostics.controls_protocol_reported;
    } else if (category == RSS_DDC_PROBE_RESULT_MALFORMED ||
               category == RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH) {
        ++probe->diagnostics.controls_malformed;
    } else {
        ++probe->diagnostics.controls_transport_error;
    }
}

static RSSDDCError add_knowledge_fact(RSSDDCMonitorKnowledge *knowledge,
                                      const RSSDDCProbeObservation *observation,
                                      bool declared) {
    RSSDDCKnowledgeRoute *route = probe_calloc(1, sizeof(*route));
    if (route == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    (void)snprintf(route->semantic_id, sizeof(route->semantic_id), "%s", observation->semantic_id);
    (void)snprintf(route->route_id, sizeof(route->route_id), "mccs-vcp-%02x", observation->requested_vcp);
    route->kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP;
    route->address = observation->requested_vcp;
    (void)snprintf(route->transport_family, sizeof(route->transport_family), "%s", "mccs-vcp");
    (void)snprintf(route->command_semantics, sizeof(route->command_semantics), "%s",
                   declared ? "monitor-declared-mccs" : "read-only-get-vcp");
    route->provenance.source = RSS_DDC_PROFILE_SOURCE_RESEARCH;
    route->provenance.confidence = declared ? RSS_DDC_PROFILE_CONFIDENCE_OBSERVED
                                            : RSS_DDC_PROFILE_CONFIDENCE_OBSERVED;
    route->provenance.fact_kind = declared ? RSS_DDC_KNOWLEDGE_FACT_DECLARED
                                           : RSS_DDC_KNOWLEDGE_FACT_OBSERVED;
    (void)snprintf(route->provenance.source_id, sizeof(route->provenance.source_id), "%s",
                   declared ? "mccs-capabilities" : "alien-probe-live-read");
    (void)snprintf(route->provenance.evidence_id, sizeof(route->provenance.evidence_id), "%s",
                   declared ? "mccs-advertised" :
                   observation->stable ? "stable-get" : "variable-get");
    if (declared) {
        route->value.state = RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN;
    } else {
        route->readable = true;
        route->value.state = RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED;
        route->value.unsigned_value = observation->current_value;
        route->reported_maximum_present = true;
        route->reported_maximum = observation->maximum_value;
    }
    /* A live read or MCCS declaration never grants a write route. */
    route->writable = false;
    route->write_authorized = false;
    RSSDDCError error = rss_ddc_monitor_knowledge_add_route(knowledge, route);
    free(route);
    return error;
}

static RSSDDCError build_knowledge(RSSDDCProbe *probe) {
    RSSDDCMonitorKnowledge *knowledge = rss_ddc_monitor_knowledge_create();
    if (knowledge == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        const RSSDDCProbeObservation *observation = &probe->observations[index];
        RSSDDCError error = RSS_DDC_OK;
        if (observation->protocol_valid && observation->semantic_request_match) {
            error = add_knowledge_fact(knowledge, observation, false);
        }
        if (error == RSS_DDC_OK && observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_YES) {
            error = add_knowledge_fact(knowledge, observation, true);
        }
        if (error != RSS_DDC_OK) {
            rss_ddc_monitor_knowledge_destroy(knowledge);
            return error;
        }
    }
    probe->knowledge = knowledge;
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_probe_create(const RSSDDCProbeTarget *target,
                                 const RSSDDCProbeReadTransport *transport,
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
    *probe = result;
    return RSS_DDC_OK;
}

void rss_ddc_probe_destroy(RSSDDCProbe *probe) {
    if (probe == NULL) {
        return;
    }
    rss_ddc_monitor_knowledge_destroy(probe->knowledge);
    free(probe->mccs);
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

    if ((probe->target.display.capabilities & RSS_DDC_CAP_MCCS_CAPABILITIES) != 0 &&
        probe->transport.get_mccs_capabilities != NULL) {
        probe->mccs = probe_calloc(1, sizeof(*probe->mccs));
        if (probe->mccs == NULL) {
            return RSS_DDC_ERROR_SYSTEM;
        }
        probe->diagnostics.mccs_error = probe->transport.get_mccs_capabilities(probe->transport.context,
                                                                                 probe->mccs);
        if (probe->diagnostics.mccs_error == RSS_DDC_OK) {
            probe->diagnostics.mccs_available = true;
        } else {
            free(probe->mccs);
            probe->mccs = NULL;
        }
    }

    for (size_t index = 0; index < RSS_DDC_PROBE_QUICK_CONTROL_COUNT; ++index) {
        const RSSDDCQuickControl *control = &quick_controls[index];
        RSSDDCProbeObservation *observation = &probe->observations[index];
        observation->first_error = RSS_DDC_ERROR_NOT_FOUND;
        observation->repeat_error = RSS_DDC_ERROR_NOT_FOUND;
        observation->semantic_id = control->semantic_id;
        observation->requested_vcp = control->vcp;
        observation->advertised = probe->diagnostics.mccs_available
                                      ? (mccs_advertises(probe, control->vcp) ? RSS_DDC_PROBE_KNOWLEDGE_YES
                                                                               : RSS_DDC_PROBE_KNOWLEDGE_NO)
                                      : RSS_DDC_PROBE_KNOWLEDGE_UNKNOWN;
        observation->profile_known = profile_knows(probe, control);
        ++probe->diagnostics.controls_attempted;

        RSSDDCVCPResult first = {0};
        observation->first_error = probe->transport.get_vcp(probe->transport.context, control->vcp, &first);
        observation->transport = transport_for_error(observation->first_error);
        if (observation->first_error != RSS_DDC_OK) {
            observation->category = category_for_error(observation->first_error);
            count_first_failure(probe, observation->category);
            continue;
        }
        if (first.vcp_code != control->vcp) {
            observation->category = RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH;
            observation->transport = RSS_DDC_PROBE_TRANSPORT_SUCCEEDED;
            observation->semantic_request_match = false;
            ++probe->diagnostics.controls_malformed;
            continue;
        }
        observation->protocol_valid = true;
        observation->semantic_request_match = true;
        observation->current_value = first.current_value;
        observation->maximum_value = first.maximum_value;
        observation->current_exceeds_maximum = first.current_value > first.maximum_value;
        ++probe->diagnostics.controls_protocol_valid;

        RSSDDCVCPResult repeat = {0};
        observation->repeat_error = probe->transport.get_vcp(probe->transport.context, control->vcp, &repeat);
        observation->stable = observation->repeat_error == RSS_DDC_OK && repeat.vcp_code == control->vcp &&
                              repeat.current_value == first.current_value && repeat.maximum_value == first.maximum_value;
        if (observation->stable) {
            observation->category = RSS_DDC_PROBE_RESULT_STABLE;
            ++probe->diagnostics.controls_stable;
        } else {
            observation->category = RSS_DDC_PROBE_RESULT_VARIABLE;
            ++probe->diagnostics.controls_variable;
        }
    }
    return build_knowledge(probe);
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

RSSDDCError rss_ddc_probe_quick_for_display(uint32_t list_index, RSSDDCProbe **probe) {
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
        RSSDDCProbeReadTransport transport = {.context = context, .get_vcp = system_get_vcp,
                                               .get_mccs_capabilities = system_get_mccs};
        error = rss_ddc_probe_create(target, &transport, probe);
        if (error == RSS_DDC_OK) {
            /* Probe owns the context for its public-read callbacks. */
            (*probe)->transport.context = context;
            (*probe)->owns_transport_context = true;
            context = NULL;
            error = rss_ddc_probe_quick(*probe);
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
