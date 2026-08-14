#include "render.h"

#include <stdio.h>
#include <string.h>

#include "color.h"
#include "plain.h"
#include "table.h"

static const char *probe_knowledge_state_name(RSSDDCProbeKnowledgeState state) {
    if (state == RSS_DDC_PROBE_KNOWLEDGE_YES) {
        return "yes";
    }
    if (state == RSS_DDC_PROBE_KNOWLEDGE_NO) {
        return "no";
    }
    return "unknown";
}

static void format_probe_notes(char *buffer, size_t capacity, const RSSDDCProbeObservation *observation,
                               const RSSDDCProbeExtendedObservation *extended) {
    char *cursor = buffer;
    size_t remaining = capacity;
    buffer[0] = '\0';
    int written = 0;
    if (observation->transport != RSS_DDC_PROBE_TRANSPORT_SUCCEEDED) {
        written = snprintf(cursor, remaining, "transport-failed");
        if (written < 0 || (size_t)written >= remaining) {
            return;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (!observation->protocol_valid) {
        written = snprintf(cursor, remaining, "%sfirst=%s", remaining == capacity ? "" : "; ",
                             rss_ddc_error_string(observation->first_error));
        if (written < 0 || (size_t)written >= remaining) {
            return;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (strcmp(rss_ddc_probe_repeat_error_name(observation), "not-attempted") == 0) {
        written = snprintf(cursor, remaining, "%srepeat=not-attempted", remaining == capacity ? "" : "; ");
        if (written < 0 || (size_t)written >= remaining) {
            return;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (observation->current_exceeds_maximum) {
        written = snprintf(cursor, remaining, "%scur>max", remaining == capacity ? "" : "; ");
        if (written < 0 || (size_t)written >= remaining) {
            return;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (extended != NULL && extended->enum_list_present) {
        written = snprintf(cursor, remaining, "%senum=%s", remaining == capacity ? "" : "; ",
                           extended->current_in_declared_enum ? "yes" : "no");
        if (written < 0 || (size_t)written >= remaining) {
            return;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (extended != NULL &&
        extended->interpretation == RSS_DDC_PROBE_INTERPRETATION_OBSERVED_UNADVERTISED) {
        snprintf(cursor, remaining, "%sunadvertised", remaining == capacity ? "" : "; ");
    }
}

void rss_ddc_cli_render_display_list(FILE *stream, const RSSDDCDisplay *displays, size_t count,
                                     const RSSDDCCliEffectiveOutput *output) {
    if (stream == NULL || displays == NULL || output == NULL) {
        return;
    }
    if (!output->table) {
        for (size_t index = 0; index < count; ++index) {
            rss_ddc_cli_plain_print_display(stream, &displays[index]);
        }
        return;
    }
    static const char *headers[] = {"IDX", "DISPLAY", "PROVIDER", "TRANSPORT", "CG", "STATUS"};
    RSSDDCCliTable table = {};
    if (!rss_ddc_cli_table_init(&table, headers, sizeof(headers) / sizeof(headers[0]))) {
        return;
    }
    char index_text[16];
    char cg_text[16];
    for (size_t index = 0; index < count; ++index) {
        snprintf(index_text, sizeof(index_text), "%u", displays[index].list_index);
        snprintf(cg_text, sizeof(cg_text), "%u", displays[index].cg_display_id);
        const char *cells[] = {index_text, displays[index].product_name, rss_ddc_provider_string(displays[index].provider),
                               displays[index].transport, cg_text, displays[index].online ? "online" : "offline"};
        rss_ddc_cli_table_add_row(&table, cells, sizeof(cells) / sizeof(cells[0]));
    }
    rss_ddc_cli_table_render(stream, &table, output);
}

static void render_probe_summary_quick(FILE *stream, const RSSDDCProbeDiagnostics *diagnostics) {
    fprintf(stream, "display=%u provider=%s transport=%s mccs=%s\n", diagnostics->display.list_index,
            rss_ddc_provider_string(diagnostics->display.provider), diagnostics->display.transport,
            diagnostics->mccs_available ? "available" : rss_ddc_error_string(diagnostics->mccs_error));
    fprintf(stream, "%zu controls | %zu protocol-valid | %zu stable | %zu variable | %zu protocol-reported\n",
            diagnostics->controls_attempted, diagnostics->controls_protocol_valid, diagnostics->controls_stable,
            diagnostics->controls_variable, diagnostics->controls_protocol_reported);
}

static void render_probe_table_row(const RSSDDCCliEffectiveOutput *output, const RSSDDCProbeObservation *observation,
                                   const RSSDDCProbeExtendedObservation *extended, RSSDDCCliTable *table) {
    char vcp_text[16];
    char current_text[16];
    char max_text[16];
    char state_text[96];
    char adv_text[96];
    char notes[128];
    snprintf(vcp_text, sizeof(vcp_text), "0x%02x", observation->requested_vcp);
    if (observation->protocol_valid) {
        snprintf(current_text, sizeof(current_text), "%u", observation->current_value);
        snprintf(max_text, sizeof(max_text), "%u", observation->maximum_value);
    } else {
        strcpy(current_text, "-");
        strcpy(max_text, "-");
    }
    format_probe_notes(notes, sizeof(notes), observation, extended);
    const RSSDDCProbeInterpretationConfidence interpretation =
        extended != NULL ? extended->interpretation : RSS_DDC_PROBE_INTERPRETATION_UNKNOWN;
    rss_ddc_cli_color_format(state_text, sizeof(state_text), output,
                             rss_ddc_cli_probe_observation_color(observation, interpretation),
                             rss_ddc_probe_result_category_name(observation->category));
    if (observation->advertised == RSS_DDC_PROBE_KNOWLEDGE_YES) {
        rss_ddc_cli_color_format(adv_text, sizeof(adv_text), output, RSS_DDC_CLI_COLOR_CYAN, "yes");
    } else {
        snprintf(adv_text, sizeof(adv_text), "%s", probe_knowledge_state_name(observation->advertised));
    }
    const char *cells[] = {vcp_text, observation->semantic_id, state_text, adv_text, current_text, max_text, notes};
    rss_ddc_cli_table_add_row(table, cells, 7);
}

void rss_ddc_cli_render_probe_quick(FILE *stream, const RSSDDCProbeDiagnostics *diagnostics,
                                    const RSSDDCCliEffectiveOutput *output) {
    if (stream == NULL || diagnostics == NULL || output == NULL) {
        return;
    }
    if (!output->table) {
        rss_ddc_cli_plain_print_probe_quick(stream, diagnostics);
        return;
    }
    fprintf(stream, "Alien Probe Quick: READ-ONLY; writes=0; repeats=%u; repeat-delay-ms=%u\n",
            RSS_DDC_PROBE_QUICK_REPEAT_COUNT, RSS_DDC_PROBE_QUICK_REPEAT_DELAY_MS);
    render_probe_summary_quick(stream, diagnostics);
    static const char *headers[] = {"VCP", "SEMANTIC", "STATE", "ADV", "CURRENT", "MAX", "NOTES"};
    RSSDDCCliTable table = {};
    rss_ddc_cli_table_init(&table, headers, sizeof(headers) / sizeof(headers[0]));
    for (size_t index = 0; index < diagnostics->observation_count; ++index) {
        render_probe_table_row(output, &diagnostics->observations[index], NULL, &table);
    }
    rss_ddc_cli_table_render(stream, &table, output);
}

static void render_probe_summary_extended(FILE *stream, const RSSDDCProbeExtendedDiagnostics *diagnostics) {
    fprintf(stream,
            "%zu requested | %zu strict-valid | %zu stable | %zu variable | %zu protocol-reported | %zu malformed | "
            "%zu transport-errors",
            diagnostics->requested, diagnostics->strict_valid, diagnostics->stable_valid, diagnostics->variable_valid,
            diagnostics->protocol_reported, diagnostics->malformed, diagnostics->transport_errors);
    if (diagnostics->advertised_valid > 0 || diagnostics->unadvertised_valid > 0) {
        fprintf(stream, " | %zu advertised-valid | %zu unadvertised-valid", diagnostics->advertised_valid,
                diagnostics->unadvertised_valid);
    }
    fprintf(stream, "\n");
}

void rss_ddc_cli_render_probe_extended(FILE *stream, const RSSDDCProbeExtendedDiagnostics *diagnostics,
                                       const RSSDDCCliEffectiveOutput *output) {
    if (stream == NULL || diagnostics == NULL || output == NULL) {
        return;
    }
    if (!output->table) {
        rss_ddc_cli_plain_print_probe_extended(stream, diagnostics);
        return;
    }
    fprintf(stream,
            "Alien Probe Extended: READ-ONLY; writes=0; repeats=%u; inter-address-delay-ms=%u; repeat-delay-ms=%u\n",
            RSS_DDC_PROBE_EXTENDED_REPEAT_COUNT, RSS_DDC_PROBE_EXTENDED_INTER_ADDRESS_DELAY_MS,
            RSS_DDC_PROBE_EXTENDED_REPEAT_DELAY_MS);
    fprintf(stream, "display=%u provider=%s transport=%s mccs=%s\n", diagnostics->display.list_index,
            rss_ddc_provider_string(diagnostics->display.provider), diagnostics->display.transport,
            diagnostics->mccs_available ? "available" : rss_ddc_error_string(diagnostics->mccs_error));
    render_probe_summary_extended(stream, diagnostics);
    static const char *headers[] = {"VCP", "SEMANTIC", "STATE", "ADV", "CURRENT", "MAX", "NOTES"};
    RSSDDCCliTable table = {};
    rss_ddc_cli_table_init(&table, headers, sizeof(headers) / sizeof(headers[0]));
    for (size_t index = 0; index < diagnostics->observation_count; ++index) {
        const RSSDDCProbeExtendedObservation *extended = &diagnostics->observations[index];
        if (extended->observation.category == RSS_DDC_PROBE_RESULT_UNATTEMPTED) {
            continue;
        }
        render_probe_table_row(output, &extended->observation, extended, &table);
    }
    rss_ddc_cli_table_render(stream, &table, output);
}
