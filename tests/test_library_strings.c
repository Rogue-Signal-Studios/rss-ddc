#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rss_ddc.h"

/** Returns true when `text` contains an ANSI escape introducer. */
static bool contains_ansi_escape(const char *text) {
    if (text == NULL) {
        return false;
    }
    return strstr(text, "\033[") != NULL || strstr(text, "\x1b[") != NULL;
}

/** Asserts a public library diagnostic/name string is plain text. */
static void assert_plain_library_string(const char *label, const char *text) {
    assert(text != NULL);
    assert(text[0] != '\0');
    assert(!contains_ansi_escape(text));
    (void)label;
}

static void test_error_strings(void) {
    for (int code = RSS_DDC_OK; code <= RSS_DDC_ERROR_PROFILE_UNSAFE; ++code) {
        assert_plain_library_string("error", rss_ddc_error_string((RSSDDCError)code));
    }
    assert_plain_library_string("error-unknown", rss_ddc_error_string((RSSDDCError)9999));
}

static void test_provider_and_backend_strings(void) {
    for (int provider = RSS_DDC_PROVIDER_UNKNOWN; provider <= RSS_DDC_PROVIDER_PS190; ++provider) {
        assert_plain_library_string("provider", rss_ddc_provider_string((RSSDDCProvider)provider));
    }
    for (int backend = RSS_DDC_BACKEND_UNSUPPORTED; backend <= RSS_DDC_BACKEND_PS190; ++backend) {
        assert_plain_library_string("backend", rss_ddc_backend_name((RSSDDCBackend)backend));
    }
}

static void test_profile_strings(void) {
    for (int control = RSS_DDC_PROFILE_CONTROL_UNKNOWN; control <= RSS_DDC_PROFILE_CONTROL_AUDIO_MUTE; ++control) {
        assert_plain_library_string("profile-control", rss_ddc_profile_control_name((RSSDDCProfileControlID)control));
    }
    for (int source = RSS_DDC_PROFILE_SOURCE_BUILTIN; source <= RSS_DDC_PROFILE_SOURCE_RESEARCH; ++source) {
        assert_plain_library_string("profile-source", rss_ddc_profile_source_name((RSSDDCProfileSource)source));
    }
    for (int confidence = RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN;
         confidence <= RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED; ++confidence) {
        assert_plain_library_string("profile-confidence",
                                    rss_ddc_profile_confidence_name((RSSDDCProfileConfidence)confidence));
    }
}

static void test_probe_strings(void) {
    for (int category = RSS_DDC_PROBE_RESULT_UNATTEMPTED; category <= RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR;
         ++category) {
        assert_plain_library_string("probe-category",
                                    rss_ddc_probe_result_category_name((RSSDDCProbeResultCategory)category));
    }
    for (int interpretation = RSS_DDC_PROBE_INTERPRETATION_UNKNOWN;
         interpretation <= RSS_DDC_PROBE_INTERPRETATION_OBSERVED_UNADVERTISED; ++interpretation) {
        assert_plain_library_string("probe-interpretation",
                                    rss_ddc_probe_interpretation_name((RSSDDCProbeInterpretationConfidence)interpretation));
    }
    RSSDDCProbeObservation not_attempted = {.repeat_attempted = false};
    assert_plain_library_string("probe-repeat-not-attempted", rss_ddc_probe_repeat_error_name(&not_attempted));
    RSSDDCProbeObservation attempted = {.repeat_attempted = true, .repeat_error = RSS_DDC_ERROR_NOT_FOUND};
    assert_plain_library_string("probe-repeat-error", rss_ddc_probe_repeat_error_name(&attempted));
}

static void test_edid_and_picture_mode_strings(void) {
    for (int type = RSS_DDC_EDID_EXTENSION_UNKNOWN; type <= RSS_DDC_EDID_EXTENSION_DISPLAYID; ++type) {
        assert_plain_library_string("edid-extension", rss_ddc_edid_extension_type_string((RSSDDCEDIDExtensionType)type));
    }
    for (int mode = RSS_DDC_PICTURE_MODE_UNKNOWN; mode <= RSS_DDC_PICTURE_MODE_READER; ++mode) {
        assert_plain_library_string("picture-mode", rss_ddc_picture_mode_name((RSSDDCPictureMode)mode));
    }
}

static void test_dpcd_decode_strings(void) {
    uint8_t bytes[16] = {0x14, 0x14, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    RSSDDCDPCDCapabilities capabilities = {};
    assert(rss_ddc_decode_dpcd_capabilities(0, bytes, sizeof(bytes), &capabilities) == RSS_DDC_OK);
    assert_plain_library_string("dpcd-link-rate", capabilities.max_link_rate_name);
}

int main(void) {
    test_error_strings();
    test_provider_and_backend_strings();
    test_profile_strings();
    test_probe_strings();
    test_edid_and_picture_mode_strings();
    test_dpcd_decode_strings();
    puts("test_library_strings: passed");
    return 0;
}
