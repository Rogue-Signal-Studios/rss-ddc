#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "color.h"
#include "table.h"

static const char *product_controls[] = {
    "display.brightness", "display.contrast", "display.color_preset", "display.picture_mode",
    "inputs.switching",
};

static const char *control_label(const char *semantic_id) {
    if (strcmp(semantic_id, "display.brightness") == 0) {
        return "Brightness";
    }
    if (strcmp(semantic_id, "display.contrast") == 0) {
        return "Contrast";
    }
    if (strcmp(semantic_id, "display.color_preset") == 0) {
        return "Color Preset";
    }
    if (strcmp(semantic_id, "display.picture_mode") == 0) {
        return "Picture Mode";
    }
    if (strcmp(semantic_id, "inputs.switching") == 0) {
        return "Input";
    }
    return semantic_id;
}

static bool is_vendor_unknown(const char *semantic_id) {
    return semantic_id != NULL && strncmp(semantic_id, "vendor.unknown.vcp.", 19) == 0;
}

static bool is_product_control(const char *semantic_id) {
    for (size_t index = 0; index < sizeof(product_controls) / sizeof(product_controls[0]); ++index) {
        if (strcmp(semantic_id, product_controls[index]) == 0) {
            return true;
        }
    }
    return false;
}

static const char *profile_status_name(RSSDDCCharacterizationProfileStatus status) {
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_MATCHED) {
        return "matched";
    }
    if (status == RSS_DDC_CHARACTERIZATION_PROFILE_CONFLICT) {
        return "conflict";
    }
    return "none";
}

static const char *sufficiency_name(RSSDDCCharacterizationSufficiency status) {
    if (status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT) {
        return "SUFFICIENT";
    }
    if (status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT) {
        return "INSUFFICIENT";
    }
    if (status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_CONFLICT) {
        return "CONFLICT";
    }
    return "UNAVAILABLE";
}

static RSSDDCCliColorRole sufficiency_color(RSSDDCCharacterizationSufficiency status) {
    if (status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT) {
        return RSS_DDC_CLI_COLOR_GREEN;
    }
    if (status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT) {
        return RSS_DDC_CLI_COLOR_YELLOW;
    }
    if (status == RSS_DDC_CHARACTERIZATION_SUFFICIENCY_CONFLICT) {
        return RSS_DDC_CLI_COLOR_RED;
    }
    return RSS_DDC_CLI_COLOR_DIM;
}

