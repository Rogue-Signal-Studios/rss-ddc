#include "profile_store.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum { RSS_DDC_PROFILE_SCHEMA_VERSION = 1 };

typedef struct {
    char id[RSS_DDC_PROFILE_ID_MAX];
    RSSDDCProfileIdentity identity;
    RSSDDCProfileSource source;
    RSSDDCProfileConfidence confidence;
    size_t control_count;
    RSSDDCProfileControl controls[RSS_DDC_PROFILE_MAX_CONTROLS];
} RSSDDCProfileRecord;

struct RSSDDCProfileStore {
    RSSDDCProfilePackInfo info;
    bool has_info;
    size_t profile_count;
    RSSDDCProfileRecord profiles[RSS_DDC_PROFILE_MAX_PROFILES];
};

typedef struct { const char *cursor; const char *end; } JSONCursor;

static void skip_ws(JSONCursor *json) { while (json->cursor < json->end && isspace((unsigned char)*json->cursor)) ++json->cursor; }
static bool consume(JSONCursor *json, char expected) { skip_ws(json); if (json->cursor == json->end || *json->cursor != expected) return false; ++json->cursor; return true; }

static bool skip_string(JSONCursor *json) {
    if (!consume(json, '\"')) return false;
    while (json->cursor < json->end) {
        unsigned char value = (unsigned char)*json->cursor++;
        if (value == '\"') return true;
        if (value < 0x20) return false;
        if (value == '\\') { if (json->cursor == json->end) return false; ++json->cursor; }
    }
    return false;
}

static bool parse_string(JSONCursor *json, char *output, size_t capacity) {
    if (output == NULL || capacity == 0 || !consume(json, '\"')) return false;
    size_t length = 0;
    while (json->cursor < json->end) {
        unsigned char value = (unsigned char)*json->cursor++;
        if (value == '\"') { output[length] = '\0'; return true; }
        if (value < 0x20 || value == '\\' || length + 1 >= capacity) return false;
        output[length++] = (char)value;
    }
    return false;
}

static bool parse_u32(JSONCursor *json, uint32_t *output) {
    skip_ws(json);
    if (json->cursor == json->end || !isdigit((unsigned char)*json->cursor)) return false;
    uint64_t value = 0;
    do { value = value * 10u + (uint32_t)(*json->cursor++ - '0'); if (value > UINT32_MAX) return false; }
    while (json->cursor < json->end && isdigit((unsigned char)*json->cursor));
    if (json->cursor < json->end && (*json->cursor == '.' || *json->cursor == 'e' || *json->cursor == 'E')) return false;
    *output = (uint32_t)value;
    return true;
}

static bool parse_bool(JSONCursor *json, bool *output) {
    skip_ws(json);
    if ((size_t)(json->end - json->cursor) >= 4 && memcmp(json->cursor, "true", 4) == 0) { json->cursor += 4; *output = true; return true; }
    if ((size_t)(json->end - json->cursor) >= 5 && memcmp(json->cursor, "false", 5) == 0) { json->cursor += 5; *output = false; return true; }
    return false;
}

static bool skip_value(JSONCursor *json) {
    skip_ws(json);
    if (json->cursor == json->end) return false;
    if (*json->cursor == '\"') return skip_string(json);
    if (*json->cursor == '{') {
        ++json->cursor; skip_ws(json); if (json->cursor < json->end && *json->cursor == '}') { ++json->cursor; return true; }
        for (;;) { if (!skip_string(json) || !consume(json, ':') || !skip_value(json)) return false; skip_ws(json); if (json->cursor < json->end && *json->cursor == '}') { ++json->cursor; return true; } if (!consume(json, ',')) return false; }
    }
    if (*json->cursor == '[') {
        ++json->cursor; skip_ws(json); if (json->cursor < json->end && *json->cursor == ']') { ++json->cursor; return true; }
        for (;;) { if (!skip_value(json)) return false; skip_ws(json); if (json->cursor < json->end && *json->cursor == ']') { ++json->cursor; return true; } if (!consume(json, ',')) return false; }
    }
    if (*json->cursor == '-' || isdigit((unsigned char)*json->cursor)) { if (*json->cursor == '-') ++json->cursor; while (json->cursor < json->end && (isdigit((unsigned char)*json->cursor) || *json->cursor == '.' || *json->cursor == 'e' || *json->cursor == 'E' || *json->cursor == '+' || *json->cursor == '-')) ++json->cursor; return true; }
    if ((size_t)(json->end - json->cursor) >= 4 && memcmp(json->cursor, "true", 4) == 0) { json->cursor += 4; return true; }
    if ((size_t)(json->end - json->cursor) >= 5 && memcmp(json->cursor, "false", 5) == 0) { json->cursor += 5; return true; }
    if ((size_t)(json->end - json->cursor) >= 4 && memcmp(json->cursor, "null", 4) == 0) { json->cursor += 4; return true; }
    return false;
}

static RSSDDCProvider provider_from_name(const char *name) {
    return rss_ddc_provider_from_registry_class(name);
}

static RSSDDCProfileConfidence confidence_from_name(const char *name) {
    static const char *names[] = {"unknown", "candidate", "observed", "correlated", "set-observed", "hardware-validated"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) if (strcmp(name, names[index]) == 0) return (RSSDDCProfileConfidence)index;
    return RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN;
}

