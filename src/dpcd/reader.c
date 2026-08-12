#include "reader.h"
#include "dpcd.h"

RSSDDCError rss_ddc_run_dpcd_candidate_read(unsigned int candidate_count,
                                             const RSSDDCDPCDReadCallbacks *callbacks, uint32_t address,
                                             uint8_t *bytes, size_t length) {
    if (callbacks == NULL || callbacks->construct == NULL || callbacks->read == NULL || callbacks->release == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    RSSDDCError range_error = rss_ddc_validate_dpcd_request(address, bytes, length);
    if (range_error != RSS_DDC_OK) return range_error;
    if (candidate_count != 1) return RSS_DDC_ERROR_SAFETY_GATE;
    void *device = NULL;
    RSSDDCError error = callbacks->construct(callbacks->context, &device);
    if (error != RSS_DDC_OK) return error;
    if (device == NULL) return RSS_DDC_ERROR_SERVICE_CONSTRUCTION;
    /* One construction, one caller-requested bounded read, and no retry or fallback. */
    error = callbacks->read(callbacks->context, device, address, bytes, length);
    callbacks->release(callbacks->context, device);
    return error;
}