static void format_reasons(char *buffer, size_t capacity, uint32_t reasons) {
    buffer[0] = '\0';
    if (reasons == RSS_DDC_CHARACTERIZATION_REASON_NONE) {
        snprintf(buffer, capacity, "none");
        return;
    }
    static const struct {
        uint32_t bit;
        const char *name;
    } bits[] = {
        {RSS_DDC_CHARACTERIZATION_REASON_MISSING_CONTROL, "missing-control"},
        {RSS_DDC_CHARACTERIZATION_REASON_UNRESOLVED_METHOD, "unresolved-method"},
        {RSS_DDC_CHARACTERIZATION_REASON_CONFLICTING_METHOD, "conflicting-method"},
        {RSS_DDC_CHARACTERIZATION_REASON_VARIABLE_OBSERVATION, "variable-observation"},
        {RSS_DDC_CHARACTERIZATION_REASON_NO_GET_SUPPORT, "no-get-support"},
        {RSS_DDC_CHARACTERIZATION_REASON_PROFILE_CONFLICT, "profile-conflict"},
        {RSS_DDC_CHARACTERIZATION_REASON_PROBE_HELPFUL, "probe-helpful"},
    };
    char *cursor = buffer;
    size_t remaining = capacity;
    for (size_t index = 0; index < sizeof(bits) / sizeof(bits[0]); ++index) {
        if ((reasons & bits[index].bit) == 0) {
            continue;
        }
        int written = snprintf(cursor, remaining, "%s%s", cursor == buffer ? "" : ",", bits[index].name);
        if (written < 0 || (size_t)written >= remaining) {
            return;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
}

static void format_transport_caps(char *buffer, size_t capacity, uint32_t caps) {
    buffer[0] = '\0';
    static const struct {
        uint32_t bit;
        const char *name;
    } bits[] = {
        {RSS_DDC_CAP_GET_VCP, "GET"},
        {RSS_DDC_CAP_SET_VCP, "SET"},
        {RSS_DDC_CAP_READ_EDID, "EDID"},
        {RSS_DDC_CAP_READ_DPCD, "DPCD"},
        {RSS_DDC_CAP_MCCS_CAPABILITIES, "MCCS"},
        {RSS_DDC_CAP_ALTERNATE_INPUT, "ALT-INPUT"},
    };
    char *cursor = buffer;
    size_t remaining = capacity;
    for (size_t index = 0; index < sizeof(bits) / sizeof(bits[0]); ++index) {
        if ((caps & bits[index].bit) == 0) {
            continue;
        }
        int written = snprintf(cursor, remaining, "%s%s", cursor == buffer ? "" : ",", bits[index].name);
        if (written < 0 || (size_t)written >= remaining) {
            return;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (buffer[0] == '\0') {
        snprintf(buffer, capacity, "none");
    }
}

static const char *stage_status(bool supported, bool attempted, RSSDDCError status) {
    if (!supported) {
        return "unsupported";
    }
    if (!attempted) {
        return "not-attempted";
    }
    return rss_ddc_error_string(status);
}

static void format_method(char *buffer, size_t capacity, const RSSDDCKnowledgeRoute *route) {
    if (route == NULL) {
        snprintf(buffer, capacity, "-");
        return;
    }
    if (route->kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) {
        snprintf(buffer, capacity, "LG_ALT");
        return;
    }
    if (route->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP ||
        route->kind == RSS_DDC_KNOWLEDGE_ROUTE_PICTURE_MODE) {
        snprintf(buffer, capacity, "vcp:0x%02x", route->address);
        return;
    }
    snprintf(buffer, capacity, "%s", route->route_id[0] != '\0' ? route->route_id : "-");
}

static void format_current(char *buffer, size_t capacity, RSSDDCCharacterizationValueState state,
                           const RSSDDCKnowledgeRoute *route) {
    if (state == RSS_DDC_CHARACTERIZATION_VALUE_CONFLICT) {
        snprintf(buffer, capacity, "conflict");
        return;
    }
    if (state != RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED || route == NULL) {
        snprintf(buffer, capacity, "-");
        return;
    }
    if (route->value.state == RSS_DDC_KNOWLEDGE_VALUE_STRING) {
        snprintf(buffer, capacity, "%s", route->value.string_value);
        return;
    }
    snprintf(buffer, capacity, "%u", route->value.unsigned_value);
}

static void format_evidence(char *buffer, size_t capacity, const RSSDDCMonitorKnowledge *knowledge,
                            const char *semantic_id) {
    bool profile = false;
    bool declared = false;
    bool observed = false;
    size_t count = rss_ddc_monitor_knowledge_route_count(knowledge);
    for (size_t index = 0; index < count; ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route == NULL || strcmp(route->semantic_id, semantic_id) != 0) {
            continue;
        }
        if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_PROFILE) {
            profile = true;
        } else if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_DECLARED) {
            declared = true;
        } else if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED) {
            observed = true;
        }
    }
    snprintf(buffer, capacity, "%s%s%s%s%s%s", profile ? "profile" : "",
             profile && (declared || observed) ? "," : "", declared ? "declared" : "",
             declared && observed ? "," : "", observed ? "observed" : "",
             !profile && !declared && !observed ? "-" : "");
}

static const char *mccs_semantic(uint8_t vcp) {
    switch (vcp) {
    case 0x10:
        return "display.brightness";
    case 0x12:
        return "display.contrast";
    case 0x14:
        return "display.color_preset";
    case 0x15:
        return "display.picture_mode";
    case 0x16:
        return "display.rgb.red_gain";
    case 0x18:
        return "display.rgb.green_gain";
    case 0x1a:
        return "display.rgb.blue_gain";
    case 0x60:
        return "inputs.switching";
    default:
        return NULL;
    }
}

static int compare_ids(const void *left, const void *right) {
    const char *const *first = left;
    const char *const *second = right;
    return strcmp(*first, *second);
}

static void print_heading(FILE *stream, const RSSDDCCliEffectiveOutput *output, const char *title) {
    rss_ddc_cli_color_begin(stream, output, RSS_DDC_CLI_COLOR_CYAN);
    fprintf(stream, "%s\n", title);
    rss_ddc_cli_color_reset(stream, output);
}

static void print_kv(FILE *stream, const char *key, const char *value) {
    fprintf(stream, "%s=%s\n", key, value);
}

