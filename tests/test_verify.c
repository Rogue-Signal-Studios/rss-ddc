#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "verify.h"

typedef struct {
    RSSDDCError set_result;
    RSSDDCError get_results[8];
    RSSDDCVCPResult replies[8];
    size_t get_result_count;
    size_t get_cursor;
    unsigned int set_calls;
    unsigned int get_calls;
    unsigned int identity_rejections;
    uint32_t sleeps[8];
    size_t sleep_count;
    bool identity_available;
} MockTarget;

static RSSDDCError mock_set(void *opaque, uint8_t vcp_code, uint16_t value) {
    MockTarget *target = opaque;
    assert(vcp_code != 0 && value <= UINT16_MAX);
    ++target->set_calls;
    return target->set_result;
}

static RSSDDCError mock_get(void *opaque, uint8_t vcp_code, RSSDDCVCPResult *result) {
    MockTarget *target = opaque;
    assert(vcp_code != 0);
    /* A topology change fails closed before a sibling provider can be called. */
    if (!target->identity_available) {
        ++target->identity_rejections;
        return RSS_DDC_ERROR_VERIFY_UNAVAILABLE;
    }
    assert(target->get_cursor < target->get_result_count);
    ++target->get_calls;
    RSSDDCError error = target->get_results[target->get_cursor];
    if (error == RSS_DDC_OK) *result = target->replies[target->get_cursor];
    ++target->get_cursor;
    return error;
}

static void mock_sleep(void *opaque, uint32_t milliseconds) {
    MockTarget *target = opaque;
    assert(target->sleep_count < sizeof(target->sleeps) / sizeof(target->sleeps[0]));
    target->sleeps[target->sleep_count++] = milliseconds;
}

static RSSDDCVerifyOperations mock_operations(MockTarget *target) {
    return (RSSDDCVerifyOperations){
        .context = target,
        .set_vcp = mock_set,
        .get_vcp = mock_get,
        .sleep_ms = mock_sleep,
    };
}

static RSSDDCError run(MockTarget *target, const RSSDDCVerifyPolicy *policy, uint16_t value,
                       RSSDDCVCPResult *result) {
    RSSDDCVerifyOperations operations = mock_operations(target);
    return rss_ddc_orchestrate_set_vcp_and_verify(0x10, value, policy, result, &operations);
}

