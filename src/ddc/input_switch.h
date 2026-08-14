#ifndef RSS_DDC_INPUT_SWITCH_H
#define RSS_DDC_INPUT_SWITCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rss_ddc.h"
#include "protocol.h"

enum {
    RSS_DDC_LG_ALT_INPUT_VCP = 0xf4u,
    RSS_DDC_LG_ALT_INPUT_CHIP = 0x37u,
    RSS_DDC_LG_ALT_INPUT_DATA = 0x50u,
    RSS_DDC_LG_ALT_INPUT_WRITE_COUNT = 2u,
    RSS_DDC_LG_ALT_INPUT_PREWRITE_DELAY_US = 10000u,
};

typedef struct {
    uint32_t chip;
    uint32_t data;
    uint8_t payload[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE];
    size_t payload_length;
    unsigned int write_count;
    uint32_t prewrite_delay_us;
} RSSDDCLGAltInputPlan;

typedef struct {
    void *context;
    RSSDDCError (*construct)(void *context, void **service_out);
    RSSDDCError (*prewrite_delay)(void *context);
    RSSDDCError (*write_i2c)(void *context, void *service, uint32_t chip, uint32_t data,
                             const uint8_t *payload, size_t payload_length);
    void (*release)(void *context, void *service);
} RSSDDCLGAltInputCallbacks;

/** Returns true only for the three values hardware-validated on the documented LG. */
bool rss_ddc_lg_alt_input_value_is_supported(uint16_t value);
/**
 * Validates the exact target evidence before an alternate write: DCPDP13,
 * its normal safety gate, product name LG HDR QHD, and service role DCPEXT0.
 */
RSSDDCError rss_ddc_validate_lg_alt_input_target(RSSDDCProvider provider, bool dp_safety_gate,
                                                 const char *product_name, const char *transport);
/** Builds the fixed, six-byte validated alternate-input request. */
RSSDDCError rss_ddc_prepare_lg_alt_input(uint16_t value, RSSDDCLGAltInputPlan *plan_out);
/** Runs exactly two delayed writes; this interface has no read, verify, retry, or fallback callback. */
RSSDDCError rss_ddc_run_lg_alt_input(RSSDDCProvider provider, uint16_t value,
                                     const RSSDDCLGAltInputCallbacks *callbacks);

#endif
