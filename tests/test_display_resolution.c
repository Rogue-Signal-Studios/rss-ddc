#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "macos_internal.h"
#include "verify.h"

static RSSDDCDisplay snapshot;
static unsigned int snapshot_calls, resolver_calls;

RSSDDCError rss_macos_get_display_snapshot(uint32_t list_index, RSSDDCDisplay *display,
                                           RSSMacOSCorrelationFailure *failure,
                                           char *detail, size_t detail_capacity) {
    assert(list_index == 1);
    ++snapshot_calls;
    *display = snapshot;
    if (failure != NULL) *failure = RSS_MACOS_CORRELATION_NONE;
    if (detail != NULL && detail_capacity != 0) detail[0] = '\0';
    return RSS_DDC_OK;
}
RSSDDCError rss_macos_resolve_binding(uint32_t list_index, RSSMacOSBinding *binding) {
    assert(list_index == 1);
    ++resolver_calls;
    *binding = (RSSMacOSBinding){.display = snapshot};
    return RSS_DDC_OK;
}
void rss_macos_release_binding(RSSMacOSBinding *binding) { (void)binding; }
const char *rss_macos_correlation_failure_string(RSSMacOSCorrelationFailure failure) {
    (void)failure; return "mock failure";
}
const char *rss_macos_correlation_detail_string(const RSSMacOSBinding *binding) {
    (void)binding; return NULL;
}
RSSDDCError rss_macos_discover_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count) {
    (void)displays; (void)capacity; (void)count; return RSS_DDC_ERROR_DISCOVERY;
}
RSSDDCError rss_macos_probe_dpcd_path(uint32_t list_index, const RSSDDCDiagnostics *diagnostics) {
    (void)list_index; (void)diagnostics; return RSS_DDC_ERROR_DISCOVERY;
}
bool rss_macos_capture_binding_identity(RSSMacOSBinding *binding) { (void)binding; return false; }
bool rss_macos_binding_matches_identity(const RSSMacOSBinding *binding,
                                        const RSSMacOSDisplayIdentity *identity) {
    (void)binding; (void)identity; return false;
}
RSSDDCError rss_macos_provider_get_vcp(RSSMacOSBinding *binding, uint8_t code,
                                        RSSDDCVCPResult *result, const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)code; (void)result; (void)diagnostics; return RSS_DDC_ERROR_READ;
}
RSSDDCError rss_macos_provider_set_vcp(RSSMacOSBinding *binding, uint8_t code, uint16_t value,
                                        const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)code; (void)value; (void)diagnostics; return RSS_DDC_ERROR_WRITE;
}
RSSDDCError rss_macos_provider_read_edid(RSSMacOSBinding *binding, RSSDDCEDID *edid,
                                         const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)edid; (void)diagnostics; return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
RSSDDCError rss_macos_provider_read_dpcd(RSSMacOSBinding *binding, uint32_t address,
                                         uint8_t *bytes, size_t length, const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)address; (void)bytes; (void)length; (void)diagnostics;
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
RSSDDCError rss_ddc_parse_edid(const RSSDDCEDID *edid, RSSDDCEDIDInfo *info) {
    (void)edid; (void)info; return RSS_DDC_ERROR_EDID_LENGTH;
}
const char *rss_ddc_edid_extension_type_string(RSSDDCEDIDExtensionType type) {
    (void)type; return "mock";
}
RSSDDCError rss_ddc_validate_dpcd_request(uint32_t address, uint8_t *buffer, size_t length) {
    (void)address; (void)buffer; (void)length; return RSS_DDC_ERROR_ARGUMENT;
}
RSSDDCVerifyPolicy rss_ddc_default_verify_policy(void) { return (RSSDDCVerifyPolicy){}; }
bool rss_ddc_verify_policy_is_valid(const RSSDDCVerifyPolicy *policy) { (void)policy; return false; }
RSSDDCError rss_ddc_orchestrate_set_vcp_and_verify(uint8_t code, uint16_t value,
                                                    const RSSDDCVerifyPolicy *policy,
                                                    RSSDDCVCPResult *result,
                                                    const RSSDDCVerifyOperations *operations) {
    (void)code; (void)value; (void)policy; (void)result; (void)operations;
    return RSS_DDC_ERROR_ARGUMENT;
}

typedef struct {
    unsigned char before[32];
    RSSDDCDisplay display;
    unsigned char after[32];
} GuardedDisplay;

static void *resolve_on_small_stack(void *opaque) {
    GuardedDisplay *guarded = opaque;
    assert(rss_ddc_get_display(1, &guarded->display) == RSS_DDC_OK);
    return NULL;
}

int main(void) {
    snapshot = (RSSDDCDisplay){.list_index = 1, .online = true, .external = true,
                               .provider = RSS_DDC_PROVIDER_PS190};
    snprintf(snapshot.product_name, sizeof(snapshot.product_name), "Mock display");
    GuardedDisplay guarded = {};
    memset(guarded.before, 0xa5, sizeof(guarded.before));
    memset(guarded.after, 0x5a, sizeof(guarded.after));
    pthread_attr_t attributes;
    assert(pthread_attr_init(&attributes) == 0);
    assert(pthread_attr_setstacksize(&attributes, 512 * 1024) == 0);
    pthread_t thread;
    assert(pthread_create(&thread, &attributes, resolve_on_small_stack, &guarded) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(pthread_attr_destroy(&attributes) == 0);
    for (size_t index = 0; index < sizeof(guarded.before); ++index) {
        assert(guarded.before[index] == 0xa5);
        assert(guarded.after[index] == 0x5a);
    }
    assert(snapshot_calls == 1);
    assert(resolver_calls == 0);
    assert(guarded.display.list_index == 1 && guarded.display.provider == RSS_DDC_PROVIDER_PS190);
    puts("test_display_resolution: passed");
    return 0;
}
