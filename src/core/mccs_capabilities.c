#include "rss_ddc.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *bytes;
    size_t length;
} RSSMCCSRange;

static bool is_space(char value) { return isspace((unsigned char)value) != 0; }
static bool is_identifier_start(char value) { return isalpha((unsigned char)value) != 0 || value == '_'; }
static bool is_identifier_char(char value) {
    return isalnum((unsigned char)value) != 0 || value == '_' || value == '-';
}

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static RSSDDCError validate_balanced(const char *raw, size_t length) {
    size_t depth = 0;
    for (size_t index = 0; index < length; ++index) {
        if (raw[index] == '\0') return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
        if (raw[index] == '(') {
            if (++depth > RSS_DDC_MCCS_CAPABILITIES_MAX_NESTING) return RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE;
        } else if (raw[index] == ')') {
            if (depth == 0) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
            --depth;
        }
    }
    return depth == 0 ? RSS_DDC_OK : RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
}

static RSSDDCError matching_parenthesis(RSSMCCSRange range, size_t open_index, size_t *close_index) {
    if (close_index == NULL || open_index >= range.length || range.bytes[open_index] != '(') {
        return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    }
    size_t depth = 0;
    for (size_t index = open_index; index < range.length; ++index) {
        if (range.bytes[index] == '(') ++depth;
        else if (range.bytes[index] == ')') {
            if (depth == 0) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
            if (--depth == 0) {
                *close_index = index;
                return RSS_DDC_OK;
            }
        }
    }
    return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
}

static bool identifier_is_vcp(const char *bytes, size_t length) {
    return length == 3 && (bytes[0] == 'v' || bytes[0] == 'V') &&
        (bytes[1] == 'c' || bytes[1] == 'C') && (bytes[2] == 'p' || bytes[2] == 'P');
}

static RSSDDCError append_feature(RSSDDCMCCSCapabilities *capabilities, uint8_t code,
                                   size_t enum_offset, size_t enum_count) {
    for (size_t index = 0; index < capabilities->feature_count; ++index) {
        if (capabilities->features[index].vcp_code == code) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    }
    if (capabilities->feature_count == RSS_DDC_MCCS_CAPABILITIES_MAX_FEATURES) {
        return RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE;
    }
    capabilities->features[capabilities->feature_count++] = (RSSDDCMCCSVcpCapability){
        .vcp_code = code, .enum_value_offset = enum_offset, .enum_value_count = enum_count};
    return RSS_DDC_OK;
}

static RSSDDCError parse_exact_hex_byte(RSSMCCSRange range, size_t *index, uint8_t *value) {
    if (index == NULL || value == NULL || *index >= range.length) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    size_t start = *index;
    while (*index < range.length && isxdigit((unsigned char)range.bytes[*index])) ++*index;
    if (*index - start != 2) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    int high = hex_value(range.bytes[start]);
    int low = hex_value(range.bytes[start + 1]);
    if (high < 0 || low < 0) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    *value = (uint8_t)((high << 4) | low);
    return RSS_DDC_OK;
}

static RSSDDCError parse_enum_values(RSSMCCSRange range, RSSDDCMCCSCapabilities *capabilities,
                                      size_t *offset, size_t *count) {
    bool saw_value = false;
    *offset = capabilities->enum_value_count;
    *count = 0;
    for (size_t index = 0; index < range.length;) {
        while (index < range.length && is_space(range.bytes[index])) ++index;
        if (index == range.length) break;
        if (!isxdigit((unsigned char)range.bytes[index])) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
        uint8_t value = 0;
        RSSDDCError error = parse_exact_hex_byte(range, &index, &value);
        if (error != RSS_DDC_OK) return error;
        if (capabilities->enum_value_count == RSS_DDC_MCCS_CAPABILITIES_MAX_ENUM_VALUES) {
            return RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE;
        }
        capabilities->enum_values[capabilities->enum_value_count++] = value;
        ++*count;
        saw_value = true;
        if (index < range.length && !is_space(range.bytes[index])) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    }
    return saw_value ? RSS_DDC_OK : RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
}

