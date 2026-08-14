#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "macos_internal.h"
#include "picture_mode.h"

static unsigned int set_calls;
static uint32_t last_index;
static uint8_t last_vcp;
static uint16_t last_value;
static const char *last_mode_name;
static RSSDDCError next_set_error;

RSSDDCError rss_macos_set_lg_picture_mode_snapshot(uint32_t index, uint8_t vcp_code, uint16_t value,
                                                    const char *mode_name,
                                                    const RSSDDCDiagnostics *diagnostics) {
    (void)diagnostics;
    ++set_calls;
    last_index = index;
    last_vcp = vcp_code;
    last_value = value;
    last_mode_name = mode_name;
    return next_set_error;
}

static RSSDDCDisplay validated_profile(void) {
    RSSDDCDisplay display = {.external = true, .provider = RSS_DDC_PROVIDER_DCPDP13};
    snprintf(display.product_name, sizeof(display.product_name), "LG HDR QHD");
    snprintf(display.transport, sizeof(display.transport), "DCPEXT0");
    return display;
}

int main(void) {
    uint16_t raw_value = 0;
    assert(rss_ddc_lg_hdr_qhd_picture_mode_raw_value(RSS_DDC_PICTURE_MODE_VIVID, &raw_value) == RSS_DDC_OK);
    assert(raw_value == 0x31);
    assert(rss_ddc_lg_hdr_qhd_picture_mode_raw_value(RSS_DDC_PICTURE_MODE_READER, &raw_value) == RSS_DDC_OK);
    assert(raw_value == 0x01);
    assert(rss_ddc_lg_hdr_qhd_picture_mode_raw_value(RSS_DDC_PICTURE_MODE_UNKNOWN, &raw_value) == RSS_DDC_ERROR_ARGUMENT);
    assert(strcmp(rss_ddc_picture_mode_name(RSS_DDC_PICTURE_MODE_VIVID), "Vivid") == 0);
    assert(strcmp(rss_ddc_picture_mode_name(RSS_DDC_PICTURE_MODE_READER), "Reader") == 0);

    RSSDDCDisplay display = validated_profile();
    assert(rss_ddc_lg_hdr_qhd_picture_mode_profile_matches(&display));
    assert(rss_ddc_picture_mode_profile_capabilities(&display) == RSS_DDC_CAP_PICTURE_MODE);
    assert(rss_ddc_validate_lg_hdr_qhd_picture_mode_target(&display, true) == RSS_DDC_OK);
    assert(rss_ddc_validate_lg_hdr_qhd_picture_mode_target(&display, false) == RSS_DDC_ERROR_SAFETY_GATE);

    RSSDDCDisplay rejected = display;
    snprintf(rejected.product_name, sizeof(rejected.product_name), "Other LG");
    assert(rss_ddc_validate_lg_hdr_qhd_picture_mode_target(&rejected, true) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    rejected = display;
    rejected.provider = RSS_DDC_PROVIDER_DCPDP_SERVICE;
    assert(rss_ddc_validate_lg_hdr_qhd_picture_mode_target(&rejected, true) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    rejected = display;
    snprintf(rejected.transport, sizeof(rejected.transport), "DCPEXT1");
    assert(rss_ddc_validate_lg_hdr_qhd_picture_mode_target(&rejected, true) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);
    rejected = display;
    rejected.external = false;
    assert(rss_ddc_validate_lg_hdr_qhd_picture_mode_target(&rejected, true) == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY);

    assert(rss_ddc_set_picture_mode(4, RSS_DDC_PICTURE_MODE_VIVID) == RSS_DDC_OK);
    assert(set_calls == 1 && last_index == 4 && last_vcp == 0x15 && last_value == 0x31);
    assert(strcmp(last_mode_name, "Vivid") == 0); /* one semantic call: no GET, verify, retry, or fallback */
    assert(rss_ddc_set_picture_mode(4, RSS_DDC_PICTURE_MODE_READER) == RSS_DDC_OK);
    assert(set_calls == 2 && last_vcp == 0x15 && last_value == 0x01 && strcmp(last_mode_name, "Reader") == 0);

    next_set_error = RSS_DDC_ERROR_WRITE;
    assert(rss_ddc_set_picture_mode(4, RSS_DDC_PICTURE_MODE_VIVID) == RSS_DDC_ERROR_WRITE);
    assert(set_calls == 3); /* transport errors propagate; no retry or fallback */
    assert(rss_ddc_set_picture_mode(4, RSS_DDC_PICTURE_MODE_UNKNOWN) == RSS_DDC_ERROR_ARGUMENT);
    assert(set_calls == 3); /* unsupported value is rejected before display resolution or write */

    puts("test_picture_mode: passed");
    return 0;
}
