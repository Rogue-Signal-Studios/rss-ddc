@import Foundation;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "discovery.h"

static void usage(const char *program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s discover --display <index> --report <file> [--category all|picture] [--reads 1..10]\n"
            "      [--vcp <code[,code...]>] [--range <first:last>]\n"
            "      [--allow-set --vcp <code[,code...]> --values <value[,value...]> [--restore] [--settle-ms <ms>]]\n"
            "\nRead-only is the default. --allow-set is deliberately gated, requires an explicit --vcp\n"
            "and --values, and rejects input (0x60/0xF4), power (0xD6), reset (0x04/0x05/0x08), and degauss (0x01).\n",
            program);
}

static bool append_vcp(RSSDDCResearchOptions *options, unsigned long value) {
    if (value > UINT8_MAX || options->explicit_vcp_count == RSS_DDC_RESEARCH_MAX_CANDIDATES) return false;
    for (size_t index = 0; index < options->explicit_vcp_count; ++index) if (options->explicit_vcps[index] == value) return true;
    options->explicit_vcps[options->explicit_vcp_count++] = (uint8_t)value;
    return true;
}

static bool parse_list(const char *text, unsigned long maximum, bool vcp, RSSDDCResearchOptions *options) {
    if (text == NULL || text[0] == '\0') return false;
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *comma = strchr(cursor, ',');
        size_t length = comma == NULL ? strlen(cursor) : (size_t)(comma - cursor);
        if (length == 0 || length >= 32) return false;
        char token[32] = {};
        memcpy(token, cursor, length);
        unsigned long value = 0;
        if (!rss_ddc_research_parse_unsigned(token, maximum, &value)) return false;
        if (vcp) {
            if (!append_vcp(options, value)) return false;
        } else {
            if (options->mutation_value_count == RSS_DDC_RESEARCH_MAX_VALUES) return false;
            options->mutation_values[options->mutation_value_count++] = (uint16_t)value;
        }
        if (comma == NULL) return true;
        cursor = comma + 1;
    }
    return false;
}

static bool parse_range(const char *text, RSSDDCResearchOptions *options) {
    const char *separator = text == NULL ? NULL : strchr(text, ':');
    if (separator == NULL || separator == text || separator[1] == '\0' || strchr(separator + 1, ':') != NULL) return false;
    char first_text[32] = {}, last_text[32] = {};
    size_t first_length = (size_t)(separator - text);
    size_t last_length = strlen(separator + 1);
    if (first_length >= sizeof(first_text) || last_length >= sizeof(last_text)) return false;
    memcpy(first_text, text, first_length);
    memcpy(last_text, separator + 1, last_length);
    unsigned long first = 0, last = 0;
    if (!rss_ddc_research_parse_unsigned(first_text, UINT8_MAX, &first) ||
        !rss_ddc_research_parse_unsigned(last_text, UINT8_MAX, &last) || first > last || last - first + 1 > RSS_DDC_RESEARCH_MAX_CANDIDATES) return false;
    for (unsigned long value = first; value <= last; ++value) if (!append_vcp(options, value)) return false;
    return true;
}

static RSSDDCError transport_get(void *context, uint8_t vcp, RSSDDCVCPResult *result) {
    return rss_ddc_get_vcp(*(const uint32_t *)context, vcp, result);
}

static RSSDDCError transport_set(void *context, uint8_t vcp, uint16_t value) {
    return rss_ddc_set_vcp(*(const uint32_t *)context, vcp, value);
}

static void transport_settle(void *context, uint32_t milliseconds) {
    (void)context;
    if (milliseconds != 0) usleep((useconds_t)milliseconds * 1000u);
}

