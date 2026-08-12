#ifndef RSS_DDC_DPCD_READER_H
#define RSS_DDC_DPCD_READER_H

#include "rss_ddc.h"

/*
 * Internal, reusable lifecycle for one correlated DPCD read. It owns neither
 * the registry candidate nor the constructed private object. The callback
 * boundary keeps one-construction/one-read/no-retry semantics testable.
 */
typedef struct {
    void *context;
    RSSDDCError (*construct)(void *context, void **device);
    RSSDDCError (*read)(void *context, void *device, uint32_t address, uint8_t *bytes, size_t length);
    void (*release)(void *context, void *device);
} RSSDDCDPCDReadCallbacks;

RSSDDCError rss_ddc_run_dpcd_candidate_read(unsigned int candidate_count,
                                             const RSSDDCDPCDReadCallbacks *callbacks, uint32_t address,
                                             uint8_t *bytes, size_t length);

#endif