static RSSDDCError parse_vcp_section(RSSMCCSRange range, RSSDDCMCCSCapabilities *capabilities) {
    for (size_t index = 0; index < range.length;) {
        while (index < range.length && is_space(range.bytes[index])) ++index;
        if (index == range.length) break;
        if (!isxdigit((unsigned char)range.bytes[index])) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
        uint8_t feature = 0;
        RSSDDCError error = parse_exact_hex_byte(range, &index, &feature);
        if (error != RSS_DDC_OK) return error;

        size_t enum_offset = capabilities->enum_value_count;
        size_t enum_count = 0;
        if (index < range.length && range.bytes[index] == '(') {
            size_t close = 0;
            error = matching_parenthesis(range, index, &close);
            if (error != RSS_DDC_OK) return error;
            RSSMCCSRange values = {.bytes = range.bytes + index + 1, .length = close - index - 1};
            error = parse_enum_values(values, capabilities, &enum_offset, &enum_count);
            if (error != RSS_DDC_OK) return error;
            index = close + 1;
        }
        error = append_feature(capabilities, feature, enum_offset, enum_count);
        if (error != RSS_DDC_OK) return error;
        if (index < range.length && !is_space(range.bytes[index])) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    }
    return RSS_DDC_OK;
}

static RSSDDCError parse_top_level_sections(RSSMCCSRange range, RSSDDCMCCSCapabilities *capabilities) {
    for (size_t index = 0; index < range.length;) {
        while (index < range.length && is_space(range.bytes[index])) ++index;
        if (index == range.length) break;
        if (!is_identifier_start(range.bytes[index])) return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
        size_t name_start = index++;
        while (index < range.length && is_identifier_char(range.bytes[index])) ++index;
        size_t name_length = index - name_start;
        while (index < range.length && is_space(range.bytes[index])) ++index;
        if (index == range.length || range.bytes[index] != '(') return RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
        size_t close = 0;
        RSSDDCError error = matching_parenthesis(range, index, &close);
        if (error != RSS_DDC_OK) return error;
        if (identifier_is_vcp(range.bytes + name_start, name_length)) {
            RSSMCCSRange vcp = {.bytes = range.bytes + index + 1, .length = close - index - 1};
            error = parse_vcp_section(vcp, capabilities);
            if (error != RSS_DDC_OK) return error;
        }
        index = close + 1;
    }
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_parse_mccs_capabilities(const char *raw, size_t raw_length,
                                            RSSDDCMCCSCapabilities *capabilities) {
    if (raw == NULL || capabilities == NULL || raw_length == 0) return RSS_DDC_ERROR_ARGUMENT;
    if (raw_length > RSS_DDC_MCCS_CAPABILITIES_MAX_BYTES) return RSS_DDC_ERROR_CAPABILITIES_TOO_LARGE;
    RSSDDCError error = validate_balanced(raw, raw_length);
    if (error != RSS_DDC_OK) return error;

    RSSDDCMCCSCapabilities *parsed = calloc(1, sizeof(*parsed));
    if (parsed == NULL) return RSS_DDC_ERROR_SYSTEM;
    memcpy(parsed->raw, raw, raw_length);
    parsed->raw_length = raw_length;
    size_t start = 0;
    size_t end = raw_length;
    while (start < end && is_space(raw[start])) ++start;
    while (end > start && is_space(raw[end - 1])) --end;
    if (start == end) error = RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
    if (error == RSS_DDC_OK && raw[start] == '(') {
        size_t close = 0;
        RSSMCCSRange whole = {.bytes = raw, .length = end};
        error = matching_parenthesis(whole, start, &close);
        if (error == RSS_DDC_OK && close != end - 1) error = RSS_DDC_ERROR_CAPABILITIES_MALFORMED;
        if (error == RSS_DDC_OK) {
            ++start;
            --end;
        }
    }
    if (error == RSS_DDC_OK) {
        RSSMCCSRange top_level = {.bytes = raw + start, .length = end - start};
        error = parse_top_level_sections(top_level, parsed);
    }
    if (error == RSS_DDC_OK) *capabilities = *parsed;
    free(parsed);
    return error;
}

bool rss_ddc_mccs_capabilities_has_vcp(const RSSDDCMCCSCapabilities *capabilities, uint8_t vcp_code) {
    if (capabilities == NULL) return false;
    for (size_t index = 0; index < capabilities->feature_count; ++index) {
        if (capabilities->features[index].vcp_code == vcp_code) return true;
    }
    return false;
}

RSSDDCError rss_ddc_mccs_capabilities_enum_values(const RSSDDCMCCSCapabilities *capabilities,
                                                  uint8_t vcp_code, const uint8_t **values, size_t *count) {
    if (capabilities == NULL || values == NULL || count == NULL) return RSS_DDC_ERROR_ARGUMENT;
    for (size_t index = 0; index < capabilities->feature_count; ++index) {
        const RSSDDCMCCSVcpCapability *feature = &capabilities->features[index];
        if (feature->vcp_code == vcp_code) {
            *values = capabilities->enum_values + feature->enum_value_offset;
            *count = feature->enum_value_count;
            return RSS_DDC_OK;
        }
    }
    return RSS_DDC_ERROR_NOT_FOUND;
}
