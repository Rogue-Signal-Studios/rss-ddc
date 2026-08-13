#include "discovery.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void append_warning(RSSDDCResearchReport *report, const char *warning) {
    if (report->warning_count >= sizeof(report->warnings) / sizeof(report->warnings[0])) return;
    snprintf(report->warnings[report->warning_count++], sizeof(report->warnings[0]), "%s", warning);
}

bool rss_ddc_research_parse_unsigned(const char *text, unsigned long maximum, unsigned long *value) {
    char *end = NULL;
    if (text == NULL || value == NULL || text[0] == '-') return false;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) return false;
    *value = parsed;
    return true;
}

bool rss_ddc_research_parse_category(const char *text, RSSDDCResearchCategory *category) {
    if (text == NULL || category == NULL) return false;
    if (strcmp(text, "all") == 0) *category = RSS_DDC_RESEARCH_CATEGORY_ALL;
    else if (strcmp(text, "picture") == 0) *category = RSS_DDC_RESEARCH_CATEGORY_PICTURE;
    else return false;
    return true;
}

const char *rss_ddc_research_category_name(RSSDDCResearchCategory category) {
    return category == RSS_DDC_RESEARCH_CATEGORY_PICTURE ? "picture" : "all";
}

const char *rss_ddc_research_classification_name(RSSDDCResearchClassification classification) {
    static const char *const names[] = {"numeric", "enum-advertised", "readable-unknown", "unsupported",
        "malformed", "unstable", "transport-error"};
    return classification <= RSS_DDC_RESEARCH_CLASS_TRANSPORT_ERROR ? names[classification] : "transport-error";
}

/* Conservative, standard MCCS picture/color candidates; this is prioritization, never proof of semantics. */
bool rss_ddc_research_is_picture_candidate(uint8_t vcp) {
    switch (vcp) {
        case 0x10: case 0x12: case 0x14: case 0x16: case 0x18: case 0x1a: case 0x6c: case 0x72: case 0x87:
            return true;
        default: return false;
    }
}

static const char *semantic_label(uint8_t vcp) {
    switch (vcp) {
        case 0x10: return "standard-brightness";
        case 0x12: return "standard-contrast";
        case 0x14: return "standard-select-color-preset";
        case 0x16: return "standard-video-gain-red";
        case 0x18: return "standard-video-gain-green";
        case 0x1a: return "standard-video-gain-blue";
        case 0x6c: return "standard-video-black-level";
        case 0x72: return "standard-gamma";
        case 0x87: return "standard-sharpness";
        default: return "monitor-advertised-or-explicit-unknown";
    }
}

/* Input/source, power, reset, and degauss controls are never mutation candidates. */
bool rss_ddc_research_is_mutation_denied(uint8_t vcp) {
    switch (vcp) {
        case 0x01: /* degauss */
        case 0x04: /* factory reset */
        case 0x05: /* reset luminance/contrast */
        case 0x08: /* reset color */
        case 0x60: /* input source */
        case 0xd6: /* power mode */
        case 0xf4: /* LG alternate input transport; write-only and never generic VCP mutation */
            return true;
        default: return false;
    }
}

bool rss_ddc_research_mutation_authorized(const RSSDDCResearchOptions *options, uint8_t vcp) {
    if (options == NULL || !options->allow_set || options->mutation_value_count == 0 ||
        options->explicit_vcp_count == 0 || rss_ddc_research_is_mutation_denied(vcp)) return false;
    for (size_t index = 0; index < options->explicit_vcp_count; ++index) {
        if (options->explicit_vcps[index] == vcp) return true;
    }
    return false;
}

RSSDDCError rss_ddc_research_validate_options(const RSSDDCResearchOptions *options) {
    if (options == NULL || options->reads == 0 || options->reads > RSS_DDC_RESEARCH_MAX_READS ||
        options->explicit_vcp_count > RSS_DDC_RESEARCH_MAX_CANDIDATES ||
        options->mutation_value_count > RSS_DDC_RESEARCH_MAX_VALUES) return RSS_DDC_ERROR_ARGUMENT;
    if (options->allow_set && (options->explicit_vcp_count == 0 || options->mutation_value_count == 0)) {
        return RSS_DDC_ERROR_SAFETY_GATE;
    }
    if (options->allow_set && !options->restore &&
        (options->explicit_vcp_count != 1 || options->mutation_value_count != 1)) return RSS_DDC_ERROR_SAFETY_GATE;
    if (options->allow_set) {
        for (size_t index = 0; index < options->explicit_vcp_count; ++index) {
            if (rss_ddc_research_is_mutation_denied(options->explicit_vcps[index])) return RSS_DDC_ERROR_SAFETY_GATE;
        }
    }
    return RSS_DDC_OK;
}

