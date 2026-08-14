#ifndef RSS_DDC_H
#define RSS_DDC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pre-1.0 API marker: source compatibility may evolve as provider coverage
 * matures. */
#define RSS_DDC_VERSION_MAJOR 0
#define RSS_DDC_VERSION_MINOR 4
#define RSS_DDC_VERSION_PATCH 0

/** Runtime provider classes derived from the macOS registry, never CPU
 * generation. */
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

/** Independent capabilities. Most are provider-owned; profile capabilities are
 * documented separately. */
typedef enum {
  RSS_DDC_CAP_NONE = 0,
  RSS_DDC_CAP_GET_VCP = 1u << 0,
  RSS_DDC_CAP_SET_VCP = 1u << 1,
  RSS_DDC_CAP_READ_EDID = 1u << 2,
  RSS_DDC_CAP_READ_DPCD = 1u << 3,
  /** Provider can retrieve and strictly parse one complete MCCS capabilities
     string. */
  RSS_DDC_CAP_MCCS_CAPABILITIES = 1u << 4,
  /** Provider can issue the separately validated alternate input transport. */
  RSS_DDC_CAP_ALTERNATE_INPUT = 1u << 5,
  /** Selected monitor profile has an evidence-backed semantic Picture Mode
     operation. */
  RSS_DDC_CAP_PICTURE_MODE = 1u << 6,
} RSSDDCCapability;

/** Stable operation outcomes; reply failures remain specific so malformed data
 * is never accepted. */
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
  RSS_DDC_ERROR_PROFILE_MALFORMED,
  RSS_DDC_ERROR_PROFILE_SCHEMA,
  RSS_DDC_ERROR_PROFILE_VERSION,
  RSS_DDC_ERROR_PROFILE_CONFLICT,
  RSS_DDC_ERROR_PROFILE_UNSAFE,
  RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED,
  RSS_DDC_ERROR_MONITOR_KNOWLEDGE_SCHEMA,
  RSS_DDC_ERROR_MONITOR_KNOWLEDGE_CONFLICT,
  RSS_DDC_ERROR_MONITOR_KNOWLEDGE_UNSAFE,
  RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE,
  RSS_DDC_ERROR_SYSTEM,
} RSSDDCError;

enum {
  RSS_DDC_TEXT_MAX = 128,
  RSS_DDC_EDID_BLOCK_SIZE = 128,
  RSS_DDC_EDID_MAX_BLOCKS = 8,
  RSS_DDC_EDID_MAX_BYTES = RSS_DDC_EDID_BLOCK_SIZE * RSS_DDC_EDID_MAX_BLOCKS,
  /** Largest single DPCD read proven by the current hardware-validated scope.
     No chunking is performed. */
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
  RSS_DDC_PROFILE_MAX_PROFILES = 32,
  RSS_DDC_PROFILE_MAX_CONTROLS = 16,
  RSS_DDC_PROFILE_MAX_ENUM_VALUES = 32,
  RSS_DDC_PROFILE_ID_MAX = 64,
  RSS_DDC_PROFILE_VERSION_MAX = 64,
  RSS_DDC_MONITOR_KNOWLEDGE_ID_MAX = 128,
  RSS_DDC_MONITOR_KNOWLEDGE_NOTE_MAX = 256,
  RSS_DDC_MONITOR_KNOWLEDGE_RAW_MAX_BYTES = 1024,
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
  char edid_manufacturer[4];
  uint16_t edid_product_code;
  bool edid_product_code_present;
  char serial[RSS_DDC_TEXT_MAX];
  char branch_device_id[RSS_DDC_TEXT_MAX];
  char transport[RSS_DDC_TEXT_MAX];
  RSSDDCProvider provider;
  uint32_t capabilities;
} RSSDDCDisplay;

/** A strictly validated Get VCP response, decoded from big-endian DDC/CI
 * fields. */
typedef struct {
  uint8_t vcp_code;
  uint16_t maximum_value;
  uint16_t current_value;
} RSSDDCVCPResult;

/** One monitor-advertised VCP feature, with an optional slice of enum_values.
 */
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

/** A conservative label for an acquired extension's tag byte; unknown tags
 * remain raw data. */
typedef enum {
  RSS_DDC_EDID_EXTENSION_UNKNOWN = 0,
  RSS_DDC_EDID_EXTENSION_CTA_861,
  RSS_DDC_EDID_EXTENSION_DISPLAYID,
} RSSDDCEDIDExtensionType;

/** Strictly decoded base-block identity plus metadata for present extensions.
 */
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

/** A deliberately small decode of the standard receiver-capability registers at
 * DPCD 0x00000. */
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
  /** Hardware-validated alternate transport; provider support is independently
     gated. */
  RSS_DDC_INPUT_SWITCH_LG_ALT,
} RSSDDCInputSwitchMethod;

/**
 * Friendly semantic Picture Mode values. A monitor-specific profile decides
 * whether this capability exists; these names never imply universal VCP
 * semantics. UNKNOWN means a supported profile returned a raw value outside
 * its validated mapping.
 */
typedef enum {
  RSS_DDC_PICTURE_MODE_UNKNOWN = 0,
  RSS_DDC_PICTURE_MODE_CUSTOM,
  RSS_DDC_PICTURE_MODE_VIVID,
  RSS_DDC_PICTURE_MODE_HDR_EFFECT,
  RSS_DDC_PICTURE_MODE_CINEMA,
  RSS_DDC_PICTURE_MODE_FPS,
  RSS_DDC_PICTURE_MODE_RTS,
  RSS_DDC_PICTURE_MODE_COLOR_WEAKNESS,
  RSS_DDC_PICTURE_MODE_READER,
} RSSDDCPictureMode;

/** Origin is distinct from the evidence strength attached to a profile/control.
 */
typedef enum {
  RSS_DDC_PROFILE_SOURCE_BUILTIN = 0,
  RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK,
  RSS_DDC_PROFILE_SOURCE_LOCAL,
  RSS_DDC_PROFILE_SOURCE_RESEARCH,
} RSSDDCProfileSource;

/** Evidence strength; only HARDWARE_VALIDATED can authorize a semantic SET in
 * v1. */
