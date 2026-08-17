/*
 * Minimal out-of-tree consumer fixture. It deliberately includes only the
 * installed public header and is compiled/linked but never run by CI.
 */
#include <rss_ddc.h>

#include <stdio.h>
#include <string.h>

static RSSDDCError list_displays_for_an_application(void) {
    size_t count = 0;

    return rss_ddc_list_displays(NULL, 0, &count);
}

int main(int argc, char *argv[]) {
    const char *error_name = rss_ddc_error_string(RSS_DDC_OK);
    const char *provider_name = rss_ddc_provider_string(RSS_DDC_PROVIDER_PS190);
    const char *interaction_name = rss_ddc_characterization_interaction_kind_name(
        rss_ddc_characterization_next_interaction(NULL));
    const char *wait_name =
        rss_ddc_characterization_action_name(RSS_DDC_CHARACTERIZATION_ACTION_WAIT_FOR_INTERACTION);

    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        return list_displays_for_an_application() == RSS_DDC_OK ? 0 : 1;
    }

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--version") == 0)) {
        printf("rss-ddc API %d.%d.%d (%s, %s, %s, %s)\n", RSS_DDC_VERSION_MAJOR,
               RSS_DDC_VERSION_MINOR, RSS_DDC_VERSION_PATCH, error_name,
               provider_name, interaction_name, wait_name);
        return 0;
    }

    return 2;
}
