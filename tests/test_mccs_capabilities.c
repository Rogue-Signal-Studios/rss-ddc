#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rss_ddc.h"

static RSSDDCMCCSCapabilities parse(const char *raw) {
    RSSDDCMCCSCapabilities capabilities = {};
    assert(rss_ddc_parse_mccs_capabilities(raw, strlen(raw), &capabilities) == RSS_DDC_OK);
    return capabilities;
}

static void expect_rejected(const char *raw, size_t length) {
    RSSDDCMCCSCapabilities untouched;
    memset(&untouched, 0xa5, sizeof(untouched));
    RSSDDCMCCSCapabilities before = untouched;
    assert(rss_ddc_parse_mccs_capabilities(raw, length, &untouched) != RSS_DDC_OK);
    assert(memcmp(&untouched, &before, sizeof(untouched)) == 0);
}

int main(void) {
    RSSDDCMCCSCapabilities simple = parse("prot(monitor)type(lcd)vcp(10 12)");
    assert(simple.raw_length == strlen(simple.raw));
    assert(simple.feature_count == 2);
    assert(rss_ddc_mccs_capabilities_has_vcp(&simple, 0x10));
    assert(rss_ddc_mccs_capabilities_has_vcp(&simple, 0x12));
    assert(!rss_ddc_mccs_capabilities_has_vcp(&simple, 0x60));

    const char non_terminated[] = {'v', 'c', 'p', '(', '6', '0', '(', '0', 'f', ' ', '1', '1', ')', ')'};
    RSSDDCMCCSCapabilities enumerated = {};
    assert(rss_ddc_parse_mccs_capabilities(non_terminated, sizeof(non_terminated), &enumerated) == RSS_DDC_OK);
    const uint8_t *values = NULL;
    size_t count = 0;
    assert(rss_ddc_mccs_capabilities_enum_values(&enumerated, 0x60, &values, &count) == RSS_DDC_OK);
    assert(count == 2 && values[0] == 0x0f && values[1] == 0x11);

    RSSDDCMCCSCapabilities mixed = parse(
        " ( prot(monitor)\ttype(lcd)unknown-token(alpha(beta))\nVCP(10 60(0F 11 12) d6(01 04 05)) ) ");
    assert(mixed.feature_count == 3 && strstr(mixed.raw, "unknown-token") != NULL);
    assert(rss_ddc_mccs_capabilities_enum_values(&mixed, 0xd6, &values, &count) == RSS_DDC_OK);
    assert(count == 3 && values[0] == 1 && values[1] == 4 && values[2] == 5);
    assert(rss_ddc_mccs_capabilities_enum_values(&mixed, 0x10, &values, &count) == RSS_DDC_OK && count == 0);
    assert(rss_ddc_mccs_capabilities_enum_values(&mixed, 0x99, &values, &count) == RSS_DDC_ERROR_NOT_FOUND);

    char deeply_nested[128] = "opaque(";
    for (size_t index = 0; index < RSS_DDC_MCCS_CAPABILITIES_MAX_NESTING - 1; ++index) strcat(deeply_nested, "(");
    strcat(deeply_nested, "x");
    for (size_t index = 0; index < RSS_DDC_MCCS_CAPABILITIES_MAX_NESTING - 1; ++index) strcat(deeply_nested, ")");
    strcat(deeply_nested, ")vcp(10)");
    assert(rss_ddc_parse_mccs_capabilities(deeply_nested, strlen(deeply_nested), &simple) == RSS_DDC_OK);

    const char *malformed[] = {
        "", "vcp(10 12", "vcp(1)", "vcp(60(0f zz))", "vcp(60(0f(11)))", "vcp(60())",
        "vcp(10 10)", "vcp(10)truncated", "vcp(10))", "vcp(100)", "vcp(60(0f 1))",
    };
    for (size_t index = 0; index < sizeof(malformed) / sizeof(malformed[0]); ++index) {
        expect_rejected(malformed[index], strlen(malformed[index]));
    }
    expect_rejected("vcp(10 12)", 8);
    const char embedded_nul[] = {'v', 'c', 'p', '(', '1', '0', '\0', ')'};
    expect_rejected(embedded_nul, sizeof(embedded_nul));

    static char too_large[RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES + 1];
    memset(too_large, 'x', sizeof(too_large));
    expect_rejected(too_large, sizeof(too_large));
    static char long_token[1024];
    memset(long_token, 'a', sizeof(long_token));
    long_token[sizeof(long_token) - 10] = '(';
    long_token[sizeof(long_token) - 9] = 'x';
    long_token[sizeof(long_token) - 8] = ')';
    memcpy(long_token + sizeof(long_token) - 7, "vcp(10)", 7);
    assert(rss_ddc_parse_mccs_capabilities(long_token, sizeof(long_token), &simple) == RSS_DDC_OK);
    assert(simple.feature_count == 1 && simple.features[0].vcp_code == 0x10);

    assert(rss_ddc_parse_mccs_capabilities("vcp(10)", 7, NULL) == RSS_DDC_ERROR_ARGUMENT);
    puts("test_mccs_capabilities: passed");
    return 0;
}