typedef enum {
  RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN = 0,
  RSS_DDC_PROFILE_CONFIDENCE_CANDIDATE,
  RSS_DDC_PROFILE_CONFIDENCE_OBSERVED,
  RSS_DDC_PROFILE_CONFIDENCE_CORRELATED,
  RSS_DDC_PROFILE_CONFIDENCE_SET_OBSERVED,
  RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED,
} RSSDDCProfileConfidence;

/** Stable, provider-independent semantic control identifiers. */
typedef enum {
  RSS_DDC_PROFILE_CONTROL_UNKNOWN = 0,
  RSS_DDC_PROFILE_CONTROL_PICTURE_MODE,
  RSS_DDC_PROFILE_CONTROL_INPUT,
  RSS_DDC_PROFILE_CONTROL_BRIGHTNESS,
  RSS_DDC_PROFILE_CONTROL_CONTRAST,
  RSS_DDC_PROFILE_CONTROL_COLOR_PRESET,
  RSS_DDC_PROFILE_CONTROL_RESPONSE_TIME,
  RSS_DDC_PROFILE_CONTROL_ADAPTIVE_SYNC,
  RSS_DDC_PROFILE_CONTROL_ENERGY_SAVING,
  RSS_DDC_PROFILE_CONTROL_BLACK_STABILIZER,
  RSS_DDC_PROFILE_CONTROL_GAMMA,
  RSS_DDC_PROFILE_CONTROL_SHARPNESS,
  RSS_DDC_PROFILE_CONTROL_AUDIO_MUTE,
} RSSDDCProfileControlID;

/** A supported semantic operation shape. Additional methods require a future
 * schema version. */
typedef enum {
  RSS_DDC_PROFILE_METHOD_UNKNOWN = 0,
  RSS_DDC_PROFILE_METHOD_VCP,
  RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT,
} RSSDDCProfileMethod;

/** Persistable matching facts. list_index is intentionally not representable.
 */
typedef struct {
  char manufacturer[RSS_DDC_TEXT_MAX];
  char product_name[RSS_DDC_TEXT_MAX];
  char serial[RSS_DDC_TEXT_MAX];
  char branch_device_id[RSS_DDC_TEXT_MAX];
  char transport[RSS_DDC_TEXT_MAX];
  RSSDDCProvider provider;
  bool external;
} RSSDDCProfileIdentity;

typedef struct {
  char id[RSS_DDC_PROFILE_ID_MAX];
  char name[RSS_DDC_TEXT_MAX];
  uint16_t raw_value;
} RSSDDCProfileEnumValue;

typedef struct {
  RSSDDCProfileControlID id;
  RSSDDCProfileMethod method;
  uint16_t address;
  bool readable;
  bool writable;
  bool write_authorized;
  uint16_t minimum_value;
  uint16_t maximum_value;
  bool has_numeric_range;
  RSSDDCProfileSource source;
  RSSDDCProfileConfidence confidence;
  size_t enum_value_count;
  RSSDDCProfileEnumValue enum_values[RSS_DDC_PROFILE_MAX_ENUM_VALUES];
} RSSDDCProfileControl;

/** Value-only result of an offline profile resolution. */
typedef struct {
  RSSDDCProfileIdentity identity;
  size_t control_count;
  RSSDDCProfileControl controls[RSS_DDC_PROFILE_MAX_CONTROLS];
} RSSDDCEffectiveProfile;

/** Metadata needed by a consumer-owned download/signature/update workflow. */
typedef struct {
  uint32_t schema_version;
  char database_version[RSS_DDC_PROFILE_VERSION_MAX];
  char minimum_rss_ddc_version[RSS_DDC_PROFILE_VERSION_MAX];
  char pack_id[RSS_DDC_PROFILE_ID_MAX];
} RSSDDCProfilePackInfo;

/** Opaque, heap-owned profile store. Parsing and resolution never contact
 * hardware. */
typedef struct RSSDDCProfileStore RSSDDCProfileStore;

/** Returns a static, human-readable name for an error or provider. */
const char *rss_ddc_error_string(RSSDDCError error);
const char *rss_ddc_provider_string(RSSDDCProvider provider);
/** Returns the backend policy selected by a registry-derived provider identity.
 */
RSSDDCBackend rss_ddc_provider_backend(RSSDDCProvider provider);
/** Returns a static backend label suitable for diagnostics and tests. */
const char *rss_ddc_backend_name(RSSDDCBackend backend);
/** Classifies a synthetic or registry-derived provider class without macOS
 * types. */
RSSDDCProvider rss_ddc_provider_from_registry_class(const char *provider_class);
/** Returns only the independently validated provider-owned capabilities. */
uint32_t rss_ddc_provider_capabilities(RSSDDCProvider provider);

/** Creates an empty offline profile store. Callers own it and may swap stores
 * atomically. */
RSSDDCProfileStore *rss_ddc_profile_store_create(void);
void rss_ddc_profile_store_destroy(RSSDDCProfileStore *store);
/** Adds the independently validated bundled profiles. It is transactional on
 * failure. */
RSSDDCError rss_ddc_profile_store_load_builtin(RSSDDCProfileStore *store);
/**
 * Parses and validates a versioned validated-pack JSON document before adding
 * it to `store`; a failure leaves that store unchanged. Unknown optional JSON
 * keys are ignored, but unknown required schema data and malformed values fail
 * closed. This function never performs I/O beyond its caller-provided bytes.
 */
RSSDDCError rss_ddc_profile_store_load_pack_data(RSSDDCProfileStore *store,
                                                 const char *data,
                                                 size_t length);
/** File convenience form of rss_ddc_profile_store_load_pack_data; no network is
 * involved. */
RSSDDCError rss_ddc_profile_store_load_pack_file(RSSDDCProfileStore *store,
                                                 const char *path);
/** Local profiles use the same schema but retain local source provenance and
 * load transactionally. */
RSSDDCError rss_ddc_profile_store_load_local_data(RSSDDCProfileStore *store,
                                                  const char *data,
                                                  size_t length);
RSSDDCError rss_ddc_profile_store_load_local_file(RSSDDCProfileStore *store,
                                                  const char *path);
/** Research profiles are introspectable evidence only and can never authorize a
 * semantic SET. */
RSSDDCError rss_ddc_profile_store_load_research_data(RSSDDCProfileStore *store,
                                                     const char *data,
                                                     size_t length);
