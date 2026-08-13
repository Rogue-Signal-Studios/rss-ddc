@import Foundation;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "discovery.h"
#include "compare.h"

static void global_usage(FILE *output, const char *program) {
    fprintf(output, "Usage:\n  %s discover [options]\n  %s compare <report.json> <report.json> [more-report.json ...] [--report <comparison.json>]\n\nRun '%s discover --help' or '%s compare --help' for subcommand options.\n", program, program, program, program);
}

static void discover_usage(FILE *output, const char *program) {
    fprintf(output,
            "Usage: %s discover --display <index> [options]\n"
            "\nRead-only discovery (default):\n"
            "  --fingerprint                  Read advertised VCPs only (bounded; no full 0x00-0xFF scan).\n"
            "  --category <all|picture>       Select advertised/all or standard-picture candidates.\n"
            "  --reads <1..10>                GET samples per candidate (default: 1).\n"
            "  --label <known-osd-state>      Store a human OSD-state label in JSON.\n"
            "  --vcp <code[,code...]>         Add explicit bounded GET candidates.\n"
            "  --range <first:last>           Add an explicit range of at most 64 VCPs.\n"
            "  --report <file>                Exclusively create a JSON report (recommended; optional).\n"
            "\nControlled mutation (never default):\n"
            "  --allow-set                    Required before any SET operation.\n"
            "  --values <value[,value...]>    Required SET candidate values.\n"
            "  --restore                      Restore and verify the original value (default with --allow-set).\n"
            "  --no-restore                   One-shot SET: exactly one --vcp and one --values entry; leaves state changed.\n"
            "  --settle-ms <0..60000>         Bounded delay before verification GETs.\n"
            "\nSafety: read-only is the default. SET requires --allow-set, explicit --vcp, and --values.\n"
            "Input/source (0x60), power (0xD6), reset (0x04/0x05/0x08), degauss (0x01),\n"
            "and LG alternate input (0xF4) are always denied from generic mutation.\n"
            "--no-restore intentionally leaves the monitor changed; it still performs one bounded observation GET.\n"
            "\nExamples:\n"
            "  %s discover --display 2 --fingerprint --reads 3 --label FPS --report /tmp/fps.json\n"
            "  %s discover --display 2 --allow-set --vcp 0x15 --values 0x31 --restore\n"
            "  %s discover --display 2 --allow-set --vcp 0x15 --values 0x31 --no-restore\n",
            program, program, program, program);
}

static void compare_usage(FILE *output, const char *program) {
    fprintf(output, "Usage: %s compare <report.json> <report.json> [more-report.json ...] [--report <comparison.json>]\n\nCompare is offline-only: it reads JSON reports and never opens a display or performs GET/SET.\nExample: %s compare a.json b.json\n", program, program);
}

static int discover_error(const char *program, const char *message) {
    fprintf(stderr, "rss-ddc-research: %s\nRun '%s discover --help' for usage.\n", message, program);
    return EXIT_FAILURE;
}

static int compare_error(const char *program, const char *message) {
    fprintf(stderr, "rss-ddc-research: %s\nRun '%s compare --help' for usage.\n", message, program);
    return EXIT_FAILURE;
}