static bool add_candidate(uint8_t candidate, uint8_t *candidates, size_t capacity, size_t *count) {
    for (size_t index = 0; index < *count; ++index) if (candidates[index] == candidate) return true;
    if (*count == capacity) return false;
    candidates[(*count)++] = candidate;
    return true;
}

RSSDDCError rss_ddc_research_select_candidates(const RSSDDCMCCSCapabilities *capabilities,
                                                const RSSDDCResearchOptions *options,
                                                uint8_t *candidates, size_t capacity, size_t *count) {
    if (options == NULL || candidates == NULL || count == NULL || capacity == 0) return RSS_DDC_ERROR_ARGUMENT;
    *count = 0;
    if (capabilities != NULL) {
        for (size_t index = 0; index < capabilities->feature_count; ++index) {
            uint8_t vcp = capabilities->features[index].vcp_code;
            if (options->category == RSS_DDC_RESEARCH_CATEGORY_PICTURE && !rss_ddc_research_is_picture_candidate(vcp)) continue;
            if (!add_candidate(vcp, candidates, capacity, count)) return RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE;
        }
    }
    if (options->category == RSS_DDC_RESEARCH_CATEGORY_PICTURE) {
        static const uint8_t picture[] = {0x10, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x6c, 0x72, 0x87};
        for (size_t index = 0; index < sizeof(picture); ++index) {
            if (!add_candidate(picture[index], candidates, capacity, count)) return RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE;
        }
    }
    for (size_t index = 0; index < options->explicit_vcp_count; ++index) {
        if (!add_candidate(options->explicit_vcps[index], candidates, capacity, count)) return RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE;
    }
    return *count == 0 ? RSS_DDC_ERROR_ARGUMENT : RSS_DDC_OK;
}

RSSDDCResearchClassification rss_ddc_research_classify(const RSSDDCResearchSample *samples, size_t sample_count,
                                                        size_t advertised_value_count) {
    if (samples == NULL || sample_count == 0) return RSS_DDC_RESEARCH_CLASS_TRANSPORT_ERROR;
    size_t successes = 0;
    RSSDDCVCPResult first = {};
    for (size_t index = 0; index < sample_count; ++index) {
        if (samples[index].status == RSS_DDC_OK) {
            if (successes != 0 && (first.current_value != samples[index].result.current_value ||
                first.maximum_value != samples[index].result.maximum_value)) return RSS_DDC_RESEARCH_CLASS_UNSTABLE;
            first = samples[index].result;
            ++successes;
        }
    }
    if (successes != sample_count) return successes != 0 ? RSS_DDC_RESEARCH_CLASS_UNSTABLE :
        (samples[0].status == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY || samples[0].status == RSS_DDC_ERROR_UNSUPPORTED_PROVIDER ?
         RSS_DDC_RESEARCH_CLASS_UNSUPPORTED :
         (samples[0].status == RSS_DDC_ERROR_REPLY_LENGTH || samples[0].status == RSS_DDC_ERROR_REPLY_SOURCE ||
          samples[0].status == RSS_DDC_ERROR_REPLY_COMMAND || samples[0].status == RSS_DDC_ERROR_REPLY_STATUS ||
          samples[0].status == RSS_DDC_ERROR_REPLY_VCP || samples[0].status == RSS_DDC_ERROR_REPLY_CHECKSUM ?
          RSS_DDC_RESEARCH_CLASS_MALFORMED : RSS_DDC_RESEARCH_CLASS_TRANSPORT_ERROR));
    if (advertised_value_count != 0) return RSS_DDC_RESEARCH_CLASS_ENUM_ADVERTISED;
    return first.maximum_value != 0 ? RSS_DDC_RESEARCH_CLASS_NUMERIC : RSS_DDC_RESEARCH_CLASS_READABLE_UNKNOWN;
}

