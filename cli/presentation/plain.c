#include "plain.h"

static const char *probe_knowledge_state_name(RSSDDCProbeKnowledgeState state) {
    if (state == RSS_DDC_PROBE_KNOWLEDGE_YES) {
        return "yes";
    }
    if (state == RSS_DDC_PROBE_KNOWLEDGE_NO) {
        return "no";
    }
    return "unknown";
}

void rss_ddc_cli_plain_print_display(FILE *stream, const RSSDDCDisplay *display) {
    fprintf(stream, "%u  %s  provider=%s  capabilities=0x%02x  cg=%u\n", display->list_index, display->product_name,
            rss_ddc_provider_string(display->provider), display->capabilities, display->cg_display_id);
}

void rss_ddc_cli_plain_print_probe_quick(FILE *stream, const RSSDDCProbeDiagnostics *diagnostics) {
    fprintf(stream, "Alien Probe Quick: READ-ONLY; writes=0; repeats=%u; repeat-delay-ms=%u\n",
            RSS_DDC_PROBE_QUICK_REPEAT_COUNT, RSS_DDC_PROBE_QUICK_REPEAT_DELAY_MS);
    fprintf(stream, "display=%u provider=%s transport=%s mccs=%s\n", diagnostics->display.list_index,
            rss_ddc_provider_string(diagnostics->display.provider), diagnostics->display.transport,
            diagnostics->mccs_available ? "available" : rss_ddc_error_string(diagnostics->mccs_error));
    for (size_t index = 0; index < diagnostics->observation_count; ++index) {
        const RSSDDCProbeObservation *observation = &diagnostics->observations[index];
        fprintf(stream,
                "vcp=0x%02x semantic=%s category=%s transport=%s protocol-valid=%s request-match=%s advertised=%s "
                "profile-known=%s",
                observation->requested_vcp, observation->semantic_id,
                rss_ddc_probe_result_category_name(observation->category),
                observation->transport == RSS_DDC_PROBE_TRANSPORT_SUCCEEDED ? "succeeded" : "failed",
                observation->protocol_valid ? "yes" : "no", observation->semantic_request_match ? "yes" : "no",
                probe_knowledge_state_name(observation->advertised),
                probe_knowledge_state_name(observation->profile_known));
        if (observation->protocol_valid) {
            fprintf(stream, " current=%u maximum=%u stable=%s unusual-current-gt-max=%s", observation->current_value,
                    observation->maximum_value, observation->stable ? "yes" : "no",
                    observation->current_exceeds_maximum ? "yes" : "no");
        }
        fprintf(stream, " first=%s repeat=%s\n", rss_ddc_error_string(observation->first_error),
                rss_ddc_probe_repeat_error_name(observation));
    }
}

void rss_ddc_cli_plain_print_probe_extended(FILE *stream, const RSSDDCProbeExtendedDiagnostics *diagnostics) {
    fprintf(stream,
            "Alien Probe Extended: READ-ONLY; writes=0; repeats=%u; inter-address-delay-ms=%u; repeat-delay-ms=%u\n",
            RSS_DDC_PROBE_EXTENDED_REPEAT_COUNT, RSS_DDC_PROBE_EXTENDED_INTER_ADDRESS_DELAY_MS,
            RSS_DDC_PROBE_EXTENDED_REPEAT_DELAY_MS);
    fprintf(stream, "display=%u provider=%s transport=%s mccs=%s\n", diagnostics->display.list_index,
            rss_ddc_provider_string(diagnostics->display.provider), diagnostics->display.transport,
            diagnostics->mccs_available ? "available" : rss_ddc_error_string(diagnostics->mccs_error));
    fprintf(stream,
            "requested=%zu attempted=%zu strict-valid=%zu stable-valid=%zu variable-valid=%zu "
            "protocol-reported=%zu semantic-mismatch=%zu malformed=%zu transport-errors=%zu "
            "advertised-valid=%zu unadvertised-valid=%zu duration-ms=%llu aborted=%s\n",
            diagnostics->requested, diagnostics->attempted, diagnostics->strict_valid, diagnostics->stable_valid,
            diagnostics->variable_valid, diagnostics->protocol_reported, diagnostics->semantic_mismatch,
            diagnostics->malformed, diagnostics->transport_errors, diagnostics->advertised_valid,
            diagnostics->unadvertised_valid, (unsigned long long)diagnostics->duration_ms,
            diagnostics->aborted ? "yes" : "no");
    for (size_t index = 0; index < diagnostics->observation_count; ++index) {
        const RSSDDCProbeExtendedObservation *extended = &diagnostics->observations[index];
        const RSSDDCProbeObservation *observation = &extended->observation;
        if (observation->category == RSS_DDC_PROBE_RESULT_UNATTEMPTED) {
            continue;
        }
        fprintf(stream,
                "vcp=0x%02x semantic=%s category=%s interpretation=%s transport=%s protocol-valid=%s "
                "request-match=%s advertised=%s profile-known=%s enum-list=%s current-in-declared-enum=%s",
                observation->requested_vcp, observation->semantic_id,
                rss_ddc_probe_result_category_name(observation->category),
                rss_ddc_probe_interpretation_name(extended->interpretation),
                observation->transport == RSS_DDC_PROBE_TRANSPORT_SUCCEEDED ? "succeeded" : "failed",
                observation->protocol_valid ? "yes" : "no", observation->semantic_request_match ? "yes" : "no",
                probe_knowledge_state_name(observation->advertised),
                probe_knowledge_state_name(observation->profile_known),
                extended->enum_list_present ? "present" : "absent",
                extended->enum_list_present ? (extended->current_in_declared_enum ? "yes" : "no") : "unknown");
        if (observation->protocol_valid) {
            fprintf(stream, " current=%u maximum=%u stable=%s unusual-current-gt-max=%s", observation->current_value,
                    observation->maximum_value, observation->stable ? "yes" : "no",
                    observation->current_exceeds_maximum ? "yes" : "no");
        }
        fprintf(stream, " first=%s repeat=%s\n", rss_ddc_error_string(observation->first_error),
                rss_ddc_probe_repeat_error_name(observation));
    }
}