RSSDDCError rss_ddc_profile_store_load_research_file(RSSDDCProfileStore *store,
                                                     const char *path);
/** Validates a candidate pack without changing a store. */
RSSDDCError rss_ddc_profile_validate_pack_data(const char *data, size_t length,
                                               RSSDDCProfileSource source,
                                               RSSDDCProfilePackInfo *info);
/** Returns the metadata for the most recently accepted pack, if any. */
RSSDDCError rss_ddc_profile_store_pack_info(const RSSDDCProfileStore *store,
                                            RSSDDCProfilePackInfo *info);
/**
 * Exports a validated, self-contained JSON pack into caller storage. It does
 * not write a file, choose a location, or include network/signature metadata.
 * Pass NULL/0 to query the required byte count including the terminating NUL.
 */
RSSDDCError rss_ddc_profile_store_export_json(const RSSDDCProfileStore *store,
                                              char *buffer, size_t capacity,
                                              size_t *required);
/**
 * Resolves matching profiles without display discovery. Controls compose by
 * confidence, source, and identity specificity; equal-authority conflicts
 * fail closed rather than choosing a raw operation.
 */
RSSDDCError rss_ddc_profile_store_resolve(const RSSDDCProfileStore *store,
                                          const RSSDDCProfileIdentity *identity,
                                          RSSDDCEffectiveProfile *effective);
size_t rss_ddc_effective_profile_control_count(
    const RSSDDCEffectiveProfile *effective);
RSSDDCError
rss_ddc_effective_profile_control(const RSSDDCEffectiveProfile *effective,
                                  size_t index, RSSDDCProfileControl *control);
RSSDDCError
rss_ddc_profile_control_enum_value(const RSSDDCProfileControl *control,
                                   size_t index, RSSDDCProfileEnumValue *value);
const char *rss_ddc_profile_control_name(RSSDDCProfileControlID id);
const char *rss_ddc_profile_source_name(RSSDDCProfileSource source);
const char *rss_ddc_profile_confidence_name(RSSDDCProfileConfidence confidence);

/**
 * Parses a bounded MCCS capabilities string without contacting a display.
 * `raw` need not be NUL-terminated; its supplied length is authoritative.
 * The result is written only on success. Duplicate VCP features, malformed
 * syntax, and nested/empty malformed enum lists fail closed.
 */
RSSDDCError
rss_ddc_parse_mccs_capabilities(const char *raw, size_t raw_length,
                                RSSDDCMCCSCapabilities *capabilities);
/** Returns true when `vcp_code` is explicitly advertised in a parsed result. */
bool rss_ddc_mccs_capabilities_has_vcp(
    const RSSDDCMCCSCapabilities *capabilities, uint8_t vcp_code);
/**
 * Returns the advertised raw enum-value slice for one VCP feature. A
 * successful lookup may return count zero: MCCS does not require every VCP
 * feature to advertise enumerated values. The pointer remains owned by
 * `capabilities` and valid until that caller-owned object is overwritten.
 */
RSSDDCError rss_ddc_mccs_capabilities_enum_values(
    const RSSDDCMCCSCapabilities *capabilities, uint8_t vcp_code,
    const uint8_t **values, size_t *count);
/**
 * Retrieves and parses one complete monitor MCCS capabilities string into
 * caller-owned storage. On success, `capabilities` owns its raw text and all
 * feature/enum arrays; enum slices remain valid until this object is
 * overwritten.
 */
RSSDDCError rss_ddc_get_mccs_capabilities(uint32_t list_index,
                                          RSSDDCMCCSCapabilities *capabilities);
/** Diagnostic form of rss_ddc_get_mccs_capabilities; callback data is transient
 * as usual. */
RSSDDCError rss_ddc_get_mccs_capabilities_with_diagnostics(
    uint32_t list_index, RSSDDCMCCSCapabilities *capabilities,
    const RSSDDCDiagnostics *diagnostics);

/**
 * Snapshots online displays into caller storage. `displays` may be NULL only
 * when `capacity` is zero. `count` always receives the total number observed,
 * while at most min(capacity, *count) snapshots are written. This permits a
 * safe two-call allocation pattern. A display topology may change between
 * calls, so callers must retry or handle a larger returned count explicitly.
 */
RSSDDCError rss_ddc_list_displays(RSSDDCDisplay *displays, size_t capacity,
                                  size_t *count);
/** Resolves one current list index and returns its safe public display
 * snapshot. */
RSSDDCError rss_ddc_get_display(uint32_t list_index, RSSDDCDisplay *display);
/**
 * Resolves one current list index with optional correlation diagnostics. The
 * callback follows the same transient ownership and re-entrancy rules as the
 * Get/Set diagnostic APIs; it never opens an IOAV user client.
 */
RSSDDCError
rss_ddc_get_display_with_diagnostics(uint32_t list_index,
                                     RSSDDCDisplay *display,
                                     const RSSDDCDiagnostics *diagnostics);
/** Reads the evidence-backed provider EDID acquisition path into caller-owned
 * storage. */
RSSDDCError rss_ddc_read_edid(uint32_t list_index, RSSDDCEDID *edid);
/** Diagnostic form of rss_ddc_read_edid; it remains independent of
 * GET/SET/verify timing. */
RSSDDCError
rss_ddc_read_edid_with_diagnostics(uint32_t list_index, RSSDDCEDID *edid,
                                   const RSSDDCDiagnostics *diagnostics);
/**
 * Validates the base block and every extension block present in `edid`, then
 * decodes portable identity metadata. A declared extension may be absent from
 * a base-only acquisition; that is reported as `extensions_complete=false`,
 * not silently treated as an extension checksum failure.
 */
RSSDDCError rss_ddc_parse_edid(const RSSDDCEDID *edid, RSSDDCEDIDInfo *info);
/** Returns a static label for an extension type classified by
 * rss_ddc_parse_edid. */
const char *rss_ddc_edid_extension_type_string(RSSDDCEDIDExtensionType type);
/**
 * Reads one bounded DPCD register range into caller-owned storage. `buffer`
 * remains owned by the caller; `length` must be 1..RSS_DDC_DPCD_MAX_READ_BYTES
 * and the inclusive range must fit the 20-bit DPCD address space. This API
 * performs exactly one provider read and never scans, chunks, or writes.
 */
