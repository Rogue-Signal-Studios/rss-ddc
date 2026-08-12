@import Foundation;

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rss_ddc.h"

static void usage(const char *program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s list\n"
            "  %s [--verbose] info <display-index>\n"
            "  %s [--verbose] edid <display-index> [--decode|--hex|--raw <file>]\n"
            "  %s [--verbose] get <display-index> <vcp>\n"
            "  %s [--verbose] set <display-index> <vcp> <value>\n"
            "  %s [--verbose] set <display-index> <vcp> <value> --verify [--settle-ms <ms>] "
            "[--retries <count>] [--retry-delay-ms <ms>]\n",
            program, program, program, program, program, program);
}

static bool parse_unsigned(const char *text, unsigned long maximum, unsigned long *value) {
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) return false;
    *value = parsed;
    return true;
}

static void print_display(const RSSDDCDisplay *display) {
    printf("%u  %s  provider=%s  capabilities=0x%02x  cg=%u\n", display->list_index,
           display->product_name, rss_ddc_provider_string(display->provider), display->capabilities,
           display->cg_display_id);
}

/** CLI adapter for portable diagnostics; stderr preserves concise script-friendly stdout. */
static void write_diagnostic(void *context, const char *message) {
    (void)context;
    fprintf(stderr, "rss-ddc: %s\n", message);
}

static void print_edid_decode(const RSSDDCEDIDInfo *info) {
    printf("manufacturer: %s\nproduct-code: 0x%04x\n", info->manufacturer_id, info->product_code);
    printf("serial: %s\n", info->serial_number_present ? "present" : "unavailable");
    if (info->serial_number_present) printf("serial-number: %u\n", info->serial_number);
    if (info->serial_text[0] != '\0') printf("serial-text: %s\n", info->serial_text);
    if (info->monitor_name[0] != '\0') printf("monitor-name: %s\n", info->monitor_name);
    printf("edid-version: %u.%u\nsize-cm: %ux%u\nextensions: %u declared, %zu received\nchecksum: valid\n",
           info->version, info->revision, info->width_cm, info->height_cm, info->declared_extension_count,
           info->received_block_count > 0 ? info->received_block_count - 1 : 0);
    if (!info->extensions_complete) printf("extensions-complete: no (base-block acquisition only)\n");
    for (size_t block = 1; block < info->received_block_count; ++block) {
        printf("extension-%zu-tag: 0x%02x\n", block, info->extension_tags[block - 1]);
    }
}

static void print_edid_hex(const RSSDDCEDID *edid) {
    for (size_t index = 0; index < edid->length; ++index) {
        if (index % 16 == 0) printf("%04zx: ", index);
        printf("%02x%s", edid->bytes[index], index % 16 == 15 || index + 1 == edid->length ? "\n" : " ");
    }
}

