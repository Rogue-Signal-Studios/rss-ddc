#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mccs_retrieval.h"
#include "protocol.h"

typedef enum {
    MOCK_FRAMES,
    MOCK_UNTERMINATED,
    MOCK_AGGREGATE_OVERFLOW,
} MockMode;

typedef struct {
    uint8_t bytes[RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE];
    size_t write_count;
    RSSDDCError status;
} MockFrame;

typedef struct {
    MockMode mode;
    MockFrame frames[4];
    size_t frame_count;
    size_t calls;
    uint16_t requested_offsets[RSS_DDC_CAPABILITIES_MAX_REQUESTS + 1];
} MockTransport;

static void make_frame(MockFrame *frame, uint16_t offset, const char *text) {
    size_t text_length = text == NULL ? 0 : strlen(text);
    assert(text_length <= 32);
    *frame = (MockFrame){};
    size_t frame_size = text_length + 6;
    frame->bytes[0] = 0x6e;
    frame->bytes[1] = (uint8_t)(0x83 + text_length);
    frame->bytes[2] = 0xe3;
    frame->bytes[3] = (uint8_t)(offset >> 8);
    frame->bytes[4] = (uint8_t)offset;
    if (text_length != 0) memcpy(frame->bytes + 5, text, text_length);
    uint8_t checksum = 0x50;
    for (size_t index = 0; index + 1 < frame_size; ++index) checksum ^= frame->bytes[index];
    frame->bytes[frame_size - 1] = checksum;
    frame->write_count = frame_size;
}

static RSSDDCError mock_read_fragment(void *opaque, uint16_t requested_offset,
                                      uint8_t *reply, size_t reply_capacity) {
    MockTransport *mock = opaque;
    assert(reply_capacity == RSS_DDC_CAPABILITIES_REPLY_MAX_SIZE);
    for (size_t index = 0; index < reply_capacity; ++index) assert(reply[index] == 0xcc);
    assert(mock->calls < sizeof(mock->requested_offsets) / sizeof(mock->requested_offsets[0]));
    mock->requested_offsets[mock->calls++] = requested_offset;
    if (mock->mode == MOCK_UNTERMINATED || mock->mode == MOCK_AGGREGATE_OVERFLOW) {
        MockFrame frame = {};
        char text[33] = {};
        size_t text_length = mock->mode == MOCK_UNTERMINATED ? 1 : 32;
        memset(text, 'x', text_length);
        make_frame(&frame, requested_offset, text);
        memcpy(reply, frame.bytes, frame.write_count);
        return RSS_DDC_OK;
    }
    assert(mock->calls <= mock->frame_count);
    const MockFrame *frame = &mock->frames[mock->calls - 1];
    if (frame->status != RSS_DDC_OK) return frame->status;
    memcpy(reply, frame->bytes, frame->write_count);
    return RSS_DDC_OK;
}

static RSSDDCError retrieve(MockTransport *mock, RSSDDCMCCSCapabilities *capabilities) {
    RSSDDCMCCSTransport transport = {.context = mock, .read_fragment = mock_read_fragment};
    return rss_ddc_retrieve_mccs_capabilities(&transport, capabilities);
}

static void expect_failure(MockTransport *mock, RSSDDCError expected) {
    static RSSDDCMCCSCapabilities capabilities;
    static RSSDDCMCCSCapabilities before;
    memset(&capabilities, 0xa5, sizeof(capabilities));
    before = capabilities;
    assert(retrieve(mock, &capabilities) == expected);
    assert(memcmp(&capabilities, &before, sizeof(capabilities)) == 0);
}

