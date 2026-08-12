#ifndef RSS_DDC_DPCD_VALIDATION_H
#define RSS_DDC_DPCD_VALIDATION_H

#include "rss_ddc.h"

/**
 * Internal testable lifecycle for a correlated DCPDP13 DPCD read. It owns
 * neither the candidate nor the constructed private object; callbacks make
 * those ownership rules explicit and let tests prove there is no retry.
 */
typedef struct {
    void *context;
    RSSDDCError (*construct)(void *context, void **device);
    RSSDDCError (*read)(void *context, void *device, uint32_t address, uint8_t *bytes, size_t length);
    void (*release)(void *context, void *device);
} RSSDDCDPCDValidationCallbacks;

RSSDDCError rss_ddc_run_dpcd_candidate_read(unsigned int candidate_count,
                                            const RSSDDCDPCDValidationCallbacks *callbacks, uint32_t address,
                                            uint8_t *bytes, size_t length);

#endif