int main(void) {
    const RSSDDCVerifyPolicy one_retry = {.settle_ms = 100, .retry_count = 1, .retry_delay_ms = 250};
    RSSDDCVCPResult result = {};

    /* First matching GET verifies immediately after the configured settle only. */
    MockTarget immediate = {.set_result = RSS_DDC_OK, .get_results = {RSS_DDC_OK},
                            .replies = {{.vcp_code = 0x10, .maximum_value = 100, .current_value = 50}},
                            .get_result_count = 1, .identity_available = true};
    assert(run(&immediate, &one_retry, 50, &result) == RSS_DDC_OK);
    assert(immediate.set_calls == 1 && immediate.get_calls == 1 && immediate.sleep_count == 1 &&
           immediate.sleeps[0] == 100 && result.current_value == 50);

    /* A strict-parser error is retryable; a later valid GET proves the requested value. */
    MockTarget malformed_then_match = {.set_result = RSS_DDC_OK,
                                       .get_results = {RSS_DDC_ERROR_REPLY_SOURCE, RSS_DDC_OK},
                                       .replies = {{}, {.vcp_code = 0x10, .maximum_value = 100, .current_value = 50}},
                                       .get_result_count = 2, .identity_available = true};
    assert(run(&malformed_then_match, &one_retry, 50, &result) == RSS_DDC_OK);
    assert(malformed_then_match.set_calls == 1 && malformed_then_match.get_calls == 2 &&
           malformed_then_match.sleep_count == 2 && malformed_then_match.sleeps[0] == 100 &&
           malformed_then_match.sleeps[1] == 250);

    /* A valid but stale value is also retried during the caller-selected settling window. */
    MockTarget mismatch_then_match = {.set_result = RSS_DDC_OK, .get_results = {RSS_DDC_OK, RSS_DDC_OK},
                                      .replies = {{.vcp_code = 0x10, .maximum_value = 100, .current_value = 49},
                                                  {.vcp_code = 0x10, .maximum_value = 100, .current_value = 50}},
                                      .get_result_count = 2, .identity_available = true};
    assert(run(&mismatch_then_match, &one_retry, 50, &result) == RSS_DDC_OK);
    assert(mismatch_then_match.get_calls == 2 && mismatch_then_match.sleep_count == 2);

    /* retry_count is exactly additional GET attempts after the initial one. */
    MockTarget mismatches = {.set_result = RSS_DDC_OK, .get_results = {RSS_DDC_OK, RSS_DDC_OK},
                             .replies = {{.current_value = 49}, {.current_value = 49}},
                             .get_result_count = 2, .identity_available = true};
    assert(run(&mismatches, &one_retry, 50, &result) == RSS_DDC_ERROR_VERIFY_MISMATCH);
    assert(mismatches.set_calls == 1 && mismatches.get_calls == 2 && mismatches.sleep_count == 2);

    MockTarget malformed_exhausted = {.set_result = RSS_DDC_OK,
                                      .get_results = {RSS_DDC_ERROR_REPLY_SOURCE, RSS_DDC_ERROR_REPLY_SOURCE},
                                      .get_result_count = 2, .identity_available = true};
    assert(run(&malformed_exhausted, &one_retry, 50, &result) == RSS_DDC_ERROR_VERIFY_RETRY_EXHAUSTED);
    assert(malformed_exhausted.get_calls == 2 && malformed_exhausted.sleep_count == 2);

    /* A failed provider SET is never followed by a verification GET or delay. */
    MockTarget set_failed = {.set_result = RSS_DDC_ERROR_WRITE, .identity_available = true};
    assert(run(&set_failed, &one_retry, 50, &result) == RSS_DDC_ERROR_WRITE);
    assert(set_failed.set_calls == 1 && set_failed.get_calls == 0 && set_failed.sleep_count == 0);

    RSSDDCVerifyPolicy invalid_retries = {.settle_ms = 0, .retry_count = RSS_DDC_VERIFY_MAX_RETRIES + 1,
                                          .retry_delay_ms = 0};
    assert(!rss_ddc_verify_policy_is_valid(&invalid_retries));
    assert(run(&immediate, &invalid_retries, 50, &result) == RSS_DDC_ERROR_ARGUMENT);

    /* Re-enumeration cannot fall through to a sibling display at the old index. */
    MockTarget disappeared = {.set_result = RSS_DDC_OK, .identity_available = false};
    assert(run(&disappeared, &one_retry, 50, &result) == RSS_DDC_ERROR_VERIFY_UNAVAILABLE);
    assert(disappeared.set_calls == 1 && disappeared.identity_rejections == 1 && disappeared.get_calls == 0);

    /* Values use the whole DDC/CI field; no 8-bit comparison truncation is allowed. */
    MockTarget full_width = {.set_result = RSS_DDC_OK, .get_results = {RSS_DDC_OK},
                             .replies = {{.vcp_code = 0x10, .maximum_value = UINT16_MAX,
                                          .current_value = UINT16_MAX}},
                             .get_result_count = 1, .identity_available = true};
    const RSSDDCVerifyPolicy no_delay = {.settle_ms = 0, .retry_count = 0, .retry_delay_ms = 0};
    assert(run(&full_width, &no_delay, UINT16_MAX, &result) == RSS_DDC_OK);
    assert(result.current_value == UINT16_MAX && full_width.sleep_count == 0);

    RSSDDCVerifyPolicy defaults = rss_ddc_default_verify_policy();
    assert(defaults.settle_ms == 100 && defaults.retry_count == 3 && defaults.retry_delay_ms == 250);
    assert(rss_ddc_verify_error_is_retryable(RSS_DDC_ERROR_REPLY_SOURCE));
    assert(!rss_ddc_verify_error_is_retryable(RSS_DDC_ERROR_UNSUPPORTED_PROVIDER));
    puts("test_verify: passed");
    return 0;
}