static const char *extended_policy(RSSDDCCharacterizeMode mode, bool attempted, bool recommended,
                                   bool get_supported) {
    if (mode == RSS_DDC_CHARACTERIZE_MODE_PASSIVE) {
        return "not-run (passive)";
    }
    if (mode == RSS_DDC_CHARACTERIZE_MODE_DEEP) {
        return get_supported && attempted ? "forced" : "not-run (GET unavailable)";
    }
    if (attempted) {
        return "recommended-and-run";
    }
    return recommended ? "recommended-not-run" : "not-needed";
}

static void render_monitor(FILE *stream, const RSSDDCCharacterization *characterization,
                           const RSSDDCCliEffectiveOutput *output) {
    const RSSDDCDisplay *display = rss_ddc_characterization_display(characterization);
    const RSSDDCEDIDInfo *edid = rss_ddc_characterization_edid(characterization);
    char index_text[16];
    char cg_text[16];
    char product_code[16];
    print_heading(stream, output, "MONITOR");
    if (display == NULL) {
        print_kv(stream, "display", "unavailable");
        return;
    }
    snprintf(index_text, sizeof(index_text), "%u", display->list_index);
    snprintf(cg_text, sizeof(cg_text), "%u", display->cg_display_id);
    print_kv(stream, "index", index_text);
    print_kv(stream, "product", display->product_name);
    if (display->manufacturer[0] != '\0') {
        print_kv(stream, "manufacturer", display->manufacturer);
    }
    if (display->serial[0] != '\0') {
        print_kv(stream, "serial", display->serial);
    }
    print_kv(stream, "cg", cg_text);
    print_kv(stream, "online", display->online ? "yes" : "no");
    print_kv(stream, "external", display->external ? "yes" : "no");
    print_kv(stream, "provider", rss_ddc_provider_string(display->provider));
    print_kv(stream, "transport", display->transport);
    if (display->branch_device_id[0] != '\0') {
        print_kv(stream, "branch", display->branch_device_id);
    }
    if (edid == NULL) {
        print_kv(stream, "edid", "unavailable");
        return;
    }
    if (edid->manufacturer_id[0] != '\0') {
        print_kv(stream, "edid-manufacturer", edid->manufacturer_id);
    }
    snprintf(product_code, sizeof(product_code), "0x%04x", edid->product_code);
    print_kv(stream, "edid-product-code", product_code);
    if (edid->monitor_name[0] != '\0' && strcmp(edid->monitor_name, display->product_name) != 0) {
        print_kv(stream, "edid-name", edid->monitor_name);
    }
    if (edid->serial_text[0] != '\0' && strcmp(edid->serial_text, display->serial) != 0) {
        print_kv(stream, "edid-serial", edid->serial_text);
    }
}

static void render_characterization_summary(FILE *stream, const RSSDDCCharacterization *characterization,
                                            RSSDDCCharacterizeMode mode, const RSSDDCCliEffectiveOutput *output) {
    RSSDDCCharacterizationSufficiencyResult sufficiency = {0};
    char caps[96];
    char reasons[192];
    char sufficiency_text[48];
    (void)rss_ddc_characterization_sufficiency(characterization, &sufficiency);
    format_transport_caps(caps, sizeof(caps), rss_ddc_characterization_provider_capabilities(characterization));
    format_reasons(reasons, sizeof(reasons), sufficiency.reasons);
    rss_ddc_cli_color_format(sufficiency_text, sizeof(sufficiency_text), output,
                             sufficiency_color(sufficiency.status), sufficiency_name(sufficiency.status));
    print_heading(stream, output, "CHARACTERIZATION");
    print_kv(stream, "mode", rss_ddc_cli_characterize_mode_name(mode));
    print_kv(stream, "read-only", "yes");
    print_kv(stream, "profile", profile_status_name(rss_ddc_characterization_profile_status(characterization)));
    print_kv(stream, "structured",
             rss_ddc_characterization_structured_match_name(
                 rss_ddc_characterization_structured_match(characterization)));
    print_kv(stream, "discovery",
             rss_ddc_characterization_discovery_performed(characterization) ? "completed" : "skipped");
    print_kv(stream, "prior-knowledge",
             rss_ddc_characterization_prior_augmented(characterization)
                 ? "augmented"
                 : "none");
    print_kv(stream, "transport", caps);
    print_kv(stream, "mccs",
             stage_status(rss_ddc_characterization_mccs_supported(characterization),
                          rss_ddc_characterization_mccs_attempted(characterization),
                          rss_ddc_characterization_mccs_status(characterization)));
    print_kv(stream, "alien-probe-quick",
             stage_status(rss_ddc_characterization_quick_supported(characterization),
                          rss_ddc_characterization_quick_attempted(characterization),
                          rss_ddc_characterization_quick_status(characterization)));
    print_kv(stream, "alien-probe-extended",
             stage_status(rss_ddc_characterization_quick_supported(characterization),
                          rss_ddc_characterization_extended_attempted(characterization),
                          rss_ddc_characterization_extended_status(characterization)));
    print_kv(stream, "extended",
             extended_policy(mode, rss_ddc_characterization_extended_attempted(characterization),
                             sufficiency.extended_recommended,
                             rss_ddc_characterization_quick_supported(characterization)));
    fprintf(stream, "sufficiency=%s\n", sufficiency_text);
    print_kv(stream, "reasons", reasons);
}

