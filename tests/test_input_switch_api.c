#include <assert.h>
#include <stdio.h>

#include "macos_internal.h"

static unsigned int standard_calls, resolve_calls, release_calls, alt_calls;
static uint8_t standard_vcp;
static uint16_t standard_value, alt_value;
static RSSDDCProvider resolved_provider = RSS_DDC_PROVIDER_DCPDP13;

RSSDDCError rss_ddc_set_vcp_with_diagnostics(uint32_t index, uint8_t vcp, uint16_t value,
                                              const RSSDDCDiagnostics *diagnostics) {
    (void)index; (void)diagnostics; ++standard_calls; standard_vcp = vcp; standard_value = value; return RSS_DDC_OK;
}
RSSDDCError rss_macos_resolve_binding(uint32_t index, RSSMacOSBinding *binding) {
    (void)index; ++resolve_calls; binding->display.provider = resolved_provider; return RSS_DDC_OK;
}
void rss_macos_release_binding(RSSMacOSBinding *binding) { (void)binding; ++release_calls; }
RSSDDCError rss_macos_provider_set_lg_alt_input(RSSMacOSBinding *binding, uint16_t value,
                                                 const RSSDDCDiagnostics *diagnostics) {
    (void)diagnostics; ++alt_calls; alt_value = value;
    return binding->display.provider == RSS_DDC_PROVIDER_DCPDP13 ? RSS_DDC_OK : RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
void rss_macos_diagnostic(const RSSDDCDiagnostics *diagnostics, const char *message) { (void)diagnostics; (void)message; }
const char *rss_macos_correlation_failure_string(RSSMacOSCorrelationFailure failure) { (void)failure; return "failed"; }
const char *rss_macos_correlation_detail_string(const RSSMacOSBinding *binding) { (void)binding; return NULL; }

int main(void) {
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_STANDARD, 0x11) == RSS_DDC_OK);
    assert(standard_calls == 1 && standard_vcp == 0x60 && standard_value == 0x11);
    assert(resolve_calls == 0 && alt_calls == 0 && release_calls == 0);
    assert(rss_ddc_set_input(4, (RSSDDCInputSwitchMethod)99, 0x11) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_LG_ALT, 0x100) == RSS_DDC_ERROR_ARGUMENT);
    assert(resolve_calls == 0 && alt_calls == 0 && release_calls == 0);
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_LG_ALT, 0x90) == RSS_DDC_OK);
    assert(resolve_calls == 1 && alt_calls == 1 && alt_value == 0x90 && release_calls == 1);
    resolved_provider = RSS_DDC_PROVIDER_DCPDP_SERVICE;
    assert(rss_ddc_set_input(4, RSS_DDC_INPUT_SWITCH_LG_ALT, 0x90) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(resolve_calls == 2 && alt_calls == 2 && release_calls == 2);
    puts("test_input_switch_api: passed");
    return 0;
}
