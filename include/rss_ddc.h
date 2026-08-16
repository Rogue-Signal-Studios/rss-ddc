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

/** Independent capabilities. Provider and exact-profile capabilities are documented separately. */
typedef enum {
    RSS_DDC_CAP_NONE = 0,
    RSS_DDC_CAP_GET_VCP = 1u << 0,
    RSS_DDC_CAP_SET_VCP = 1u << 1,
    RSS_DDC_CAP_READ_EDID = 1u << 2,
    RSS_DDC_CAP_READ_DPCD = 1u << 3,
    /** DCPDP13 can retrieve and strictly parse a complete MCCS capabilities string. */
    RSS_DDC_CAP_MCCS_CAPABILITIES = 1u << 4,
    /** DCPDP13 can issue the separately validated LG alternate-input transport. */
    RSS_DDC_CAP_ALTERNATE_INPUT = 1u << 5,
    /** An exact monitor profile has an evidence-backed semantic Picture Mode operation. */
    RSS_DDC_CAP_PICTURE_MODE = 1u << 6,
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
    RSS_DDC_ERROR_SYSTEM,
    /** A bounded MCCS capabilities string is syntactically invalid. */
    RSS_DDC_ERROR_CAPABILITIES_MALFORMED,
    /** A capabilities string or parsed model exceeds an explicit bound. */
    RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE,
    /** MCCS retrieval exceeded its bounded fragment request count. */
    RSS_DDC_ERROR_CAPABILITIES_REQUEST_LIMIT,
    /** MCCS fragment progression would exceed the 16-bit protocol offset. */
    RSS_DDC_ERROR_CAPABILITIES_OFFSET_OVERFLOW,
    /** MCCS retrieval ended without an explicit zero-length terminator. */
    RSS_DDC_ERROR_CAPABILITIES_INCOMPLETE,
    RSS_DDC_ERROR_PROFILE_MALFORMED,
    RSS_DDC_ERROR_PROFILE_SCHEMA,
    RSS_DDC_ERROR_PROFILE_VERSION,
    RSS_DDC_ERROR_PROFILE_CONFLICT,
    RSS_DDC_ERROR_PROFILE_UNSAFE,
    /** monitor-knowledge/v0.1 JSON is syntactically invalid or missing required fields. */
    RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED,
    /** schemaVersion is absent or is not monitor-knowledge/v0.1. */
    RSS_DDC_ERROR_MONITOR_KNOWLEDGE_SCHEMA,
    /** JSON document, capability, or string bounds were exceeded. */
    RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE,
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
    /** Maximum raw bytes preserved by the pure MCCS capabilities parser. */
    RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES = 4096,
    /** Maximum monitor-advertised VCP feature codes represented in one model. */
    RSS_DDC_MCCS_CAPABILITIES_MAX_FEATURES = 256,
    /** Maximum raw enum bytes represented across all advertised VCP features. */
    RSS_DDC_MCCS_CAPABILITIES_MAX_ENUM_VALUES = 1400,
    /** Nested unknown MCCS tokens are accepted only to this structural depth. */
    RSS_DDC_MCCS_CAPABILITIES_MAX_NESTING = 32,
    RSS_DDC_PROFILE_FILE_MAX_BYTES = 65536,
    RSS_DDC_PROFILE_MAX_PROFILES = 32,
    RSS_DDC_PROFILE_MAX_CONTROLS = 16,
    RSS_DDC_PROFILE_MAX_ENUM_VALUES = 32,
    RSS_DDC_PROFILE_ID_MAX = 64,
    RSS_DDC_PROFILE_VERSION_MAX = 64,
    /** Largest accepted or emitted monitor-knowledge/v0.1 document, including NUL. */
    RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_BYTES = 262144,
    /** Unique capability ids in one v0.1 document; matches the 128-route runtime cap. */
    RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_CAPABILITIES = 128,
    /** Methods retained per capability when parsing v0.1 JSON. */
    RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_METHODS = 32,
    /** Values retained per capability when parsing v0.1 JSON. */
    RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_VALUES = 32,
    /** Evidence records retained per object when parsing v0.1 JSON. */
    RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_EVIDENCE = 8,
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

/** One monitor-advertised VCP code and its optional raw enum-value slice. */
typedef struct {
    uint8_t vcp_code;
    size_t enum_value_offset;
    size_t enum_value_count;
} RSSDDCMCCSVcpCapability;

/**
 * Caller-owned, bounded result of parsing an MCCS capabilities string. The
 * raw bytes preserve unknown tokens verbatim and are NUL-terminated solely
 * for diagnostics. No capability in this model authorizes a display write.
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
 * Input selection deliberately distinguishes the ordinary MCCS VCP from the
 * one independently validated LG-specific transport.  The caller must choose
 * the method from monitor-specific evidence; this enum does not infer it.
 */
typedef enum {
    /** Use the existing provider-specific Set VCP path for MCCS VCP 0x60. */
    RSS_DDC_INPUT_SWITCH_STANDARD = 0,
    /** Use the narrowly gated, write-only LG alternate-input transport. */
    RSS_DDC_INPUT_SWITCH_LG_ALT,
} RSSDDCInputSwitchMethod;

/**
 * Friendly Picture Mode values for the one documented monitor profile. These
 * names are not generic MCCS semantics and deliberately expose no raw value.
 */
typedef enum {
    RSS_DDC_PICTURE_MODE_UNKNOWN = 0,
    RSS_DDC_PICTURE_MODE_VIVID,
    RSS_DDC_PICTURE_MODE_READER,
} RSSDDCPictureMode;

/** Profile provenance and evidence remain pure metadata in this slice. */
typedef enum { RSS_DDC_PROFILE_SOURCE_BUILTIN = 0, RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK,
               RSS_DDC_PROFILE_SOURCE_LOCAL, RSS_DDC_PROFILE_SOURCE_RESEARCH } RSSDDCProfileSource;
typedef enum { RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN = 0, RSS_DDC_PROFILE_CONFIDENCE_CANDIDATE,
               RSS_DDC_PROFILE_CONFIDENCE_OBSERVED, RSS_DDC_PROFILE_CONFIDENCE_CORRELATED,
               RSS_DDC_PROFILE_CONFIDENCE_SET_OBSERVED,
               RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED } RSSDDCProfileConfidence;
typedef enum { RSS_DDC_PROFILE_CONTROL_UNKNOWN = 0, RSS_DDC_PROFILE_CONTROL_PICTURE_MODE,
               RSS_DDC_PROFILE_CONTROL_INPUT, RSS_DDC_PROFILE_CONTROL_BRIGHTNESS,
               RSS_DDC_PROFILE_CONTROL_CONTRAST, RSS_DDC_PROFILE_CONTROL_COLOR_PRESET,
               RSS_DDC_PROFILE_CONTROL_RESPONSE_TIME, RSS_DDC_PROFILE_CONTROL_ADAPTIVE_SYNC,
               RSS_DDC_PROFILE_CONTROL_ENERGY_SAVING, RSS_DDC_PROFILE_CONTROL_BLACK_STABILIZER,
               RSS_DDC_PROFILE_CONTROL_GAMMA, RSS_DDC_PROFILE_CONTROL_SHARPNESS,
               RSS_DDC_PROFILE_CONTROL_AUDIO_MUTE } RSSDDCProfileControlID;
typedef enum { RSS_DDC_PROFILE_METHOD_UNKNOWN = 0, RSS_DDC_PROFILE_METHOD_VCP,
               RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT } RSSDDCProfileMethod;

/** Persistable matching facts; live list indexes and IOKit identities are intentionally absent. */
typedef struct {
    char manufacturer[RSS_DDC_TEXT_MAX], product_name[RSS_DDC_TEXT_MAX], serial[RSS_DDC_TEXT_MAX];
    char branch_device_id[RSS_DDC_TEXT_MAX], transport[RSS_DDC_TEXT_MAX];
    RSSDDCProvider provider;
    bool external;
} RSSDDCProfileIdentity;
typedef struct { char id[RSS_DDC_PROFILE_ID_MAX], name[RSS_DDC_TEXT_MAX]; uint16_t raw_value; } RSSDDCProfileEnumValue;
/** Stored control data only; `write_authorized` cannot execute a transport operation. */
typedef struct {
    RSSDDCProfileControlID id; RSSDDCProfileMethod method; uint16_t address;
    bool readable, writable, write_authorized;
    RSSDDCProfileSource source; RSSDDCProfileConfidence confidence;
    size_t enum_value_count;
    RSSDDCProfileEnumValue enum_values[RSS_DDC_PROFILE_MAX_ENUM_VALUES];
} RSSDDCProfileControl;
/** Caller-owned result; resolver changes it only on success. */
typedef struct {
    RSSDDCProfileIdentity identity; size_t control_count;
    RSSDDCProfileControl controls[RSS_DDC_PROFILE_MAX_CONTROLS];
} RSSDDCEffectiveProfile;
typedef struct {
    uint32_t schema_version;
    char database_version[RSS_DDC_PROFILE_VERSION_MAX], minimum_rss_ddc_version[RSS_DDC_PROFILE_VERSION_MAX];
    char pack_id[RSS_DDC_PROFILE_ID_MAX];
} RSSDDCProfilePackInfo;
/** Opaque, heap-owned store. Parsing and resolution never contact hardware. */
typedef struct RSSDDCProfileStore RSSDDCProfileStore;

/**
 * Pure monitor-knowledge facts. Distinct sources are retained even when their
 * values agree; a resolved view never erases their provenance.
 */
typedef enum { RSS_DDC_KNOWLEDGE_VALUE_UNKNOWN = 0, RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED,
               RSS_DDC_KNOWLEDGE_VALUE_STRING, RSS_DDC_KNOWLEDGE_VALUE_UNSUPPORTED } RSSDDCKnowledgeValueState;
typedef enum { RSS_DDC_KNOWLEDGE_ROUTE_UNKNOWN = 0, RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP,
               RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT, RSS_DDC_KNOWLEDGE_ROUTE_PICTURE_MODE,
               RSS_DDC_KNOWLEDGE_ROUTE_UNSUPPORTED } RSSDDCKnowledgeRouteKind;
typedef enum { RSS_DDC_KNOWLEDGE_FACT_DECLARED = 0, RSS_DDC_KNOWLEDGE_FACT_PROFILE,
               RSS_DDC_KNOWLEDGE_FACT_OBSERVED, RSS_DDC_KNOWLEDGE_FACT_INFERRED,
               RSS_DDC_KNOWLEDGE_FACT_RESOLVED } RSSDDCKnowledgeFactKind;
typedef enum { RSS_DDC_KNOWLEDGE_RESOLUTION_UNRESOLVED = 0,
               RSS_DDC_KNOWLEDGE_RESOLUTION_RESOLVED,
               RSS_DDC_KNOWLEDGE_RESOLUTION_CONFLICT } RSSDDCKnowledgeResolutionState;
typedef struct { RSSDDCKnowledgeValueState state; uint16_t unsigned_value; char string_value[RSS_DDC_TEXT_MAX]; } RSSDDCKnowledgeValue;
/*
 * source_id is the acquisition source, not a VCP-address inference:
 * mccs-capabilities, alien-probe-quick, alien-probe-extended. Older documents
 * may still carry alien-probe-live-read when stage was unspecified.
 * evidence_id records stability or advertisement: mccs-advertised, stable-get,
 * variable-get.
 */
typedef struct { char source_id[RSS_DDC_PROFILE_ID_MAX]; RSSDDCProfileSource source;
                 RSSDDCProfileConfidence confidence; RSSDDCKnowledgeFactKind fact_kind;
                 char evidence_id[RSS_DDC_PROFILE_ID_MAX]; } RSSDDCKnowledgeProvenance;
typedef struct { char semantic_id[RSS_DDC_TEXT_MAX], route_id[RSS_DDC_PROFILE_ID_MAX];
                 RSSDDCKnowledgeRouteKind kind; uint16_t address; bool readable, writable;
                 bool write_authorized; char transport_family[RSS_DDC_TEXT_MAX];
                 char command_semantics[RSS_DDC_TEXT_MAX]; char applicability[RSS_DDC_TEXT_MAX];
                 bool reported_maximum_present; uint16_t reported_maximum;
                 RSSDDCKnowledgeValue value; RSSDDCKnowledgeProvenance provenance; } RSSDDCKnowledgeRoute;
typedef struct RSSDDCMonitorKnowledge RSSDDCMonitorKnowledge;
typedef struct RSSDDCMonitorKnowledgeResolution RSSDDCMonitorKnowledgeResolution;

/** Canonical durable discovery schema. Not profile schemaVersion 1. */
#define RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA "monitor-knowledge/v0.1"

/**
 * Identity snapshot for monitor-knowledge/v0.1 JSON only. Not a second
 * runtime knowledge model. Process-local list_index and cg_display_id are
 * intentionally absent. Empty strings are omitted on emit.
 */
typedef struct {
    char manufacturer[RSS_DDC_TEXT_MAX];
    char model[RSS_DDC_TEXT_MAX];
    char edid_manufacturer[RSS_DDC_TEXT_MAX];
    char serial[RSS_DDC_TEXT_MAX];
    char provider[RSS_DDC_TEXT_MAX];
    char transport[RSS_DDC_TEXT_MAX];
    char branch[RSS_DDC_TEXT_MAX];
    uint16_t edid_product_code;
    bool edid_product_code_present;
} RSSDDCMonitorKnowledgeIdentity;

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

/** Create/destroy transfer sole ownership of a heap-backed offline profile store. */
RSSDDCProfileStore *rss_ddc_profile_store_create(void);
void rss_ddc_profile_store_destroy(RSSDDCProfileStore *store);
/** All loads are transactional: failure leaves `store` unchanged. */
RSSDDCError rss_ddc_profile_store_load_builtin(RSSDDCProfileStore *store);
RSSDDCError rss_ddc_profile_store_load_pack_data(RSSDDCProfileStore *store, const char *data, size_t length);
RSSDDCError rss_ddc_profile_store_load_local_data(RSSDDCProfileStore *store, const char *data, size_t length);
RSSDDCError rss_ddc_profile_store_load_research_data(RSSDDCProfileStore *store, const char *data, size_t length);
RSSDDCError rss_ddc_profile_store_load_pack_file(RSSDDCProfileStore *store, const char *path);
RSSDDCError rss_ddc_profile_store_load_local_file(RSSDDCProfileStore *store, const char *path);
RSSDDCError rss_ddc_profile_store_load_research_file(RSSDDCProfileStore *store, const char *path);
/** Validates bytes without changing a store; `info` is written only after success. */
RSSDDCError rss_ddc_profile_validate_pack_data(const char *data, size_t length, RSSDDCProfileSource source,
                                               RSSDDCProfilePackInfo *info);
RSSDDCError rss_ddc_profile_store_pack_info(const RSSDDCProfileStore *store, RSSDDCProfilePackInfo *info);
/** Export is caller-buffer owned; NULL/0 queries required bytes including NUL. */
RSSDDCError rss_ddc_profile_store_export_json(const RSSDDCProfileStore *store, char *buffer, size_t capacity,
                                              size_t *required);
/**
 * Export only LOCAL-origin records using explicit local-pack metadata
 * (`schemaVersion` 1, `packId`/`databaseVersion` `local-export`). Builtin,
 * validated-pack, and research records are omitted. NULL/0 queries required
 * bytes including NUL.
 */
RSSDDCError rss_ddc_profile_store_export_local_json(const RSSDDCProfileStore *store, char *buffer, size_t capacity,
                                                    size_t *required);
/** Save writes a complete temporary file and atomically renames it over `path` on success. */
RSSDDCError rss_ddc_profile_store_save_file(const RSSDDCProfileStore *store, const char *path);
/**
 * Atomically writes LOCAL overlay JSON to `path`. Does not flatten builtin
 * profiles into the file and does not copy last-loaded pack metadata.
 */
RSSDDCError rss_ddc_profile_store_save_local_file(const RSSDDCProfileStore *store, const char *path);
/**
 * Inserts or replaces one LOCAL overlay profile in memory. Builtin, validated-pack,
 * and research records are never modified. A LOCAL record with the same identity
 * is replaced; otherwise a new LOCAL record is appended. Does not write disk.
 * Failure leaves `store` unchanged.
 */
RSSDDCError rss_ddc_profile_store_put_local_profile(RSSDDCProfileStore *store, const char *id,
                                                    const RSSDDCProfileIdentity *identity,
                                                    RSSDDCProfileConfidence confidence,
                                                    const RSSDDCProfileControl *controls,
                                                    size_t control_count);
/** Pure deterministic resolution; caller output remains unchanged on failure. */
RSSDDCError rss_ddc_profile_store_resolve(const RSSDDCProfileStore *store, const RSSDDCProfileIdentity *identity,
                                          RSSDDCEffectiveProfile *effective);
size_t rss_ddc_effective_profile_control_count(const RSSDDCEffectiveProfile *effective);
RSSDDCError rss_ddc_effective_profile_control(const RSSDDCEffectiveProfile *effective, size_t index,
                                              RSSDDCProfileControl *control);
RSSDDCError rss_ddc_profile_control_enum_value(const RSSDDCProfileControl *control, size_t index,
                                               RSSDDCProfileEnumValue *value);
/** Copies only an existing public snapshot into a persistable identity; it never discovers displays. */
void rss_ddc_profile_identity_from_display(const RSSDDCDisplay *display, RSSDDCProfileIdentity *identity);
const char *rss_ddc_profile_control_name(RSSDDCProfileControlID id);
const char *rss_ddc_profile_source_name(RSSDDCProfileSource source);
const char *rss_ddc_profile_confidence_name(RSSDDCProfileConfidence confidence);
/** Heap-owned pure knowledge objects; add/merge never executes a monitor operation. */
RSSDDCMonitorKnowledge *rss_ddc_monitor_knowledge_create(void);
void rss_ddc_monitor_knowledge_destroy(RSSDDCMonitorKnowledge *knowledge);
RSSDDCError rss_ddc_monitor_knowledge_add_route(RSSDDCMonitorKnowledge *knowledge,
                                                 const RSSDDCKnowledgeRoute *route);
/**
 * Copies a Slice 5 profile control as one profile-derived fact. The resulting
 * record is metadata only: it neither selects a live display nor authorizes
 * or performs a transport operation.
 */
RSSDDCError rss_ddc_monitor_knowledge_add_profile_control(RSSDDCMonitorKnowledge *knowledge,
                                                           const char *semantic_id,
                                                           const char *source_id,
                                                           const RSSDDCProfileControl *control);
size_t rss_ddc_monitor_knowledge_route_count(const RSSDDCMonitorKnowledge *knowledge);
/** Returns a borrowed immutable route valid until `knowledge` is changed or destroyed. */
const RSSDDCKnowledgeRoute *rss_ddc_monitor_knowledge_route_at(const RSSDDCMonitorKnowledge *knowledge, size_t index);
/**
 * Deterministic monitor-knowledge/v0.1 JSON from the current route bag plus
 * optional identity. NULL/0 queries required bytes including NUL. Does not
 * invent write authority. PROFILE routes are included only if present in
 * `knowledge`; characterization export uses discovered knowledge instead.
 * Evidence type follows acquisition source_id (Quick=stable_get,
 * Extended=extended_discovery), not VCP address. Capability confidence is the
 * strongest route confidence; capability validation is read_validated when any
 * grouped route is OBSERVED and is omitted when that read evidence coexists
 * with PROFILE write-class confidence.
 */
RSSDDCError rss_ddc_monitor_knowledge_serialize_json(const RSSDDCMonitorKnowledge *knowledge,
                                                     const RSSDDCMonitorKnowledgeIdentity *identity,
                                                     char *buffer, size_t capacity, size_t *required);
/**
 * Bounded parse of monitor-knowledge/v0.1 into the current route bag.
 * Failure leaves `*knowledge` NULL and does not write `identity`. Unknown
 * keys are ignored. Parsed GET/DECLARED methods never set write_authorized.
 */
RSSDDCError rss_ddc_monitor_knowledge_parse_json(const char *data, size_t length,
                                                 RSSDDCMonitorKnowledge **knowledge,
                                                 RSSDDCMonitorKnowledgeIdentity *identity);
/**
 * Atomically writes v0.1 JSON to `path` (temporary file + rename). An existing
 * destination is replaced only after the complete document is on disk.
 */
RSSDDCError rss_ddc_monitor_knowledge_write_json_file(const RSSDDCMonitorKnowledge *knowledge,
                                                      const RSSDDCMonitorKnowledgeIdentity *identity,
                                                      const char *path);
/** Transactionally creates a deep-copied union retaining every non-identical source route. */
RSSDDCError rss_ddc_monitor_knowledge_merge(const RSSDDCMonitorKnowledge *first,
                                            const RSSDDCMonitorKnowledge *second,
                                            RSSDDCMonitorKnowledge **merged);
/** Resolves one semantic control; all candidate routes and provenance remain borrowed from the sources. */
RSSDDCError rss_ddc_monitor_knowledge_resolve(const RSSDDCMonitorKnowledge *const *sources, size_t source_count,
                                              const char *semantic_id, RSSDDCMonitorKnowledgeResolution **resolution);
void rss_ddc_monitor_knowledge_resolution_destroy(RSSDDCMonitorKnowledgeResolution *resolution);
RSSDDCKnowledgeResolutionState rss_ddc_monitor_knowledge_resolution_state(const RSSDDCMonitorKnowledgeResolution *resolution);
bool rss_ddc_monitor_knowledge_resolution_has_conflict(const RSSDDCMonitorKnowledgeResolution *resolution);
const RSSDDCKnowledgeRoute *rss_ddc_monitor_knowledge_resolution_preferred_read(const RSSDDCMonitorKnowledgeResolution *resolution);
const RSSDDCKnowledgeRoute *rss_ddc_monitor_knowledge_resolution_preferred_write(const RSSDDCMonitorKnowledgeResolution *resolution);
/** A selected writable route is not automatically authorized. */
bool rss_ddc_monitor_knowledge_resolution_write_authorized(const RSSDDCMonitorKnowledgeResolution *resolution);
size_t rss_ddc_monitor_knowledge_resolution_candidate_count(const RSSDDCMonitorKnowledgeResolution *resolution);
const RSSDDCKnowledgeRoute *rss_ddc_monitor_knowledge_resolution_candidate_at(const RSSDDCMonitorKnowledgeResolution *resolution,size_t index);

/** Alien Probe Quick is a bounded, read-only observation consumer. */
enum { RSS_DDC_PROBE_QUICK_CONTROL_COUNT = 6, RSS_DDC_PROBE_QUICK_REPEAT_COUNT = 2,
       RSS_DDC_PROBE_QUICK_REPEAT_DELAY_MS = 0 };
/** Alien Probe Extended scans 0x00..0xFF with paced, read-only GET requests. */
enum { RSS_DDC_PROBE_EXTENDED_ADDRESS_COUNT = 256, RSS_DDC_PROBE_EXTENDED_REPEAT_COUNT = 2,
       RSS_DDC_PROBE_EXTENDED_INTER_ADDRESS_DELAY_MS = 25,
       RSS_DDC_PROBE_EXTENDED_REPEAT_DELAY_MS = 25,
       RSS_DDC_PROBE_EXTENDED_TRANSPORT_FAILURE_LIMIT = 8 };
typedef enum { RSS_DDC_PROBE_CORRELATION_EXACT = 0, RSS_DDC_PROBE_CORRELATION_AMBIGUOUS } RSSDDCProbeCorrelation;
typedef enum { RSS_DDC_PROBE_TRANSPORT_NOT_ATTEMPTED = 0, RSS_DDC_PROBE_TRANSPORT_SUCCEEDED,
               RSS_DDC_PROBE_TRANSPORT_FAILED } RSSDDCProbeTransportState;
typedef enum { RSS_DDC_PROBE_KNOWLEDGE_UNKNOWN = 0, RSS_DDC_PROBE_KNOWLEDGE_YES,
               RSS_DDC_PROBE_KNOWLEDGE_NO } RSSDDCProbeKnowledgeState;
typedef enum { RSS_DDC_PROBE_RESULT_UNATTEMPTED = 0, RSS_DDC_PROBE_RESULT_STABLE,
               RSS_DDC_PROBE_RESULT_VARIABLE, RSS_DDC_PROBE_RESULT_PROTOCOL_REPORTED,
               RSS_DDC_PROBE_RESULT_MALFORMED, RSS_DDC_PROBE_RESULT_SEMANTIC_MISMATCH,
               RSS_DDC_PROBE_RESULT_TRANSPORT_ERROR } RSSDDCProbeResultCategory;
typedef enum { RSS_DDC_PROBE_INTERPRETATION_UNKNOWN = 0,
               RSS_DDC_PROBE_INTERPRETATION_OBSERVED_PROTOCOL_VALID,
               RSS_DDC_PROBE_INTERPRETATION_OBSERVED_ADVERTISED,
               RSS_DDC_PROBE_INTERPRETATION_OBSERVED_UNADVERTISED } RSSDDCProbeInterpretationConfidence;
typedef RSSDDCError (*RSSDDCProbeGetVCP)(void *context, uint8_t vcp_code, RSSDDCVCPResult *result);
typedef RSSDDCError (*RSSDDCProbeGetMCCSCapabilities)(void *context, RSSDDCMCCSCapabilities *capabilities);
/** Optional pacing callback for read-only probe scans; it has no write capability. */
typedef void (*RSSDDCProbeDelay)(void *context, uint32_t milliseconds);
typedef struct { void *context; RSSDDCProbeGetVCP get_vcp;
                 RSSDDCProbeGetMCCSCapabilities get_mccs_capabilities;
                 RSSDDCProbeDelay delay; } RSSDDCProbeReadTransport;
typedef struct { RSSDDCDisplay display; RSSDDCProbeCorrelation correlation;
                 const RSSDDCMonitorKnowledge *profile_knowledge; } RSSDDCProbeTarget;
typedef struct { const char *semantic_id; uint8_t requested_vcp; RSSDDCProbeResultCategory category;
                 RSSDDCProbeTransportState transport; RSSDDCError first_error, repeat_error;
                 bool protocol_valid, semantic_request_match, stable, current_exceeds_maximum, repeat_attempted;
                 RSSDDCProbeKnowledgeState advertised, profile_known; uint16_t current_value, maximum_value; } RSSDDCProbeObservation;
typedef struct { RSSDDCProbeObservation observation; char semantic_id_buffer[32];
                 RSSDDCProbeInterpretationConfidence interpretation;
                 bool enum_list_present, current_in_declared_enum; } RSSDDCProbeExtendedObservation;
typedef struct { RSSDDCDisplay display; RSSDDCError mccs_error; bool mccs_available;
                 size_t controls_attempted, controls_protocol_valid, controls_stable, controls_variable,
                        controls_protocol_reported, controls_malformed, controls_transport_error;
                 size_t observation_count; const RSSDDCProbeObservation *observations; } RSSDDCProbeDiagnostics;
typedef struct { RSSDDCDisplay display; RSSDDCError mccs_error; bool mccs_available; uint64_t duration_ms;
                 bool aborted; size_t requested, attempted, strict_valid, stable_valid, variable_valid,
                        protocol_reported, semantic_mismatch, malformed, transport_errors, advertised_valid,
                        unadvertised_valid; size_t observation_count;
                 const RSSDDCProbeExtendedObservation *observations; } RSSDDCProbeExtendedDiagnostics;
typedef struct RSSDDCProbe RSSDDCProbe;
/** Creates a heap-backed observer. The target's optional profile knowledge is borrowed. */
RSSDDCError rss_ddc_probe_create(const RSSDDCProbeTarget *target, const RSSDDCProbeReadTransport *transport,
                                 RSSDDCProbe **probe);
void rss_ddc_probe_destroy(RSSDDCProbe *probe);
/** Makes exactly two immediate reads of each of six fixed VCPs; it has no write callback or mutation path. */
RSSDDCError rss_ddc_probe_quick(RSSDDCProbe *probe);
/** Scans 0x00..0xFF with paced, read-only GET requests on validated provider transports only. */
RSSDDCError rss_ddc_probe_extended(RSSDDCProbe *probe);
/** Returns probe-owned knowledge; it is valid until the probe changes or is destroyed. */
RSSDDCError rss_ddc_probe_knowledge(const RSSDDCProbe *probe, const RSSDDCMonitorKnowledge **knowledge);
RSSDDCError rss_ddc_probe_diagnostics(const RSSDDCProbe *probe, RSSDDCProbeDiagnostics *diagnostics);
RSSDDCError rss_ddc_probe_extended_diagnostics(const RSSDDCProbe *probe,
                                             RSSDDCProbeExtendedDiagnostics *diagnostics);
/** Returns the heap-backed parsed MCCS observation when available, otherwise NOT_FOUND. */
RSSDDCError rss_ddc_probe_mccs_capabilities(const RSSDDCProbe *probe, const RSSDDCMCCSCapabilities **capabilities);
/** Convenience form using only the existing public display, GetVCP, and MCCS APIs. */
RSSDDCError rss_ddc_probe_quick_for_display(uint32_t list_index, RSSDDCProbe **probe);
RSSDDCError rss_ddc_probe_extended_for_display(uint32_t list_index, RSSDDCProbe **probe);
const char *rss_ddc_probe_result_category_name(RSSDDCProbeResultCategory category);
const char *rss_ddc_probe_interpretation_name(RSSDDCProbeInterpretationConfidence interpretation);
/** Returns "not-attempted" when the stability repeat GET was not executed. */
const char *rss_ddc_probe_repeat_error_name(const RSSDDCProbeObservation *observation);

/**
 * Parses a bounded MCCS capabilities string without contacting a display.
 * `raw` need not be NUL-terminated; `raw_length` is authoritative. On
 * failure `capabilities` remains unchanged. Unknown top-level tokens remain
 * available in `raw`, while VCP declarations and enum values are modeled.
 */
RSSDDCError rss_ddc_parse_mccs_capabilities(const char *raw, size_t raw_length,
                                            RSSDDCMCCSCapabilities *capabilities);
/** Returns whether one VCP code was explicitly advertised by the parsed model. */
bool rss_ddc_mccs_capabilities_has_vcp(const RSSDDCMCCSCapabilities *capabilities, uint8_t vcp_code);
/**
 * Returns a caller-owned model's raw enum-value slice for one advertised VCP.
 * A successful lookup may have a zero count when the monitor advertised no
 * finite enum list for that VCP.
 */
RSSDDCError rss_ddc_mccs_capabilities_enum_values(const RSSDDCMCCSCapabilities *capabilities,
                                                  uint8_t vcp_code, const uint8_t **values,
                                                  size_t *count);
/**
 * Retrieves and parses DCPDP13's complete MCCS capabilities string. This is
 * read-only, provider-gated, and leaves unsupported providers—including
 * PS190—unsupported rather than attempting another transport family.
 */
RSSDDCError rss_ddc_get_mccs_capabilities(uint32_t list_index, RSSDDCMCCSCapabilities *capabilities);
/** Diagnostic form of rss_ddc_get_mccs_capabilities; callback text is transient. */
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
 * Selects an input through an explicit method. STANDARD is exactly the
 * ordinary Set VCP(0x60) path. LG_ALT is restricted to the documented LG HDR
 * QHD / DCPDP13Service / DCPEXT0 target and its three validated values.
 */
RSSDDCError rss_ddc_set_input(uint32_t list_index, RSSDDCInputSwitchMethod method, uint16_t value);
/** Diagnostic form of rss_ddc_set_input; diagnostics do not alter its writes or timing. */
RSSDDCError rss_ddc_set_input_with_diagnostics(uint32_t list_index, RSSDDCInputSwitchMethod method,
                                                uint16_t value, const RSSDDCDiagnostics *diagnostics);
/** Returns a static friendly name for one supported semantic Picture Mode. */
const char *rss_ddc_picture_mode_name(RSSDDCPictureMode mode);
/**
 * Sets one documented LG HDR QHD Picture Mode. This write-only semantic
 * operation is profile-gated and performs no GET, verification, retry,
 * restore, or transport fallback.
 */
RSSDDCError rss_ddc_set_picture_mode(uint32_t list_index, RSSDDCPictureMode mode);
/** Diagnostic form of rss_ddc_set_picture_mode. */
RSSDDCError rss_ddc_set_picture_mode_with_diagnostics(uint32_t list_index, RSSDDCPictureMode mode,
                                                       const RSSDDCDiagnostics *diagnostics);
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

/**
 * Automatic monitor characterization. The object is opaque; callers receive an
 * owned pointer from rss_ddc_characterize_display and release it with
 * rss_ddc_characterization_destroy. Accessor pointers are borrowed from the
 * owned object and remain valid until destroy. This API never SET/writes a
 * monitor or mutates a profile store.
 */
typedef struct RSSDDCCharacterization RSSDDCCharacterization;

typedef enum {
    RSS_DDC_CHARACTERIZE_MODE_PASSIVE = 0,
    RSS_DDC_CHARACTERIZE_MODE_DEFAULT,
    RSS_DDC_CHARACTERIZE_MODE_DEEP
} RSSDDCCharacterizeMode;

/** Whether characterization may load monitor-specific structured prior knowledge. */
typedef enum {
    RSS_DDC_CHARACTERIZE_KNOWLEDGE_NORMAL = 0,
    RSS_DDC_CHARACTERIZE_KNOWLEDGE_IGNORE_KNOWN
} RSSDDCCharacterizeKnowledgePolicy;

/**
 * v1 options. NULL options to rss_ddc_characterize_display means DEFAULT + NORMAL.
 * IGNORE_KNOWN disables profile/structured prior knowledge for a true alien path.
 */
typedef struct {
    RSSDDCCharacterizeMode mode;
    RSSDDCCharacterizeKnowledgePolicy knowledge_policy;
} RSSDDCCharacterizeOptions;

typedef enum {
    RSS_DDC_CHARACTERIZATION_VALUE_UNRESOLVED = 0,
    RSS_DDC_CHARACTERIZATION_VALUE_RESOLVED,
    RSS_DDC_CHARACTERIZATION_VALUE_CONFLICT
} RSSDDCCharacterizationValueState;

typedef enum {
    RSS_DDC_CHARACTERIZATION_PROFILE_NONE = 0,
    RSS_DDC_CHARACTERIZATION_PROFILE_MATCHED,
    RSS_DDC_CHARACTERIZATION_PROFILE_CONFLICT
} RSSDDCCharacterizationProfileStatus;

typedef enum {
    RSS_DDC_CHARACTERIZATION_SUFFICIENCY_SUFFICIENT = 0,
    RSS_DDC_CHARACTERIZATION_SUFFICIENCY_INSUFFICIENT,
    RSS_DDC_CHARACTERIZATION_SUFFICIENCY_UNAVAILABLE,
    RSS_DDC_CHARACTERIZATION_SUFFICIENCY_CONFLICT
} RSSDDCCharacterizationSufficiency;

/** Early structured-knowledge lookup result. Evaluated after identity/profile assemble. */
typedef enum {
    RSS_DDC_CHARACTERIZATION_STRUCTURED_NONE = 0,
    RSS_DDC_CHARACTERIZATION_STRUCTURED_PARTIAL,
    RSS_DDC_CHARACTERIZATION_STRUCTURED_COMPLETE,
    RSS_DDC_CHARACTERIZATION_STRUCTURED_CONFLICT
} RSSDDCCharacterizationStructuredMatch;

enum {
    RSS_DDC_CHARACTERIZATION_REASON_NONE = 0,
    RSS_DDC_CHARACTERIZATION_REASON_MISSING_CONTROL = 1u << 0,
    RSS_DDC_CHARACTERIZATION_REASON_UNRESOLVED_METHOD = 1u << 1,
    RSS_DDC_CHARACTERIZATION_REASON_CONFLICTING_METHOD = 1u << 2,
    RSS_DDC_CHARACTERIZATION_REASON_VARIABLE_OBSERVATION = 1u << 3,
    RSS_DDC_CHARACTERIZATION_REASON_NO_GET_SUPPORT = 1u << 4,
    RSS_DDC_CHARACTERIZATION_REASON_PROFILE_CONFLICT = 1u << 5,
    RSS_DDC_CHARACTERIZATION_REASON_PROBE_HELPFUL = 1u << 6
};

typedef struct {
    RSSDDCCharacterizationSufficiency status;
    uint32_t reasons;
    bool extended_recommended;
} RSSDDCCharacterizationSufficiencyResult;

typedef struct {
    size_t considered;
    size_t promoted;
    size_t skipped_capacity;
    size_t skipped_nonpromotable;
} RSSDDCCharacterizationPromotionSummary;

/** DEFAULT mode, no profile mutation, read-only automatic characterization. */
RSSDDCCharacterizeOptions rss_ddc_default_characterize_options(void);

/**
 * Characterizes the current 1-based `list_index` end-to-end.
 *
 * `profiles` is borrowed and may be NULL. The store is never mutated.
 * `options` may be NULL, which selects rss_ddc_default_characterize_options().
 *
 * PASSIVE runs identity and structured lookup; a COMPLETE match returns loaded
 * effective knowledge, otherwise passive MCCS only. DEFAULT does the same
 * lookup, then Alien Probe Quick and optional Extended unless the match is
 * COMPLETE. PARTIAL prior knowledge is retained but does not decide Quick or
 * Extended; it is merged into effective knowledge only after discovery.
 * DEEP always rediscovers (Quick + Extended when GET exists) even if a complete
 * profile is present. IGNORE_KNOWN disables monitor-specific profile data.
 * DEEP is still read-only and is
 * not Guided Discovery or Experimental Validation.
 *
 * On success or safe degradation, `*out` is an owned characterization. On
 * fatal failure (invalid arguments, allocation failure, or unresolvable
 * display), `*out` is NULL. INSUFFICIENT or CONFLICT sufficiency is not a
 * fatal API error.
 *
 * This entry point does not call SET VCP, set-and-verify, alternate-input
 * write, picture-mode SET, profile persistence, Guided Discovery, or
 * Experimental Validation.
 */
RSSDDCError rss_ddc_characterize_display(uint32_t list_index, const RSSDDCProfileStore *profiles,
                                         const RSSDDCCharacterizeOptions *options,
                                         RSSDDCCharacterization **out);
void rss_ddc_characterization_destroy(RSSDDCCharacterization *characterization);

/**
 * Copies `semantic_id` into `out`, replacing a known profile/schema alias with
 * its canonical dotted ID. Matching is exact and case-sensitive.
 */
RSSDDCError rss_ddc_characterization_normalize_semantic_id(const char *semantic_id, char *out,
                                                           size_t capacity);

/** Copied display snapshot, or NULL before a successful characterization. */
const RSSDDCDisplay *rss_ddc_characterization_display(const RSSDDCCharacterization *characterization);
/** Copied EDID decode, or NULL when EDID was not acquired. */
const RSSDDCEDIDInfo *rss_ddc_characterization_edid(const RSSDDCCharacterization *characterization);
/** Transport/platform bits from rss_ddc_provider_capabilities, not DECLARED MCCS facts. */
uint32_t rss_ddc_characterization_provider_capabilities(const RSSDDCCharacterization *characterization);
RSSDDCCharacterizationProfileStatus rss_ddc_characterization_profile_status(
    const RSSDDCCharacterization *characterization);
const RSSDDCProfileIdentity *rss_ddc_characterization_profile_identity(
    const RSSDDCCharacterization *characterization);
/** Effective matched profile, or NULL unless status is MATCHED. */
const RSSDDCEffectiveProfile *rss_ddc_characterization_effective_profile(
    const RSSDDCCharacterization *characterization);

/**
 * Borrowed effective/augmented knowledge; valid until destroy.
 * After discovery completes this may include retained prior PROFILE facts.
 * Use rss_ddc_characterization_discovered_knowledge for discovery-only facts.
 */
const RSSDDCMonitorKnowledge *rss_ddc_characterization_knowledge(
    const RSSDDCCharacterization *characterization);
/**
 * Borrowed discovery-only monitor knowledge: identity-adjacent evidence from
 * passive/MCCS, Quick, and Extended. Never includes prior PROFILE
 * augmentation. Valid until destroy. With IGNORE_KNOWN this is the same
 * object as rss_ddc_characterization_knowledge.
 */
const RSSDDCMonitorKnowledge *rss_ddc_characterization_discovered_knowledge(
    const RSSDDCCharacterization *characterization);
/**
 * Emits monitor-knowledge/v0.1 from discovery-only knowledge and hardware
 * identity. Never serializes prior PROFILE augmentation, LG_ALT profile
 * authority, or effective/augmented knowledge. COMPLETE cache-hits emit
 * identity with empty capabilities (no fabricated Alien Probe observations).
 * NULL/0 queries required bytes including NUL.
 */
RSSDDCError rss_ddc_characterization_serialize_discovered_json(
    const RSSDDCCharacterization *characterization, char *buffer, size_t capacity,
    size_t *required);
/**
 * Atomically writes the discovery v0.1 document to `path`. Same JSON as
 * rss_ddc_characterization_serialize_discovered_json. Overwrites `path` only
 * after a complete temporary file is fsynced.
 */
RSSDDCError rss_ddc_characterization_write_discovered_json_file(
    const RSSDDCCharacterization *characterization, const char *path);
/**
 * Resolves effective read/write methods for `semantic_id` after alias
 * normalization, using effective/augmented knowledge. Caller owns
 * `*resolution` and must destroy it.
 */
RSSDDCError rss_ddc_characterization_resolve(const RSSDDCCharacterization *characterization,
                                             const char *semantic_id,
                                             RSSDDCMonitorKnowledgeResolution **resolution);
/**
 * Selects a live current OBSERVED UNSIGNED/STRING value independently of
 * method resolution. UNKNOWN never wins.
 */
RSSDDCError rss_ddc_characterization_current_value(const RSSDDCCharacterization *characterization,
                                                   const char *semantic_id,
                                                   RSSDDCCharacterizationValueState *state,
                                                   const RSSDDCKnowledgeRoute **route);

bool rss_ddc_characterization_mccs_supported(const RSSDDCCharacterization *characterization);
bool rss_ddc_characterization_mccs_attempted(const RSSDDCCharacterization *characterization);
RSSDDCError rss_ddc_characterization_mccs_status(const RSSDDCCharacterization *characterization);
/** Borrowed parsed MCCS model, or NULL when none was successfully applied. */
const RSSDDCMCCSCapabilities *rss_ddc_characterization_mccs(
    const RSSDDCCharacterization *characterization);

bool rss_ddc_characterization_quick_supported(const RSSDDCCharacterization *characterization);
bool rss_ddc_characterization_quick_attempted(const RSSDDCCharacterization *characterization);
RSSDDCError rss_ddc_characterization_quick_status(const RSSDDCCharacterization *characterization);
/** Borrowed Quick diagnostics; valid until destroy. NULL if Quick did not run. */
const RSSDDCProbeDiagnostics *rss_ddc_characterization_quick_diagnostics(
    const RSSDDCCharacterization *characterization);

bool rss_ddc_characterization_extended_attempted(const RSSDDCCharacterization *characterization);
RSSDDCError rss_ddc_characterization_extended_status(const RSSDDCCharacterization *characterization);
/** Borrowed Extended diagnostics; valid until destroy. NULL if Extended did not copy diagnostics. */
const RSSDDCProbeExtendedDiagnostics *rss_ddc_characterization_extended_diagnostics(
    const RSSDDCCharacterization *characterization);
const RSSDDCCharacterizationPromotionSummary *rss_ddc_characterization_extended_promotion(
    const RSSDDCCharacterization *characterization);

/**
 * Pure DEFAULT-mode sufficiency over effective/augmented knowledge. After
 * discovery this may include prior PROFILE methods. PASSIVE reports the
 * passive-only state without having probed. DEEP reports the post-Extended
 * state when Extended ran. DEEP does not imply SUFFICIENT.
 */
RSSDDCError rss_ddc_characterization_sufficiency(
    const RSSDDCCharacterization *characterization,
    RSSDDCCharacterizationSufficiencyResult *result);

/**
 * Sufficiency over discovery-only knowledge. Pre-Extended depth uses this
 * result, never prior PROFILE methods. IGNORE_KNOWN matches
 * rss_ddc_characterization_sufficiency.
 */
RSSDDCError rss_ddc_characterization_discovery_sufficiency(
    const RSSDDCCharacterization *characterization,
    RSSDDCCharacterizationSufficiencyResult *result);

/**
 * Merges retained prior structured knowledge into effective knowledge after
 * discovery. Idempotent. Discovery knowledge is unchanged. COMPLETE
 * short-circuit also uses this to load the validated match.
 */
RSSDDCError rss_ddc_characterization_augment_with_prior(RSSDDCCharacterization *characterization);
/** True after retained prior knowledge has been merged into effective knowledge. */
bool rss_ddc_characterization_prior_augmented(const RSSDDCCharacterization *characterization);
/** True when automatic passive/Quick/Extended stages ran (not COMPLETE skip). */
bool rss_ddc_characterization_discovery_performed(const RSSDDCCharacterization *characterization);

/** Snapshot of exact structured-knowledge completeness after assemble. */
RSSDDCCharacterizationStructuredMatch rss_ddc_characterization_structured_match(
    const RSSDDCCharacterization *characterization);
const char *rss_ddc_characterization_structured_match_name(RSSDDCCharacterizationStructuredMatch match);

typedef enum {
    RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CREATED = 0,
    RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UPDATED,
    RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNCHANGED,
    RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_CONFLICT,
    RSS_DDC_CHARACTERIZATION_PROFILE_UPDATE_UNSUPPORTED
} RSSDDCCharacterizationProfileUpdateStatus;

typedef struct {
    RSSDDCCharacterizationProfileUpdateStatus status;
    char profile_id[RSS_DDC_PROFILE_ID_MAX];
    size_t controls_added;
    size_t controls_preserved;
} RSSDDCCharacterizationProfileUpdateResult;

/**
 * Explicit in-memory profile update from a completed characterization.
 * Never contacts the monitor, never SET/writes, and never mutates `store`
 * during rss_ddc_characterize_display. Persists only hardware-validated
 * PROFILE/production methods the current schema can represent faithfully.
 * Does not save to disk; call rss_ddc_profile_store_save_local_file for a
 * LOCAL overlay, or rss_ddc_profile_store_save_file to export the entire store.
 * Builtin records are not modified; additions are LOCAL overlays.
 * On CONFLICT/UNSUPPORTED the store is unchanged. `*result` is written on
 * every return except RSS_DDC_ERROR_ARGUMENT when `result` is NULL.
 */
RSSDDCError rss_ddc_characterization_update_profile(
    const RSSDDCCharacterization *characterization, RSSDDCProfileStore *store,
    RSSDDCCharacterizationProfileUpdateResult *result);

#ifdef __cplusplus
}
#endif

#endif