static void add_control_row(RSSDDCCliTable *table, const RSSDDCCliEffectiveOutput *output,
                            const RSSDDCCharacterization *characterization, const char *semantic_id) {
    RSSDDCMonitorKnowledgeResolution *resolution = NULL;
    RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
    const RSSDDCKnowledgeRoute *current = NULL;
    const RSSDDCKnowledgeRoute *read = NULL;
    const RSSDDCKnowledgeRoute *write = NULL;
    char current_text[32];
    char read_text[32];
    char write_text[32];
    char authorized_text[32];
    char evidence[64];
    const char *authorized_plain = "-";
    RSSDDCCliColorRole authorized_color = RSS_DDC_CLI_COLOR_DIM;
    (void)rss_ddc_characterization_resolve(characterization, semantic_id, &resolution);
    (void)rss_ddc_characterization_current_value(characterization, semantic_id, &value_state, &current);
    if (resolution != NULL) {
        read = rss_ddc_monitor_knowledge_resolution_preferred_read(resolution);
        write = rss_ddc_monitor_knowledge_resolution_preferred_write(resolution);
    }
    format_current(current_text, sizeof(current_text), value_state, current);
    format_method(read_text, sizeof(read_text), read);
    format_method(write_text, sizeof(write_text), write);
    format_evidence(evidence, sizeof(evidence), rss_ddc_characterization_knowledge(characterization), semantic_id);
    if (write != NULL) {
        if (resolution != NULL && rss_ddc_monitor_knowledge_resolution_write_authorized(resolution)) {
            authorized_plain = "yes";
            authorized_color = RSS_DDC_CLI_COLOR_GREEN;
        } else {
            authorized_plain = "no";
            authorized_color = RSS_DDC_CLI_COLOR_YELLOW;
        }
    }
    rss_ddc_cli_color_format(authorized_text, sizeof(authorized_text), output, authorized_color, authorized_plain);
    const char *cells[] = {control_label(semantic_id), semantic_id, current_text, read_text, write_text,
                           authorized_text, evidence};
    rss_ddc_cli_table_add_row(table, cells, sizeof(cells) / sizeof(cells[0]));
    rss_ddc_monitor_knowledge_resolution_destroy(resolution);
}

static void render_controls_plain(FILE *stream, const RSSDDCCharacterization *characterization,
                                  const char *const *ids, size_t id_count) {
    for (size_t index = 0; index < id_count; ++index) {
        RSSDDCMonitorKnowledgeResolution *resolution = NULL;
        RSSDDCCharacterizationValueState value_state = RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED;
        const RSSDDCKnowledgeRoute *current = NULL;
        const RSSDDCKnowledgeRoute *read = NULL;
        const RSSDDCKnowledgeRoute *write = NULL;
        char current_text[32];
        char read_text[32];
        char write_text[32];
        char evidence[64];
        const char *authorized = "-";
        (void)rss_ddc_characterization_resolve(characterization, ids[index], &resolution);
        (void)rss_ddc_characterization_current_value(characterization, ids[index], &value_state, &current);
        if (resolution != NULL) {
            read = rss_ddc_monitor_knowledge_resolution_preferred_read(resolution);
            write = rss_ddc_monitor_knowledge_resolution_preferred_write(resolution);
            if (write != NULL) {
                authorized = rss_ddc_monitor_knowledge_resolution_write_authorized(resolution) ? "yes" : "no";
            }
        }
        format_current(current_text, sizeof(current_text), value_state, current);
        format_method(read_text, sizeof(read_text), read);
        format_method(write_text, sizeof(write_text), write);
        format_evidence(evidence, sizeof(evidence), rss_ddc_characterization_knowledge(characterization),
                        ids[index]);
        fprintf(stream, "control=%s id=%s current=%s read=%s write=%s authorized=%s evidence=%s\n",
                control_label(ids[index]), ids[index], current_text, read_text, write_text, authorized, evidence);
        rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    }
}

