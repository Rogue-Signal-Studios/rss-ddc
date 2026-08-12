#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rss_ddc.h"

int main(void) {
    assert(rss_ddc_provider_from_registry_class("DCPDP13Service") == RSS_DDC_PROVIDER_DCPDP13);
    assert(rss_ddc_provider_from_registry_class("AppleDCPMCDP29XX") == RSS_DDC_PROVIDER_MCDP29XX);
    assert(rss_ddc_provider_from_registry_class("AppleDCPPS190") == RSS_DDC_PROVIDER_PS190);
    assert(rss_ddc_provider_from_registry_class("Other") == RSS_DDC_PROVIDER_UNKNOWN);
    assert(rss_ddc_provider_backend(RSS_DDC_PROVIDER_DCPDP13) == RSS_DDC_BACKEND_DCPDP13);
    assert(rss_ddc_provider_backend(RSS_DDC_PROVIDER_MCDP29XX) == RSS_DDC_BACKEND_MCDP29XX);
    assert(rss_ddc_provider_backend(RSS_DDC_PROVIDER_PS190) == RSS_DDC_BACKEND_PS190);
    assert(rss_ddc_provider_backend(RSS_DDC_PROVIDER_UNKNOWN) == RSS_DDC_BACKEND_UNSUPPORTED);
    assert(strcmp(rss_ddc_backend_name(RSS_DDC_BACKEND_DCPDP13), "DCPDP13Service") == 0);
    assert(rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_PS190) ==
           (RSS_DDC_CAP_GET_VCP | RSS_DDC_CAP_SET_VCP));
    assert(rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_DCPDP13) ==
           (RSS_DDC_CAP_GET_VCP | RSS_DDC_CAP_SET_VCP));
    assert(rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_MCDP29XX) == RSS_DDC_CAP_NONE);
    assert((rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_DCPDP13) & RSS_DDC_CAP_SET_VCP) != 0);
    assert((rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_MCDP29XX) & RSS_DDC_CAP_SET_VCP) == 0);
    assert((rss_ddc_provider_capabilities(RSS_DDC_PROVIDER_UNKNOWN) & RSS_DDC_CAP_SET_VCP) == 0);
    puts("test_provider: passed");
    return 0;
}
