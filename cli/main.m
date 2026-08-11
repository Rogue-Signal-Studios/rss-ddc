@import Foundation;

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rss_ddc.h"

static void usage(const char *program) {
    fprintf(stderr, "Usage:\n  %s list\n  %s info <display-index>\n  %s get <display-index> <vcp>\n  %s set <display-index> <vcp> <value>\n",
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

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "list") == 0) {
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
    if (argc < 3 || !parse_unsigned(argv[2], UINT32_MAX, &display_index) || display_index == 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "info") == 0) {
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
    if (argc < 4 || !parse_unsigned(argv[3], UINT8_MAX, &vcp)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "get") == 0) {
        RSSDDCVCPResult result = {};
        RSSDDCError error = rss_ddc_get_vcp((uint32_t)display_index, (uint8_t)vcp, &result);
        if (error != RSS_DDC_OK) {
            fprintf(stderr, "rss-ddc: %s\n", rss_ddc_error_string(error));
            return EXIT_FAILURE;
        }
        printf("vcp=0x%02x maximum=%u current=%u\n", result.vcp_code, result.maximum_value, result.current_value);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[1], "set") == 0 && argc == 5) {
        unsigned long value = 0;
        if (!parse_unsigned(argv[4], UINT16_MAX, &value)) {
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
