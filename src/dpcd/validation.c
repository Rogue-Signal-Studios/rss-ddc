#include "validation.h"

RSSDDCError rss_ddc_run_dpcd_validation(unsigned int candidate_count,
                                        const RSSDDCDPCDValidationCallbacks *callbacks, uint8_t bytes[16]) {
    if (bytes == NULL || callbacks == NULL || callbacks->construct == NULL || callbacks->read == NULL ||
        callbacks->release == NULL) return RSS_DDC_ERROR_ARGUMENT;
    if (candidate_count != 1) return RSS_DDC_ERROR_SAFETY_GATE;
    void *device = NULL;
    RSSDDCError error = callbacks->construct(callbacks->context, &device);
    if (error != RSS_DDC_OK) return error;
    if (device == NULL) return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    /* The harness proves exactly this bounded capability read, not general DP DPCD support. */
    error = callbacks->read(callbacks->context, device, 0x00000, bytes, RSS_DDC_DPCD_MAX_READ_BYTES);
    callbacks->release(callbacks->context, device);
    return error;
}
