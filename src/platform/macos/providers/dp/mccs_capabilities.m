@import Foundation;

#include <stdio.h>
#include <unistd.h>

#include "macos_internal.h"
#include "mccs_retrieval.h"
#include "protocol.h"
#include "private/ioav_private.h"

enum { RSS_DDC_MCCS_SETTLE_DELAY_US = 50000 };

typedef struct {
    RSSMacOSBinding *binding;
    const RSSDDCDiagnostics *diagnostics;
} RSSMacOSMCCSTransportContext;

/** Reads one conventional F3/E3 exchange; the portable caller owns buffer initialization and parsing. */
static RSSDDCError dcpdp13_read_mccs_fragment(void *opaque, uint16_t requested_offset,
                                               uint8_t *reply, size_t reply_capacity) {
    RSSMacOSMCCSTransportContext *context = opaque;
    if (context == NULL || context->binding == NULL || reply == NULL ||
        reply_capacity != RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE ||
        context->binding->display.provider != RSS_DDC_PROVIDER_DCPDP13 || !context->binding->dp_safety_gate) {
        return RSS_DDC_ERROR_SAFETY_GATE;
    }
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, context->binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(context->diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }
    uint8_t request[RSS_DDC_CONVENTIONAL_CAPABILITIES_REQUEST_SIZE] = {};
    rss_ddc_build_conventional_capabilities_request(requested_offset, request);
    IOReturn write_result = IOAVServiceWriteI2C(service, 0x37, 0x51, request, sizeof(request));
    if (write_result != KERN_SUCCESS) {
        CFRelease(service);
        rss_macos_diagnostic(context->diagnostics, "mccs F3 write failed; E3 read skipped");
        return RSS_DDC_ERROR_WRITE;
    }
    usleep(RSS_DDC_MCCS_SETTLE_DELAY_US);
    IOReturn read_result = IOAVServiceReadI2C(service, 0x37, 0x51, reply, (uint32_t)reply_capacity);
    CFRelease(service);
    if (read_result != KERN_SUCCESS) return RSS_DDC_ERROR_READ;
    return RSS_DDC_OK;
}

/** Executes only the DCPDP13-backed, bounded, read-only MCCS retrieval state machine. */
RSSDDCError rss_macos_dp_get_mccs_capabilities(RSSMacOSBinding *binding,
                                               RSSDDCMCCSCapabilities *capabilities,
                                               const RSSDDCDiagnostics *diagnostics) {
    if (binding == NULL || capabilities == NULL || binding->display.provider != RSS_DDC_PROVIDER_DCPDP13 ||
        !binding->dp_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    RSSMacOSMCCSTransportContext context = {.binding = binding, .diagnostics = diagnostics};
    RSSDDCMCCSTransport transport = {.context = &context, .read_fragment = dcpdp13_read_mccs_fragment};
    return rss_ddc_retrieve_mccs_capabilities(&transport, capabilities);
}