RSSDDCError rss_ddc_read_dpcd(uint32_t list_index, uint32_t address,
                              uint8_t *buffer, size_t length);
/** Diagnostic form of rss_ddc_read_dpcd; callback data is transient as usual.
 */
RSSDDCError
rss_ddc_read_dpcd_with_diagnostics(uint32_t list_index, uint32_t address,
                                   uint8_t *buffer, size_t length,
                                   const RSSDDCDiagnostics *diagnostics);
/**
 * Decodes only the standard receiver-capability bytes when `address` is zero
 * and at least six bytes are supplied. It never alters the raw caller bytes.
 */
RSSDDCError
rss_ddc_decode_dpcd_capabilities(uint32_t address, const uint8_t *bytes,
                                 size_t length,
                                 RSSDDCDPCDCapabilities *capabilities);
/** Registry-only diagnostic for the DCPDP13 same-role IODP candidate
 * relationship. */
RSSDDCError
rss_ddc_probe_dpcd_path_with_diagnostics(uint32_t list_index,
                                         const RSSDDCDiagnostics *diagnostics);
/** Performs Get VCP with no diagnostics; equivalent to the diagnostic form with
 * NULL options. */
RSSDDCError rss_ddc_get_vcp(uint32_t list_index, uint8_t vcp_code,
                            RSSDDCVCPResult *result);
/**
 * Performs Get VCP after provider-specific safety correlation. Diagnostics are
 * optional and do not change transport behavior. `result` is written only on
 * a fully validated reply.
 */
RSSDDCError
rss_ddc_get_vcp_with_diagnostics(uint32_t list_index, uint8_t vcp_code,
                                 RSSDDCVCPResult *result,
                                 const RSSDDCDiagnostics *diagnostics);
/**
 * Performs a provider-specific Set VCP operation after the same display and
 * provider safety correlation used for GET. Values cover the full DDC/CI
 * 16-bit field; provider support and monitor-level value semantics are
 * independent and unsupported providers fail closed.
 */
RSSDDCError rss_ddc_set_vcp(uint32_t list_index, uint8_t vcp_code,
                            uint16_t value);
/**
 * Diagnostic form of Set VCP. Callback messages are transient and follow the
 * same ownership/re-entrancy rules as Get VCP diagnostics.
 */
RSSDDCError
rss_ddc_set_vcp_with_diagnostics(uint32_t list_index, uint8_t vcp_code,
                                 uint16_t value,
                                 const RSSDDCDiagnostics *diagnostics);
/**
 * Selects an input using an explicit mechanism. STANDARD is exactly the
 * ordinary VCP 0x60 Set VCP path. LG_ALT is a write-only alternate transport
 * available only on validated provider paths; callers must select it from
 * monitor-specific evidence or an explicit override.
 */
RSSDDCError rss_ddc_set_input(uint32_t list_index,
                              RSSDDCInputSwitchMethod method, uint16_t value);
/** Diagnostic form of rss_ddc_set_input with the usual transient callback
 * rules. */
RSSDDCError rss_ddc_set_input_with_diagnostics(
    uint32_t list_index, RSSDDCInputSwitchMethod method, uint16_t value,
    const RSSDDCDiagnostics *diagnostics);
/** Returns a static friendly name for a semantic Picture Mode value. */
const char *rss_ddc_picture_mode_name(RSSDDCPictureMode mode);
/**
 * Gets the selected monitor profile's Picture Mode. Unknown raw profile values
 * return RSS_DDC_OK with `mode` set to RSS_DDC_PICTURE_MODE_UNKNOWN; they are
 * never assigned a guessed semantic name. Unsupported displays fail closed.
 */
RSSDDCError rss_ddc_get_picture_mode(uint32_t list_index,
                                     RSSDDCPictureMode *mode);
/** Diagnostic form of rss_ddc_get_picture_mode. */
RSSDDCError
rss_ddc_get_picture_mode_with_diagnostics(uint32_t list_index,
                                          RSSDDCPictureMode *mode,
                                          const RSSDDCDiagnostics *diagnostics);
/** Resolves Picture Mode through a caller-owned store; NULL uses the bundled
 * validated store. */
RSSDDCError
rss_ddc_get_picture_mode_with_profile_store(uint32_t list_index,
                                            const RSSDDCProfileStore *store,
                                            RSSDDCPictureMode *mode);
/**
 * Sets exactly one validated semantic Picture Mode operation for the selected
 * monitor profile. It does not write brightness, contrast, color preset, or
 * any other correlated secondary VCP.
 */
RSSDDCError rss_ddc_set_picture_mode(uint32_t list_index,
                                     RSSDDCPictureMode mode);
/** Diagnostic form of rss_ddc_set_picture_mode. */
RSSDDCError
rss_ddc_set_picture_mode_with_diagnostics(uint32_t list_index,
                                          RSSDDCPictureMode mode,
                                          const RSSDDCDiagnostics *diagnostics);
/** Resolves and authorizes Picture Mode through a caller-owned store; NULL uses
 * the bundled validated store. */
RSSDDCError
rss_ddc_set_picture_mode_with_profile_store(uint32_t list_index,
                                            const RSSDDCProfileStore *store,
                                            RSSDDCPictureMode mode);
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
RSSDDCError rss_ddc_set_vcp_and_verify(uint32_t list_index, uint8_t vcp_code,
                                       uint16_t value,
                                       const RSSDDCVerifyPolicy *policy,
                                       RSSDDCVCPResult *result);
/** Diagnostic form of rss_ddc_set_vcp_and_verify with the usual transient
 * callback rules. */
RSSDDCError rss_ddc_set_vcp_and_verify_with_diagnostics(
    uint32_t list_index, uint8_t vcp_code, uint16_t value,
    const RSSDDCVerifyPolicy *policy, RSSDDCVCPResult *result,
    const RSSDDCDiagnostics *diagnostics);

/* Monitor knowledge is an offline, semantic-first representation. None of
 * these APIs enumerate displays, access files, use networking, or issue DDC. */
#define RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA "monitor-knowledge/v0.1"

