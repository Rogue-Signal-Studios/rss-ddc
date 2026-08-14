#ifndef RSS_DDC_MCCS_RETRIEVAL_H
#define RSS_DDC_MCCS_RETRIEVAL_H

#include <stddef.h>
#include <stdint.h>

#include "rss_ddc.h"

/**
 * Reads one F3-requested E3 response into exactly `reply_capacity` bytes.
 * The caller initializes that bounded window and validates it before any
 * payload is consumed. A successful callback result does not imply a valid
 * MCCS response.
 */
typedef RSSDDCError (*RSSDDCMCCSReadFragment)(void *context, uint16_t requested_offset,
                                              uint8_t *reply, size_t reply_capacity);

typedef struct {
    void *context;
    RSSDDCMCCSReadFragment read_fragment;
} RSSDDCMCCSTransport;

/** Executes bounded retrieval and parses only validated E3 text payloads. */
RSSDDCError rss_ddc_retrieve_mccs_capabilities(const RSSDDCMCCSTransport *transport,
                                               RSSDDCMCCSCapabilities *capabilities);

#endif
