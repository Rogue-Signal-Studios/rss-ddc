#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "rss_ddc.h"

typedef struct {
    RSSDDCProvider provider;
} ProviderRow;

static void print_capability(uint32_t capabilities, RSSDDCCapability capability) {
    printf("%s", (capabilities & capability) != 0 ? "true" : "false");
}

int main(void) {
    const ProviderRow providers[] = {
        {RSS_DDC_PROVIDER_PS190},
        {RSS_DDC_PROVIDER_DCPDP13},
        {RSS_DDC_PROVIDER_DCPDP_SERVICE},
        {RSS_DDC_PROVIDER_MCDP29XX},
    };

    printf("{\n  \"version\": \"%d.%d.%d\",\n  \"providers\": [\n",
           RSS_DDC_VERSION_MAJOR, RSS_DDC_VERSION_MINOR, RSS_DDC_VERSION_PATCH);
    for (size_t index = 0; index < sizeof(providers) / sizeof(providers[0]); ++index) {
        uint32_t capabilities = rss_ddc_provider_capabilities(providers[index].provider);
        printf("    {\"name\": \"%s\", \"capabilities\": {\"get\": ",
               rss_ddc_provider_string(providers[index].provider));
        print_capability(capabilities, RSS_DDC_CAP_GET_VCP);
        printf(", \"set\": "); print_capability(capabilities, RSS_DDC_CAP_SET_VCP);
        printf(", \"edid\": "); print_capability(capabilities, RSS_DDC_CAP_READ_EDID);
        printf(", \"dpcd\": "); print_capability(capabilities, RSS_DDC_CAP_READ_DPCD);
        printf(", \"mccs\": "); print_capability(capabilities, RSS_DDC_CAP_MCCS_CAPABILITIES);
        printf(", \"alternateInput\": "); print_capability(capabilities, RSS_DDC_CAP_ALTERNATE_INPUT);
        printf("}}%s\n", index + 1 == sizeof(providers) / sizeof(providers[0]) ? "" : ",");
    }
    printf("  ]\n}\n");
    return 0;
}