static void timestamp_utc(char destination[32]) {
    time_t now = time(NULL);
    struct tm calendar = {};
    if (gmtime_r(&now, &calendar) == NULL || strftime(destination, 32, "%Y-%m-%dT%H:%M:%SZ", &calendar) == 0) {
        snprintf(destination, 32, "unknown");
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(argv[0]); return EXIT_SUCCESS; }
    if (argc < 2 || strcmp(argv[1], "discover") != 0) { usage(argv[0]); return EXIT_FAILURE; }
    RSSDDCResearchOptions options = {.category = RSS_DDC_RESEARCH_CATEGORY_ALL, .reads = 1, .restore = true};
    unsigned long display = 0;
    const char *report_path = NULL;
    bool have_display = false;
    for (int argument = 2; argument < argc; ++argument) {
        const char *option = argv[argument];
        if (strcmp(option, "--allow-set") == 0 && !options.allow_set) { options.allow_set = true; continue; }
        if (strcmp(option, "--restore") == 0) { options.restore = true; continue; }
        if ((strcmp(option, "--display") == 0 || strcmp(option, "--report") == 0 || strcmp(option, "--category") == 0 ||
             strcmp(option, "--reads") == 0 || strcmp(option, "--vcp") == 0 || strcmp(option, "--values") == 0 ||
             strcmp(option, "--range") == 0 || strcmp(option, "--settle-ms") == 0) && ++argument < argc) {
            const char *value = argv[argument];
            unsigned long parsed = 0;
            if (strcmp(option, "--display") == 0) {
                if (!rss_ddc_research_parse_unsigned(value, UINT32_MAX, &display) || display == 0) { fprintf(stderr, "invalid --display\n"); return EXIT_FAILURE; }
                have_display = true;
            } else if (strcmp(option, "--report") == 0) report_path = value;
            else if (strcmp(option, "--category") == 0) {
                if (!rss_ddc_research_parse_category(value, &options.category)) { fprintf(stderr, "invalid --category (expected all or picture)\n"); return EXIT_FAILURE; }
            } else if (strcmp(option, "--reads") == 0) {
                if (!rss_ddc_research_parse_unsigned(value, RSS_DDC_RESEARCH_MAX_READS, &parsed) || parsed == 0) { fprintf(stderr, "invalid --reads (expected 1..10)\n"); return EXIT_FAILURE; }
                options.reads = (unsigned int)parsed;
            } else if (strcmp(option, "--vcp") == 0) {
                if (!parse_list(value, UINT8_MAX, true, &options)) { fprintf(stderr, "invalid --vcp list\n"); return EXIT_FAILURE; }
            } else if (strcmp(option, "--values") == 0) {
                if (!parse_list(value, UINT16_MAX, false, &options)) { fprintf(stderr, "invalid --values list\n"); return EXIT_FAILURE; }
            } else if (strcmp(option, "--range") == 0) {
                if (!parse_range(value, &options)) { fprintf(stderr, "invalid --range (maximum 64 VCPs)\n"); return EXIT_FAILURE; }
            } else {
                if (!rss_ddc_research_parse_unsigned(value, 60000, &parsed)) { fprintf(stderr, "invalid --settle-ms (maximum 60000)\n"); return EXIT_FAILURE; }
                options.settle_ms = (uint32_t)parsed;
            }
            continue;
        }
        fprintf(stderr, "unknown or incomplete option: %s\n", option);
        return EXIT_FAILURE;
    }
    if (!have_display || report_path == NULL) { fprintf(stderr, "--display and --report are required\n"); return EXIT_FAILURE; }
    RSSDDCError error = rss_ddc_research_validate_options(&options);
    if (error != RSS_DDC_OK) {
        fprintf(stderr, "research safety gate: %s (SET requires --allow-set, explicit --vcp, explicit --values, and no denylisted VCP)\n", rss_ddc_error_string(error));
        return EXIT_FAILURE;
    }

    RSSDDCResearchReport report = {};
    report.display.list_index = (uint32_t)display;
    error = rss_ddc_get_display((uint32_t)display, &report.display);
    if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc-research: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
    report.capabilities_status = rss_ddc_get_mccs_capabilities((uint32_t)display, &report.capabilities);
    if (report.capabilities_status != RSS_DDC_OK) snprintf(report.warnings[report.warning_count++], sizeof(report.warnings[0]), "MCCS capabilities unavailable: %s", rss_ddc_error_string(report.capabilities_status));
    timestamp_utc(report.timestamp);
    uint32_t display_index = (uint32_t)display;
    RSSDDCResearchTransport transport = {.get_vcp = transport_get, .set_vcp = transport_set, .settle = transport_settle, .context = &display_index};
    error = rss_ddc_research_run(&report, &options, &transport);
    if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc-research: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
    FILE *json = fopen(report_path, "wx");
    if (json == NULL) { fprintf(stderr, "rss-ddc-research: could not create report %s: %s\n", report_path, strerror(errno)); return EXIT_FAILURE; }
    bool written = rss_ddc_research_write_json(json, &report);
    if (fclose(json) != 0 || !written) { fprintf(stderr, "rss-ddc-research: could not write report\n"); return EXIT_FAILURE; }
    rss_ddc_research_print_summary(stdout, &report, report_path);
    return EXIT_SUCCESS;
}
