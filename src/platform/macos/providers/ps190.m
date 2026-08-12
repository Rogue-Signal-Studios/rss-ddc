@import Foundation;

#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "macos_internal.h"
#include "protocol.h"

/*
 * Private IOAV ABI declarations reconstructed from Apple interfaces and our
 * research. They remain backend-private so public headers stay portable.
 * CreateWithService returns a retained CF object released with CFRelease.
 */
typedef CFTypeRef IOAVServiceRef;
extern IOAVServiceRef IOAVServiceCreateWithService(CFAllocatorRef, io_service_t);
extern CFTypeID IOAVServiceGetTypeID(void);
extern IOReturn IOAVServiceReadI2C(IOAVServiceRef, uint32_t, uint32_t, void *, uint32_t);
extern IOReturn IOAVServiceWriteI2C(IOAVServiceRef, uint32_t, uint32_t, void *, uint32_t);

enum {
    RSS_PS190_SET_WRITE_COUNT = 2,
    RSS_PS190_SET_PREWRITE_DELAY_US = 10000,
};

/** Formats a bounded byte trace for operator diagnostics without exposing pointers. */
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
 * Executes the only currently enabled PS190 capability: raw-framed Get VCP.
 * Hardware validation confirmed this exact rss-ddc path on macOS 25F84 with
 * an Odyssey G75F and AppleDCPPS190; other providers and configurations are
 * not implied by that result.
 *
 * Passing 0x51 as IOAV's data/subaddress caused DCP offset preparation and
 * invalid replies. The validated form sends 0x51 inline and uses UINT32_MAX
 * as the no-offset sentinel for both the 5-byte write and 11-byte read.
 */
RSSDDCError rss_macos_ps190_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                     const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || result == NULL || !binding->ps190_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    rss_macos_diagnostic(diagnostics, "backend=AppleDCPPS190 operation=GetVCP");
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE];
    uint8_t reply[RSS_DDC_GET_VCP_REPLY_SIZE];
    memset(reply, 0xcc, sizeof(reply));
    rss_ddc_build_raw_get_vcp(vcp_code, request);
    const uint32_t no_offset = UINT32_MAX; /* DCP firmware's no-subaddress sentinel. */
    diagnostic_bytes(diagnostics, "request", request, sizeof(request));
    IOReturn write_result = IOAVServiceWriteI2C(service, 0x37, no_offset, request, sizeof(request));
    char message[256] = {};
    snprintf(message, sizeof(message), "write chip=0x37 data=0xffffffff length=5 IOReturn=0x%08x",
             (unsigned int)write_result);
    rss_macos_diagnostic(diagnostics, message);
    if (write_result == KERN_SUCCESS) {
        rss_macos_diagnostic(diagnostics, "delay=50ms"); /* Validated PS190 reply construction delay. */
        usleep(50000);
    }
    IOReturn read_result = KERN_SUCCESS;
    if (write_result == KERN_SUCCESS) {
        read_result = IOAVServiceReadI2C(service, 0x37, no_offset, reply, sizeof(reply));
    }
    CFRelease(service);
    if (write_result != KERN_SUCCESS) {
        rss_macos_diagnostic(diagnostics, "read=skipped because write failed");
        return RSS_DDC_ERROR_WRITE;
    }
    snprintf(message, sizeof(message), "read chip=0x37 data=0xffffffff length=11 IOReturn=0x%08x",
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

/**
 * Executes the hardware-validated PS190 Set VCP path recovered from m1ddc:
 * two conventional Service writes, each preceded by 10 ms, with no response
 * read. This is intentionally distinct from raw PS190 GET. The legacy path
 * supplied 0x51 as the IOAV data/subaddress argument and formed the matching
 * six-byte payload/checksum; rss-ddc validation confirmed that VCP 0x60 can
 * visibly switch the documented Odyssey G75F input.
 *
 * The repeated write is preserved as evidence-backed transaction behavior,
 * not presented as a retry policy. A failed call returns WRITE immediately;
 * no response is implied or parsed after a successful Set VCP write.
 */
RSSDDCError rss_macos_ps190_set_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, uint16_t value,
                                     const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || !binding->ps190_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    char message[256] = {};
    snprintf(message, sizeof(message), "backend=AppleDCPPS190 operation=SetVCP framing=conventional requested-vcp=0x%02x requested-value=%u",
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
    for (unsigned int write_index = 0; write_index < RSS_PS190_SET_WRITE_COUNT; ++write_index) {
        usleep(RSS_PS190_SET_PREWRITE_DELAY_US);
        IOReturn write_result = IOAVServiceWriteI2C(service, 0x37, 0x51, request, sizeof(request));
        snprintf(message, sizeof(message),
                 "write=%u/%u chip=0x37 data=0x00000051 length=6 pre-delay=10ms IOReturn=0x%08x",
                 write_index + 1, RSS_PS190_SET_WRITE_COUNT, (unsigned int)write_result);
        rss_macos_diagnostic(diagnostics, message);
        if (write_result != KERN_SUCCESS) {
            CFRelease(service);
            return RSS_DDC_ERROR_WRITE;
        }
    }
    CFRelease(service);
    rss_macos_diagnostic(diagnostics, "response=none historical SetVCP path is write-only");
    return RSS_DDC_OK;
}
