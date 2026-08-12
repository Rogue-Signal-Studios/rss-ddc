@import Foundation;

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "macos_internal.h"
#include "protocol.h"
#include "get_validation.h"
#include "correlation.h"

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
 * Executes the conventional Service-level DDC/CI Get VCP sequence previously
 * validated for DCPDP13 hardware in the research fork. Unlike PS190, 0x51 is
 * intentionally supplied as the IOAV data/subaddress argument and is absent
 * from the four-byte payload. This provider-specific distinction is required
 * because PS190's validated transaction is raw framed and uses UINT32_MAX.
 */
RSSDDCError rss_macos_dp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result,
                                  const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || result == NULL || !binding->dp_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    char message[256] = {};
    snprintf(message, sizeof(message), "backend=DCPDP13Service operation=GetVCP framing=conventional requested-vcp=0x%02x",
             vcp_code);
    rss_macos_diagnostic(diagnostics, message);
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }

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

typedef struct {
    io_service_t service_proxy;
    const RSSDDCDiagnostics *diagnostics;
} ConventionalGetValidationContext;

static RSSDDCError conventional_get_validate_construct(void *opaque, void **service_out) {
    ConventionalGetValidationContext *context = opaque;
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

static RSSDDCError conventional_get_validate_write(void *opaque, void *service, uint32_t chip, uint32_t data,
                                                   const uint8_t *payload, size_t payload_length) {
    ConventionalGetValidationContext *context = opaque;
    char message[256] = {};
    diagnostic_bytes(context->diagnostics, "request", payload, payload_length);
    IOReturn write_result = IOAVServiceWriteI2C((IOAVServiceRef)(uintptr_t)service, chip, data,
                                                  (void *)(uintptr_t)payload, (uint32_t)payload_length);
    snprintf(message, sizeof(message), "write chip=0x%02x data=0x%08x length=%zu IOReturn=0x%08x", chip, data,
             payload_length, (unsigned int)write_result);
    rss_macos_diagnostic(context->diagnostics, message);
    if (write_result != KERN_SUCCESS) {
        rss_macos_diagnostic(context->diagnostics, "read=skipped because write failed");
        return RSS_DDC_ERROR_WRITE;
    }
    return RSS_DDC_OK;
}

static RSSDDCError conventional_get_validate_delay(void *opaque) {
    ConventionalGetValidationContext *context = opaque;
    rss_macos_diagnostic(context->diagnostics, "delay=50ms");
    usleep(RSS_DDC_DCPDP_SERVICE_GET_VALIDATION_DELAY_US);
    return RSS_DDC_OK;
}

static RSSDDCError conventional_get_validate_read(void *opaque, void *service, uint32_t chip, uint32_t data,
                                                  uint8_t *reply, size_t reply_length) {
    ConventionalGetValidationContext *context = opaque;
    char message[256] = {};
    IOReturn read_result = IOAVServiceReadI2C((IOAVServiceRef)(uintptr_t)service, chip, data, reply,
                                              (uint32_t)reply_length);
    snprintf(message, sizeof(message), "read chip=0x%02x data=0x%08x length=%zu IOReturn=0x%08x", chip, data,
             reply_length, (unsigned int)read_result);
    rss_macos_diagnostic(context->diagnostics, message);
    if (read_result != KERN_SUCCESS) return RSS_DDC_ERROR_READ;
    diagnostic_bytes(context->diagnostics, "reply", reply, reply_length);
    return RSS_DDC_OK;
}

static void conventional_get_validate_release(void *opaque, void *service) {
    (void)opaque;
    CFRelease((IOAVServiceRef)(uintptr_t)service);
}

/**
 * Runs one validation-only conventional GET on the selected dcpav-service-epic
 * proxy. DCPDPServiceProxy is not used; DCPDPDeviceProxy is not involved.
 */
RSSDDCError rss_macos_run_dcpdpservice_get_validation(io_service_t service_proxy,
                                                      RSSDDCDCPDPServiceCorrelationResult correlation,
                                                      RSSDDCVCPResult *result,
                                                      const RSSDDCDiagnostics *diagnostics) {
    if (service_proxy == MACH_PORT_NULL || result == NULL) return RSS_DDC_ERROR_ARGUMENT;
    char message[256] = {};
    snprintf(message, sizeof(message),
             "backend=DCPDPService operation=ValidateGetVCP framing=conventional requested-vcp=0x%02x",
             RSS_DDC_DCPDP_SERVICE_GET_VALIDATION_VCP);
    rss_macos_diagnostic(diagnostics, message);
    ConventionalGetValidationContext context = {.service_proxy = service_proxy, .diagnostics = diagnostics};
    const RSSDDCConventionalGetValidationCallbacks callbacks = {
        .context = &context,
        .construct = conventional_get_validate_construct,
        .write_i2c = conventional_get_validate_write,
        .delay = conventional_get_validate_delay,
        .read_i2c = conventional_get_validate_read,
        .release = conventional_get_validate_release,
    };
    RSSDDCError error = rss_ddc_run_dcpdpservice_get_validation(correlation, &callbacks, result);
    if (error == RSS_DDC_OK) {
        snprintf(message, sizeof(message), "decoded vcp=0x%02x maximum=%u current=%u checksum=valid",
                 result->vcp_code, result->maximum_value, result->current_value);
        rss_macos_diagnostic(diagnostics, message);
    } else if (error != RSS_DDC_ERROR_WRITE && error != RSS_DDC_ERROR_READ &&
               error != RSS_DDC_ERROR_SERVICE_CONSTRUCTION && error != RSS_DDC_ERROR_SAFETY_GATE) {
        rss_macos_diagnostic(diagnostics, rss_ddc_error_string(error));
    }
    return error;
}
