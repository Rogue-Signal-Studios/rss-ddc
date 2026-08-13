/*
 * Minimal out-of-tree consumer fixture. It deliberately includes only the
 * installed public header and is compiled/linked but never run by CI.
 */
#include <rss_ddc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RSSDDCError list_displays_for_an_application(void) {
    size_t count = 0;

    return rss_ddc_list_displays(NULL, 0, &count);
}

static RSSDDCError read_raw_input_candidates(uint32_t display_index) {
    RSSDDCMCCSCapabilities capabilities = {};
    RSSDDCError error = rss_ddc_get_mccs_capabilities(display_index, &capabilities);
    if (error != RSS_DDC_OK || !rss_ddc_mccs_capabilities_has_vcp(&capabilities, 0x60)) return error;
    const uint8_t *values = NULL;
    size_t count = 0;
    return rss_ddc_mccs_capabilities_enum_values(&capabilities, 0x60, &values, &count);
}

int main(int argc, char *argv[]) {
    const char *error_name = rss_ddc_error_string(RSS_DDC_OK);
    const char *provider_name = rss_ddc_provider_string(RSS_DDC_PROVIDER_PS190);

    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        return list_displays_for_an_application() == RSS_DDC_OK ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "--capabilities") == 0) {
        return read_raw_input_candidates((uint32_t)strtoul(argv[2], NULL, 10)) == RSS_DDC_OK ? 0 : 1;
    }

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--version") == 0)) {
        printf("rss-ddc API %d.%d.%d (%s, %s)\n", RSS_DDC_VERSION_MAJOR,
               RSS_DDC_VERSION_MINOR, RSS_DDC_VERSION_PATCH, error_name,
               provider_name);
        return 0;
    }

    return 2;
}