static RSSDDCProfileControlID control_from_name(const char *name) {
    static const char *names[] = {"", "picture-mode", "input", "brightness", "contrast", "color-preset", "response-time", "adaptive-sync", "energy-saving", "black-stabilizer", "gamma", "sharpness", "audio-mute"};
    for (size_t index = 1; index < sizeof(names) / sizeof(names[0]); ++index) if (strcmp(name, names[index]) == 0) return (RSSDDCProfileControlID)index;
    return RSS_DDC_PROFILE_CONTROL_UNKNOWN;
}

static RSSDDCProfileMethod method_from_name(const char *name) {
    if (strcmp(name, "vcp") == 0) return RSS_DDC_PROFILE_METHOD_VCP;
    if (strcmp(name, "lg-alt-input") == 0) return RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT;
    return RSS_DDC_PROFILE_METHOD_UNKNOWN;
}

const char *rss_ddc_profile_control_name(RSSDDCProfileControlID id) {
    static const char *names[] = {"unknown", "picture-mode", "input", "brightness", "contrast", "color-preset", "response-time", "adaptive-sync", "energy-saving", "black-stabilizer", "gamma", "sharpness", "audio-mute"};
    return (size_t)id < sizeof(names) / sizeof(names[0]) ? names[id] : "unknown";
}
const char *rss_ddc_profile_source_name(RSSDDCProfileSource source) {
    static const char *names[] = {"builtin", "validated-pack", "local", "research"};
    return (size_t)source < sizeof(names) / sizeof(names[0]) ? names[source] : "unknown";
}
const char *rss_ddc_profile_confidence_name(RSSDDCProfileConfidence confidence) {
    static const char *names[] = {"unknown", "candidate", "observed", "correlated", "set-observed", "hardware-validated"};
    return (size_t)confidence < sizeof(names) / sizeof(names[0]) ? names[confidence] : "unknown";
}

