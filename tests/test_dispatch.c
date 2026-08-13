#include <assert.h>
#include <stdio.h>

#include "macos_internal.h"

static unsigned int ps190_set_calls;
static unsigned int dp_set_calls;
static unsigned int ps190_get_calls;
static unsigned int dp_get_calls;
static unsigned int dcpdpservice_get_calls;
static unsigned int ps190_edid_calls;
static unsigned int ps190_dpcd_calls;
static unsigned int dp_dpcd_calls;
static unsigned int dp_mccs_calls;
static uint32_t dp_dpcd_address;
static size_t dp_dpcd_length;

void rss_macos_diagnostic(const RSSDDCDiagnostics *diagnostics, const char *message) {
    (void)diagnostics;
    (void)message;
}

RSSDDCError rss_macos_ps190_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                     const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)vcp_code; (void)result; (void)diagnostics;
    ++ps190_get_calls;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_dp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                  const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)vcp_code; (void)result; (void)diagnostics;
    ++dp_get_calls;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_dcpdpservice_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                            const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)vcp_code; (void)result; (void)diagnostics;
    ++dcpdpservice_get_calls;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_mcdp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                    const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)vcp_code; (void)result; (void)diagnostics;
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}

RSSDDCError rss_macos_ps190_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                     const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)vcp_code; (void)value; (void)diagnostics;
    ++ps190_set_calls;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_dp_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                  const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)vcp_code; (void)value; (void)diagnostics;
    ++dp_set_calls;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_ps190_read_edid(RSSMacOSBinding *binding, RSSDDCEDID *edid,
                                      const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)edid; (void)diagnostics;
    ++ps190_edid_calls;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_ps190_read_dpcd(RSSMacOSBinding *binding, uint32_t address, uint8_t *buffer,
                                      size_t length, const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)address; (void)buffer; (void)length; (void)diagnostics;
    ++ps190_dpcd_calls;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_dp_read_dpcd(RSSMacOSBinding *binding, uint32_t address, uint8_t *buffer,
                                   size_t length, const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)buffer; (void)diagnostics;
    ++dp_dpcd_calls;
    dp_dpcd_address = address;
    dp_dpcd_length = length;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_dp_get_mccs_capabilities(RSSMacOSBinding *binding,
                                               RSSDDCMCCSCapabilities *capabilities,
                                               const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)diagnostics;
    ++dp_mccs_calls;
    *capabilities = (RSSDDCMCCSCapabilities){};
    return RSS_DDC_OK;
}

int main(void) {
    RSSMacOSBinding selected_dp = {.display.provider = RSS_DDC_PROVIDER_DCPDP13};
    RSSDDCVCPResult result = {};
    RSSDDCEDID edid = {};
    RSSDDCMCCSCapabilities capabilities = {};
    uint8_t dpcd[16] = {};
    assert(rss_macos_provider_get_vcp(&selected_dp, 0x60, &result, NULL) == RSS_DDC_OK);
    assert(rss_macos_provider_set_vcp(&selected_dp, 0x60, 18, NULL) == RSS_DDC_OK);
    assert(dp_get_calls == 1 && ps190_get_calls == 0 && dp_set_calls == 1 && ps190_set_calls == 0);
    assert(rss_macos_provider_get_mccs_capabilities(&selected_dp, &capabilities, NULL) == RSS_DDC_OK);
    assert(dp_mccs_calls == 1);

    RSSMacOSBinding selected_dcpdpservice = {.display.provider = RSS_DDC_PROVIDER_DCPDP_SERVICE};
    assert(rss_macos_provider_get_vcp(&selected_dcpdpservice, 0x10, &result, NULL) == RSS_DDC_OK);
    assert(rss_macos_provider_set_vcp(&selected_dcpdpservice, 0x10, 62, NULL) == RSS_DDC_OK);
    assert(rss_macos_provider_read_edid(&selected_dcpdpservice, &edid, NULL) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(rss_macos_provider_read_dpcd(&selected_dcpdpservice, 0, dpcd, sizeof(dpcd), NULL) == RSS_DDC_OK);
    assert(dcpdpservice_get_calls == 1 && dp_get_calls == 1 && dp_dpcd_calls == 1);
    assert(dp_set_calls == 2 && ps190_set_calls == 0);
    assert(rss_macos_provider_get_mccs_capabilities(&selected_dcpdpservice, &capabilities, NULL) ==
           RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);

    RSSMacOSBinding selected_ps190 = {.display.provider = RSS_DDC_PROVIDER_PS190};
    assert(rss_macos_provider_get_vcp(&selected_ps190, 0x60, &result, NULL) == RSS_DDC_OK);
    assert(rss_macos_provider_set_vcp(&selected_ps190, 0x60, 18, NULL) == RSS_DDC_OK);
    assert(dp_get_calls == 1 && ps190_get_calls == 1 && dp_set_calls == 2 && ps190_set_calls == 1);
    assert(rss_macos_provider_get_mccs_capabilities(&selected_ps190, &capabilities, NULL) ==
           RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);

    RSSMacOSBinding selected_mcdp = {.display.provider = RSS_DDC_PROVIDER_MCDP29XX};
    assert(rss_macos_provider_read_edid(&selected_ps190, &edid, NULL) == RSS_DDC_OK);
    assert(ps190_edid_calls == 1 && ps190_get_calls == 1 && dp_get_calls == 1);
    assert(rss_macos_provider_read_edid(&selected_dp, &edid, NULL) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(rss_macos_provider_read_edid(&selected_dcpdpservice, &edid, NULL) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(dp_get_calls == 1 && ps190_edid_calls == 1);
    assert(rss_macos_provider_read_dpcd(&selected_ps190, 0, dpcd, sizeof(dpcd), NULL) == RSS_DDC_OK);
    assert(ps190_dpcd_calls == 1);
    assert(rss_macos_provider_read_dpcd(&selected_dp, 0x200, dpcd, 8, NULL) == RSS_DDC_OK);
    assert(ps190_dpcd_calls == 1 && dp_dpcd_calls == 2);
    assert(dp_dpcd_address == 0x200 && dp_dpcd_length == 8);
    assert(rss_macos_provider_read_dpcd(&selected_mcdp, 0, dpcd, sizeof(dpcd), NULL) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(rss_macos_provider_set_vcp(&selected_mcdp, 0x60, 18, NULL) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    RSSMacOSBinding selected_unknown = {.display.provider = RSS_DDC_PROVIDER_UNKNOWN};
    assert(rss_macos_provider_set_vcp(&selected_unknown, 0x60, 18, NULL) == RSS_DDC_ERROR_UNSUPPORTED_PROVIDER);
    assert(rss_macos_provider_get_mccs_capabilities(&selected_unknown, &capabilities, NULL) ==
           RSS_DDC_ERROR_UNSUPPORTED_PROVIDER);
    assert(dp_set_calls == 2 && ps190_set_calls == 1 && ps190_dpcd_calls == 1 && dp_dpcd_calls == 2);
    puts("test_dispatch: passed");
    return 0;
}
