#ifndef RSS_DDC_H
#define RSS_DDC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Runtime provider classes derived from the macOS registry, never CPU generation. */
typedef enum {
    RSS_DDC_PROVIDER_UNKNOWN = 0,
    RSS_DDC_PROVIDER_DCPDP13,
    RSS_DDC_PROVIDER_MCDP29XX,
    RSS_DDC_PROVIDER_PS190,
} RSSDDCProvider;

/** Independent capabilities. A provider must opt in to each one after validation. */
typedef enum {
    RSS_DDC_CAP_NONE = 0,
    RSS_DDC_CAP_GET_VCP = 1u << 0,
    RSS_DDC_CAP_SET_VCP = 1u << 1,
    RSS_DDC_CAP_READ_EDID = 1u << 2,
    RSS_DDC_CAP_READ_DPCD = 1u << 3,
} RSSDDCCapability;

/** Stable operation outcomes; reply failures remain specific so malformed data is never accepted. */
typedef enum {
    RSS_DDC_OK = 0,
    RSS_DDC_ERROR_ARGUMENT,
    RSS_DDC_ERROR_NOT_FOUND,
    RSS_DDC_ERROR_UNSUPPORTED_PROVIDER,
    RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY,
    RSS_DDC_ERROR_DISCOVERY,
    RSS_DDC_ERROR_SAFETY_GATE,
    RSS_DDC_ERROR_SERVICE_CONSTRUCTION,
    RSS_DDC_ERROR_WRITE,
    RSS_DDC_ERROR_READ,
    RSS_DDC_ERROR_REPLY_LENGTH,
    RSS_DDC_ERROR_REPLY_SOURCE,
    RSS_DDC_ERROR_REPLY_COMMAND,
    RSS_DDC_ERROR_REPLY_STATUS,
    RSS_DDC_ERROR_REPLY_VCP,
    RSS_DDC_ERROR_REPLY_CHECKSUM,
    RSS_DDC_ERROR_SYSTEM,
} RSSDDCError;

enum {
    RSS_DDC_TEXT_MAX = 128,
};

/**
 * A user-visible display snapshot. `list_index` is valid only for the current
 * process invocation; registry identifiers are intentionally not public IDs.
 */
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

/** A strictly validated Get VCP response, decoded from big-endian DDC/CI fields. */
typedef struct {
    uint8_t vcp_code;
    uint16_t maximum_value;
    uint16_t current_value;
} RSSDDCVCPResult;

/**
 * Optional portable text diagnostics. Messages are transient: callbacks must
 * copy any data they need before returning and must not call back into rss-ddc.
 */
typedef void (*RSSDDCDiagnosticCallback)(void *context, const char *message);

typedef struct {
    RSSDDCDiagnosticCallback callback;
    void *context;
} RSSDDCDiagnostics;

/** Returns a static, human-readable name for an error or provider. */
const char *rss_ddc_error_string(RSSDDCError error);
const char *rss_ddc_provider_string(RSSDDCProvider provider);
/** Classifies a synthetic or registry-derived provider class without macOS types. */
RSSDDCProvider rss_ddc_provider_from_registry_class(const char *provider_class);
/** Returns only the independently validated capabilities for a provider. */
uint32_t rss_ddc_provider_capabilities(RSSDDCProvider provider);

/**
 * Snapshots online displays into caller storage. `displays` may be NULL only
 * when `capacity` is zero; `count` receives the number written.
 */
RSSDDCError rss_ddc_list_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count);
/** Resolves one current list index and returns its safe public display snapshot. */
RSSDDCError rss_ddc_get_display(uint32_t list_index, RSSDDCDisplay *display);
/** Performs Get VCP with no diagnostics; equivalent to the diagnostic form with NULL options. */
RSSDDCError rss_ddc_get_vcp(uint32_t list_index, uint8_t vcp_code, RSSDDCVCPResult *result);
/**
 * Performs Get VCP after provider-specific safety correlation. Diagnostics are
 * optional and do not change transport behavior. `result` is written only on
 * a fully validated reply.
 */
RSSDDCError rss_ddc_get_vcp_with_diagnostics(uint32_t list_index, uint8_t vcp_code,
                                              RSSDDCVCPResult *result,
                                              const RSSDDCDiagnostics *diagnostics);
/**
 * Reserved high-level Set VCP API. Providers without separately validated SET
 * support return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY rather than guessing.
 */
RSSDDCError rss_ddc_set_vcp(uint32_t list_index, uint8_t vcp_code, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