int main(void) {
    RSSDDCMCCSCapabilities capabilities = {};
    MockTransport single = {.mode = MOCK_FRAMES, .frame_count = 2};
    make_frame(&single.frames[0], 0, "vcp(10)");
    make_frame(&single.frames[1], 7, NULL);
    assert(retrieve(&single, &capabilities) == RSS_DDC_OK);
    assert(single.calls == 2 && single.requested_offsets[0] == 0 && single.requested_offsets[1] == 7);
    assert(capabilities.raw_length == 7 && memcmp(capabilities.raw, "vcp(10)", 7) == 0);
    assert(rss_ddc_mccs_capabilities_has_vcp(&capabilities, 0x10));

    MockTransport multipart = {.mode = MOCK_FRAMES, .frame_count = 3};
    make_frame(&multipart.frames[0], 0, "(vcp(10");
    make_frame(&multipart.frames[1], 7, " 60(0f)))");
    make_frame(&multipart.frames[2], 16, NULL);
    assert(retrieve(&multipart, &capabilities) == RSS_DDC_OK);
    const uint8_t *values = NULL;
    size_t count = 0;
    assert(rss_ddc_mccs_capabilities_enum_values(&capabilities, 0x60, &values, &count) == RSS_DDC_OK);
    assert(count == 1 && values[0] == 0x0f && multipart.calls == 3);

    MockTransport stale_tail = {.mode = MOCK_FRAMES, .frame_count = 2};
    make_frame(&stale_tail.frames[0], 0, "vcp(10)");
    memcpy(stale_tail.frames[0].bytes + stale_tail.frames[0].write_count, "vcp(60(0f))", 12);
    stale_tail.frames[0].write_count = sizeof(stale_tail.frames[0].bytes);
    make_frame(&stale_tail.frames[1], 7, NULL);
    assert(retrieve(&stale_tail, &capabilities) == RSS_DDC_OK);
    assert(capabilities.raw_length == 7 && !rss_ddc_mccs_capabilities_has_vcp(&capabilities, 0x60));

    MockTransport malformed_length = {.mode = MOCK_FRAMES, .frame_count = 1};
    malformed_length.frames[0] = (MockFrame){.bytes = {0x6e, 0x04}, .write_count = 2};
    expect_failure(&malformed_length, RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    MockTransport oversized_length = {.mode = MOCK_FRAMES, .frame_count = 1};
    oversized_length.frames[0] = (MockFrame){.bytes = {0x6e, 0xa4}, .write_count = 2};
    expect_failure(&oversized_length, RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    MockTransport checksum_failure = {.mode = MOCK_FRAMES, .frame_count = 1};
    make_frame(&checksum_failure.frames[0], 0, "vcp(10)");
    checksum_failure.frames[0].bytes[checksum_failure.frames[0].write_count - 1] ^= 0xff;
    expect_failure(&checksum_failure, RSS_DDC_ERROR_REPLY_CHECKSUM);
    MockTransport wrong_command = {.mode = MOCK_FRAMES, .frame_count = 1};
    make_frame(&wrong_command.frames[0], 0, "vcp(10)");
    wrong_command.frames[0].bytes[2] = 0x02;
    expect_failure(&wrong_command, RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    MockTransport wrong_source = {.mode = MOCK_FRAMES, .frame_count = 1};
    make_frame(&wrong_source.frames[0], 0, "vcp(10)");
    wrong_source.frames[0].bytes[0] = 0x51;
    expect_failure(&wrong_source, RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    MockTransport wrong_offset = {.mode = MOCK_FRAMES, .frame_count = 1};
    make_frame(&wrong_offset.frames[0], 1, "vcp(10)");
    expect_failure(&wrong_offset, RSS_DDC_ERROR_CAPABILITIES_MALFORMED);

    MockTransport repeated_offset = {.mode = MOCK_FRAMES, .frame_count = 2};
    make_frame(&repeated_offset.frames[0], 0, "(vcp");
    make_frame(&repeated_offset.frames[1], 0, "(10)");
    expect_failure(&repeated_offset, RSS_DDC_ERROR_CAPABILITIES_MALFORMED);
    MockTransport truncated_with_stale_frame = {.mode = MOCK_FRAMES, .frame_count = 1};
    truncated_with_stale_frame.frames[0] = (MockFrame){.bytes = {0x6e, 0x8a, 0xe3, 0x00, 0x00}, .write_count = 5};
    MockFrame stale_complete = {};
    make_frame(&stale_complete, 0, "vcp(10)");
    memcpy(truncated_with_stale_frame.frames[0].bytes + 16, stale_complete.bytes, stale_complete.write_count);
    truncated_with_stale_frame.frames[0].write_count = 16 + stale_complete.write_count;
    expect_failure(&truncated_with_stale_frame, RSS_DDC_ERROR_REPLY_CHECKSUM);

    MockTransport unterminated = {.mode = MOCK_UNTERMINATED};
    expect_failure(&unterminated, RSS_DDC_ERROR_CAPABILITIES_REQUEST_LIMIT);
    assert(unterminated.calls == RSS_DDC_CAPABILITIES_MAX_REQUESTS);
    MockTransport aggregate_overflow = {.mode = MOCK_AGGREGATE_OVERFLOW};
    expect_failure(&aggregate_overflow, RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE);
    assert(aggregate_overflow.calls == 129);

    MockTransport transport_error = {.mode = MOCK_FRAMES, .frame_count = 1};
    transport_error.frames[0].status = RSS_DDC_ERROR_READ;
    expect_failure(&transport_error, RSS_DDC_ERROR_READ);
    puts("test_mccs_retrieval: passed");
    return 0;
}
