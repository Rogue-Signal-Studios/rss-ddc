#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rss_ddc.h"

static RSSDDCMCCSCapabilities parse(const char *raw) {
    RSSDDCMCCSCapabilities capabilities = {};
    assert(rss_ddc_parse_mccs_capabilities(raw, strlen(raw), &capabilities) == RSS_DDC_OK);
    return capabilities;
}

int main(void) {
    RSSDDCMCCSCapabilities simple = parse("prot(monitor)type(lcd)vcp(10 12)");
    assert(simple.raw_length == strlen(simple.raw));
    assert(simple.feature_count == 2);
    assert(rss_ddc_mccs_capabilities_has_vcp(&simple, 0x10));
    assert(rss_ddc_mccs_capabilities_has_vcp(&simple, 0x12));
    assert(!rss_ddc_mccs_capabilities_has_vcp(&simple, 0x60));

    RSSDDCMCCSCapabilities enumerated = parse("vcp(60(0f 11 12))");
    const uint8_t *values = NULL;
    size_t count = 0;
    assert(rss_ddc_mccs_capabilities_enum_values(&enumerated, 0x60, &values, &count) == RSS_DDC_OK);
    assert(count == 3);
    assert(values[0] == 0x0f && values[1] == 0x11 && values[2] == 0x12);

    const char lg_hardware_capabilities[] =
        "(prot(monitor)type(lcd)QN600cmds(01 02 03 0C E3 F3)vcp(02 04 05 08 10 12 14(05 08 0B ) 16 18 1A "
        "52 60( 11 12 0F 00) AC AE B2 B6 C0 C6 C8 C9 D6(01 04) DF 62 8D F4 F5(00 01 02) F6(00 01 02) "
        "4D 4E 4F 15(01 06 11 13 14 18 28 29 32 48) F7(00 01 02 03) F8(00 01) F9 E4 E5 E6 E7 E8 E9 EA "
        "EB EF FD(00 01) FE(00 01 02) FF)mccs_ver(2.1)mswhql(1))";
    RSSDDCMCCSCapabilities lg = parse(lg_hardware_capabilities);
    assert(rss_ddc_mccs_capabilities_has_vcp(&lg, 0x60));
    assert(rss_ddc_mccs_capabilities_enum_values(&lg, 0x60, &values, &count) == RSS_DDC_OK);
    assert(count == 4 && values[0] == 0x11 && values[1] == 0x12 && values[2] == 0x0f && values[3] == 0x00);

    RSSDDCMCCSCapabilities mixed = parse(
        "(prot(monitor)type(lcd)model(Ignored)cmds(01 E3 F3)vcp(10 12 60(0F 11 12) d6(01 04 05)))");
    assert(mixed.feature_count == 4);
    assert(rss_ddc_mccs_capabilities_enum_values(&mixed, 0xd6, &values, &count) == RSS_DDC_OK);
    assert(count == 3 && values[0] == 1 && values[1] == 4 && values[2] == 5);
    assert(rss_ddc_mccs_capabilities_enum_values(&mixed, 0x10, &values, &count) == RSS_DDC_OK && count == 0);
    assert(rss_ddc_mccs_capabilities_enum_values(&mixed, 0x99, &values, &count) == RSS_DDC_ERROR_NOT_FOUND);

    const char *malformed[] = {
        "vcp(10 12", "vcp(1)", "vcp(60(0f zz))", "vcp(60(0f(11)))", "vcp(60())",
        "vcp(10 10)", "vcp(10)truncated", "vcp(10))",
    };
    for (size_t index = 0; index < sizeof(malformed) / sizeof(malformed[0]); ++index) {
        RSSDDCMCCSCapabilities rejected = {};
        assert(rss_ddc_parse_mccs_capabilities(malformed[index], strlen(malformed[index]), &rejected) ==
               RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    }

    char too_large[RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES + 1] = {};
    memset(too_large, 'x', RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES);
    RSSDDCMCCSCapabilities rejected = {};
    assert(rss_ddc_parse_mccs_capabilities(too_large, sizeof(too_large), &rejected) ==
           RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE);
    assert(rss_ddc_parse_mccs_capabilities("vcp(10)", 7, NULL) == RSS_DDC_ERROR_ARGUMENT);
    puts("test_mccs_capabilities: passed");
    return 0;
}
