#ifndef RSS_DDC_H
#define RSS_DDC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pre-1.0 API marker: source compatibility may evolve as provider coverage matures. */
#define RSS_DDC_VERSION_MAJOR 0
#define RSS_DDC_VERSION_MINOR 2
#define RSS_DDC_VERSION_PATCH 0

/** Runtime provider classes derived from the macOS registry, never CPU generation. */
typedef enum {
    RSS_DDC_PROVIDER_UNKNOWN = 0,
    RSS_DDC_PROVIDER_DCPDP13,
    RSS_DDC_PROVIDER_DCPDP_SERVICE,
    RSS_DDC_PROVIDER_MCDP29XX,
    RSS_DDC_PROVIDER_PS190,
} RSSDDCProvider;

/*
 * The protocol implementation selected for a provider identity. This remains
 * a portable policy value; it exposes no macOS private-framework or IOKit
 * objects to callers.
 */
typedef enum {
    RSS_DDC_BACKEND_UNSUPPORTED = 0,
    RSS_DDC_BACKEND_DCPDP13,
    RSS_DDC_BACKEND_DCPDP_SERVICE,
    RSS_DDC_BACKEND_MCDP29XX,
    RSS_DDC_BACKEND_PS190,
} RSSDDCBackend;

/** Independent capabilities. A provider must opt in to each one after validation. */
typedef enum {
    RSS_DDC_CAP_NONE = 0,
    RSS_DDC_CAP_GET_VCP = 1u << 0,
    RSS_DDC_CAP_SET_VCP = 1u << 1,
    RSS_DDC_CAP_READ_EDID = 1u << 2,
    RSS_DDC_CAP_READ_DPCD = 1u << 3,
    /** Provider can retrieve and strictly parse one complete MCCS capabilities string. */
    RSS_DDC_CAP_MCCS_CAPABILITIES = 1u << 4,
    /** Provider can issue the separately validated alternate input transport. */
    RSS_DDC_CAP_ALTERNATE_INPUT = 1u << 5,
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
    RSS_DDC_ERROR_EDID_LENGTH,
    RSS_DDC_ERROR_EDID_HEADER,
    RSS_DDC_ERROR_EDID_CHECKSUM,
    RSS_DDC_ERROR_DPCD_LENGTH,
    RSS_DDC_ERROR_DPCD_RANGE,
    RSS_DDC_ERROR_DPCD_READ,
    RSS_DDC_ERROR_VERIFY_MISMATCH,
    RSS_DDC_ERROR_VERIFY_RETRY_EXHAUSTED,
    RSS_DDC_ERROR_VERIFY_UNAVAILABLE,
    RSS_DDC_ERROR_CAPABILITIES_MALFORMED,
    RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE,
    RSS_DDC_ERROR_CAPABILITIES_REQUEST_LIMIT,
    RSS_DDC_ERROR_CAPABILITIES_OFFSET_OVERFLOW,
    RSS_DDC_ERROR_CAPABILITIES_INCOMPLETE,
    RSS_DDC_ERROR_SYSTEM,
} RSSDDCError;

