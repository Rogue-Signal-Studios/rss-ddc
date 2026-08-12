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

RSSDDCError rss_macos_dp_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                 const RSSDDCDiagnostics *diagnostics) {
    return conventional_service_set_vcp(binding, vcp_code, value, diagnostics);
}