static bool option_requires_value(const char *option) {
    return strcmp(option, "--display") == 0 || strcmp(option, "--report") == 0 || strcmp(option, "--category") == 0 ||
        strcmp(option, "--reads") == 0 || strcmp(option, "--vcp") == 0 || strcmp(option, "--values") == 0 ||
        strcmp(option, "--range") == 0 || strcmp(option, "--settle-ms") == 0 || strcmp(option, "--label") == 0;
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
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { global_usage(stdout, argv[0]); return EXIT_SUCCESS; }
    if (argc >= 2 && strcmp(argv[1], "compare") == 0) {
        if (argc == 3 && strcmp(argv[2], "--help") == 0) { compare_usage(stdout, argv[0]); return EXIT_SUCCESS; }
        const char *paths[16] = {};
        int path_count = 0;
        const char *comparison_path = NULL;
        for (int argument = 2; argument < argc; ++argument) {
            if (strcmp(argv[argument], "--report") == 0) {
                if (comparison_path != NULL || ++argument >= argc) return compare_error(argv[0], "--report requires exactly one path");
                comparison_path = argv[argument];
            } else if (argv[argument][0] != '-' && path_count < (int)(sizeof(paths) / sizeof(paths[0]))) {
                paths[path_count++] = argv[argument];
            } else {
                return compare_error(argv[0], "unknown option or too many report paths");
            }
        }
        if (path_count < 2) return compare_error(argv[0], "compare requires at least two report paths");
        return rss_ddc_research_compare_files(path_count, paths, comparison_path, stdout, stderr) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && strcmp(argv[1], "discover") == 0 && argc == 3 && strcmp(argv[2], "--help") == 0) {
        discover_usage(stdout, argv[0]); return EXIT_SUCCESS;
    }
    if (argc < 2 || strcmp(argv[1], "discover") != 0) { global_usage(stderr, argv[0]); return EXIT_FAILURE; }
    RSSDDCResearchOptions options = {.category = RSS_DDC_RESEARCH_CATEGORY_ALL, .reads = 1, .restore = true};
    unsigned long display = 0;
    const char *report_path = NULL;
    const char *label = NULL;
    bool have_display = false;
    bool restore_seen = false;
    bool no_restore_seen = false;
    bool settle_seen = false;
    for (int argument = 2; argument < argc; ++argument) {
        const char *option = argv[argument];
        if (strcmp(option, "--help") == 0) return discover_error(argv[0], "--help must be the only discover option");
        if (strcmp(option, "--allow-set") == 0) {
            if (options.allow_set) return discover_error(argv[0], "--allow-set was supplied more than once");
            options.allow_set = true; continue;
        }
        if (strcmp(option, "--restore") == 0) {
            if (restore_seen || no_restore_seen) return discover_error(argv[0], "--restore and --no-restore are mutually exclusive");
            restore_seen = true; options.restore = true; continue;
        }
        if (strcmp(option, "--no-restore") == 0) {
            if (no_restore_seen || restore_seen) return discover_error(argv[0], "--restore and --no-restore are mutually exclusive");
            no_restore_seen = true; options.restore = false; continue;
        }
        if (strcmp(option, "--fingerprint") == 0) { options.category = RSS_DDC_RESEARCH_CATEGORY_ALL; continue; }
        if (option_requires_value(option)) {
            if (++argument >= argc) {
                char message[96] = {};
                snprintf(message, sizeof(message), "%s requires an argument", option);
                return discover_error(argv[0], message);
            }
            const char *value = argv[argument];
            unsigned long parsed = 0;
            if (strcmp(option, "--display") == 0) {
                if (!rss_ddc_research_parse_unsigned(value, UINT32_MAX, &display) || display == 0) return discover_error(argv[0], "invalid --display (expected a positive integer)");
                have_display = true;
            } else if (strcmp(option, "--report") == 0) report_path = value;
            else if (strcmp(option, "--label") == 0) {
                if (value[0] == '\0' || strlen(value) >= RSS_DDC_TEXT_MAX) return discover_error(argv[0], "invalid --label");
                label = value;
            }
            else if (strcmp(option, "--category") == 0) {
                if (!rss_ddc_research_parse_category(value, &options.category)) return discover_error(argv[0], "invalid --category (expected all or picture)");
            } else if (strcmp(option, "--reads") == 0) {
                if (!rss_ddc_research_parse_unsigned(value, RSS_DDC_RESEARCH_MAX_READS, &parsed) || parsed == 0) return discover_error(argv[0], "invalid --reads (expected 1..10)");
                options.reads = (unsigned int)parsed;
            } else if (strcmp(option, "--vcp") == 0) {
                if (!parse_list(value, UINT8_MAX, true, &options)) return discover_error(argv[0], "invalid --vcp list");
            } else if (strcmp(option, "--values") == 0) {
                if (!parse_list(value, UINT16_MAX, false, &options)) return discover_error(argv[0], "invalid --values list");
            } else if (strcmp(option, "--range") == 0) {
                if (!parse_range(value, &options)) return discover_error(argv[0], "invalid --range (maximum 64 VCPs)");
            } else {
                if (!rss_ddc_research_parse_unsigned(value, 60000, &parsed)) return discover_error(argv[0], "invalid --settle-ms (maximum 60000)");
                options.settle_ms = (uint32_t)parsed;
                settle_seen = true;
            }
            continue;
        }
        char message[96] = {};
        snprintf(message, sizeof(message), "unknown option: %s", option);
        return discover_error(argv[0], message);
    }
    if (!have_display) return discover_error(argv[0], "--display is required");
    if ((restore_seen || no_restore_seen || options.mutation_value_count != 0 || settle_seen) && !options.allow_set) {
        return discover_error(argv[0], "mutation options require --allow-set");
    }
    RSSDDCError error = rss_ddc_research_validate_options(&options);
    if (error != RSS_DDC_OK) {
        return discover_error(argv[0], "research safety gate rejected the requested mutation (SET requires explicit authorization, candidates, and no denylisted VCP)");
    }

    RSSDDCResearchReport report = {};
    report.display.list_index = (uint32_t)display;
    error = rss_ddc_get_display((uint32_t)display, &report.display);
    if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc-research: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
    report.capabilities_status = rss_ddc_get_mccs_capabilities((uint32_t)display, &report.capabilities);
    if (report.capabilities_status != RSS_DDC_OK) snprintf(report.warnings[report.warning_count++], sizeof(report.warnings[0]), "MCCS capabilities unavailable: %s", rss_ddc_error_string(report.capabilities_status));
    timestamp_utc(report.timestamp);
    if (label != NULL) snprintf(report.label, sizeof(report.label), "%s", label);
    uint32_t display_index = (uint32_t)display;
    RSSDDCResearchTransport transport = {.get_vcp = transport_get, .set_vcp = transport_set, .settle = transport_settle, .context = &display_index};
    error = rss_ddc_research_run(&report, &options, &transport);
    if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc-research: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
    if (report_path != NULL) {
        FILE *json = fopen(report_path, "wx");
        if (json == NULL) { fprintf(stderr, "rss-ddc-research: could not create report %s: %s\n", report_path, strerror(errno)); return EXIT_FAILURE; }
        bool written = rss_ddc_research_write_json(json, &report);
        if (fclose(json) != 0 || !written) { fprintf(stderr, "rss-ddc-research: could not write report\n"); return EXIT_FAILURE; }
    }
    rss_ddc_research_print_summary(stdout, &report, report_path);
    return EXIT_SUCCESS;
}
