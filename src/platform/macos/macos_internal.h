#ifndef RSS_DDC_MACOS_INTERNAL_H
#define RSS_DDC_MACOS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <IOKit/IOKitLib.h>

#include "rss_ddc.h"

typedef struct {
    RSSDDCDisplay display;
    io_service_t service_proxy;
    bool ps190_safety_gate;
} RSSMacOSBinding;

RSSDDCError rss_macos_discover_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count);
RSSDDCError rss_macos_resolve_binding(uint32_t list_index, RSSMacOSBinding *binding);
void rss_macos_release_binding(RSSMacOSBinding *binding);
RSSDDCError rss_macos_ps190_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result);
RSSDDCError rss_macos_dp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result);
RSSDDCError rss_macos_mcdp_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result);
RSSDDCError rss_macos_provider_get_vcp(RSSMacOSBinding *binding, uint8_t vcp_code, RSSDDCVCPResult *result);

#endif
