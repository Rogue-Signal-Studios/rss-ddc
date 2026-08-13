#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "macos_internal.h"
#include "picture_mode.h"

static RSSDDCDisplay resolved_display;
static RSSDDCVCPResult next_get;
static unsigned int resolve_calls, release_calls, get_calls, set_calls;
static uint8_t last_get_vcp, last_set_vcp;
static uint16_t last_set_value;

RSSDDCError rss_macos_resolve_binding(uint32_t index, RSSMacOSBinding *binding) {
    (void)index;
    ++resolve_calls;
    binding->display = resolved_display;
    return RSS_DDC_OK;
}

void rss_macos_release_binding(RSSMacOSBinding *binding) { (void)binding; ++release_calls; }
void rss_macos_diagnostic(const RSSDDCDiagnostics *diagnostics, const char *message) { (void)diagnostics; (void)message; }

RSSDDCError rss_macos_provider_get_vcp(RSSMacOSBinding *binding, uint8_t vcp, RSSDDCVCPResult *result,
                                        const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)diagnostics; ++get_calls; last_get_vcp = vcp; *result = next_get; return RSS_DDC_OK;
}

RSSDDCError rss_macos_provider_set_vcp(RSSMacOSBinding *binding, uint8_t vcp, uint16_t value,
                                        const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)diagnostics; ++set_calls; last_set_vcp = vcp; last_set_value = value; return RSS_DDC_OK;
}

static RSSDDCDisplay lg_profile(void) {
    RSSDDCDisplay display = {.external = true, .provider = RSS_DDC_PROVIDER_DCPDP13};
    snprintf(display.product_name, sizeof(display.product_name), "LG HDR QHD");
    snprintf(display.transport, sizeof(display.transport), "DCPEXT0");
    return display;
}