typedef enum {
  RSS_DDC_CONFIDENCE_UNKNOWN,
  RSS_DDC_CONFIDENCE_CANDIDATE,
  RSS_DDC_CONFIDENCE_OBSERVED,
  RSS_DDC_CONFIDENCE_CORRELATED,
  RSS_DDC_CONFIDENCE_VALIDATED,
  RSS_DDC_CONFIDENCE_HARDWARE_VALIDATED
} RSSDDCConfidence;
typedef enum {
  RSS_DDC_VALIDATION_NOT_VALIDATED,
  RSS_DDC_VALIDATION_READ_VALIDATED,
  RSS_DDC_VALIDATION_CORRELATION_VALIDATED,
  RSS_DDC_VALIDATION_SET_CONFIRMED,
  RSS_DDC_VALIDATION_HARDWARE_VALIDATED
} RSSDDCValidation;
typedef enum {
  RSS_DDC_RISK_READ_STANDARD,
  RSS_DDC_RISK_READ_EXTENDED,
  RSS_DDC_RISK_GUIDED_READ,
  RSS_DDC_RISK_VALIDATE_SAFE_SET,
  RSS_DDC_RISK_VENDOR_EXPERIMENTAL_SET,
  RSS_DDC_RISK_HIGH_RISK_DENIED
} RSSDDCRisk;
typedef enum {
  RSS_DDC_EVIDENCE_STANDARD_DEFINED,
  RSS_DDC_EVIDENCE_MCCS_ADVERTISED,
  RSS_DDC_EVIDENCE_EDID_DERIVED,
  RSS_DDC_EVIDENCE_PROFILE_KNOWN,
  RSS_DDC_EVIDENCE_ROGUE_VALIDATED_PROFILE,
  RSS_DDC_EVIDENCE_LOCAL_VALIDATED,
  RSS_DDC_EVIDENCE_STABLE_GET,
  RSS_DDC_EVIDENCE_EXTENDED_DISCOVERY,
  RSS_DDC_EVIDENCE_EXTERNAL_CANDIDATE,
  RSS_DDC_EVIDENCE_MANUFACTURER_FAMILY_HINT,
  RSS_DDC_EVIDENCE_MODEL_FAMILY_HINT,
  RSS_DDC_EVIDENCE_OSD_CORRELATED,
  RSS_DDC_EVIDENCE_SET_CONFIRMED
} RSSDDCEvidenceType;
typedef enum {
  RSS_DDC_AVAILABILITY_UNKNOWN,
  RSS_DDC_AVAILABILITY_SUPPORTED,
  RSS_DDC_AVAILABILITY_UNSUPPORTED,
  RSS_DDC_AVAILABILITY_CONDITIONAL
} RSSDDCAvailability;
typedef enum {
  RSS_DDC_METHOD_MCCS_VCP,
  RSS_DDC_METHOD_VENDOR_PROTOCOL,
  RSS_DDC_METHOD_PROVIDER_SPECIFIC,
  RSS_DDC_METHOD_UNKNOWN
} RSSDDCMethodType;
typedef enum {
  RSS_DDC_RAW_UNSIGNED,
  RSS_DDC_RAW_SIGNED,
  RSS_DDC_RAW_BYTES,
  RSS_DDC_RAW_STRING
} RSSDDCRawType;
typedef enum {
  RSS_DDC_RELATIONSHIP_SECONDARY_EFFECT,
  RSS_DDC_RELATIONSHIP_CORRELATES_WITH,
  RSS_DDC_RELATIONSHIP_DEPENDS_ON,
  RSS_DDC_RELATIONSHIP_CONFLICTS_WITH,
  RSS_DDC_RELATIONSHIP_ENABLED_BY
} RSSDDCRelationshipType;
typedef enum {
  RSS_DDC_CONDITION_EQUALS,
  RSS_DDC_CONDITION_NOT_EQUALS,
  RSS_DDC_CONDITION_ENABLED,
  RSS_DDC_CONDITION_DISABLED,
  RSS_DDC_CONDITION_PRESENT,
  RSS_DDC_CONDITION_ABSENT,
  RSS_DDC_CONDITION_LESS_THAN,
  RSS_DDC_CONDITION_LESS_OR_EQUAL,
  RSS_DDC_CONDITION_GREATER_THAN,
  RSS_DDC_CONDITION_GREATER_OR_EQUAL
} RSSDDCConditionOperator;
typedef enum {
  RSS_DDC_CONDITION_GROUP_ALL_OF,
  RSS_DDC_CONDITION_GROUP_ANY_OF
} RSSDDCConditionGroupType;

typedef struct {
  RSSDDCRawType type;
  uint64_t unsigned_value;
  int64_t signed_value;
  const uint8_t *data;
  size_t data_length;
} RSSDDCRawValue;
typedef struct {
  bool present;
  int64_t minimum;
  int64_t maximum;
  int64_t step;
  const char *units;
} RSSDDCRange;
typedef struct {
  RSSDDCEvidenceType type;
  const char *source_id;
  const char *reference;
  const char *timestamp;
  const char *scope;
  RSSDDCConfidence contribution;
} RSSDDCEvidence;
typedef struct {
  const char *semantic_id;
  const char *value_id;
  RSSDDCConditionOperator op;
  bool comparison_present;
  RSSDDCRawValue comparison;
  RSSDDCConfidence confidence;
  RSSDDCValidation validation;
  size_t evidence_count;
  const RSSDDCEvidence *evidence;
} RSSDDCMonitorKnowledgeCondition;
typedef struct {
  RSSDDCConditionGroupType type;
  size_t condition_count;
  const RSSDDCMonitorKnowledgeCondition *conditions;
} RSSDDCMonitorKnowledgeConditionGroup;
typedef struct {
  const char *id;
  const char *label;
  RSSDDCRawValue raw;
  size_t raw_alias_count;
  const RSSDDCRawValue *raw_aliases;
  bool readable;
  bool writable;
  RSSDDCConfidence confidence;
  RSSDDCValidation validation;
  RSSDDCAvailability availability;
  size_t evidence_count;
  const RSSDDCEvidence *evidence;
} RSSDDCMonitorKnowledgeValue;
typedef struct {
  const char *id;
  RSSDDCMethodType type;
  uint32_t vcp_code;
  const char *protocol_id;
  const char *address;
  bool readable;
  bool writable;
  RSSDDCRisk risk;
  const char *parameters;
  RSSDDCConfidence confidence;
  size_t evidence_count;
  const RSSDDCEvidence *evidence;
} RSSDDCMonitorKnowledgeMethod;
typedef struct {
  const char *id;
  const char *label;
  RSSDDCAvailability availability;
  const char *conditions;
  RSSDDCConfidence confidence;
  RSSDDCValidation validation;
  RSSDDCRange advertised_range;
  RSSDDCRange observed_range;
  RSSDDCRange validated_range;
  bool reported_maximum_present;
  RSSDDCRawValue reported_maximum;
  size_t condition_group_count;
  const RSSDDCMonitorKnowledgeConditionGroup *condition_groups;
  size_t method_count;
  const RSSDDCMonitorKnowledgeMethod *methods;
  size_t value_count;
  const RSSDDCMonitorKnowledgeValue *values;
  size_t evidence_count;
  const RSSDDCEvidence *evidence;
} RSSDDCMonitorKnowledgeCapability;
/** Immutable provenance retained once per document. Evidence records may refer
 * to a source id instead of repeating its raw content. */
