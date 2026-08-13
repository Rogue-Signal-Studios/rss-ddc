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
} RSSMacOSLGAltInputContext;

static RSSDDCError lg_alt_input_construct(void *opaque, void **service_out) {
    RSSMacOSLGAltInputContext *context = opaque;
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

static RSSDDCError lg_alt_input_delay(void *opaque) {
    RSSMacOSLGAltInputContext *context = opaque;
    (void)context;
    usleep(RSS_DDC_LG_ALT_INPUT_PREWRITE_DELAY_US);
    return RSS_DDC_OK;
}

static RSSDDCError lg_alt_input_write(void *opaque, void *opaque_service, uint32_t chip, uint32_t data,
                                      const uint8_t *payload, size_t payload_length) {
    RSSMacOSLGAltInputContext *context = opaque;
    IOReturn result = IOAVServiceWriteI2C((IOAVServiceRef)opaque_service, chip, data, (void *)payload,
                                          (uint32_t)payload_length);
    char message[256] = {};
    ++context->write_index;
    snprintf(message, sizeof(message), "write=%u/%u chip=0x%02x data=0x%02x length=%zu IOReturn=0x%08x",
             context->write_index, RSS_DDC_LG_ALT_INPUT_WRITE_COUNT, chip, data, payload_length,
             (unsigned int)result);
    rss_macos_diagnostic(context->diagnostics, message);
    return result == KERN_SUCCESS ? RSS_DDC_OK : RSS_DDC_ERROR_WRITE;
}

static void lg_alt_input_release(void *opaque, void *opaque_service) {
    (void)opaque;
    CFRelease((IOAVServiceRef)opaque_service);
}

RSSDDCError rss_macos_dp_set_lg_alt_input(RSSMacOSBinding *binding, uint16_t value,
                                          const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL) return RSS_DDC_ERROR_ARGUMENT;
    char message[256] = {};
    snprintf(message, sizeof(message), "operation=SetInput method=LG_ALT provider=%s value=0x%02x",
             rss_ddc_provider_string(binding->display.provider), value);
    rss_macos_diagnostic(diagnostics, message);
    if (binding->display.provider != RSS_DDC_PROVIDER_DCPDP13) return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
    if (!binding->dp_safety_gate) {
        rss_macos_diagnostic(diagnostics, "operation=SetInput method=LG_ALT status=unsafe; correlated DCPDP13Service gate required");
        return RSS_DDC_ERROR_SAFETY_GATE;
    }

    RSSDDCLGAltInputPlan plan = {};
    RSSDDCError error = rss_ddc_prepare_lg_alt_input(value, &plan);
    if (error != RSS_DDC_OK) {
        return error;
    }
    snprintf(message, sizeof(message), "method=LG_ALT vcp=0x%02x chip=0x%02x data=0x%02x write-count=%u pre-delay=%ums response=none no-get=yes no-verify=yes no-restore=yes no-retry=yes no-fallback=yes",
             plan.payload[2], plan.chip, plan.data, plan.write_count,
             plan.prewrite_delay_us / 1000u);
    rss_macos_diagnostic(diagnostics, message);
    diagnostic_bytes(diagnostics, "payload", plan.payload, plan.payload_length);
    snprintf(message, sizeof(message), "checksum=0x%02x convention=0x6e xor IOAV-data(0x%02x) xor payload[0..4]",
             plan.payload[5], plan.data);
    rss_macos_diagnostic(diagnostics, message);
    RSSMacOSLGAltInputContext context = {.binding = binding, .diagnostics = diagnostics};
    RSSDDCLGAltInputCallbacks callbacks = {.context = &context, .construct = lg_alt_input_construct,
        .prewrite_delay = lg_alt_input_delay, .write_i2c = lg_alt_input_write, .release = lg_alt_input_release};
    return rss_ddc_run_lg_alt_input(binding->display.provider, value, &callbacks);
}

RSSDDCError rss_macos_dp_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                 const RSSDDCDiagnostics *diagnostics) {
    return conventional_service_set_vcp(binding, vcp_code, value, diagnostics);
}