enum {
    RSS_DDC_TEXT_MAX = 128,
    RSS_DDC_EDID_BLOCK_SIZE = 128,
    RSS_DDC_EDID_MAX_BLOCKS = 8,
    RSS_DDC_EDID_MAX_BYTES = RSS_DDC_EDID_BLOCK_SIZE * RSS_DDC_EDID_MAX_BLOCKS,
    /** Largest single DPCD read proven by the current hardware-validated scope. No chunking is performed. */
    RSS_DDC_DPCD_MAX_READ_BYTES = 16,
    /** DisplayPort DPCD uses a 20-bit register address. */
    RSS_DDC_DPCD_MAX_ADDRESS = 0x000fffff,
    /**
     * MCCS capabilities strings are variable length. This bound permits more
     * than 100 maximum-size (32-byte) protocol fragments while keeping the
     * caller-owned parsed object bounded and stack-safe.
     */
    RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES = 4096,
    RSS_DDC_MCCS_CAPABILITIES_MAX_FEATURES = 256,
    /** 4096 bytes cannot encode more than 1365 separated two-digit values. */
    RSS_DDC_MCCS_CAPABILITIES_MAX_ENUM_VALUES = 1400,
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

/** One monitor-advertised VCP feature, with an optional slice of enum_values. */
typedef struct {
    uint8_t vcp_code;
    size_t enum_value_offset;
    size_t enum_value_count;
} RSSDDCMCCSVcpCapability;

/**
 * Caller-owned, bounded result of parsing an MCCS capabilities string. The
 * `raw` bytes are preserved verbatim and NUL-terminated for diagnostics.
 * Values are raw monitor-advertised bytes; rss-ddc intentionally assigns no
 * friendly labels and does not infer that a SET is safe.
 */
typedef struct {
    char raw[RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES + 1];
    size_t raw_length;
    RSSDDCMCCSVcpCapability features[RSS_DDC_MCCS_CAPABILITIES_MAX_FEATURES];
    size_t feature_count;
    uint8_t enum_values[RSS_DDC_MCCS_CAPABILITIES_MAX_ENUM_VALUES];
    size_t enum_value_count;
} RSSDDCMCCSCapabilities;

/**
 * Caller-owned raw EDID storage. `length` is the number of bytes received or
 * supplied for parsing; it is always a multiple of 128 for a valid object.
 * Acquisition is provider-specific. A successful read can deliberately be
 * partial when a monitor declares blocks requiring an unvalidated transport;
 * callers must inspect RSSDDCEDIDInfo.extensions_complete before treating the
 * result as a complete EDID image.
 */
typedef struct {
    uint8_t bytes[RSS_DDC_EDID_MAX_BYTES];
    size_t length;
} RSSDDCEDID;

/** A conservative label for an acquired extension's tag byte; unknown tags remain raw data. */
typedef enum {
    RSS_DDC_EDID_EXTENSION_UNKNOWN = 0,
    RSS_DDC_EDID_EXTENSION_CTA_861,
    RSS_DDC_EDID_EXTENSION_DISPLAYID,
} RSSDDCEDIDExtensionType;

/** Strictly decoded base-block identity plus metadata for present extensions. */
typedef struct {
    char manufacturer_id[4];
    uint16_t product_code;
    uint32_t serial_number;
    bool serial_number_present;
    uint8_t manufacture_week;
    uint16_t manufacture_year;
    bool manufacture_date_present;
    uint8_t version;
    uint8_t revision;
    uint8_t width_cm;
    uint8_t height_cm;
    char monitor_name[RSS_DDC_TEXT_MAX];
    char serial_text[RSS_DDC_TEXT_MAX];
    uint8_t declared_extension_count;
    size_t received_block_count;
    uint8_t extension_tags[RSS_DDC_EDID_MAX_BLOCKS - 1];
    RSSDDCEDIDExtensionType extension_types[RSS_DDC_EDID_MAX_BLOCKS - 1];
    uint8_t extension_revisions[RSS_DDC_EDID_MAX_BLOCKS - 1];
    bool extensions_complete;
    bool present_extension_checksums_valid;
} RSSDDCEDIDInfo;

/** A deliberately small decode of the standard receiver-capability registers at DPCD 0x00000. */
typedef struct {
    uint8_t revision;
    uint8_t max_link_rate_raw;
    const char *max_link_rate_name;
    uint8_t max_lane_count;
    bool enhanced_framing;
    bool downstream_port_present;
} RSSDDCDPCDCapabilities;

/**
 * Explicit policy for the optional high-level Set-and-Verify operation.
 * All delays are milliseconds. `retry_count` means additional verification
 * GET attempts after the initial GET, not total attempts. The operation never
 * applies this policy to ordinary Set VCP or Get VCP calls.
 */
typedef struct {
    uint32_t settle_ms;
    uint32_t retry_count;
    uint32_t retry_delay_ms;
} RSSDDCVerifyPolicy;

enum {
    /** Bounds keep an explicit verification request deterministic and finite. */
    RSS_DDC_VERIFY_MAX_RETRIES = 10,
    RSS_DDC_VERIFY_MAX_DELAY_MS = 60000,
};

/**
 * Optional portable text diagnostics. Messages are transient: callbacks must
 * copy any data they need before returning and must not call back into rss-ddc.
 */
typedef void (*RSSDDCDiagnosticCallback)(void *context, const char *message);

typedef struct {
    RSSDDCDiagnosticCallback callback;
    void *context;
} RSSDDCDiagnostics;

/**
 * An explicit input-switching mechanism selected by the caller's monitor
 * profile or user preference. This says nothing about a monitor's identity.
 */
typedef enum {
    /** Standards-oriented MCCS input-source selection through VCP 0x60. */
    RSS_DDC_INPUT_SWITCH_STANDARD = 0,
    /** Hardware-validated alternate transport; provider support is independently gated. */
    RSS_DDC_INPUT_SWITCH_LG_ALT,
} RSSDDCInputSwitchMethod;

/** Returns a static, human-readable name for an error or provider. */
const char *rss_ddc_error_string(RSSDDCError error);
const char *rss_ddc_provider_string(RSSDDCProvider provider);
/** Returns the backend policy selected by a registry-derived provider identity. */
RSSDDCBackend rss_ddc_provider_backend(RSSDDCProvider provider);
/** Returns a static backend label suitable for diagnostics and tests. */
const char *rss_ddc_backend_name(RSSDDCBackend backend);
/** Classifies a synthetic or registry-derived provider class without macOS types. */
RSSDDCProvider rss_ddc_provider_from_registry_class(const char *provider_class);
/** Returns only the independently validated capabilities for a provider. */
uint32_t rss_ddc_provider_capabilities(RSSDDCProvider provider);

/**
 * Parses a bounded MCCS capabilities string without contacting a display.
 * `raw` need not be NUL-terminated; its supplied length is authoritative.
 * The result is written only on success. Duplicate VCP features, malformed
 * syntax, and nested/empty malformed enum lists fail closed.
 */
RSSDDCError rss_ddc_parse_mccs_capabilities(const char *raw, size_t raw_length,
                                            RSSDDCMCCSCapabilities *capabilities);
/** Returns true when `vcp_code` is explicitly advertised in a parsed result. */
bool rss_ddc_mccs_capabilities_has_vcp(const RSSDDCMCCSCapabilities *capabilities, uint8_t vcp_code);
/**
 * Returns the advertised raw enum-value slice for one VCP feature. A
 * successful lookup may return count zero: MCCS does not require every VCP
 * feature to advertise enumerated values. The pointer remains owned by
 * `capabilities` and valid until that caller-owned object is overwritten.
 */
RSSDDCError rss_ddc_mccs_capabilities_enum_values(const RSSDDCMCCSCapabilities *capabilities,
                                                  uint8_t vcp_code, const uint8_t **values,
                                                  size_t *count);
/**
 * Retrieves and parses one complete monitor MCCS capabilities string into
 * caller-owned storage. On success, `capabilities` owns its raw text and all
 * feature/enum arrays; enum slices remain valid until this object is overwritten.
 */
RSSDDCError rss_ddc_get_mccs_capabilities(uint32_t list_index, RSSDDCMCCSCapabilities *capabilities);
/** Diagnostic form of rss_ddc_get_mccs_capabilities; callback data is transient as usual. */
RSSDDCError rss_ddc_get_mccs_capabilities_with_diagnostics(uint32_t list_index,
                                                           RSSDDCMCCSCapabilities *capabilities,
                                                           const RSSDDCDiagnostics *diagnostics);

/**
 * Snapshots online displays into caller storage. `displays` may be NULL only
 * when `capacity` is zero. `count` always receives the total number observed,
 * while at most min(capacity, *count) snapshots are written. This permits a
 * safe two-call allocation pattern. A display topology may change between
 * calls, so callers must retry or handle a larger returned count explicitly.
 */
RSSDDCError rss_ddc_list_displays(RSSDDCDisplay *displays, size_t capacity, size_t *count);
/** Resolves one current list index and returns its safe public display snapshot. */
RSSDDCError rss_ddc_get_display(uint32_t list_index, RSSDDCDisplay *display);
/**
 * Resolves one current list index with optional correlation diagnostics. The
 * callback follows the same transient ownership and re-entrancy rules as the
 * Get/Set diagnostic APIs; it never opens an IOAV user client.
 */
RSSDDCError rss_ddc_get_display_with_diagnostics(uint32_t list_index, RSSDDCDisplay *display,
                                                  const RSSDDCDiagnostics *diagnostics);
/** Reads the evidence-backed provider EDID acquisition path into caller-owned storage. */
RSSDDCError rss_ddc_read_edid(uint32_t list_index, RSSDDCEDID *edid);
/** Diagnostic form of rss_ddc_read_edid; it remains independent of GET/SET/verify timing. */
RSSDDCError rss_ddc_read_edid_with_diagnostics(uint32_t list_index, RSSDDCEDID *edid,
                                                const RSSDDCDiagnostics *diagnostics);
/**
 * Validates the base block and every extension block present in `edid`, then
 * decodes portable identity metadata. A declared extension may be absent from
 * a base-only acquisition; that is reported as `extensions_complete=false`,
 * not silently treated as an extension checksum failure.
 */
RSSDDCError rss_ddc_parse_edid(const RSSDDCEDID *edid, RSSDDCEDIDInfo *info);
/** Returns a static label for an extension type classified by rss_ddc_parse_edid. */
const char *rss_ddc_edid_extension_type_string(RSSDDCEDIDExtensionType type);
/**
 * Reads one bounded DPCD register range into caller-owned storage. `buffer`
 * remains owned by the caller; `length` must be 1..RSS_DDC_DPCD_MAX_READ_BYTES
 * and the inclusive range must fit the 20-bit DPCD address space. This API
 * performs exactly one provider read and never scans, chunks, or writes.
 */
RSSDDCError rss_ddc_read_dpcd(uint32_t list_index, uint32_t address, uint8_t *buffer, size_t length);
/** Diagnostic form of rss_ddc_read_dpcd; callback data is transient as usual. */
RSSDDCError rss_ddc_read_dpcd_with_diagnostics(uint32_t list_index, uint32_t address, uint8_t *buffer,
                                                size_t length, const RSSDDCDiagnostics *diagnostics);
/**
 * Decodes only the standard receiver-capability bytes when `address` is zero
 * and at least six bytes are supplied. It never alters the raw caller bytes.
 */
RSSDDCError rss_ddc_decode_dpcd_capabilities(uint32_t address, const uint8_t *bytes, size_t length,
                                              RSSDDCDPCDCapabilities *capabilities);
/** Registry-only diagnostic for the DCPDP13 same-role IODP candidate relationship. */
RSSDDCError rss_ddc_probe_dpcd_path_with_diagnostics(uint32_t list_index, const RSSDDCDiagnostics *diagnostics);
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
 * Performs a provider-specific Set VCP operation after the same display and
 * provider safety correlation used for GET. Values cover the full DDC/CI
 * 16-bit field; provider support and monitor-level value semantics are
 * independent and unsupported providers fail closed.
 */
RSSDDCError rss_ddc_set_vcp(uint32_t list_index, uint8_t vcp_code, uint16_t value);
/**
 * Diagnostic form of Set VCP. Callback messages are transient and follow the
 * same ownership/re-entrancy rules as Get VCP diagnostics.
 */
RSSDDCError rss_ddc_set_vcp_with_diagnostics(uint32_t list_index, uint8_t vcp_code, uint16_t value,
                                              const RSSDDCDiagnostics *diagnostics);
/**
 * Selects an input using an explicit mechanism. STANDARD is exactly the
 * ordinary VCP 0x60 Set VCP path. LG_ALT is a write-only alternate transport
 * available only on validated provider paths; callers must select it from
 * monitor-specific evidence or an explicit override.
 */
RSSDDCError rss_ddc_set_input(uint32_t list_index, RSSDDCInputSwitchMethod method, uint16_t value);
/** Diagnostic form of rss_ddc_set_input with the usual transient callback rules. */
RSSDDCError rss_ddc_set_input_with_diagnostics(uint32_t list_index, RSSDDCInputSwitchMethod method,
                                                uint16_t value, const RSSDDCDiagnostics *diagnostics);
/**
 * Returns the explicit default verification policy: 100 ms settling, then up
 * to three additional GET attempts separated by 250 ms. These are a modest
 * caller-visible policy choice, not a DDC/CI or provider timing requirement.
 */
RSSDDCVerifyPolicy rss_ddc_default_verify_policy(void);
/**
 * Performs one write-only provider Set VCP, then verifies the requested value
 * with an independent Get VCP sequence controlled by `policy` (or the default
 * when NULL). Before every verify GET the macOS backend must prove that the
 * current list index still denotes the original physical display; otherwise it
 * fails closed with RSS_DDC_ERROR_VERIFY_UNAVAILABLE. `result` is written only
 * when verification succeeds.
 */
RSSDDCError rss_ddc_set_vcp_and_verify(uint32_t list_index, uint8_t vcp_code, uint16_t value,
                                        const RSSDDCVerifyPolicy *policy, RSSDDCVCPResult *result);
/** Diagnostic form of rss_ddc_set_vcp_and_verify with the usual transient callback rules. */
RSSDDCError rss_ddc_set_vcp_and_verify_with_diagnostics(uint32_t list_index, uint8_t vcp_code,
                                                         uint16_t value, const RSSDDCVerifyPolicy *policy,
                                                         RSSDDCVCPResult *result,
                                                         const RSSDDCDiagnostics *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