static RSSDDCError parse_identity(JSONCursor *json, RSSDDCProfileIdentity *identity) {
    bool product = false, provider = false, transport = false;
    if (!consume(json, '{')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    skip_ws(json); if (json->cursor < json->end && *json->cursor == '}') return RSS_DDC_ERROR_PROFILE_MALFORMED;
    for (;;) {
        char key[48] = {};
        if (!parse_string(json, key, sizeof(key)) || !consume(json, ':')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        if (strcmp(key, "manufacturer") == 0) { if (identity->manufacturer[0] || !parse_string(json, identity->manufacturer, sizeof(identity->manufacturer))) return RSS_DDC_ERROR_PROFILE_MALFORMED; }
        else if (strcmp(key, "productName") == 0) { if (product || !parse_string(json, identity->product_name, sizeof(identity->product_name))) return RSS_DDC_ERROR_PROFILE_MALFORMED; product = true; }
        else if (strcmp(key, "serial") == 0) { if (identity->serial[0] || !parse_string(json, identity->serial, sizeof(identity->serial))) return RSS_DDC_ERROR_PROFILE_MALFORMED; }
        else if (strcmp(key, "branchDeviceId") == 0) { if (identity->branch_device_id[0] || !parse_string(json, identity->branch_device_id, sizeof(identity->branch_device_id))) return RSS_DDC_ERROR_PROFILE_MALFORMED; }
        else if (strcmp(key, "provider") == 0) { char value[RSS_DDC_TEXT_MAX] = {}; if (provider || !parse_string(json, value, sizeof(value)) || (identity->provider = provider_from_name(value)) == RSS_DDC_PROVIDER_UNKNOWN) return RSS_DDC_ERROR_PROFILE_MALFORMED; provider = true; }
        else if (strcmp(key, "transport") == 0) { if (transport || !parse_string(json, identity->transport, sizeof(identity->transport))) return RSS_DDC_ERROR_PROFILE_MALFORMED; transport = true; }
        else if (strcmp(key, "external") == 0) { if (!parse_bool(json, &identity->external)) return RSS_DDC_ERROR_PROFILE_MALFORMED; }
        else if (!skip_value(json)) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        skip_ws(json); if (json->cursor < json->end && *json->cursor == '}') { ++json->cursor; break; } if (!consume(json, ',')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    }
    return product && provider && transport && identity->product_name[0] && identity->transport[0] ? RSS_DDC_OK : RSS_DDC_ERROR_PROFILE_MALFORMED;
}

static RSSDDCError parse_enums(JSONCursor *json, RSSDDCProfileControl *control) {
    if (!consume(json, '[')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    skip_ws(json); if (json->cursor < json->end && *json->cursor == ']') { ++json->cursor; return RSS_DDC_OK; }
    for (;;) {
        if (control->enum_value_count == RSS_DDC_PROFILE_MAX_ENUM_VALUES || !consume(json, '{')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        RSSDDCProfileEnumValue value = {};
        bool id = false, name = false, raw = false;
        for (;;) {
            char key[32] = {}; if (!parse_string(json, key, sizeof(key)) || !consume(json, ':')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
            if (strcmp(key, "id") == 0) { if (id || !parse_string(json, value.id, sizeof(value.id))) return RSS_DDC_ERROR_PROFILE_MALFORMED; id = true; }
            else if (strcmp(key, "name") == 0) { if (name || !parse_string(json, value.name, sizeof(value.name))) return RSS_DDC_ERROR_PROFILE_MALFORMED; name = true; }
            else if (strcmp(key, "value") == 0) { uint32_t number = 0; if (raw || !parse_u32(json, &number) || number > UINT16_MAX) return RSS_DDC_ERROR_PROFILE_MALFORMED; value.raw_value = (uint16_t)number; raw = true; }
            else if (!skip_value(json)) return RSS_DDC_ERROR_PROFILE_MALFORMED;
            skip_ws(json); if (json->cursor < json->end && *json->cursor == '}') { ++json->cursor; break; } if (!consume(json, ',')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        }
        if (!id || !name || !raw || !value.id[0]) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        for (size_t index = 0; index < control->enum_value_count; ++index) if (strcmp(control->enum_values[index].id, value.id) == 0 || control->enum_values[index].raw_value == value.raw_value) return RSS_DDC_ERROR_PROFILE_CONFLICT;
        control->enum_values[control->enum_value_count++] = value;
        skip_ws(json); if (json->cursor < json->end && *json->cursor == ']') { ++json->cursor; return RSS_DDC_OK; } if (!consume(json, ',')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    }
}

static RSSDDCError validate_control(const RSSDDCProfileControl *control, RSSDDCProfileSource source) {
    if (control->id == RSS_DDC_PROFILE_CONTROL_UNKNOWN || control->method == RSS_DDC_PROFILE_METHOD_UNKNOWN || control->address == 0 || control->confidence == RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    if (control->method == RSS_DDC_PROFILE_METHOD_VCP && control->address > UINT8_MAX) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    if (control->method == RSS_DDC_PROFILE_METHOD_LG_ALT_INPUT && control->id != RSS_DDC_PROFILE_CONTROL_INPUT) return RSS_DDC_ERROR_PROFILE_UNSAFE;
    if (control->id == RSS_DDC_PROFILE_CONTROL_INPUT && control->method == RSS_DDC_PROFILE_METHOD_VCP && control->address != 0x60) return RSS_DDC_ERROR_PROFILE_UNSAFE;
    if (control->has_numeric_range && control->minimum_value > control->maximum_value) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    if (control->id == RSS_DDC_PROFILE_CONTROL_PICTURE_MODE && control->enum_value_count == 0) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    if (control->writable && (source == RSS_DDC_PROFILE_SOURCE_RESEARCH || control->confidence != RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED)) return RSS_DDC_ERROR_PROFILE_UNSAFE;
    return RSS_DDC_OK;
}

static RSSDDCError parse_control(JSONCursor *json, RSSDDCProfileControl *control, RSSDDCProfileSource source) {
    bool id = false, method = false, address = false, readable = false, writable = false, confidence = false, enums = false;
    if (!consume(json, '{')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    for (;;) {
        char key[32] = {}; if (!parse_string(json, key, sizeof(key)) || !consume(json, ':')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        if (strcmp(key, "id") == 0) { char value[64] = {}; if (id || !parse_string(json, value, sizeof(value)) || (control->id = control_from_name(value)) == RSS_DDC_PROFILE_CONTROL_UNKNOWN) return RSS_DDC_ERROR_PROFILE_MALFORMED; id = true; }
        else if (strcmp(key, "method") == 0) { char value[64] = {}; if (method || !parse_string(json, value, sizeof(value)) || (control->method = method_from_name(value)) == RSS_DDC_PROFILE_METHOD_UNKNOWN) return RSS_DDC_ERROR_PROFILE_MALFORMED; method = true; }
        else if (strcmp(key, "address") == 0) { uint32_t value = 0; if (address || !parse_u32(json, &value) || value > UINT16_MAX) return RSS_DDC_ERROR_PROFILE_MALFORMED; control->address = (uint16_t)value; address = true; }
        else if (strcmp(key, "readable") == 0) { if (readable || !parse_bool(json, &control->readable)) return RSS_DDC_ERROR_PROFILE_MALFORMED; readable = true; }
        else if (strcmp(key, "writable") == 0) { if (writable || !parse_bool(json, &control->writable)) return RSS_DDC_ERROR_PROFILE_MALFORMED; writable = true; }
        else if (strcmp(key, "confidence") == 0) { char value[64] = {}; if (confidence || !parse_string(json, value, sizeof(value)) || (control->confidence = confidence_from_name(value)) == RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN) return RSS_DDC_ERROR_PROFILE_MALFORMED; confidence = true; }
        else if (strcmp(key, "minimum") == 0) { uint32_t value = 0; if (control->has_numeric_range || !parse_u32(json, &value) || value > UINT16_MAX) return RSS_DDC_ERROR_PROFILE_MALFORMED; control->minimum_value = (uint16_t)value; control->has_numeric_range = true; }
        else if (strcmp(key, "maximum") == 0) { uint32_t value = 0; if (!control->has_numeric_range || !parse_u32(json, &value) || value > UINT16_MAX) return RSS_DDC_ERROR_PROFILE_MALFORMED; control->maximum_value = (uint16_t)value; }
        else if (strcmp(key, "enums") == 0) { if (enums || (parse_enums(json, control) != RSS_DDC_OK)) return RSS_DDC_ERROR_PROFILE_MALFORMED; enums = true; }
        else if (!skip_value(json)) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        skip_ws(json); if (json->cursor < json->end && *json->cursor == '}') { ++json->cursor; break; } if (!consume(json, ',')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    }
    if (!id || !method || !address || !readable || !writable || !confidence || !enums) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    return validate_control(control, source);
}

static bool identity_equal(const RSSDDCProfileIdentity *first, const RSSDDCProfileIdentity *second) {
    return first->provider == second->provider && first->external == second->external && strcmp(first->manufacturer, second->manufacturer) == 0 && strcmp(first->product_name, second->product_name) == 0 && strcmp(first->serial, second->serial) == 0 && strcmp(first->branch_device_id, second->branch_device_id) == 0 && strcmp(first->transport, second->transport) == 0;
}

static RSSDDCError parse_profile(JSONCursor *json, RSSDDCProfileRecord *profile, RSSDDCProfileSource source) {
    bool id = false, identity = false, controls = false, confidence = false;
    profile->source = source;
    if (!consume(json, '{')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    for (;;) {
        char key[32] = {}; if (!parse_string(json, key, sizeof(key)) || !consume(json, ':')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        if (strcmp(key, "id") == 0) { if (id || !parse_string(json, profile->id, sizeof(profile->id))) return RSS_DDC_ERROR_PROFILE_MALFORMED; id = true; }
        else if (strcmp(key, "identity") == 0) { if (identity || (parse_identity(json, &profile->identity) != RSS_DDC_OK)) return RSS_DDC_ERROR_PROFILE_MALFORMED; identity = true; }
        else if (strcmp(key, "confidence") == 0) { char value[64] = {}; if (confidence || !parse_string(json, value, sizeof(value)) || (profile->confidence = confidence_from_name(value)) == RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN) return RSS_DDC_ERROR_PROFILE_MALFORMED; confidence = true; }
        else if (strcmp(key, "controls") == 0) {
            if (controls || !consume(json, '[')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
            skip_ws(json); if (json->cursor < json->end && *json->cursor == ']') return RSS_DDC_ERROR_PROFILE_MALFORMED;
            for (;;) { if (profile->control_count == RSS_DDC_PROFILE_MAX_CONTROLS) return RSS_DDC_ERROR_PROFILE_MALFORMED; RSSDDCError error = parse_control(json, &profile->controls[profile->control_count], source); if (error != RSS_DDC_OK) return error; ++profile->control_count; skip_ws(json); if (json->cursor < json->end && *json->cursor == ']') { ++json->cursor; break; } if (!consume(json, ',')) return RSS_DDC_ERROR_PROFILE_MALFORMED; }
            controls = true;
        } else if (!skip_value(json)) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        skip_ws(json); if (json->cursor < json->end && *json->cursor == '}') { ++json->cursor; break; } if (!consume(json, ',')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    }
    if (!id || !identity || !controls || !confidence || !profile->id[0]) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    for (size_t i = 0; i < profile->control_count; ++i) {
        if ((unsigned int)profile->controls[i].confidence > (unsigned int)profile->confidence) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        if (profile->controls[i].writable && profile->confidence != RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED) return RSS_DDC_ERROR_PROFILE_UNSAFE;
        for (size_t j = i + 1; j < profile->control_count; ++j)
            if (profile->controls[i].id == profile->controls[j].id) return RSS_DDC_ERROR_PROFILE_CONFLICT;
    }
    return RSS_DDC_OK;
}

static bool version_supported(const char *minimum) {
    unsigned int major = 0, minor = 0, patch = 0; char extra = '\0';
    if (sscanf(minimum, "%u.%u.%u%c", &major, &minor, &patch, &extra) != 3) return false;
    if (major != RSS_DDC_VERSION_MAJOR) return major < RSS_DDC_VERSION_MAJOR;
    if (minor != RSS_DDC_VERSION_MINOR) return minor < RSS_DDC_VERSION_MINOR;
    return patch <= RSS_DDC_VERSION_PATCH;
}

static RSSDDCError parse_pack(const char *data, size_t length, RSSDDCProfileSource source, RSSDDCProfileStore *parsed) {
    if (data == NULL || length == 0 || length > 65536 || parsed == NULL || (source != RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK && source != RSS_DDC_PROFILE_SOURCE_LOCAL && source != RSS_DDC_PROFILE_SOURCE_RESEARCH)) return RSS_DDC_ERROR_ARGUMENT;
    JSONCursor json = {.cursor = data, .end = data + length};
    bool schema = false, database = false, minimum = false, profiles = false;
    if (!consume(&json, '{')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    for (;;) {
        char key[48] = {}; if (!parse_string(&json, key, sizeof(key)) || !consume(&json, ':')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        if (strcmp(key, "schemaVersion") == 0) { uint32_t value = 0; if (schema || !parse_u32(&json, &value)) return RSS_DDC_ERROR_PROFILE_MALFORMED; parsed->info.schema_version = value; schema = true; }
        else if (strcmp(key, "databaseVersion") == 0) { if (database || !parse_string(&json, parsed->info.database_version, sizeof(parsed->info.database_version))) return RSS_DDC_ERROR_PROFILE_MALFORMED; database = true; }
        else if (strcmp(key, "minimumRSSDDCVersion") == 0) { if (minimum || !parse_string(&json, parsed->info.minimum_rss_ddc_version, sizeof(parsed->info.minimum_rss_ddc_version))) return RSS_DDC_ERROR_PROFILE_MALFORMED; minimum = true; }
        else if (strcmp(key, "packId") == 0) { if (parsed->info.pack_id[0] || !parse_string(&json, parsed->info.pack_id, sizeof(parsed->info.pack_id))) return RSS_DDC_ERROR_PROFILE_MALFORMED; }
        else if (strcmp(key, "profiles") == 0) {
            if (profiles || !consume(&json, '[')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
            skip_ws(&json);
            if (json.cursor < json.end && *json.cursor == ']') ++json.cursor;
            else for (;;) { if (parsed->profile_count == RSS_DDC_PROFILE_MAX_PROFILES) return RSS_DDC_ERROR_PROFILE_MALFORMED; RSSDDCError error = parse_profile(&json, &parsed->profiles[parsed->profile_count], source); if (error != RSS_DDC_OK) return error; for (size_t index = 0; index < parsed->profile_count; ++index) if (identity_equal(&parsed->profiles[index].identity, &parsed->profiles[parsed->profile_count].identity)) return RSS_DDC_ERROR_PROFILE_CONFLICT; ++parsed->profile_count; skip_ws(&json); if (json.cursor < json.end && *json.cursor == ']') { ++json.cursor; break; } if (!consume(&json, ',')) return RSS_DDC_ERROR_PROFILE_MALFORMED; }
            profiles = true;
        } else if (!skip_value(&json)) return RSS_DDC_ERROR_PROFILE_MALFORMED;
        skip_ws(&json); if (json.cursor < json.end && *json.cursor == '}') { ++json.cursor; break; } if (!consume(&json, ',')) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    }
    skip_ws(&json); if (json.cursor != json.end) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    if (!schema || !database || !minimum || !profiles || !parsed->info.database_version[0] || !parsed->info.minimum_rss_ddc_version[0]) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    if (parsed->info.schema_version != RSS_DDC_PROFILE_SCHEMA_VERSION) return RSS_DDC_ERROR_PROFILE_SCHEMA;
    if (!version_supported(parsed->info.minimum_rss_ddc_version)) return RSS_DDC_ERROR_PROFILE_VERSION;
    parsed->has_info = true;
    return RSS_DDC_OK;
}

RSSDDCProfileStore *rss_ddc_profile_store_create(void) { return calloc(1, sizeof(RSSDDCProfileStore)); }
void rss_ddc_profile_store_destroy(RSSDDCProfileStore *store) { free(store); }

static RSSDDCError append_parsed(RSSDDCProfileStore *store, const RSSDDCProfileStore *parsed) {
    if (store == NULL || parsed == NULL || store->profile_count + parsed->profile_count > RSS_DDC_PROFILE_MAX_PROFILES) return RSS_DDC_ERROR_PROFILE_CONFLICT;
    RSSDDCProfileStore *replacement = rss_ddc_profile_store_create();
    if (replacement == NULL) return RSS_DDC_ERROR_SYSTEM;
    memcpy(replacement, store, sizeof(*replacement));
    for (size_t index = 0; index < parsed->profile_count; ++index) replacement->profiles[replacement->profile_count++] = parsed->profiles[index];
    replacement->info = parsed->info; replacement->has_info = true;
    memcpy(store, replacement, sizeof(*store));
    rss_ddc_profile_store_destroy(replacement);
    return RSS_DDC_OK;
}

static RSSDDCError load_data(RSSDDCProfileStore *store, const char *data, size_t length, RSSDDCProfileSource source) {
    if (store == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSDDCProfileStore *parsed = rss_ddc_profile_store_create();
    if (parsed == NULL) return RSS_DDC_ERROR_SYSTEM;
    RSSDDCError error = parse_pack(data, length, source, parsed);
    if (error == RSS_DDC_OK) error = append_parsed(store, parsed);
    rss_ddc_profile_store_destroy(parsed);
    return error;
}
RSSDDCError rss_ddc_profile_store_load_pack_data(RSSDDCProfileStore *store, const char *data, size_t length) { return load_data(store, data, length, RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK); }
RSSDDCError rss_ddc_profile_store_load_local_data(RSSDDCProfileStore *store, const char *data, size_t length) { return load_data(store, data, length, RSS_DDC_PROFILE_SOURCE_LOCAL); }
RSSDDCError rss_ddc_profile_store_load_research_data(RSSDDCProfileStore *store, const char *data, size_t length) { return load_data(store, data, length, RSS_DDC_PROFILE_SOURCE_RESEARCH); }
RSSDDCError rss_ddc_profile_validate_pack_data(const char *data, size_t length, RSSDDCProfileSource source, RSSDDCProfilePackInfo *info) {
    RSSDDCProfileStore *parsed = rss_ddc_profile_store_create();
    if (parsed == NULL) return RSS_DDC_ERROR_SYSTEM;
    RSSDDCError error = parse_pack(data, length, source, parsed);
    if (error == RSS_DDC_OK && info != NULL) *info = parsed->info;
    rss_ddc_profile_store_destroy(parsed);
    return error;
}

static RSSDDCError load_file(RSSDDCProfileStore *store, const char *path, RSSDDCProfileSource source) {
    if (store == NULL || path == NULL) return RSS_DDC_ERROR_ARGUMENT;
    FILE *file = fopen(path, "rb"); if (file == NULL) return RSS_DDC_ERROR_READ;
    char data[65537] = {}; size_t length = fread(data, 1, sizeof(data) - 1, file); int failed = ferror(file); int extra = fgetc(file); fclose(file);
    if (failed || extra != EOF) return RSS_DDC_ERROR_PROFILE_MALFORMED;
    return load_data(store, data, length, source);
}
RSSDDCError rss_ddc_profile_store_load_pack_file(RSSDDCProfileStore *store, const char *path) { return load_file(store, path, RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK); }
RSSDDCError rss_ddc_profile_store_load_local_file(RSSDDCProfileStore *store, const char *path) { return load_file(store, path, RSS_DDC_PROFILE_SOURCE_LOCAL); }
RSSDDCError rss_ddc_profile_store_load_research_file(RSSDDCProfileStore *store, const char *path) { return load_file(store, path, RSS_DDC_PROFILE_SOURCE_RESEARCH); }
RSSDDCError rss_ddc_profile_store_pack_info(const RSSDDCProfileStore *store, RSSDDCProfilePackInfo *info) { if (store == NULL || info == NULL) return RSS_DDC_ERROR_ARGUMENT; if (!store->has_info) return RSS_DDC_ERROR_NOT_FOUND; *info = store->info; return RSS_DDC_OK; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
static void append_json(char *buffer, size_t capacity, size_t *length, const char *format, ...) {
    va_list arguments; va_start(arguments, format);
    int written = vsnprintf(buffer == NULL || *length >= capacity ? NULL : buffer + *length,
                            buffer == NULL || *length >= capacity ? 0 : capacity - *length, format, arguments);
    va_end(arguments);
    if (written > 0) *length += (size_t)written;
}
#pragma clang diagnostic pop

RSSDDCError rss_ddc_profile_store_export_json(const RSSDDCProfileStore *store, char *buffer, size_t capacity, size_t *required) {
    if (store == NULL || required == NULL) return RSS_DDC_ERROR_ARGUMENT;
    size_t length = 0;
#define EMIT(...) append_json(buffer, capacity, &length, __VA_ARGS__)
    EMIT("{\"schemaVersion\":1,\"databaseVersion\":\"%s\",\"minimumRSSDDCVersion\":\"%s\",\"packId\":\"%s\",\"profiles\":[", store->has_info ? store->info.database_version : "local-export", store->has_info ? store->info.minimum_rss_ddc_version : "0.4.0", store->has_info ? store->info.pack_id : "local-export");
    for (size_t profile_index = 0; profile_index < store->profile_count; ++profile_index) {
        const RSSDDCProfileRecord *profile = &store->profiles[profile_index];
        EMIT("%s{\"id\":\"%s\",\"identity\":{\"productName\":\"%s\",\"provider\":\"%s\",\"transport\":\"%s\",\"external\":%s", profile_index ? "," : "", profile->id, profile->identity.product_name, rss_ddc_provider_string(profile->identity.provider), profile->identity.transport, profile->identity.external ? "true" : "false");
        if (profile->identity.manufacturer[0]) EMIT(",\"manufacturer\":\"%s\"", profile->identity.manufacturer);
        if (profile->identity.serial[0]) EMIT(",\"serial\":\"%s\"", profile->identity.serial);
        if (profile->identity.branch_device_id[0]) EMIT(",\"branchDeviceId\":\"%s\"", profile->identity.branch_device_id);
        EMIT("},\"confidence\":\"%s\",\"controls\":[", rss_ddc_profile_confidence_name(profile->confidence));
        for (size_t control_index = 0; control_index < profile->control_count; ++control_index) {
            const RSSDDCProfileControl *control = &profile->controls[control_index];
            const char *method = control->method == RSS_DDC_PROFILE_METHOD_VCP ? "vcp" : "lg-alt-input";
            EMIT("%s{\"id\":\"%s\",\"method\":\"%s\",\"address\":%u,\"readable\":%s,\"writable\":%s,\"confidence\":\"%s\"", control_index ? "," : "", rss_ddc_profile_control_name(control->id), method, control->address, control->readable ? "true" : "false", control->writable ? "true" : "false", rss_ddc_profile_confidence_name(control->confidence));
            if (control->has_numeric_range) EMIT(",\"minimum\":%u,\"maximum\":%u", control->minimum_value, control->maximum_value);
            EMIT(",\"enums\":[");
            for (size_t enum_index = 0; enum_index < control->enum_value_count; ++enum_index) { const RSSDDCProfileEnumValue *value = &control->enum_values[enum_index]; EMIT("%s{\"id\":\"%s\",\"name\":\"%s\",\"value\":%u}", enum_index ? "," : "", value->id, value->name, value->raw_value); }
            EMIT("]}");
        }
        EMIT("]}");
    }
    EMIT("]}");
#undef EMIT
    *required = length + 1;
    if (buffer == NULL && capacity == 0) return RSS_DDC_OK;
    if (buffer == NULL || capacity < length + 1) return RSS_DDC_ERROR_ARGUMENT;
    buffer[length] = '\0';
    return RSS_DDC_OK;
}

static bool identity_matches(const RSSDDCProfileIdentity *profile, const RSSDDCProfileIdentity *actual, unsigned int *specificity) {
    if (profile->provider != actual->provider || profile->external != actual->external || strcmp(profile->product_name, actual->product_name) != 0 || strcmp(profile->transport, actual->transport) != 0) return false;
    unsigned int score = 4;
#define OPTIONAL_MATCH(field) if (profile->field[0]) { if (strcmp(profile->field, actual->field) != 0) return false; ++score; }
    OPTIONAL_MATCH(manufacturer) OPTIONAL_MATCH(serial) OPTIONAL_MATCH(branch_device_id)
#undef OPTIONAL_MATCH
    *specificity = score; return true;
}

static unsigned int source_rank(RSSDDCProfileSource source) { static const unsigned int ranks[] = {2, 3, 4, 1}; return (size_t)source < sizeof(ranks) / sizeof(ranks[0]) ? ranks[source] : 0; }
static bool same_control(const RSSDDCProfileControl *first, const RSSDDCProfileControl *second) { return first->method == second->method && first->address == second->address && first->readable == second->readable && first->writable == second->writable && first->enum_value_count == second->enum_value_count; }

RSSDDCError rss_ddc_profile_store_resolve(const RSSDDCProfileStore *store, const RSSDDCProfileIdentity *identity, RSSDDCEffectiveProfile *effective) {
    if (store == NULL || identity == NULL || effective == NULL) return RSS_DDC_ERROR_ARGUMENT;
    *effective = (RSSDDCEffectiveProfile){.identity = *identity};
    unsigned int confidence[RSS_DDC_PROFILE_MAX_CONTROLS] = {}, source[RSS_DDC_PROFILE_MAX_CONTROLS] = {}, specificity[RSS_DDC_PROFILE_MAX_CONTROLS] = {};
    for (size_t profile_index = 0; profile_index < store->profile_count; ++profile_index) {
        const RSSDDCProfileRecord *profile = &store->profiles[profile_index]; unsigned int score = 0;
        if (!identity_matches(&profile->identity, identity, &score)) continue;
        for (size_t control_index = 0; control_index < profile->control_count; ++control_index) {
            RSSDDCProfileControl candidate = profile->controls[control_index]; candidate.source = profile->source;
            candidate.write_authorized = candidate.writable && candidate.confidence == RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED && candidate.source != RSS_DDC_PROFILE_SOURCE_RESEARCH;
            size_t target = effective->control_count;
            for (size_t index = 0; index < effective->control_count; ++index) if (effective->controls[index].id == candidate.id) { target = index; break; }
            if (target == effective->control_count) { if (target == RSS_DDC_PROFILE_MAX_CONTROLS) return RSS_DDC_ERROR_PROFILE_CONFLICT; effective->controls[target] = candidate; confidence[target] = candidate.confidence; source[target] = source_rank(candidate.source); specificity[target] = score; ++effective->control_count; continue; }
            if ((unsigned int)candidate.confidence > confidence[target] || ((unsigned int)candidate.confidence == confidence[target] && source_rank(candidate.source) > source[target]) || ((unsigned int)candidate.confidence == confidence[target] && source_rank(candidate.source) == source[target] && score > specificity[target])) { effective->controls[target] = candidate; confidence[target] = candidate.confidence; source[target] = source_rank(candidate.source); specificity[target] = score; }
            else if ((unsigned int)candidate.confidence == confidence[target] && source_rank(candidate.source) == source[target] && score == specificity[target] && !same_control(&candidate, &effective->controls[target])) return RSS_DDC_ERROR_PROFILE_CONFLICT;
        }
    }
    return effective->control_count == 0 ? RSS_DDC_ERROR_NOT_FOUND : RSS_DDC_OK;
}

size_t rss_ddc_effective_profile_control_count(const RSSDDCEffectiveProfile *effective) { return effective == NULL ? 0 : effective->control_count; }
RSSDDCError rss_ddc_effective_profile_control(const RSSDDCEffectiveProfile *effective, size_t index, RSSDDCProfileControl *control) { if (effective == NULL || control == NULL) return RSS_DDC_ERROR_ARGUMENT; if (index >= effective->control_count) return RSS_DDC_ERROR_NOT_FOUND; *control = effective->controls[index]; return RSS_DDC_OK; }
RSSDDCError rss_ddc_profile_control_enum_value(const RSSDDCProfileControl *control, size_t index, RSSDDCProfileEnumValue *value) { if (control == NULL || value == NULL) return RSS_DDC_ERROR_ARGUMENT; if (index >= control->enum_value_count) return RSS_DDC_ERROR_NOT_FOUND; *value = control->enum_values[index]; return RSS_DDC_OK; }

void rss_ddc_profile_identity_from_display(const RSSDDCDisplay *display, RSSDDCProfileIdentity *identity) {
    *identity = (RSSDDCProfileIdentity){}; if (display == NULL) return;
    snprintf(identity->manufacturer, sizeof(identity->manufacturer), "%s", display->manufacturer); snprintf(identity->product_name, sizeof(identity->product_name), "%s", display->product_name); snprintf(identity->serial, sizeof(identity->serial), "%s", display->serial); snprintf(identity->branch_device_id, sizeof(identity->branch_device_id), "%s", display->branch_device_id); snprintf(identity->transport, sizeof(identity->transport), "%s", display->transport); identity->provider = display->provider; identity->external = display->external;
}

static const char builtin_lg_pack[] =
"{\"schemaVersion\":1,\"databaseVersion\":\"2026.08.13.1\",\"minimumRSSDDCVersion\":\"0.3.0\",\"packId\":\"rogue-builtin\",\"profiles\":[{\"id\":\"lg-hdr-qhd-dcpdp13-dcpext0\",\"identity\":{\"productName\":\"LG HDR QHD\",\"provider\":\"DCPDP13Service\",\"transport\":\"DCPEXT0\",\"external\":true},\"confidence\":\"hardware-validated\",\"controls\":[{\"id\":\"picture-mode\",\"method\":\"vcp\",\"address\":21,\"readable\":true,\"writable\":true,\"confidence\":\"hardware-validated\",\"enums\":[{\"id\":\"custom\",\"name\":\"Custom\",\"value\":11},{\"id\":\"vivid\",\"name\":\"Vivid\",\"value\":49},{\"id\":\"hdr-effect\",\"name\":\"HDR Effect\",\"value\":39},{\"id\":\"cinema\",\"name\":\"Cinema\",\"value\":48},{\"id\":\"fps\",\"name\":\"FPS\",\"value\":30},{\"id\":\"rts\",\"name\":\"RTS\",\"value\":31},{\"id\":\"color-weakness\",\"name\":\"Color Weakness\",\"value\":6},{\"id\":\"reader\",\"name\":\"Reader\",\"value\":1}]}]}]}";

RSSDDCError rss_ddc_profile_store_load_builtin(RSSDDCProfileStore *store) {
    if (store == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSDDCProfileStore *parsed = rss_ddc_profile_store_create();
    if (parsed == NULL) return RSS_DDC_ERROR_SYSTEM;
    RSSDDCError error = parse_pack(builtin_lg_pack, sizeof(builtin_lg_pack) - 1, RSS_DDC_PROFILE_SOURCE_VALIDATED_PACK, parsed);
    if (error == RSS_DDC_OK) {
        for (size_t index = 0; index < parsed->profile_count; ++index) parsed->profiles[index].source = RSS_DDC_PROFILE_SOURCE_BUILTIN;
        error = append_parsed(store, parsed);
    }
    rss_ddc_profile_store_destroy(parsed);
    return error;
}

RSSDDCError rss_ddc_profile_store_resolve_builtin(const RSSDDCProfileIdentity *identity, RSSDDCEffectiveProfile *effective) {
    if (identity == NULL || effective == NULL) return RSS_DDC_ERROR_ARGUMENT;
    RSSDDCProfileStore *store = rss_ddc_profile_store_create();
    if (store == NULL) return RSS_DDC_ERROR_SYSTEM;
    RSSDDCError error = rss_ddc_profile_store_load_builtin(store);
    if (error == RSS_DDC_OK) error = rss_ddc_profile_store_resolve(store, identity, effective);
    rss_ddc_profile_store_destroy(store);
    return error;
}

static const char *picture_mode_id(RSSDDCPictureMode mode) { static const char *ids[] = {NULL, "custom", "vivid", "hdr-effect", "cinema", "fps", "rts", "color-weakness", "reader"}; return (size_t)mode < sizeof(ids) / sizeof(ids[0]) ? ids[mode] : NULL; }
static const RSSDDCProfileControl *picture_control(const RSSDDCEffectiveProfile *effective) { if (effective == NULL) return NULL; for (size_t index = 0; index < effective->control_count; ++index) if (effective->controls[index].id == RSS_DDC_PROFILE_CONTROL_PICTURE_MODE) return &effective->controls[index]; return NULL; }
RSSDDCError rss_ddc_profile_picture_mode_raw(const RSSDDCEffectiveProfile *effective, RSSDDCPictureMode mode, uint16_t *raw_value) { const RSSDDCProfileControl *control = picture_control(effective); const char *id = picture_mode_id(mode); if (raw_value == NULL || id == NULL) return RSS_DDC_ERROR_ARGUMENT; if (control == NULL || !control->write_authorized || control->method != RSS_DDC_PROFILE_METHOD_VCP) return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY; for (size_t index = 0; index < control->enum_value_count; ++index) if (strcmp(control->enum_values[index].id, id) == 0) { *raw_value = control->enum_values[index].raw_value; return RSS_DDC_OK; } return RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY; }
RSSDDCPictureMode rss_ddc_profile_picture_mode_from_raw(const RSSDDCEffectiveProfile *effective, uint16_t raw_value) { const RSSDDCProfileControl *control = picture_control(effective); if (control == NULL) return RSS_DDC_PICTURE_MODE_UNKNOWN; for (size_t index = 0; index < control->enum_value_count; ++index) if (control->enum_values[index].raw_value == raw_value) for (RSSDDCPictureMode mode = RSS_DDC_PICTURE_MODE_CUSTOM; mode <= RSS_DDC_PICTURE_MODE_READER; ++mode) if (strcmp(control->enum_values[index].id, picture_mode_id(mode)) == 0) return mode; return RSS_DDC_PICTURE_MODE_UNKNOWN; }
