@import Foundation;

#include <unistd.h>

#include "macos_internal.h"
#include "protocol.h"

typedef CFTypeRef IOAVServiceRef;
extern IOAVServiceRef IOAVServiceCreateWithService(CFAllocatorRef, io_service_t);
extern CFTypeID IOAVServiceGetTypeID(void);
extern IOReturn IOAVServiceReadI2C(IOAVServiceRef, uint32_t, uint32_t, void *, uint32_t);
extern IOReturn IOAVServiceWriteI2C(IOAVServiceRef, uint32_t, uint32_t, void *, uint32_t);

RSSDDCError rss_macos_ps190_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result) {
    if (binding == NULL || result == NULL || !binding->ps190_safety_gate) return RSS_DDC_ERROR_SAFETY_GATE;
    IOAVServiceRef service = IOAVServiceCreateWithService(kCFAllocatorDefault, binding->service_proxy);
    if (service == NULL || CFGetTypeID(service) != IOAVServiceGetTypeID()) {
        if (service != NULL) CFRelease(service);
        return RSS_DDC_ERROR_TRANSPORT;
    }
    uint8_t request[RSS_DDC_GET_VCP_REQUEST_SIZE];
    uint8_t reply[RSS_DDC_GET_VCP_REPLY_SIZE] = {0};
    rss_ddc_build_raw_get_vcp(vcp_code, request);
    const uint32_t no_offset = UINT32_MAX;
    IOReturn write_result = IOAVServiceWriteI2C(service, 0x37, no_offset, request, sizeof(request));
    if (write_result == KERN_SUCCESS) usleep(50000);
    IOReturn read_result = write_result == KERN_SUCCESS ?
        IOAVServiceReadI2C(service, 0x37, no_offset, reply, sizeof(reply)) : write_result;
    CFRelease(service);
    if (read_result != KERN_SUCCESS) return RSS_DDC_ERROR_TRANSPORT;
    return rss_ddc_parse_get_vcp_reply(reply, sizeof(reply), vcp_code, result);
}