typedef struct {
  const char *id;
  const char *type;
  const char *reference;
} RSSDDCMonitorKnowledgeSource;
typedef struct {
  const char *id;
  const char *connector;
  const char *port;
  const char *label;
  bool switching_supported;
  bool current_readable;
  bool ddc_path_may_change;
  RSSDDCRawValue read_value;
  RSSDDCRawValue switch_value;
  RSSDDCConfidence confidence;
  size_t evidence_count;
  const RSSDDCEvidence *evidence;
} RSSDDCInputRoute;
typedef struct {
  const char *source_id;
  const char *target_id;
  const char *source_value_id;
  const char *target_value_id;
  RSSDDCRelationshipType type;
  RSSDDCConfidence confidence;
  size_t evidence_count;
  const RSSDDCEvidence *evidence;
} RSSDDCRelationship;
typedef struct {
  const char *manufacturer;
  const char *model;
  const char *edid_manufacturer;
  uint32_t edid_product_code;
  bool edid_product_code_present;
  const char *serial;
  const char *provider;
  const char *transport;
  const char *branch;
  const char *family_hint;
  RSSDDCConfidence confidence;
  size_t evidence_count;
  const RSSDDCEvidence *evidence;
} RSSDDCMonitorIdentity;
typedef struct {
  const char *semantic_id;
  uint8_t vcp_code;
  const char *value_kind;
  bool typical_readable;
  bool typical_writable;
  bool unrelated_candidate_conflict;
} RSSDDCSemanticRegistryEntry;
typedef struct RSSDDCMonitorKnowledge RSSDDCMonitorKnowledge;
typedef enum {
  RSS_DDC_RESOLUTION_REASON_NONE = 0,
  RSS_DDC_RESOLUTION_REASON_IDENTITY_INCOMPATIBLE,
  RSS_DDC_RESOLUTION_REASON_NO_READ_METHOD,
  RSS_DDC_RESOLUTION_REASON_NO_WRITE_EVIDENCE,
  RSS_DDC_RESOLUTION_REASON_RISK_DENIED,
  RSS_DDC_RESOLUTION_REASON_EQUAL_AUTHORITY_CONFLICT,
  RSS_DDC_RESOLUTION_REASON_EXTERNAL_CANDIDATE_ONLY,
  RSS_DDC_RESOLUTION_REASON_EXPERIMENTAL_ONLY,
} RSSDDCResolutionReason;
/** Heap-owned effective view; returned method pointers are borrowed from the
 * source knowledge objects and remain valid until those sources are destroyed.
 */
typedef struct RSSDDCMonitorKnowledgeResolution
    RSSDDCMonitorKnowledgeResolution;
typedef struct RSSDDCMonitorKnowledgeValueResolution
    RSSDDCMonitorKnowledgeValueResolution;
typedef struct RSSDDCMonitorKnowledgeRangeResolution
    RSSDDCMonitorKnowledgeRangeResolution;
typedef struct RSSDDCInputRouteResolution RSSDDCInputRouteResolution;

/* Quick observation accepts an injected read-only transport. There is
 * deliberately no write callback in this API. The selected display snapshot
 * must come from one successful correlation, never a global match. */
typedef enum {
  RSS_DDC_PROBE_CORRELATION_EXACT = 0,
  RSS_DDC_PROBE_CORRELATION_AMBIGUOUS,
} RSSDDCProbeCorrelation;
typedef enum {
  RSS_DDC_PROBE_CONTROL_NOT_ATTEMPTED = 0,
  RSS_DDC_PROBE_CONTROL_STABLE,
  RSS_DDC_PROBE_CONTROL_VARIABLE,
  RSS_DDC_PROBE_CONTROL_UNSUPPORTED,
  RSS_DDC_PROBE_CONTROL_MALFORMED,
  RSS_DDC_PROBE_CONTROL_TRANSPORT_ERROR,
} RSSDDCProbeControlClassification;
typedef enum {
  RSS_DDC_PROBE_ABORT_NONE = 0,
  RSS_DDC_PROBE_ABORT_TRANSPORT_FAILURE_STORM,
} RSSDDCProbeAbortReason;
typedef RSSDDCError (*RSSDDCProbeGetVCP)(void *context, uint8_t vcp_code,
                                         RSSDDCVCPResult *result);
typedef RSSDDCError (*RSSDDCProbeGetMCCSCapabilities)(
    void *context, RSSDDCMCCSCapabilities *capabilities);
/** Optional pacing callback for a read-only probe. It has no monitor write
 * capability; production callers use it between Extended Probe GET requests. */
typedef void (*RSSDDCProbeDelay)(void *context, uint32_t milliseconds);
typedef void (*RSSDDCProbeProgress)(void *context, size_t attempted,
                                    size_t requested);
