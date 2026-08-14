@import Foundation;

#include <stdio.h>
#include <unistd.h>

#include "macos_internal.h"
#include "private/ioav_private.h"

typedef struct {
    RSSMacOSBinding *binding;
    const RSSDDCDiagnostics *diagnostics;
    unsigned int write_index;
    unsigned int requested_write_count;
} RSSMacOSLGAltInputContext;

/** Emits the fixed, short alternate request without exposing any registry handle. */
static void diagnostic_bytes(const RSSDDCDiagnostics *diagnostics, const uint8_t *bytes, size_t count) {
    char message[64] = "payload=";
    size_t used = 8;
    for (size_t index = 0; index < count && used + 4 < sizeof(message); ++index) {
        int written = snprintf(message + used, sizeof(message) - used, "%s%02x", index == 0 ? "" : " ", bytes[index]);
        if (written < 0) break;
        used += (size_t)written;
    }
    rss_macos_diagnostic(diagnostics, message);
}

static RSSDDCError lg_alt_construct(void *opaque, void **service_out) {
    RSSMacOSLGAltInputContext *context = opaque;
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, context->binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(context->diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    *service_out = (void *)service;
    return RSS_DDC_OK;
}

static RSSDDCError lg_alt_delay(void *opaque) {
    (void)opaque;
    usleep(RSS_DDC_LG_ALT_INPUT_PREWRITE_DELAY_US);
    return RSS_DDC_OK;
}

static RSSDDCError lg_alt_write(void *opaque, void *opaque_service, uint32_t chip, uint32_t data,
                                const uint8_t *payload, size_t payload_length) {
    RSSMacOSLGAltInputContext *context = opaque;
    IOReturn result = IOAVServiceWriteI2C((IOAVServiceRef)opaque_service, chip, data, (void *)payload,
                                          (uint32_t)payload_length);
    ++context->write_index;
    char message[160] = {};
    snprintf(message, sizeof(message), "method=LG_ALT write=%u/%u chip=0x%02x data=0x%02x length=%zu pre-delay=10ms IOReturn=0x%08x",
             context->write_index, context->requested_write_count, chip, data, payload_length,
             (unsigned int)result);
    rss_macos_diagnostic(context->diagnostics, message);
    return result == KERN_SUCCESS ? RSS_DDC_OK : RSS_DDC_ERROR_WRITE;
}

static void lg_alt_release(void *opaque, void *opaque_service) {
    (void)opaque;
    CFRelease((IOAVServiceRef)opaque_service);
}

/**
 * Executes only the documented LG HDR QHD / DCPDP13Service / DCPEXT0 command.
 * It intentionally has no GET, verification, retry, restore, or conventional-Set fallback.
 */
static RSSDDCError execute_lg_alt_input(RSSMacOSBinding *binding, uint16_t value,
                                        unsigned int write_count, bool experimental,
                                        const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (!rss_ddc_lg_alt_input_write_count_is_supported(write_count)) return RSS_DDC_ERROR_ARGUMENT;
    RSSDDCError error = rss_ddc_validate_lg_alt_input_target(binding->display.provider, binding->dp_safety_gate,
                                                              binding->display.product_name,
                                                              binding->display.transport);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, "operation=SetInput method=LG_ALT status=target-not-validated; write-skipped");
        return error;
    }
    RSSDDCLGAltInputPlan plan = {};
    error = rss_ddc_prepare_lg_alt_input(value, &plan);
    if (error != RSS_DDC_OK) return error;
    char message[224] = {};
    snprintf(message, sizeof(message), "operation=%s method=LG_ALT target=LG-HDR-QHD/DCPDP13Service/DCPEXT0 value=0x%02x requested-write-count=%u experimental=%s no-get=yes no-verify=yes no-retry=yes no-restore=yes no-fallback=yes",
             experimental ? "InputTest" : "SetInput", value, write_count, experimental ? "yes" : "no");
    rss_macos_diagnostic(diagnostics, message);
    diagnostic_bytes(diagnostics, plan.payload, plan.payload_length);
    RSSMacOSLGAltInputContext context = {.binding = binding, .diagnostics = diagnostics,
                                         .requested_write_count = write_count};
    RSSDDCLGAltInputCallbacks callbacks = {.context = &context, .construct = lg_alt_construct,
        .prewrite_delay = lg_alt_delay, .write_i2c = lg_alt_write, .release = lg_alt_release};
    return rss_ddc_run_lg_alt_input_with_write_count(binding->display.provider, value, write_count, &callbacks);
}

RSSDDCError rss_macos_dp_set_lg_alt_input(RSSMacOSBinding *binding, uint16_t value,
                                          const RSSDDCDiagnostics *diagnostics) {
    return execute_lg_alt_input(binding, value, RSS_DDC_LG_ALT_INPUT_WRITE_COUNT, false, diagnostics);
}

RSSDDCError rss_macos_dp_test_lg_alt_input(RSSMacOSBinding *binding, uint16_t value,
                                           unsigned int write_count,
                                           const RSSDDCDiagnostics *diagnostics) {
    return execute_lg_alt_input(binding, value, write_count, true, diagnostics);
}
