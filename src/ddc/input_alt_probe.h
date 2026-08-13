#ifndef RSS_DDC_INPUT_ALT_PROBE_H
#define RSS_DDC_INPUT_ALT_PROBE_H

#include <stddef.h>
#include <stdint.h>

#include "rss_ddc.h"
#include "protocol.h"

enum {
    RSS_DDC_INPUT_ALT_PROBE_VCP = 0x60u,
    RSS_DDC_INPUT_ALT_PROBE_CHIP = 0x37u,
    RSS_DDC_INPUT_ALT_PROBE_DATA = 0x50u,
    RSS_DDC_INPUT_ALT_PROBE_WRITE_COUNT = 2u,
    RSS_DDC_INPUT_ALT_PROBE_PREWRITE_DELAY_US = 10000u,
};

typedef enum {
    RSS_DDC_INPUT_ALT_PROBE_CONVENTIONAL = 0,
    /* No evidence supports an inline source-address form; this always fails closed. */
    RSS_DDC_INPUT_ALT_PROBE_INLINE_UNSUPPORTED,
} RSSDDCInputAltProbeVariant;

typedef struct {
    uint32_t chip;
    uint32_t data;
    uint8_t payload[RSS_DDC_CONVENTIONAL_SET_VCP_REQUEST_SIZE];
    size_t payload_length;
    unsigned int write_count;
    uint32_t prewrite_delay_us;
} RSSDDCInputAltProbePlan;

typedef struct {
    void *context;
    RSSDDCError (*construct)(void *context, void **service_out);
    RSSDDCError (*prewrite_delay)(void *context);
    RSSDDCError (*write_i2c)(void *context, void *service, uint32_t chip, uint32_t data,
                             const uint8_t *payload, size_t payload_length);
    void (*release)(void *context, void *service);
} RSSDDCInputAltProbeCallbacks;

const char *rss_ddc_input_alt_probe_variant_name(RSSDDCInputAltProbeVariant variant);
RSSDDCError rss_ddc_input_alt_probe_variant_from_string(const char *text, RSSDDCInputAltProbeVariant *variant_out);
RSSDDCError rss_ddc_prepare_dcpdp13_input_alt_probe(RSSDDCInputAltProbeVariant variant, uint8_t value,
                                                     RSSDDCInputAltProbePlan *plan_out);
RSSDDCError rss_ddc_run_dcpdp13_input_alt_probe(RSSDDCProvider provider, RSSDDCInputAltProbeVariant variant,
                                                 uint8_t value, const RSSDDCInputAltProbeCallbacks *callbacks);

#endif