static void collect_extra_ids(const RSSDDCMonitorKnowledge *knowledge, const char **ids, size_t *count,
                              size_t capacity) {
    *count = 0;
    size_t routes = rss_ddc_monitor_knowledge_route_count(knowledge);
    for (size_t index = 0; index < routes; ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route == NULL || is_product_control(route->semantic_id) || is_vendor_unknown(route->semantic_id)) {
            continue;
        }
        bool seen = false;
        for (size_t existing = 0; existing < *count; ++existing) {
            if (strcmp(ids[existing], route->semantic_id) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen && *count < capacity) {
            ids[(*count)++] = route->semantic_id;
        }
    }
    qsort(ids, *count, sizeof(*ids), compare_ids);
}

static void render_resolved_controls(FILE *stream, const RSSDDCCharacterization *characterization,
                                     const RSSDDCCliEffectiveOutput *output) {
    const char *extra[128];
    size_t extra_count = 0;
    collect_extra_ids(rss_ddc_characterization_knowledge(characterization), extra, &extra_count,
                      sizeof(extra) / sizeof(extra[0]));
    print_heading(stream, output, "RESOLVED CONTROLS");
    if (!output->table) {
        render_controls_plain(stream, characterization, product_controls,
                              sizeof(product_controls) / sizeof(product_controls[0]));
        render_controls_plain(stream, characterization, extra, extra_count);
        return;
    }
    static const char *headers[] = {"CONTROL", "ID", "CURRENT", "READ", "WRITE", "AUTHORIZED", "EVIDENCE"};
    RSSDDCCliTable table = {};
    if (!rss_ddc_cli_table_init(&table, headers, sizeof(headers) / sizeof(headers[0]))) {
        return;
    }
    for (size_t index = 0; index < sizeof(product_controls) / sizeof(product_controls[0]); ++index) {
        add_control_row(&table, output, characterization, product_controls[index]);
    }
    for (size_t index = 0; index < extra_count; ++index) {
        add_control_row(&table, output, characterization, extra[index]);
    }
    rss_ddc_cli_table_render(stream, &table, output);
}

static void render_evidence_summary(FILE *stream, const RSSDDCCharacterization *characterization,
                                    const RSSDDCCliEffectiveOutput *output) {
    const RSSDDCMonitorKnowledge *knowledge = rss_ddc_characterization_knowledge(characterization);
    size_t profile = 0;
    size_t declared = 0;
    size_t observed = 0;
    size_t unknown = 0;
    size_t conflicts = 0;
    size_t count = rss_ddc_monitor_knowledge_route_count(knowledge);
    print_heading(stream, output, "EVIDENCE SUMMARY");
    for (size_t index = 0; index < count; ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (route == NULL) {
            continue;
        }
        if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_PROFILE) {
            ++profile;
        } else if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_DECLARED) {
            ++declared;
        } else if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED) {
            ++observed;
        }
        if (is_vendor_unknown(route->semantic_id)) {
            ++unknown;
        }
    }
    for (size_t index = 0; index < sizeof(product_controls) / sizeof(product_controls[0]); ++index) {
        RSSDDCMonitorKnowledgeResolution *resolution = NULL;
        if (rss_ddc_characterization_resolve(characterization, product_controls[index], &resolution) == RSS_DDC_OK &&
            resolution != NULL && rss_ddc_monitor_knowledge_resolution_has_conflict(resolution)) {
            ++conflicts;
        }
        rss_ddc_monitor_knowledge_resolution_destroy(resolution);
    }
    fprintf(stream, "profile=%zu declared=%zu observed=%zu unknown-vendor=%zu method-conflicts=%zu routes=%zu\n",
            profile, declared, observed, unknown, conflicts, count);
}

