@import Foundation;

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "macos_internal.h"
#include "protocol.h"

/*
 * Private IOAV remains behind the macOS backend. CreateWithService returns a
 * retained CF object; this Set path releases it after the complete write-only
 * transaction, including on a failed write.
 */
typedef CFTypeRef IOAVServiceRef;
extern IOAVServiceRef IOAVServiceCreateWithService(CFAllocatorRef, io_service_t);
extern CFTypeID IOAVServiceGetTypeID(void);
extern IOReturn IOAVServiceWriteI2C(IOAVServiceRef, uint32_t, uint32_t, void *, uint32_t);

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
 * Executes the conventional DCPDP13 Set VCP sequence recovered from the
 * pre-HDMI m1ddc DP implementation (commit a561e56's parent): two six-byte
 * writes to chip 0x37/data 0x51, each preceded by 10 ms, with no reply read.
 * That historical DP transaction is the same conventional IOAV representation
 * later validated for PS190 SET, but its standalone rss-ddc DP validation is
 * intentionally still pending.
 *
 * `binding->service_proxy` was uniquely correlated to the caller's selected
 * display before this function runs. This backend never searches globally or
 * iterates over provider peers, preserving the single-display guarantee.
 */
RSSDDCError rss_macos_dp_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                 const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || !binding->dp_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;

    char message[256] = {};
    snprintf(message, sizeof(message),
             "backend=DCPDP13Service operation=SetVCP framing=conventional requested-vcp=0x%02x requested-value=%u",
             vcp_code, value);
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
    rss_macos_diagnostic(diagnostics, "response=none historical DCPDP13 SetVCP path is write-only");
    return RSS_DDC_OK;
}

#include "set_validation.h"

typedef struct {
    io_service_t service_proxy;
    const RSSDDCDiagnostics *diagnostics;
    unsigned int write_count;
} ConventionalSetValidationContext;

static void conventional_set_validate_release(void *opaque, void *service) {
    (void)opaque;
    CFRelease((IOAVServiceRef)(uintptr_t)service);
}

static RSSDDCError conventional_set_validate_construct(void *opaque, void **service_out) {
    ConventionalSetValidationContext *context = opaque;
    context->write_count = 0;
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, context->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(context->diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    rss_macos_diagnostic(context->diagnostics, "IOAVServiceCreateWithService=success");
    *service_out = (void *)(uintptr_t)service;
    return RSS_DDC_OK;
}

static RSSDDCError conventional_set_validate_pre_delay(void *opaque) {
    ConventionalSetValidationContext *context = opaque;
    rss_macos_diagnostic(context->diagnostics, "pre-write-delay=10ms");
    usleep(RSS_DDC_DCPDP_SERVICE_SET_PREWRITE_DELAY_US);
    return RSS_DDC_OK;
}

static RSSDDCError conventional_set_validate_write(void *opaque, void *service, uint32_t chip, uint32_t data,
                                                   const uint8_t *payload, size_t payload_length) {
    ConventionalSetValidationContext *context = opaque;
    if (context->write_count == 0) diagnostic_bytes(context->diagnostics, "request", payload, payload_length);
    IOReturn write_result = IOAVServiceWriteI2C((IOAVServiceRef)(uintptr_t)service, chip, data,
                                                  (void *)(uintptr_t)payload, (uint32_t)payload_length);
    char message[256] = {};
    ++context->write_count;
    snprintf(message, sizeof(message),
             "write=%u/%u chip=0x%02x data=0x%08x length=%zu pre-delay=10ms IOReturn=0x%08x", context->write_count,
             RSS_DDC_DCPDP_SERVICE_SET_WRITE_COUNT, chip, data, payload_length, (unsigned int)write_result);
    rss_macos_diagnostic(context->diagnostics, message);
    if (write_result != KERN_SUCCESS) return RSS_DDC_ERROR_WRITE;
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_run_dcpdpservice_set_validation(io_service_t service_proxy,
                                                      RSSDDCDCPDPServiceCorrelationResult correlation,
                                                      uint16_t current_value,
                                                      const RSSDDCDiagnostics *diagnostics) {
    if (service_proxy == MACH_PORT_NULL) return RSS_DDC_ERROR_ARGUMENT;
    char message[256] = {};
    snprintf(message, sizeof(message),
             "backend=DCPDPService operation=ValidateSetVCP framing=conventional requested-vcp=0x%02x requested-value=%u",
             RSS_DDC_DCPDP_SERVICE_SET_VALIDATION_VCP, current_value);
    rss_macos_diagnostic(diagnostics, message);
    ConventionalSetValidationContext context = {.service_proxy = service_proxy, .diagnostics = diagnostics,
                                                .write_count = 0};
    const RSSDDCConventionalSetValidationCallbacks callbacks = {
        .context = &context,
        .construct = conventional_set_validate_construct,
        .pre_write_delay = conventional_set_validate_pre_delay,
        .write_i2c = conventional_set_validate_write,
        .release = conventional_set_validate_release,
    };
    RSSDDCError error = rss_ddc_run_dcpdpservice_set_validation(correlation, current_value, &callbacks);
    if (error == RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, "response=none standard-DP SetVCP hypothesis is write-only");
    }
    return error;
}