static size_t advertised_values(const RSSDDCMCCSCapabilities *capabilities, uint8_t vcp, uint8_t *values) {
    const uint8_t *source = NULL;
    size_t count = 0;
    if (capabilities == NULL || rss_ddc_mccs_capabilities_enum_values(capabilities, vcp, &source, &count) != RSS_DDC_OK) return 0;
    if (count > RSS_DDC_RESEARCH_MAX_VALUES) count = RSS_DDC_RESEARCH_MAX_VALUES;
    if (count != 0) memcpy(values, source, count);
    return count;
}

RSSDDCError rss_ddc_research_run(RSSDDCResearchReport *report, const RSSDDCResearchOptions *options,
                                 const RSSDDCResearchTransport *transport) {
    if (report == NULL || transport == NULL || transport->get_vcp == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSDDCError error = rss_ddc_research_validate_options(options);
    if (error != RSS_DDC_OK) return error;
    uint8_t candidates[RSS_DDC_RESEARCH_MAX_CANDIDATES] = {};
    size_t candidate_count = 0;
    error = rss_ddc_research_select_candidates(report->capabilities_status == RSS_DDC_OK ? &report->capabilities : NULL,
                                               options, candidates, sizeof(candidates), &candidate_count);
    if (error != RSS_DDC_OK) return error;
    for (size_t index = 0; index < candidate_count; ++index) {
        RSSDDCResearchRead *read = &report->reads[report->read_count++];
        read->vcp = candidates[index];
        read->advertised_value_count = advertised_values(report->capabilities_status == RSS_DDC_OK ? &report->capabilities : NULL,
                                                         read->vcp, read->advertised_values);
        read->sample_count = options->reads;
        for (size_t sample = 0; sample < read->sample_count; ++sample) {
            read->samples[sample].status = transport->get_vcp(transport->context, read->vcp, &read->samples[sample].result);
        }
        read->classification = rss_ddc_research_classify(read->samples, read->sample_count, read->advertised_value_count);
    }
    if (!options->allow_set) return RSS_DDC_OK;
    if (transport->set_vcp == NULL) return RSS_DDC_ERROR_ARGUMENT;
    for (size_t index = 0; index < report->read_count; ++index) {
        RSSDDCResearchRead *read = &report->reads[index];
        if (!rss_ddc_research_mutation_authorized(options, read->vcp) ||
            read->classification == RSS_DDC_RESEARCH_CLASS_UNSTABLE || read->samples[0].status != RSS_DDC_OK) continue;
        for (size_t value = 0; value < options->mutation_value_count; ++value) {
            RSSDDCResearchMutation *mutation = &report->mutations[report->mutation_count++];
            mutation->vcp = read->vcp;
            mutation->candidate = options->mutation_values[value];
            mutation->original_read = true;
            mutation->original = read->samples[0].result.current_value;
            mutation->attempted = true;
            mutation->set_status = transport->set_vcp(transport->context, mutation->vcp, mutation->candidate);
            if (mutation->set_status != RSS_DDC_OK) continue;
            if (transport->settle != NULL) transport->settle(transport->context, options->settle_ms);
            RSSDDCVCPResult observed = {};
            mutation->observed_status = transport->get_vcp(transport->context, mutation->vcp, &observed);
            if (mutation->observed_status == RSS_DDC_OK) {
                mutation->observed = observed.current_value;
                mutation->changed = observed.current_value == mutation->candidate;
            }
            if (!options->restore) continue;
            mutation->restore_attempted = true;
            mutation->restore_status = transport->set_vcp(transport->context, mutation->vcp, mutation->original);
            if (mutation->restore_status != RSS_DDC_OK) {
                append_warning(report, "restore failed; no further mutation for this control");
                break;
            }
            if (transport->settle != NULL) transport->settle(transport->context, options->settle_ms);
            RSSDDCVCPResult restored = {};
            mutation->restored_status = transport->get_vcp(transport->context, mutation->vcp, &restored);
            mutation->restored = mutation->restored_status == RSS_DDC_OK && restored.current_value == mutation->original;
            if (!mutation->restored) {
                append_warning(report, "restore could not be verified; no further mutation for this control");
                break;
            }
        }
    }
    return RSS_DDC_OK;
}

static bool write_json_string(FILE *output, const char *text) {
    if (fputc('"', output) == EOF) return false;
    for (const unsigned char *cursor = (const unsigned char *)(text == NULL ? "" : text); *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') { if (fputc('\\', output) == EOF) return false; }
        if (*cursor < 0x20) { if (fprintf(output, "\\u%04x", *cursor) < 0) return false; }
        else if (fputc(*cursor, output) == EOF) return false;
    }
    return fputc('"', output) != EOF;
}

static bool write_values(FILE *output, const uint8_t *values, size_t count) {
    if (fputc('[', output) == EOF) return false;
    for (size_t index = 0; index < count; ++index) if (fprintf(output, "%s%u", index == 0 ? "" : ",", values[index]) < 0) return false;
    return fputc(']', output) != EOF;
}

bool rss_ddc_research_write_json(FILE *output, const RSSDDCResearchReport *report) {
    if (output == NULL || report == NULL) return false;
    if (fprintf(output, "{\n  \"schemaVersion\": 2,\n  \"timestamp\": ") < 0 || !write_json_string(output, report->timestamp) ||
        fprintf(output, ",\n  \"label\": ") < 0 || !write_json_string(output, report->label) ||
        fprintf(output, ",\n  \"display\": {\"index\": %u, \"productName\": ", report->display.list_index) < 0 ||
        !write_json_string(output, report->display.product_name) || fprintf(output, ", \"manufacturer\": ") < 0 ||
        !write_json_string(output, report->display.manufacturer) || fprintf(output, ", \"serial\": ") < 0 ||
        !write_json_string(output, report->display.serial) || fprintf(output, ", \"provider\": ") < 0 ||
        !write_json_string(output, rss_ddc_provider_string(report->display.provider)) || fprintf(output, ", \"branch\": ") < 0 ||
        !write_json_string(output, report->display.branch_device_id) || fprintf(output, ", \"transport\": ") < 0 ||
        !write_json_string(output, report->display.transport) || fprintf(output, "},\n  \"capabilities\": {\"rssDdcBits\": %u, \"mccsStatus\": ", report->display.capabilities) < 0 ||
        !write_json_string(output, rss_ddc_error_string(report->capabilities_status)) || fprintf(output, ", \"mccsRaw\": ") < 0 ||
        !write_json_string(output, report->capabilities_status == RSS_DDC_OK ? report->capabilities.raw : "") || fprintf(output, ", \"advertisedVcps\": [") < 0) return false;
    for (size_t index = 0; index < (report->capabilities_status == RSS_DDC_OK ? report->capabilities.feature_count : 0); ++index)
        if (fprintf(output, "%s%u", index == 0 ? "" : ",", report->capabilities.features[index].vcp_code) < 0) return false;
    if (fprintf(output, "]},\n  \"reads\": [") < 0) return false;
    for (size_t index = 0; index < report->read_count; ++index) {
        const RSSDDCResearchRead *read = &report->reads[index];
        if (fprintf(output, "%s\n    {\"vcp\": %u, \"vcpHex\": \"0x%02X\", \"semantic\": ", index == 0 ? "" : ",", read->vcp, read->vcp) < 0 ||
            !write_json_string(output, semantic_label(read->vcp)) || fprintf(output, ", \"status\": ") < 0 ||
            !write_json_string(output, rss_ddc_error_string(read->samples[0].status)) || fprintf(output, ", \"current\": %u, \"max\": %u, \"advertisedValues\": ", read->samples[0].result.current_value, read->samples[0].result.maximum_value) < 0 ||
            !write_values(output, read->advertised_values, read->advertised_value_count) || fprintf(output, ", \"classification\": ") < 0 ||
            !write_json_string(output, rss_ddc_research_classification_name(read->classification)) || fprintf(output, ", \"samples\": [") < 0) return false;
        for (size_t sample = 0; sample < read->sample_count; ++sample)
            if (fprintf(output, "%s{\"status\": ", sample == 0 ? "" : ",") < 0 || !write_json_string(output, rss_ddc_error_string(read->samples[sample].status)) ||
                fprintf(output, ", \"current\": %u, \"max\": %u}", read->samples[sample].result.current_value, read->samples[sample].result.maximum_value) < 0) return false;
        if (fprintf(output, "]}") < 0) return false;
    }
    if (fprintf(output, "\n  ],\n  \"mutations\": [") < 0) return false;
    for (size_t index = 0; index < report->mutation_count; ++index) {
        const RSSDDCResearchMutation *mutation = &report->mutations[index];
        if (fprintf(output, "%s\n    {\"vcp\": %u, \"candidate\": %u, \"original\": %u, \"setStatus\": ", index == 0 ? "" : ",", mutation->vcp, mutation->candidate, mutation->original) < 0 ||
            !write_json_string(output, rss_ddc_error_string(mutation->set_status)) || fprintf(output, ", \"observed\": %u, \"changed\": %s, \"restoreAttempted\": %s, \"restoreStatus\": ", mutation->observed, mutation->changed ? "true" : "false", mutation->restore_attempted ? "true" : "false") < 0 ||
            !write_json_string(output, rss_ddc_error_string(mutation->restore_status)) || fprintf(output, ", \"restored\": %s}", mutation->restored ? "true" : "false") < 0) return false;
    }
    if (fprintf(output, "\n  ],\n  \"warnings\": [") < 0) return false;
    for (size_t index = 0; index < report->warning_count; ++index) {
        if (index != 0 && fputc(',', output) == EOF) return false;
        if (!write_json_string(output, report->warnings[index])) return false;
    }
    return fprintf(output, "]\n}\n") >= 0 && !ferror(output);
}

void rss_ddc_research_print_summary(FILE *output, const RSSDDCResearchReport *report, const char *report_path) {
    size_t readable = 0, numeric = 0, enumerated = 0, unsupported = 0, unstable = 0;
    for (size_t index = 0; index < report->read_count; ++index) {
        switch (report->reads[index].classification) {
            case RSS_DDC_RESEARCH_CLASS_NUMERIC: ++numeric; ++readable; break;
            case RSS_DDC_RESEARCH_CLASS_ENUM_ADVERTISED: ++enumerated; ++readable; break;
            case RSS_DDC_RESEARCH_CLASS_READABLE_UNKNOWN: ++readable; break;
            case RSS_DDC_RESEARCH_CLASS_UNSTABLE: ++unstable; break;
            default: ++unsupported; break;
        }
    }
    fprintf(output, "%s / %s\n\nAdvertised VCPs:        %zu\nReadable:               %zu\nNumeric controls:       %zu\nEnum-advertised:        %zu\nUnsupported/malformed:  %zu\nUnstable:               %zu\n",
            report->display.product_name, rss_ddc_provider_string(report->display.provider), report->capabilities.feature_count,
            readable, numeric, enumerated, unsupported, unstable);
    bool printed_candidates = false;
    for (size_t index = 0; index < report->read_count; ++index) {
        const RSSDDCResearchRead *read = &report->reads[index];
        if (!rss_ddc_research_is_picture_candidate(read->vcp) || read->samples[0].status != RSS_DDC_OK) continue;
        if (!printed_candidates) { fprintf(output, "\nPicture candidates (standard mappings are prioritization only):\n"); printed_candidates = true; }
        fprintf(output, "  0x%02X  %-15s current=0x%04X\n", read->vcp,
                rss_ddc_research_classification_name(read->classification), read->samples[0].result.current_value);
    }
    if (report->mutation_count != 0) {
        fprintf(output, "\nMutation validation:\n");
        for (size_t index = 0; index < report->mutation_count; ++index) {
            const RSSDDCResearchMutation *mutation = &report->mutations[index];
            fprintf(output, "  0x%02X requested=0x%04X observed=0x%04X changed=%s restored=%s\n", mutation->vcp,
                    mutation->candidate, mutation->observed, mutation->changed ? "yes" : "no",
                    mutation->restored ? "yes" : "no");
        }
    }
    if (report_path != NULL) fprintf(output, "\nReport:\n  %s\n", report_path);
}
