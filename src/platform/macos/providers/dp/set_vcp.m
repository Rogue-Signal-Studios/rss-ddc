@import Foundation;

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "macos_internal.h"
#include "protocol.h"
#include "private/ioav_private.h"

enum {
    RSS_DP_SET_WRITE_COUNT = 2,
    RSS_DP_SET_PREWRITE_DELAY_US = 10000,
};

/** Formats a bounded byte trace for diagnostics without exposing registry or pointer identities. */
static void diagnostic_bytes(const RSSDDCDiagnostics *diagnostics, const char *label,
                             const uint8_t *bytes, size_t byte_count) {
    char message[256] = {};
    int written = snprintf(message, sizeof(message), "%s", label);
    for (size_t index = 0; index < byte_count && written > 0 && (size_t)written < sizeof(message); ++index) {
        written += snprintf(message + written, sizeof(message) - (size_t)written, "%s%02x",
                            index == 0 ? "=" : " ", bytes[index]);
    }
    rss_macos_diagnostic(diagnostics, message);
}

/**
 * Executes the conventional Service-level Set VCP sequence validated for
 * DCPDP13 and DCPDPService: two six-byte writes to chip 0x37/data 0x51, each
 * preceded by 10 ms, with no reply read. Provider identity selects the backend
 * label only; transport framing is shared.
 */
static RSSDDCError conventional_service_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                                const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || !binding->dp_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;

    const char *backend = rss_ddc_backend_name(rss_ddc_provider_backend(binding->display.provider));
    char message[256] = {};
    snprintf(message, sizeof(message),
             "backend=%s operation=SetVCP framing=conventional requested-vcp=0x%02x requested-value=%u",
             backend, vcp_code, value);
    rss_macos_diagnostic(diagnostics, message);

    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }

    uint8_t request[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE];
    rss_ddc_build_conventional_set_vcp(vcp_code, value, request);
    diagnostic_bytes(diagnostics, "request", request, sizeof(request));
    for (unsigned int write_index = 0; write_index < RSS_DP_SET_WRITE_COUNT; ++write_index) {
        usleep(RSS_DP_SET_PREWRITE_DELAY_US);
        IOReturn write_result = IOAVServiceWriteI2C(service, 0x37, 0x51, request, sizeof(request));
        snprintf(message, sizeof(message),
                 "write=%u/%u chip=0x37 data=0x00000051 length=6 pre-delay=10ms IOReturn=0x%08x",
                 write_index + 1, RSS_DP_SET_WRITE_COUNT, (unsigned int)write_result);
        rss_macos_diagnostic(diagnostics, message);
        if (write_result != KERN_SUCCESS) {
            CFRelease(service);
            return RSS_DDC_ERROR_WRITE;
        }
    }
    CFRelease(service);
    rss_macos_diagnostic(diagnostics, "response=none conventional SetVCP path is write-only");
    return RSS_DDC_OK;
}

typedef struct {
    RSSMacOSBinding *binding;
    const RSSDDCDiagnostics *diagnostics;
    unsigned int write_index;
} RSSMacOSInputAltProbeContext;

