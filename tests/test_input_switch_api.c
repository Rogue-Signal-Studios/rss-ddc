#include <assert.h>
#include <stdio.h>

#include "rss_ddc.h"

static unsigned int standard_calls, alternate_calls;
static uint8_t standard_vcp;
static uint16_t standard_value, alternate_value;

RSSDDCError rss_ddc_set_vcp_with_diagnostics(uint32_t index, uint8_t vcp, uint16_t value,
                                              const RSSDDCDiagnostics *diagnostics) {
    (void)index; (void)diagnostics;
    ++standard_calls;
    standard_vcp = vcp;
    standard_value = value;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_set_lg_alt_input_snapshot(uint32_t index, uint16_t value,
                                                const RSSDDCDiagnostics *diagnostics) {
    (void)index; (void)diagnostics;
    ++alternate_calls;
    alternate_value = value;
    return RSS_DDC_OK;
}

int main(void) {
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_STANDARD, 0x11) == RSS_DDC_OK);
    assert(standard_calls == 1 && standard_vcp == 0x60 && standard_value == 0x11);
    assert(alternate_calls == 0);
    assert(rss_ddc_set_input(4, (RSSDDCInputSwitchMethod)99, 0x90) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_LG_ALT, 0x11) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_LG_ALT, 0x100) == RSS_DDC_ERROR_ARGUMENT);
    assert(alternate_calls == 0); /* rejected values never enter the platform path */
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_LG_ALT, 0x90) == RSS_DDC_OK);
    assert(alternate_calls == 1 && alternate_value == 0x90 && standard_calls == 1);
    puts("test_input_switch_api: passed");
    return 0;
}