typedef struct {
  void *context;
  RSSDDCProbeGetVCP get_vcp;
  RSSDDCProbeGetMCCSCapabilities get_mccs_capabilities;
  RSSDDCProbeDelay delay;
  RSSDDCProbeProgress progress;
} RSSDDCProbeReadTransport;
typedef struct {
  RSSDDCDisplay display;
  RSSDDCProbeCorrelation correlation;
} RSSDDCProbeTarget;
typedef struct {
  const char *semantic_id;
  uint8_t vcp_code;
  RSSDDCError first_error;
  RSSDDCError repeat_error;
  bool readable;
  bool stable;
  bool mccs_advertised;
  bool known_semantic;
  uint16_t current_value;
  uint16_t maximum_value;
  uint16_t repeat_current_value;
  uint16_t repeat_maximum_value;
  RSSDDCProbeControlClassification classification;
} RSSDDCProbeControlDiagnostic;
typedef struct {
  RSSDDCDisplay display;
  RSSDDCError mccs_error;
  bool extended;
  uint32_t inter_request_delay_ms;
  size_t stability_read_count;
  uint64_t duration_ms;
  size_t requested_addresses;
  size_t controls_attempted;
  size_t controls_readable;
  size_t controls_stable;
  size_t controls_variable;
  size_t controls_failed;
  size_t controls_unsupported;
  size_t controls_malformed;
  size_t controls_transport_errors;
  bool aborted;
  RSSDDCProbeAbortReason abort_reason;
  size_t control_count;
  const RSSDDCProbeControlDiagnostic *controls;
} RSSDDCProbeDiagnostics;
typedef struct RSSDDCProbe RSSDDCProbe;

RSSDDCError rss_ddc_probe_create(const RSSDDCProbeTarget *target,
                                 const RSSDDCProbeReadTransport *transport,
                                 RSSDDCProbe **probe);
void rss_ddc_probe_destroy(RSSDDCProbe *probe);
RSSDDCError rss_ddc_probe_quick(RSSDDCProbe *probe);
/** Performs one bounded, staged, GET-only scan of the selected target. The
 * result is one fresh observation document; callers may merge it explicitly. */
RSSDDCError rss_ddc_probe_extended(RSSDDCProbe *probe);
RSSDDCError rss_ddc_probe_knowledge(const RSSDDCProbe *probe,
                                    const RSSDDCMonitorKnowledge **knowledge);
RSSDDCError rss_ddc_probe_diagnostics(const RSSDDCProbe *probe,
                                      RSSDDCProbeDiagnostics *diagnostics);
/** Convenience form for exactly one current list index. It performs only the
 * public correlated display, Get VCP, and MCCS-capability reads. */
RSSDDCError rss_ddc_probe_quick_for_display(uint32_t list_index,
                                            RSSDDCProbe **probe);
RSSDDCError rss_ddc_probe_extended_for_display(uint32_t list_index,
                                               RSSDDCProbe **probe);

RSSDDCMonitorKnowledge *rss_ddc_monitor_knowledge_create(void);
void rss_ddc_monitor_knowledge_destroy(RSSDDCMonitorKnowledge *knowledge);
RSSDDCError
rss_ddc_monitor_knowledge_parse_json(const char *data, size_t length,
                                     RSSDDCMonitorKnowledge **knowledge);
RSSDDCError rss_ddc_monitor_knowledge_serialize_json(
    const RSSDDCMonitorKnowledge *knowledge, char *buffer, size_t capacity,
    size_t *required);
RSSDDCError
rss_ddc_monitor_knowledge_validate(const RSSDDCMonitorKnowledge *knowledge);
const char *rss_ddc_monitor_knowledge_schema_version(
    const RSSDDCMonitorKnowledge *knowledge);
RSSDDCError
rss_ddc_monitor_knowledge_identity(const RSSDDCMonitorKnowledge *knowledge,
                                   RSSDDCMonitorIdentity *identity);
size_t rss_ddc_monitor_knowledge_source_count(
    const RSSDDCMonitorKnowledge *knowledge);
RSSDDCError rss_ddc_monitor_knowledge_source(
    const RSSDDCMonitorKnowledge *knowledge, size_t index,
    RSSDDCMonitorKnowledgeSource *source);
size_t rss_ddc_monitor_knowledge_capability_count(
    const RSSDDCMonitorKnowledge *knowledge);
RSSDDCError rss_ddc_monitor_knowledge_capability(
    const RSSDDCMonitorKnowledge *knowledge, size_t index,
    RSSDDCMonitorKnowledgeCapability *capability);
RSSDDCError rss_ddc_monitor_knowledge_find_capability(
    const RSSDDCMonitorKnowledge *knowledge, const char *semantic_id,
    RSSDDCMonitorKnowledgeCapability *capability);
size_t rss_ddc_monitor_knowledge_input_route_count(
    const RSSDDCMonitorKnowledge *knowledge);
RSSDDCError
rss_ddc_monitor_knowledge_input_route(const RSSDDCMonitorKnowledge *knowledge,
                                      size_t index, RSSDDCInputRoute *route);
size_t rss_ddc_monitor_knowledge_relationship_count(
    const RSSDDCMonitorKnowledge *knowledge);
RSSDDCError
rss_ddc_monitor_knowledge_relationship(const RSSDDCMonitorKnowledge *knowledge,
                                       size_t index,
                                       RSSDDCRelationship *relationship);
RSSDDCError
rss_ddc_monitor_knowledge_merge(const RSSDDCMonitorKnowledge *base,
                                const RSSDDCMonitorKnowledge *overlay,
                                RSSDDCMonitorKnowledge **merged);
RSSDDCError rss_ddc_monitor_knowledge_resolve_capability(
    const RSSDDCMonitorKnowledge *const *sources, size_t source_count,
    const char *semantic_id, RSSDDCMonitorKnowledgeResolution **resolution);
void rss_ddc_monitor_knowledge_resolution_destroy(
    RSSDDCMonitorKnowledgeResolution *resolution);
const char *rss_ddc_monitor_knowledge_resolution_semantic_id(
    const RSSDDCMonitorKnowledgeResolution *resolution);
RSSDDCAvailability rss_ddc_monitor_knowledge_resolution_availability(
    const RSSDDCMonitorKnowledgeResolution *resolution);
RSSDDCConfidence rss_ddc_monitor_knowledge_resolution_confidence(
    const RSSDDCMonitorKnowledgeResolution *resolution);
bool rss_ddc_monitor_knowledge_resolution_write_authorized(
    const RSSDDCMonitorKnowledgeResolution *resolution);
bool rss_ddc_monitor_knowledge_resolution_has_conflict(
    const RSSDDCMonitorKnowledgeResolution *resolution);