static RSSDDCError input_alt_probe_construct(void *opaque, void **service_out) {
    RSSMacOSInputAltProbeContext *context = opaque;
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, context->binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(context->diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    rss_macos_diagnostic(context->diagnostics, "IOAVServiceCreateWithService=success");
    *service_out = (void *)service;
    return RSS_DDC_OK;
}

static RSSDDCError input_alt_probe_delay(void *opaque) {
    RSSMacOSInputAltProbeContext *context = opaque;
    (void)context;
    usleep(RSS_DDC_INPUT_ALT_PROBE_PREWRITE_DELAY_US);
    return RSS_DDC_OK;
}

static RSSDDCError input_alt_probe_write(void *opaque, void *opaque_service, uint32_t chip, uint32_t data,
                                         const uint8_t *payload, size_t payload_length) {
    RSSMacOSInputAltProbeContext *context = opaque;
    IOReturn result = IOAVServiceWriteI2C((IOAVServiceRef)opaque_service, chip, data, (void *)payload,
                                          (uint32_t)payload_length);
    char message[256] = {};
    ++context->write_index;
    snprintf(message, sizeof(message), "probe-write=%u/%u chip=0x%02x data=0x%02x length=%zu IOReturn=0x%08x",
             context->write_index, RSS_DDC_INPUT_ALT_PROBE_WRITE_COUNT, chip, data, payload_length,
             (unsigned int)result);
    rss_macos_diagnostic(context->diagnostics, message);
    return result == KERN_SUCCESS ? RSS_DDC_OK : RSS_DDC_ERROR_WRITE;
}

static void input_alt_probe_release(void *opaque, void *opaque_service) {
    (void)opaque;
    CFRelease((IOAVServiceRef)opaque_service);
}

RSSDDCError rss_macos_dp_probe_input_alt(uint32_t list_index, RSSDDCInputAltProbeVariant variant, uint8_t value,
                                         const RSSDDCDiagnostics *diagnostics) {
    RSSMacOSBinding binding = {0};
    RSSDDCError error = rss_macos_resolve_binding(list_index, &binding);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, rss_macos_correlation_failure_string(binding.correlation_failure));
        rss_macos_release_binding(&binding);
        return error;
    }
    char message[256] = {};
    snprintf(message, sizeof(message), "operation=ProbeInputAlt display-index=%u name=%s provider=%s transport=%s variant=%s value=0x%02x",
             list_index, binding.display.product_name, rss_ddc_provider_string(binding.display.provider), binding.display.transport,
             rss_ddc_input_alt_probe_variant_name(variant), value);
    rss_macos_diagnostic(diagnostics, message);
    if (binding.display.provider != RSS_DDC_PROVIDER_DCPDP13 || !binding.dp_safety_gate) {
        rss_macos_diagnostic(diagnostics, "operation=ProbeInputAlt status=unsupported; DCPDP13Service with correlated DP safety gate required");
        rss_macos_release_binding(&binding);
        return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    }

    RSSDDCInputAltProbePlan plan = {};
    error = rss_ddc_prepare_dcpdp13_input_alt_probe(variant, value, &plan);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, "operation=ProbeInputAlt inline framing has no upstream or local packet evidence; no IOAV request was constructed");
        rss_macos_release_binding(&binding);
        return error;
    }
    snprintf(message, sizeof(message), "variant=%s vcp=0x%02x chip=0x%02x data=0x%02x write-count=%u pre-delay=%ums response=none no-get=yes no-verify=yes no-restore=yes no-retry=yes no-fallback=yes",
             rss_ddc_input_alt_probe_variant_name(variant), plan.payload[2], plan.chip, plan.data, plan.write_count,
             plan.prewrite_delay_us / 1000u);
    rss_macos_diagnostic(diagnostics, message);
    diagnostic_bytes(diagnostics, "payload", plan.payload, plan.payload_length);
    snprintf(message, sizeof(message), "checksum=0x%02x convention=0x6e xor IOAV-data(0x%02x) xor payload[0..4]",
             plan.payload[5], plan.data);
    rss_macos_diagnostic(diagnostics, message);
    RSSMacOSInputAltProbeContext context = {.binding = &binding, .diagnostics = diagnostics};
    RSSDDCInputAltProbeCallbacks callbacks = {.context = &context, .construct = input_alt_probe_construct,
        .prewrite_delay = input_alt_probe_delay, .write_i2c = input_alt_probe_write, .release = input_alt_probe_release};
    error = rss_ddc_run_dcpdp13_input_alt_probe(binding.display.provider, variant, value, &callbacks);
    rss_macos_release_binding(&binding);
    return error;
}

RSSDDCError rss_macos_dp_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                 const RSSDDCDiagnostics *diagnostics) {
    return conventional_service_set_vcp(binding, vcp_code, value, diagnostics);
}
