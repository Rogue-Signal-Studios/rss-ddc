@import Foundation;

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "macos_internal.h"
#include "protocol.h"
#include "private/ioav_private.h"

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

static void diagnostic_capabilities_text(const RSSDDCDiagnostics *diagnostics,
                                         const RSSDDCCapabilitiesFragment *fragment) {
    char text[RSS_DDC_CAPABILITIES_REPLY_MAX_DATA_BYTES - 2] = {};
    size_t text_length = fragment->length < sizeof(text) - 1 ? fragment->length : sizeof(text) - 1;
    for (size_t index = 0; index < text_length; ++index) {
        uint8_t value = fragment->bytes[index];
        text[index] = value >= 0x20 && value <= 0x7e ? (char)value : '.';
    }
    char message[160] = {};
    snprintf(message, sizeof(message), "fragment offset=0x%04x text-length=%zu text=%s",
             fragment->offset, fragment->length, text);
    rss_macos_diagnostic(diagnostics, message);
}

typedef struct {
    uint8_t before[16];
    uint8_t bytes[RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE];
    uint8_t after[16];
} RSSMCCSProbeReplyWindow;

static bool probe_reply_canaries_intact(const RSSMCCSProbeReplyWindow *window) {
    for (size_t index = 0; index < sizeof(window->before); ++index) {
        if (window->before[index] != 0xa5 || window->after[index] != 0x5a) return false;
    }
    return true;
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

/**
 * One-transaction developer probe. DCPDP13 is deliberately isolated because
 * its conventional Service tuple is hardware validated for Get VCP; this does
 * not promote MCCS retrieval or imply anything about PS190/DCPDPService.
 */
static RSSDDCError dcpdp13_probe_mccs_capabilities_one_fragment(RSSMacOSBinding *binding,
                                                                 const RSSDDCDiagnostics *diagnostics,
                                                                 uint16_t requested_offset,
                                                                 size_t requested_reply_size,
                                                                 bool require_exact_observed_frame,
                                                                 RSSDDCCapabilitiesCollector *collector) {
    if (binding == NULL || !binding->dp_safety_gate || binding->display.provider != RSS_DDC_PROVIDER_DCPDP13) {
        return RSS_DDC_ERROR_SAFETY_GATE;
    }
    if (requested_reply_size < 6 || requested_reply_size > RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    char message[256] = {};
    snprintf(message, sizeof(message),
             "operation=ProbeMCCSCapabilities provider=DCPDP13Service scope=one-f3-request offset=0x%04x read-length=%zu",
             requested_offset, requested_reply_size);
    rss_macos_diagnostic(diagnostics, message);
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        rss_macos_diagnostic(diagnostics, "IOAVServiceCreateWithService=failed");
        return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    }

    uint8_t request[RSS_DDC_CONVENTIONAL_CAPABILITIES_REQUEST_SIZE] = {};
    RSSMCCSProbeReplyWindow reply = {};
    rss_ddc_build_conventional_capabilities_request(requested_offset, request);
    memset(reply.before, 0xa5, sizeof(reply.before));
    memset(reply.bytes, 0xcc, sizeof(reply.bytes));
    memset(reply.after, 0x5a, sizeof(reply.after));
    diagnostic_bytes(diagnostics, "request", request, sizeof(request));
    diagnostic_bytes(diagnostics, "reply-before", reply.bytes, sizeof(reply.bytes));

    IOReturn write_result = IOAVServiceWriteI2C(service, 0x37, 0x51, request, sizeof(request));
    snprintf(message, sizeof(message), "write chip=0x37 data=0x00000051 length=5 IOReturn=0x%08x",
             (unsigned int)write_result);
    rss_macos_diagnostic(diagnostics, message);
    if (write_result != KERN_SUCCESS) {
        CFRelease(service);
        rss_macos_diagnostic(diagnostics, "read=skipped because write failed");
        return RSS_DDC_ERROR_WRITE;
    }

    /* Existing conventional-DP GET evidence uses 50 ms; MCCS-specific timing remains unvalidated. */
    rss_macos_diagnostic(diagnostics, "delay=50ms basis=existing-conventional-get-evidence");
    usleep(50000);
    IOReturn read_result = IOAVServiceReadI2C(service, 0x37, 0x51, reply.bytes, (uint32_t)requested_reply_size);
    CFRelease(service);
    snprintf(message, sizeof(message), "read chip=0x37 data=0x00000051 requested-length=%zu IOReturn=0x%08x",
             requested_reply_size, (unsigned int)read_result);
    rss_macos_diagnostic(diagnostics, message);
    diagnostic_bytes(diagnostics, "reply-after", reply.bytes, sizeof(reply.bytes));
    rss_macos_diagnostic(diagnostics, probe_reply_canaries_intact(&reply) ?
                         "reply-window-canaries=intact" : "reply-window-canaries=CHANGED");
    if (read_result != KERN_SUCCESS) return RSS_DDC_ERROR_READ;
    if (!probe_reply_canaries_intact(&reply)) return RSS_DDC_ERROR_SYSTEM;

    size_t frame_size = 0;
    RSSDDCError error = rss_ddc_capabilities_reply_frame_size(reply.bytes, requested_reply_size, &frame_size);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, "E3 frame-size=invalid; tail and payload were not interpreted");
        return error;
    }
    size_t unchanged_tail = 0;
    for (size_t index = frame_size; index < requested_reply_size; ++index) {
        if (reply.bytes[index] == 0xcc) ++unchanged_tail;
    }
    snprintf(message, sizeof(message), "E3 declared-frame-bytes=%zu tail-sentinel-bytes=%zu/%zu",
             frame_size, unchanged_tail, requested_reply_size - frame_size);
    rss_macos_diagnostic(diagnostics, message);
    if (requested_reply_size > frame_size) {
        diagnostic_bytes(diagnostics, "E3 ignored-tail", reply.bytes + frame_size, requested_reply_size - frame_size);
        rss_macos_diagnostic(diagnostics, "E3 parser=declared-prefix-only; tail=ignored");
    } else {
        rss_macos_diagnostic(diagnostics, "E3 parser=declared-prefix-only; tail=empty");
    }

    if (require_exact_observed_frame) {
        static const uint8_t observed_lg_first_frame[] = {
            0x6e, 0x8d, 0xe3, 0x00, 0x00, 0x28, 0x70, 0x72,
            0x6f, 0x74, 0x28, 0x6d, 0x6f, 0x6e, 0x69, 0x4c,
        };
        size_t unchanged_unrequested = 0;
        for (size_t index = requested_reply_size; index < sizeof(reply.bytes); ++index) {
            if (reply.bytes[index] == 0xcc) ++unchanged_unrequested;
        }
        snprintf(message, sizeof(message), "unrequested-window-sentinel-bytes=%zu/%zu",
                 unchanged_unrequested, sizeof(reply.bytes) - requested_reply_size);
        rss_macos_diagnostic(diagnostics, message);
        if (unchanged_unrequested != sizeof(reply.bytes) - requested_reply_size) {
            rss_macos_diagnostic(diagnostics, "exact-first-frame=FAILED; IOAV modified bytes outside its requested range");
            return RSS_DDC_ERROR_SYSTEM;
        }
        if (frame_size != requested_reply_size ||
            memcmp(reply.bytes, observed_lg_first_frame, sizeof(observed_lg_first_frame)) != 0) {
            rss_macos_diagnostic(diagnostics, "exact-first-frame=FAILED; no next offset was requested");
            return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
        }
        rss_macos_diagnostic(diagnostics, "exact-first-frame=match");
    }

    RSSDDCCapabilitiesFragment fragment = {};
    error = rss_ddc_parse_capabilities_reply(reply.bytes, frame_size, &fragment);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, rss_ddc_error_string(error));
        return error;
    }
    if (rss_ddc_validate_capabilities_fragment_offset(&fragment, requested_offset) != RSS_DDC_OK) {
        snprintf(message, sizeof(message), "E3 echoed-offset=0x%04x expected=0x%04x", fragment.offset, requested_offset);
        rss_macos_diagnostic(diagnostics, message);
        return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    }
    diagnostic_capabilities_text(diagnostics, &fragment);
    if (collector != NULL) {
        error = rss_ddc_capabilities_collector_append(collector, &fragment);
        if (error != RSS_DDC_OK) {
            rss_macos_diagnostic(diagnostics, rss_ddc_error_string(error));
            return error;
        }
        snprintf(message, sizeof(message), "multipart request-number=%zu next-offset=0x%04x complete=%s",
                 collector->request_count, collector->next_offset, collector->complete ? "yes" : "no");
        rss_macos_diagnostic(diagnostics, message);
    } else {
        rss_macos_diagnostic(diagnostics, "probe=complete; no next offset was requested");
    }
    return RSS_DDC_OK;
}