RSSDDCResolutionReason rss_ddc_monitor_knowledge_resolution_reason(
    const RSSDDCMonitorKnowledgeResolution *resolution);
const RSSDDCMonitorKnowledgeMethod *
rss_ddc_monitor_knowledge_resolution_preferred_read(
    const RSSDDCMonitorKnowledgeResolution *resolution);
const RSSDDCMonitorKnowledgeMethod *
rss_ddc_monitor_knowledge_resolution_preferred_write(
    const RSSDDCMonitorKnowledgeResolution *resolution);
size_t rss_ddc_monitor_knowledge_resolution_method_count(
    const RSSDDCMonitorKnowledgeResolution *resolution);
RSSDDCError rss_ddc_monitor_knowledge_resolution_method(
    const RSSDDCMonitorKnowledgeResolution *resolution, size_t index,
    const RSSDDCMonitorKnowledgeMethod **method);
size_t rss_ddc_monitor_knowledge_resolution_condition_count(
    const RSSDDCMonitorKnowledgeResolution *resolution);
RSSDDCError rss_ddc_monitor_knowledge_resolution_condition(
    const RSSDDCMonitorKnowledgeResolution *resolution, size_t index,
    const char **condition);
RSSDDCError rss_ddc_monitor_knowledge_resolve_value(
    const RSSDDCMonitorKnowledge *const *sources, size_t source_count,
    const char *semantic_id, const char *value_id,
    RSSDDCMonitorKnowledgeValueResolution **resolution);
void rss_ddc_monitor_knowledge_value_resolution_destroy(
    RSSDDCMonitorKnowledgeValueResolution *resolution);
const char *rss_ddc_monitor_knowledge_value_resolution_id(
    const RSSDDCMonitorKnowledgeValueResolution *resolution);
size_t rss_ddc_monitor_knowledge_value_resolution_candidate_count(
    const RSSDDCMonitorKnowledgeValueResolution *resolution);
RSSDDCError rss_ddc_monitor_knowledge_value_resolution_candidate(
    const RSSDDCMonitorKnowledgeValueResolution *resolution, size_t index,
    const RSSDDCMonitorKnowledgeValue **value);
const RSSDDCMonitorKnowledgeValue *
rss_ddc_monitor_knowledge_value_resolution_preferred_read(
    const RSSDDCMonitorKnowledgeValueResolution *resolution);
const RSSDDCMonitorKnowledgeValue *
rss_ddc_monitor_knowledge_value_resolution_preferred_write(
    const RSSDDCMonitorKnowledgeValueResolution *resolution);
bool rss_ddc_monitor_knowledge_value_resolution_write_authorized(
    const RSSDDCMonitorKnowledgeValueResolution *resolution);
bool rss_ddc_monitor_knowledge_value_resolution_has_conflict(
    const RSSDDCMonitorKnowledgeValueResolution *resolution);
RSSDDCError rss_ddc_monitor_knowledge_resolve_range(
    const RSSDDCMonitorKnowledge *const *sources, size_t source_count,
    const char *semantic_id,
    RSSDDCMonitorKnowledgeRangeResolution **resolution);
void rss_ddc_monitor_knowledge_range_resolution_destroy(
    RSSDDCMonitorKnowledgeRangeResolution *resolution);
bool rss_ddc_monitor_knowledge_range_resolution_advertised(
    const RSSDDCMonitorKnowledgeRangeResolution *resolution,
    RSSDDCRange *range);
bool rss_ddc_monitor_knowledge_range_resolution_observed(
    const RSSDDCMonitorKnowledgeRangeResolution *resolution,
    RSSDDCRange *range);
bool rss_ddc_monitor_knowledge_range_resolution_validated(
    const RSSDDCMonitorKnowledgeRangeResolution *resolution,
    RSSDDCRange *range);
bool rss_ddc_monitor_knowledge_range_resolution_write_range(
    const RSSDDCMonitorKnowledgeRangeResolution *resolution,
    RSSDDCRange *range);
bool rss_ddc_monitor_knowledge_range_resolution_has_conflict(
    const RSSDDCMonitorKnowledgeRangeResolution *resolution);
RSSDDCError rss_ddc_monitor_knowledge_resolve_input_route(
    const RSSDDCMonitorKnowledge *const *sources, size_t source_count,
    const char *route_id, RSSDDCInputRouteResolution **resolution);
void rss_ddc_input_route_resolution_destroy(
    RSSDDCInputRouteResolution *resolution);
size_t rss_ddc_input_route_resolution_candidate_count(
    const RSSDDCInputRouteResolution *resolution);
RSSDDCError rss_ddc_input_route_resolution_candidate(
    const RSSDDCInputRouteResolution *resolution, size_t index,
    const RSSDDCInputRoute **route);
const RSSDDCInputRoute *rss_ddc_input_route_resolution_preferred_read(
    const RSSDDCInputRouteResolution *resolution);
const RSSDDCInputRoute *rss_ddc_input_route_resolution_preferred_switch(
    const RSSDDCInputRouteResolution *resolution);
bool rss_ddc_input_route_resolution_switch_authorized(
    const RSSDDCInputRouteResolution *resolution);
bool rss_ddc_input_route_resolution_has_conflict(
    const RSSDDCInputRouteResolution *resolution);
const RSSDDCSemanticRegistryEntry *
rss_ddc_semantic_registry_lookup(const char *semantic_id);
const RSSDDCSemanticRegistryEntry *
rss_ddc_semantic_registry_lookup_vcp(uint8_t vcp_code);
const char *rss_ddc_confidence_name(RSSDDCConfidence value);
RSSDDCError rss_ddc_confidence_parse(const char *name, RSSDDCConfidence *value);
const char *rss_ddc_validation_name(RSSDDCValidation value);
RSSDDCError rss_ddc_validation_parse(const char *name, RSSDDCValidation *value);
const char *rss_ddc_risk_name(RSSDDCRisk value);
RSSDDCError rss_ddc_risk_parse(const char *name, RSSDDCRisk *value);
const char *rss_ddc_evidence_type_name(RSSDDCEvidenceType value);
RSSDDCError rss_ddc_evidence_type_parse(const char *name,
                                        RSSDDCEvidenceType *value);

#ifdef __cplusplus
}
#endif

#endif