int main(void) {
    const RSSDDCPictureMode modes[] = {RSS_DDC_PICTURE_MODE_CUSTOM, RSS_DDC_PICTURE_MODE_VIVID,
        RSS_DDC_PICTURE_MODE_HDR_EFFECT, RSS_DDC_PICTURE_MODE_CINEMA, RSS_DDC_PICTURE_MODE_FPS,
        RSS_DDC_PICTURE_MODE_RTS, RSS_DDC_PICTURE_MODE_COLOR_WEAKNESS, RSS_DDC_PICTURE_MODE_READER};
    const uint16_t values[] = {0x0b, 0x31, 0x27, 0x30, 0x1e, 0x1f, 0x06, 0x01};
    for (size_t index = 0; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        uint16_t raw = 0;
        assert(rss_ddc_lg_hdr_qhd_picture_mode_raw_value(modes[index], &raw) == RSS_DDC_OK && raw == values[index]);
        assert(rss_ddc_lg_hdr_qhd_picture_mode_from_raw(raw) == modes[index]);
        assert(strcmp(rss_ddc_picture_mode_name(modes[index]), "Unknown") != 0);
    }
    assert(rss_ddc_lg_hdr_qhd_picture_mode_raw_value(RSS_DDC_PICTURE_MODE_UNKNOWN, &(uint16_t){0}) == RSS_DDC_ERROR_ARGUMENT);
    assert(rss_ddc_lg_hdr_qhd_picture_mode_from_raw(0x48) == RSS_DDC_PICTURE_MODE_UNKNOWN);
    RSSDDCMCCSCapabilities advertised = {};
    const uint8_t *advertised_values = NULL;
    size_t advertised_count = 0;
    const char *advertised_raw = "vcp(15(01 06 11 13 14 18 28 29 32 48))";
    assert(rss_ddc_parse_mccs_capabilities(advertised_raw, strlen(advertised_raw), &advertised) == RSS_DDC_OK);
    assert(rss_ddc_mccs_capabilities_enum_values(&advertised, 0x15, &advertised_values, &advertised_count) == RSS_DDC_OK);
    assert(advertised_count == 10 && memchr(advertised_values, 0x31, advertised_count) == NULL);
    assert(rss_ddc_lg_hdr_qhd_picture_mode_from_raw(0x31) == RSS_DDC_PICTURE_MODE_VIVID);

    resolved_display = lg_profile();
    assert(rss_ddc_lg_hdr_qhd_picture_mode_profile_matches(&resolved_display));
    assert(rss_ddc_picture_mode_profile_capabilities(&resolved_display) == RSS_DDC_CAP_PICTURE_MODE);
    RSSDDCDisplay non_lg = resolved_display;
    snprintf(non_lg.product_name, sizeof(non_lg.product_name), "Odyssey G75F");
    assert(rss_ddc_picture_mode_profile_capabilities(&non_lg) == RSS_DDC_CAP_NONE);
    non_lg = resolved_display;
    non_lg.provider = RSS_DDC_PROVIDER_DCPDP_SERVICE;
    assert(rss_ddc_picture_mode_profile_capabilities(&non_lg) == RSS_DDC_CAP_NONE);
    non_lg = resolved_display;
    snprintf(non_lg.transport, sizeof(non_lg.transport), "DCPEXT1");
    assert(rss_ddc_picture_mode_profile_capabilities(&non_lg) == RSS_DDC_CAP_NONE);
    non_lg = resolved_display;
    non_lg.external = false;
    assert(rss_ddc_picture_mode_profile_capabilities(&non_lg) == RSS_DDC_CAP_NONE);

    next_get = (RSSDDCVCPResult){.vcp_code = 0x15, .current_value = 0x1e, .maximum_value = 0x48};
    RSSDDCPictureMode mode = RSS_DDC_PICTURE_MODE_UNKNOWN;
    assert(rss_ddc_get_picture_mode(2, &mode) == RSS_DDC_OK && mode == RSS_DDC_PICTURE_MODE_FPS);
    assert(resolve_calls == 1 && get_calls == 1 && last_get_vcp == 0x15 && release_calls == 1);
    next_get.current_value = 0x48;
    assert(rss_ddc_get_picture_mode(2, &mode) == RSS_DDC_OK && mode == RSS_DDC_PICTURE_MODE_UNKNOWN);
    assert(get_calls == 2 && last_get_vcp == 0x15);
    assert(rss_ddc_set_picture_mode(2, RSS_DDC_PICTURE_MODE_VIVID) == RSS_DDC_OK);
    assert(set_calls == 1 && last_set_vcp == 0x15 && last_set_value == 0x31); /* No secondary VCP writes. */
    assert(rss_ddc_set_picture_mode(2, RSS_DDC_PICTURE_MODE_UNKNOWN) == RSS_DDC_ERROR_ARGUMENT);
    assert(set_calls == 1);

    resolved_display = lg_profile();
    snprintf(resolved_display.product_name, sizeof(resolved_display.product_name), "Odyssey G75F");
    assert(rss_ddc_get_picture_mode(2, &mode) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(rss_ddc_set_picture_mode(2, RSS_DDC_PICTURE_MODE_READER) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(get_calls == 2 && set_calls == 1); /* Non-LG profile cannot use the semantic operation. */
    resolved_display = lg_profile();
    resolved_display.provider = RSS_DDC_PROVIDER_DCPDP_SERVICE;
    assert(rss_ddc_get_picture_mode(2, &mode) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(rss_ddc_set_picture_mode(2, RSS_DDC_PICTURE_MODE_READER) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    assert(get_calls == 2 && set_calls == 1); /* Wrong provider never reaches generic VCP dispatch. */
    resolved_display = lg_profile();
    resolved_display.provider = RSS_DDC_PROVIDER_UNKNOWN;
    assert(rss_ddc_get_picture_mode(2, &mode) == RSS_DDC_ERROR_UNSUPPORTED_PROVIDER);
    assert(get_calls == 2 && set_calls == 1);
    puts("test_picture_mode: passed");
    return 0;
}
