#include "mccs_retrieval.h"

#include <stdlib.h>
#include <string.h>

#include "protocol.h"

enum {
    RSS_DDC_MCCS_FRAGMENT_TEXT_MAX_BYTES = 32,
    RSS_DDC_MCCS_CANARY_SIZE = 16,
    RSS_DDC_MCCS_UNWRITTEN_SENTINEL = 0xcc,
};

typedef struct {
    uint8_t before[RSS_DDC_MCCS_CANARY_SIZE];
    uint8_t bytes[RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE];
    uint8_t after[RSS_DDC_MCCS_CANARY_SIZE];
} RSSDDCMCCSReplyWindow;

typedef struct {
    uint8_t *bytes;
    size_t byte_count;
    size_t request_count;
    uint16_t next_offset;
    bool complete;
} RSSDDCMCCSCollector;

/** Detects a callback write outside the exact receive window supplied to it. */
static bool reply_window_canaries_intact(const RSSDDCMCCSReplyWindow *window) {
    for (size_t index = 0; index < RSS_DDC_MCCS_CANARY_SIZE; ++index) {
        if (window->before[index] != 0xa5 || window->after[index] != 0x5a) return false;
    }
    return true;
}

/** Establishes canaries and an unwritten-byte sentinel before every transport read. */
static void reply_window_initialize(RSSDDCMCCSReplyWindow *window) {
    memset(window->before, 0xa5, sizeof(window->before));
    memset(window->bytes, RSS_DDC_MCCS_UNWRITTEN_SENTINEL, sizeof(window->bytes));
    memset(window->after, 0x5a, sizeof(window->after));
}

/** Allocates the bounded aggregate outside the retrieval caller's stack frame. */
static RSSDDCError collector_create(RSSDDCMCCSCollector *collector) {
    collector->bytes = calloc(RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES + 1, 1);
    return collector->bytes == NULL ? RSS_DDC_ERROR_SYSTEM : RSS_DDC_OK;
}

/** Releases the aggregate on every success and failure path. */
static void collector_destroy(RSSDDCMCCSCollector *collector) {
    if (collector == NULL) return;
    free(collector->bytes);
    *collector = (RSSDDCMCCSCollector){};
}

/** Appends exactly one validated, forward-progressing fragment or records completion. */
static RSSDDCError collector_append(RSSDDCMCCSCollector *collector,
                                    const RSSDDCCapabilitiesFragment *fragment) {
    if (collector == NULL || fragment == NULL || (fragment->length != 0 && fragment->bytes == NULL)) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (collector->complete || fragment->offset != collector->next_offset ||
        fragment->length > RSS_DDC_MCCS_FRAGMENT_TEXT_MAX_BYTES) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    if (collector->request_count >= RSS_DDC_CAPABILITIES_MAX_REQUESTS) {
        return RSS_DDC_ERROR_CAPABILITIES_REQUEST_LIMIT;
    }
    ++collector->request_count;
    if (fragment->length == 0) {
        collector->complete = true;
        collector->bytes[collector->byte_count] = '\0';
        return RSS_DDC_OK;
    }
    if (fragment->length > RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES - collector->byte_count) {
        return RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE;
    }
    if ((uint32_t)collector->next_offset + fragment->length > UINT16_MAX) {
        return RSS_DDC_ERROR_CAPABILITIES_OFFSET_OVERFLOW;
    }
    memcpy(collector->bytes + collector->byte_count, fragment->bytes, fragment->length);
    collector->byte_count += fragment->length;
    collector->next_offset = (uint16_t)(collector->next_offset + fragment->length);
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_retrieve_mccs_capabilities(const RSSDDCMCCSTransport *transport,
                                               RSSDDCMCCSCapabilities *capabilities) {
    if (transport == NULL || transport->read_fragment == NULL || capabilities == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSDDCMCCSCollector collector = {};
    RSSDDCError error = collector_create(&collector);
    if (error != RSS_DDC_OK) return error;

    while (!collector.complete) {
        if (collector.request_count >= RSS_DDC_CAPABILITIES_MAX_REQUESTS) {
            error = RSS_DDC_ERROR_CAPABILITIES_REQUEST_LIMIT;
            break;
        }
        RSSDDCMCCSReplyWindow window = {};
        reply_window_initialize(&window);
        error = transport->read_fragment(transport->context, collector.next_offset, window.bytes, sizeof(window.bytes));
        if (error != RSS_DDC_OK) break;
        if (!reply_window_canaries_intact(&window)) {
            error = RSS_DDC_ERROR_SYSTEM;
            break;
        }
        size_t frame_size = 0;
        error = rss_ddc_capabilities_reply_frame_size(window.bytes, sizeof(window.bytes), &frame_size);
        if (error != RSS_DDC_OK) break;
        RSSDDCCapabilitiesFragment fragment = {};
        error = rss_ddc_parse_capabilities_reply(window.bytes, frame_size, &fragment);
        if (error != RSS_DDC_OK) break;
        error = rss_ddc_validate_capabilities_fragment_offset(&fragment, collector.next_offset);
        if (error != RSS_DDC_OK) break;
        error = collector_append(&collector, &fragment);
        if (error != RSS_DDC_OK) break;
    }

    if (error == RSS_DDC_OK && !collector.complete) error = RSS_DDC_ERROR_CAPABILITIES_INCOMPLETE;
    if (error == RSS_DDC_OK) {
        RSSDDCMCCSCapabilities *parsed = calloc(1, sizeof(*parsed));
        if (parsed == NULL) error = RSS_DDC_ERROR_SYSTEM;
        else {
            error = rss_ddc_parse_mccs_capabilities((const char *)collector.bytes, collector.byte_count, parsed);
            if (error == RSS_DDC_OK) *capabilities = *parsed;
            free(parsed);
        }
    }
    collector_destroy(&collector);
    return error;
}