RSSDDCError rss_macos_dcpdp13_probe_mccs_capabilities(RSSMacOSBinding *binding,
                                                       const RSSDDCDiagnostics *diagnostics) {
    return dcpdp13_probe_mccs_capabilities_one_fragment(binding, diagnostics, 0,
                                                        RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE, false, NULL);
}

RSSDDCError rss_macos_dcpdp13_probe_mccs_capabilities_exact_first_frame(
    RSSMacOSBinding *binding, const RSSDDCDiagnostics *diagnostics) {
    static const char observed_product[] = "LG HDR QHD";
    enum { observed_frame_size = 16 };
    if (binding == NULL || strcmp(binding->display.product_name, observed_product) != 0) {
        rss_macos_diagnostic(diagnostics,
                             "operation=ProbeMCCSCapabilitiesExactFirstFrame status=refused; recorded LG HDR QHD only");
        return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    return dcpdp13_probe_mccs_capabilities_one_fragment(binding, diagnostics, 0, observed_frame_size, true, NULL);
}

RSSDDCError rss_macos_dcpdp13_probe_mccs_capabilities_next_fragment(
    RSSMacOSBinding *binding, const RSSDDCDiagnostics *diagnostics) {
    static const char observed_product[] = "LG HDR QHD";
    enum { next_offset = 10 };
    if (binding == NULL || strcmp(binding->display.product_name, observed_product) != 0) {
        rss_macos_diagnostic(diagnostics,
                             "operation=ProbeMCCSCapabilitiesNextFragment status=refused; recorded LG HDR QHD only");
        return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    return dcpdp13_probe_mccs_capabilities_one_fragment(binding, diagnostics, next_offset,
                                                        RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE, false, NULL);
}

RSSDDCError rss_macos_dcpdp13_probe_mccs_capabilities_full(
    RSSMacOSBinding *binding, const RSSDDCDiagnostics *diagnostics) {
    static const char observed_product[] = "LG HDR QHD";
    if (binding == NULL || strcmp(binding->display.product_name, observed_product) != 0) {
        rss_macos_diagnostic(diagnostics,
                             "operation=ProbeMCCSCapabilitiesFull status=refused; recorded LG HDR QHD only");
        return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
    }
    rss_macos_diagnostic(diagnostics,
                         "operation=ProbeMCCSCapabilitiesFull provider=DCPDP13Service scope=developer-validation-only");
    RSSDDCCapabilitiesCollector collector = {};
    while (!collector.complete) {
        if (collector.request_count >= RSS_DDC_CAPABILITIES_MAX_REQUESTS) {
            rss_macos_diagnostic(diagnostics, "multipart=request-limit-exceeded; stopped");
            return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
        }
        char message[160] = {};
        snprintf(message, sizeof(message), "multipart request-number=%zu requested-offset=0x%04x",
                 collector.request_count + 1, collector.next_offset);
        rss_macos_diagnostic(diagnostics, message);
        RSSDDCError error = dcpdp13_probe_mccs_capabilities_one_fragment(
            binding, diagnostics, collector.next_offset, RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE, false, &collector);
        if (error != RSS_DDC_OK) return error;
    }

    char raw_message[RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES + 64] = {};
    snprintf(raw_message, sizeof(raw_message), "assembled-capabilities=%s", collector.bytes);
    rss_macos_diagnostic(diagnostics, raw_message);
    char summary[160] = {};
    snprintf(summary, sizeof(summary), "multipart complete fragments=%zu text-bytes=%zu explicit-zero-length=yes",
             collector.request_count, collector.byte_count);
    rss_macos_diagnostic(diagnostics, summary);

    RSSDDCMCCSCapabilities capabilities = {};
    RSSDDCError error = rss_ddc_parse_mccs_capabilities((const char *)collector.bytes, collector.byte_count,
                                                         &capabilities);
    if (error != RSS_DDC_OK) {
        rss_macos_diagnostic(diagnostics, rss_ddc_error_string(error));
        return error;
    }
    rss_macos_diagnostic(diagnostics, "assembled-parser=valid");
    for (size_t index = 0; index < capabilities.feature_count; ++index) {
        snprintf(summary, sizeof(summary), "advertised-vcp=0x%02x", capabilities.features[index].vcp_code);
        rss_macos_diagnostic(diagnostics, summary);
    }
    if (!rss_ddc_mccs_capabilities_has_vcp(&capabilities, 0x60)) {
        rss_macos_diagnostic(diagnostics, "advertised-vcp-0x60=no");
        return RSS_DDC_OK;
    }
    const uint8_t *values = NULL;
    size_t value_count = 0;
    error = rss_ddc_mccs_capabilities_enum_values(&capabilities, 0x60, &values, &value_count);
    if (error != RSS_DDC_OK) return error;
    char values_message[RSS_DDC_MCCS_CAPABILITIES_MAX_ENUM_VALUES * 3 + 64] = {};
    int written = snprintf(values_message, sizeof(values_message), "advertised-vcp-0x60-values=");
    for (size_t index = 0; index < value_count && written > 0 && (size_t)written < sizeof(values_message); ++index) {
        written += snprintf(values_message + written, sizeof(values_message) - (size_t)written,
                            "%s%02x", index == 0 ? "" : " ", values[index]);
    }
    rss_macos_diagnostic(diagnostics, values_message);
    return RSS_DDC_OK;
}
