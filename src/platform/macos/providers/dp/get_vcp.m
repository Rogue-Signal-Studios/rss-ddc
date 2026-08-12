@import Foundation;

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "macos_internal.h"
#include "protocol.h"

/*
 * This is a private IOAV ABI boundary. It intentionally stays confined to the
 * macOS backend: CreateWithService returns a retained CF object and the I2C
 * methods use IOReturn rather than POSIX errno. No such type leaks into the C
 * public API.
 */
typedef CFTypeRef IOAVServiceRef;
extern IOAVServiceRef IOAVServiceCreateWithService(CFAllocatorRef, io_service_t);
extern CFTypeID IOAVServiceGetTypeID(void);
extern IOReturn IOAVServiceReadI2C(IOAVServiceRef, uint32_t, uint32_t, void *, uint32_t);
extern IOReturn IOAVServiceWriteI2C(IOAVServiceRef, uint32_t, uint32_t, void *, uint32_t);

/** Formats a bounded, pointer-free byte trace for the optional diagnostics callback. */
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
 * Executes the conventional Service-level DDC/CI Get VCP sequence validated for
 * DCPDP13 and DCPDPService hardware. Unlike PS190, 0x51 is intentionally
 * supplied as the IOAV data/subaddress argument and is absent from the four-byte
 * payload.
 */
static RSSDDCError conventional_service_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code,
                                                RSSDDCVCPResult *result,
                                                const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || result == NULL || !binding->dp_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    const char *backend = rss_ddc_backend_name(rss_ddc_provider_backend(binding->display.provider));
    char message[256] = {};
    snprintf(message, sizeof(message), "backend=%s operation=GetVCP framing=conventional requested-vcp=0x%02x",
             backend, vcp_code);
    rss_macos_diagnostic(diagnostics, message);
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    rss_macos_diagnostic(diagnostics, "IOAVServiceCreateWithService=success");

    uint8_t request[RSS_DDC_CONVENTIONAL_GET_VCP_REQUEST_SIZE];
    uint8_t reply[RSS_DDC_GET_VCP_REPLY_SIZE];
    memset(reply, 0xcc, sizeof(reply));
    rss_ddc_build_conventional_get_vcp(vcp_code, request);
    diagnostic_bytes(diagnostics, "request", request, sizeof(request));

    IOReturn write_result = IOAVServiceWriteI2C(service, 0x37, 0x51, request, sizeof(request));
    snprintf(message, sizeof(message), "write chip=0x37 data=0x00000051 length=4 IOReturn=0x%08x",
             (unsigned int)write_result);
    rss_macos_diagnostic(diagnostics, message);
    if (write_result != KERN_SUCCESS) {
        CFRelease(service);
        rss_macos_diagnostic(diagnostics, "read=skipped because write failed");
        return RSS_DDC_ERROR_WRITE;
    }

    rss_macos_diagnostic(diagnostics, "delay=50ms");
    usleep(50000);
    IOReturn read_result = IOAVServiceReadI2C(service, 0x37, 0x51, reply, sizeof(reply));
    CFRelease(service);
    snprintf(message, sizeof(message), "read chip=0x37 data=0x00000051 length=11 IOReturn=0x%08x",
             (unsigned int)read_result);
    rss_macos_diagnostic(diagnostics, message);
    if (read_result != KERN_SUCCESS) return RSS_DDC_ERROR_READ;

    diagnostic_bytes(diagnostics, "reply", reply, sizeof(reply));
    RSSDDCError parse_result = rss_ddc_parse_get_vcp_reply(reply, sizeof(reply), vcp_code, result);
    if (parse_result != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, rss_ddc_error_string(parse_result));
        return parse_result;
    }
    snprintf(message, sizeof(message), "decoded vcp=0x%02x maximum=%u current=%u checksum=valid",
             result->vcp_code, result->maximum_value, result->current_value);
    rss_macos_diagnostic(diagnostics, message);
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_dp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                  const RSSDDCDiagnostics *diagnostics) {
    return conventional_service_get_vcp(binding, vcp_code, result, diagnostics);
}

RSSDDCError rss_macos_dcpdpservice_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                            const RSSDDCDiagnostics *diagnostics) {
    return conventional_service_get_vcp(binding, vcp_code, result, diagnostics);
}