/** Parses the small public CLI surface; hardware access is limited to explicit GET/SET/EDID commands. */
int main(int argc, char **argv) {
    bool verbose = false;
    int argument = 1;
    if (argc > argument && strcmp(argv[argument], "--verbose") == 0) {
        verbose = true;
        ++argument;
    }
    if (argc <= argument) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "list") == 0) {
        RSSDDCDisplay displays[16] = {};
        size_t count = 0;
        RSSDDCError error = rss_ddc_list_displays(displays, 16, &count);
        if (error != RSS_DDC_OK) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            return EXIT_FAILURE;
        }
        for (size_t index = 0; index < count; ++index) print_display(&displays[index]);
        return EXIT_SUCCESS;
    }
    unsigned long display_index = 0;
    if (argc <= argument + 1 || !parse_unsigned(argv[argument + 1], UINT32_MAX, &display_index) || display_index == 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "info") == 0) {
        RSSDDCDisplay display = {};
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_get_display_with_diagnostics((uint32_t)display_index, &display,
                                                                  verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            return EXIT_FAILURE;
        }
        print_display(&display);
        printf("online=%s external=%s branch=%s transport=%s\n", display.online ? "yes" : "no",
               display.external ? "yes" : "no", display.branch_device_id, display.transport);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "edid") == 0) {
        bool hex = false;
        const char *raw_path = NULL;
        if (argc > argument + 4 || (argc == argument + 4 &&
            strcmp(argv[argument + 2], "--raw") != 0)) { usage(argv[0]); return EXIT_FAILURE; }
        if (argc == argument + 3) {
            if (strcmp(argv[argument + 2], "--hex") == 0) hex = true;
            else if (strcmp(argv[argument + 2], "--decode") != 0) { usage(argv[0]); return EXIT_FAILURE; }
        } else if (argc == argument + 4) raw_path = argv[argument + 3];
        RSSDDCEDID edid = {};
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_read_edid_with_diagnostics((uint32_t)display_index, &edid,
                                                                verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        RSSDDCEDIDInfo info = {};
        error = rss_ddc_parse_edid(&edid, &info);
        if (error != RSS_DDC_OK) { fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error)); return EXIT_FAILURE; }
        if (raw_path != NULL) {
            int file = open(raw_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (file < 0 || write(file, edid.bytes, edid.length) != (ssize_t)edid.length || close(file) != 0) {
                if (file >= 0) close(file);
                fprintf(stderr, "rss-ddc: could not create raw EDID file\n");
                return EXIT_FAILURE;
            }
            printf("wrote %zu bytes to %s\n", edid.length, raw_path);
        } else if (hex) print_edid_hex(&edid);
        else print_edid_decode(&info);
        return EXIT_SUCCESS;
    }
    unsigned long vcp = 0;
    if (argc <= argument + 2 || !parse_unsigned(argv[argument + 2], UINT8_MAX, &vcp)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[argument], "get") == 0) {
        RSSDDCVCPResult result = {};
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        RSSDDCError error = rss_ddc_get_vcp_with_diagnostics((uint32_t)display_index, (uint8_t)vcp, &result,
                                                              verbose ? &diagnostics : NULL);
        if (error != RSS_DDC_OK) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            return EXIT_FAILURE;
        }
        /* Normal mode is intentionally machine-friendly; verbose detail was sent to stderr. */
        printf("%u\n", result.current_value);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[argument], "set") == 0 && argc >= argument + 4) {
        unsigned long value = 0;
        if (!parse_unsigned(argv[argument + 3], UINT16_MAX, &value)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        RSSDDCDiagnostics diagnostics = {.callback = write_diagnostic, .context = NULL};
        bool verify = false;
        bool has_policy_option = false;
        RSSDDCVerifyPolicy policy = rss_ddc_default_verify_policy();
        for (int option = argument + 4; option < argc; ++option) {
            if (strcmp(argv[option], "--verify") == 0 && !verify) {
                verify = true;
                continue;
            }
            uint32_t *destination = NULL;
            if (strcmp(argv[option], "--settle-ms") == 0) destination = &policy.settle_ms;
            else if (strcmp(argv[option], "--retries") == 0) destination = &policy.retry_count;
            else if (strcmp(argv[option], "--retry-delay-ms") == 0) destination = &policy.retry_delay_ms;
            if (destination == NULL || ++option >= argc) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            unsigned long parsed = 0;
            if (!parse_unsigned(argv[option], UINT32_MAX, &parsed)) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            *destination = (uint32_t)parsed;
            has_policy_option = true;
        }
        if (has_policy_option && !verify) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        RSSDDCError error;
        if (verify) {
            RSSDDCVCPResult result = {};
            error = rss_ddc_set_vcp_and_verify_with_diagnostics((uint32_t)display_index, (uint8_t)vcp,
                                                                 (uint16_t)value, &policy, &result,
                                                                 verbose ? &diagnostics : NULL);
            if (error == RSS_DDC_OK) printf("verified %u\n", result.current_value);
        } else {
            error = rss_ddc_set_vcp_with_diagnostics((uint32_t)display_index, (uint8_t)vcp,
                                                      (uint16_t)value, verbose ? &diagnostics : NULL);
        }
        if (error != RSS_DDC_OK) fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    usage(argv[0]);
    return EXIT_FAILURE;
}
