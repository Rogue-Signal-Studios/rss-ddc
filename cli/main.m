@import Foundation;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rss_ddc.h"

static void usage(const char *program) {
    fprintf(stderr, "Usage:\n  %s list\n  %s info <display-index>\n  %s [--verbose] get <display-index> <vcp>\n  %s set <display-index> <vcp> <value>\n",
            program, program, program, program);
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

/** Parses only the small public CLI surface; GET is the sole hardware-facing command. */
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
        RSSDDCError error = rss_ddc_get_display((uint32_t)display_index, &display);
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
    if (strcmp(argv[argument], "set") == 0 && argc == argument + 4) {
        unsigned long value = 0;
        if (!parse_unsigned(argv[argument + 3], UINT16_MAX, &value)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        RSSDDCError error = rss_ddc_set_vcp((uint32_t)display_index, (uint8_t)vcp, (uint16_t)value);
        fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
        return error == RSS_DDC_OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    usage(argv[0]);
    return EXIT_FAILURE;
}