static void render_mccs_summary(FILE *stream, const RSSDDCCharacterization *characterization,
                                const RSSDDCCliEffectiveOutput *output) {
    const RSSDDCMCCSCapabilities *mccs = rss_ddc_characterization_mccs(characterization);
    print_heading(stream, output, "MCCS SUMMARY");
    if (mccs == NULL) {
        print_kv(stream, "mccs",
                 rss_ddc_characterization_mccs_supported(characterization) ? "unavailable"
                                                                           : "unsupported");
        return;
    }
    fprintf(stream, "advertised-vcps=%zu enums=%zu\n", mccs->feature_count, mccs->enum_value_count);
    for (size_t index = 0; index < mccs->feature_count; ++index) {
        const char *semantic = mccs_semantic(mccs->features[index].vcp_code);
        if (semantic != NULL) {
            fprintf(stream, "advertised=%s vcp=0x%02x enums=%zu\n", semantic, mccs->features[index].vcp_code,
                    mccs->features[index].enum_value_count);
        }
    }
}

static void render_quick_summary(FILE *stream, const RSSDDCCharacterization *characterization,
                                 const RSSDDCCliEffectiveOutput *output) {
    const RSSDDCProbeDiagnostics *diagnostics = rss_ddc_characterization_quick_diagnostics(characterization);
    print_heading(stream, output, "ALIEN PROBE QUICK SUMMARY");
    if (!rss_ddc_characterization_quick_attempted(characterization) || diagnostics == NULL) {
        print_kv(stream, "quick", "not-attempted");
        return;
    }
    fprintf(stream,
            "attempted=%zu protocol-valid=%zu stable=%zu variable=%zu protocol-reported=%zu malformed=%zu "
            "transport-errors=%zu\n",
            diagnostics->controls_attempted, diagnostics->controls_protocol_valid, diagnostics->controls_stable,
            diagnostics->controls_variable, diagnostics->controls_protocol_reported, diagnostics->controls_malformed,
            diagnostics->controls_transport_error);
}

static void render_extended_summary(FILE *stream, const RSSDDCCharacterization *characterization,
                                    RSSDDCCharacterizeMode mode, const RSSDDCCliEffectiveOutput *output) {
    const RSSDDCProbeExtendedDiagnostics *diagnostics =
        rss_ddc_characterization_extended_diagnostics(characterization);
    const RSSDDCCharacterizationPromotionSummary *promotion =
        rss_ddc_characterization_extended_promotion(characterization);
    bool attempted = rss_ddc_characterization_extended_attempted(characterization);
    if (!attempted && mode != RSS_DDC_CHARACTERIZE_MODE_DEEP) {
        return;
    }
    print_heading(stream, output, "ALIEN PROBE EXTENDED SUMMARY");
    if (!attempted) {
        print_kv(stream, "extended", "not-attempted");
        return;
    }
    if (diagnostics == NULL) {
        print_kv(stream, "extended", rss_ddc_error_string(rss_ddc_characterization_extended_status(characterization)));
        return;
    }
    fprintf(stream,
            "requested=%zu attempted=%zu strict-valid=%zu stable=%zu variable=%zu protocol-reported=%zu "
            "malformed=%zu transport-errors=%zu semantic-mismatch=%zu\n",
            diagnostics->requested, diagnostics->attempted, diagnostics->strict_valid, diagnostics->stable_valid,
            diagnostics->variable_valid, diagnostics->protocol_reported, diagnostics->malformed,
            diagnostics->transport_errors, diagnostics->semantic_mismatch);
    if (promotion != NULL) {
        fprintf(stream, "promoted=%zu considered=%zu skipped-capacity=%zu skipped-nonpromotable=%zu\n",
                promotion->promoted, promotion->considered, promotion->skipped_capacity,
                promotion->skipped_nonpromotable);
    }
}

void rss_ddc_cli_render_characterization(FILE *stream, const RSSDDCCharacterization *characterization,
                                         RSSDDCCharacterizeMode mode, const RSSDDCCliEffectiveOutput *output) {
    if (stream == NULL || characterization == NULL || output == NULL) {
        return;
    }
    render_monitor(stream, characterization, output);
    fputc('\n', stream);
    render_characterization_summary(stream, characterization, mode, output);
    fputc('\n', stream);
    render_resolved_controls(stream, characterization, output);
    fputc('\n', stream);
    render_evidence_summary(stream, characterization, output);
    fputc('\n', stream);
    render_mccs_summary(stream, characterization, output);
    fputc('\n', stream);
    render_quick_summary(stream, characterization, output);
    if (rss_ddc_characterization_extended_attempted(characterization) ||
        mode == RSS_DDC_CHARACTERIZE_MODE_DEEP) {
        fputc('\n', stream);
        render_extended_summary(stream, characterization, mode, output);
    }
}
