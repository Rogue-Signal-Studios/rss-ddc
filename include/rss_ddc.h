#ifndef RSS_DDC_H
#define RSS_DDC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RSS_DDC_PROVIDER_UNKNOWN = 0,
    RSS_DDC_PROVIDER_DCPDP13,
    RSS_DDC_PROVIDER_MCDP29XX,
    RSS_DDC_PROVIDER_PS190,
} RSSDDCProvider;

typedef enum {
    RSS_DDC_CAP_NONE = 0,
    RSS_DDC_CAP_GET_VCP = 1u << 0,
    RSS_DDC_CAP_SET_VCP = 1u << 1,
    RSS_DDC_CAP_READ_EDID = 1u << 2,
    RSS_DDC_CAP_READ_DPCD = 1u << 3,
} RSSDDCCapability;

typedef enum {
    RSS_DDC_OK = 0,
    RSS_DDC_ERROR_ARGUMENT,
    RSS_DDC_ERROR_NOT_FOUND,
    RSS_DDC_ERROR_UNSUPPORTED_PROVIDER,
    RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY,
    RSS_DDC_ERROR_DISCOVERY,
    RSS_DDC_ERROR_SAFETY_GATE,
    RSS_DDC_ERROR_TRANSPORT,
    RSS_DDC_ERROR_PROTOCOL,
    RSS_DDC_ERROR_SYSTEM,
} RSSDDCError;

enum {
    RSS_DDC_TEXT_MAX = 128,
};

typedef struct {
    uint32_t list_index;
    uint32_t cg_display_id;
    bool online;
    bool external;
    char product_name[RSS_DDC_TEXT_MAX];
    char manufacturer[RSS_DDC_TEXT_MAX];
    char serial[RSS_DDC_TEXT_MAX];
    char branch_device_id[RSS_DDC_TEXT_MAX];
    char transport[RSS_DDC_TEXT_MAX];
    RSSDDCProvider provider;
    uint32_t capabilities;
} RSSDDCDisplay;

typedef struct {
    uint8_t vcp_code;
    uint16_t maximum_value;
    uint16_t current_value;
} RSSDDCVCPResult;

const char *rss_ddc_error_string(RSSDDCError error);
const char *rss_ddc_provider_string(RSSDDCProvider provider);
RSSDDCProvider rss_ddc_provider_from_registry_class(const char *provider_class);
uint32_t rss_ddc_provider_capabilities(RSSDDCProvider provider);

RSSDDCError rss_ddc_list_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count);
RSSDDCError rss_ddc_get_display(uint32_t list_index, RSSDDCDisplay *display);
RSSDDCError rss_ddc_get_vcp(uint32_t list_index, uint8_t vcp_code, RSSDDCVCPResult *result);
RSSDDCError rss_ddc_set_vcp(uint32_t list_index, uint8_t vcp_code, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
