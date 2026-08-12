@import Foundation;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rss_ddc.h"

static void usage(const char *program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s list\n"
            "  %s [--verbose] info <display-index>\n"
            "  %s [--verbose] get <display-index> <vcp>\n"
            "  %s [--verbose] set <display-index> <vcp> <value>\n"
            "  %s [--verbose] set <display-index> <vcp> <value> --verify [--settle-ms <ms>] "
            "[--retries <count>] [--retry-delay-ms <ms>]\n",
            program, program, program, program, program);
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

/** Parses the small public CLI surface; hardware access is limited to explicit GET/SET commands. */
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
