#include <assert.h>
#include <pthread.h>
#include <stdio.h>

#include "macos_internal.h"
#include "verify.h"

static RSSDDCDisplay discovered_display;
static unsigned int snapshot_calls, binding_calls, release_calls, get_calls;

RSSDDCError rss_macos_get_display_snapshot(uint32_t list_index, RSSDDCDisplay *display,
                                           RSSMacOSCorrelationFailure *failure,
                                           char *detail, size_t detail_capacity) {
    assert(list_index == 1);
    ++snapshot_calls;
    *display = discovered_display;
    if (failure != NULL) *failure = RSS_MACOS_CORRELATION_NONE;
    if (detail != NULL && detail_capacity != 0) detail[0] = '\0';
    return RSS_DDC_OK;
}
RSSDDCError rss_macos_resolve_binding(uint32_t list_index, RSSMacOSBinding *binding) {
    assert(list_index == 1);
    ++binding_calls;
    *binding = (RSSMacOSBinding){.display = discovered_display};
    return RSS_DDC_OK;
}
void rss_macos_release_binding(RSSMacOSBinding *binding) { (void)binding; ++release_calls; }
const char *rss_macos_correlation_failure_string(RSSMacOSCorrelationFailure failure) {
    (void)failure; return "mock correlation failure";
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
                                        RSSDDCVCPResult *result,
                                        const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)code; (void)result; (void)diagnostics; ++get_calls;
    return RSS_DDC_ERROR_READ;
}
RSSDDCError rss_macos_provider_get_mccs_capabilities(RSSMacOSBinding *binding,
                                                      RSSDDCMCCSCapabilities *capabilities,
                                                      const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)capabilities; (void)diagnostics;
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
RSSDDCError rss_macos_provider_read_edid(RSSMacOSBinding *binding, RSSDDCEDID *edid,
                                         const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)edid; (void)diagnostics; return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
RSSDDCError rss_macos_provider_read_dpcd(RSSMacOSBinding *binding, uint32_t address,
                                         uint8_t *bytes, size_t length,
                                         const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)address; (void)bytes; (void)length; (void)diagnostics;
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
RSSDDCError rss_macos_provider_set_vcp(RSSMacOSBinding *binding, uint8_t code, uint16_t value,
                                        const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)code; (void)value; (void)diagnostics;
    return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
}
RSSDDCError rss_macos_provider_set_lg_alt_input(RSSMacOSBinding *binding, uint16_t value,
                                                 const RSSDDCDiagnostics *diagnostics) {
    (void)binding; (void)value; (void)diagnostics; return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
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
RSSDDCError rss_ddc_orchestrate_set_vcp_and_verify(uint8_t code, uint16_t value,
                                                    const RSSDDCVerifyPolicy *policy,
                                                    RSSDDCVCPResult *result,
                                                    const RSSDDCVerifyOperations *operations) {
    (void)code; (void)value; (void)policy; (void)result; (void)operations;
    return RSS_DDC_ERROR_ARGUMENT;
}
RSSDDCVerifyPolicy rss_ddc_default_verify_policy(void) { return (RSSDDCVerifyPolicy){}; }
bool rss_ddc_verify_policy_is_valid(const RSSDDCVerifyPolicy *policy) {
    (void)policy; return false;
}

typedef struct { RSSDDCError result; } ProbeThreadResult;
static void *run_extended_probe(void *opaque) {
    ProbeThreadResult *thread_result = opaque;
    RSSDDCProbe *probe = NULL;
    thread_result->result = rss_ddc_probe_extended_for_display(1, &probe);
    assert(thread_result->result == RSS_DDC_OK);
    RSSDDCProbeDiagnostics diagnostics = {};
    assert(rss_ddc_probe_diagnostics(probe, &diagnostics) == RSS_DDC_OK);
    assert(diagnostics.extended && diagnostics.aborted);
    assert(diagnostics.abort_reason == RSS_DDC_PROBE_ABORT_TRANSPORT_FAILURE_STORM);
    rss_ddc_probe_destroy(probe);
    return NULL;
}

int main(void) {
    discovered_display = (RSSDDCDisplay){.list_index = 1, .online = true, .external = true,
                                         .provider = RSS_DDC_PROVIDER_DCPDP13};
    snprintf(discovered_display.product_name, sizeof(discovered_display.product_name), "Mock DP display");
    snprintf(discovered_display.transport, sizeof(discovered_display.transport), "DCPEXT0");
    pthread_attr_t attributes;
    assert(pthread_attr_init(&attributes) == 0);
    assert(pthread_attr_setstacksize(&attributes, 512 * 1024) == 0);
    ProbeThreadResult result = {};
    pthread_t thread;
    assert(pthread_create(&thread, &attributes, run_extended_probe, &result) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(pthread_attr_destroy(&attributes) == 0);
    assert(result.result == RSS_DDC_OK);
    assert(snapshot_calls == 1);
    assert(binding_calls == 8);
    assert(release_calls == binding_calls);
    assert(get_calls == 8);
    puts("test_probe_display_resolution: passed");
    return 0;
}
