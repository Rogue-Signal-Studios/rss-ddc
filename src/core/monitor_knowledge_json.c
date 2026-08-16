#include "rss_ddc.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { MK_JSON_NESTING = 32 };

typedef struct {
    char *p;
    size_t cap;
    size_t n;
} Writer;

typedef struct {
    const char *p;
    const char *end;
} Cursor;

typedef struct {
    char type[32];
    char source_id[RSS_DDC_PROFILE_ID_MAX];
    char reference[RSS_DDC_PROFILE_ID_MAX];
} ParsedEvidence;

typedef struct {
    char id[RSS_DDC_PROFILE_ID_MAX];
    char type[32];
    char protocol_id[RSS_DDC_PROFILE_ID_MAX];
    uint16_t vcp_code;
    bool has_vcp_code;
    bool readable;
    bool writable;
    char risk[32];
    char confidence[32];
    ParsedEvidence evidence;
    bool has_evidence;
} ParsedMethod;

typedef struct {
    char id[RSS_DDC_PROFILE_ID_MAX];
    char raw_type[16];
    uint16_t unsigned_value;
    char string_value[RSS_DDC_TEXT_MAX];
    bool has_unsigned;
    bool has_string;
} ParsedValue;

static void put(Writer *w, const char *s) {
    size_t n = strlen(s);
    if (w->p != NULL && w->n + n < w->cap) {
        memcpy(w->p + w->n, s, n);
    }
    w->n += n;
}

static void putn(Writer *w, unsigned long n) {
    char b[32];
    (void)snprintf(b, sizeof(b), "%lu", n);
    put(w, b);
}

static bool json_safe(const char *s) {
    if (s == NULL) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p) {
        if (*p < 0x20 || *p == '"' || *p == '\\') {
            return false;
        }
    }
    return true;
}

static void put_quoted(Writer *w, const char *key, const char *value, bool *comma) {
    if (value == NULL || value[0] == '\0' || !json_safe(value)) {
        return;
    }
    if (*comma) {
        put(w, ",");
    }
    put(w, "\"");
    put(w, key);
    put(w, "\":\"");
    put(w, value);
    put(w, "\"");
    *comma = true;
}

static bool quick_vcp(uint16_t address) {
    return address == 0x10 || address == 0x12 || address == 0x14 || address == 0x16 ||
           address == 0x18 || address == 0x1a;
}

static bool unknown_semantic(const char *semantic_id) {
    return semantic_id != NULL && strncmp(semantic_id, "vendor.unknown.vcp.", 19) == 0;
}

static const char *confidence_json(RSSDDCProfileConfidence confidence) {
    switch (confidence) {
        case RSS_DDC_PROFILE_CONFIDENCE_CANDIDATE:
            return "candidate";
        case RSS_DDC_PROFILE_CONFIDENCE_OBSERVED:
            return "observed";
        case RSS_DDC_PROFILE_CONFIDENCE_CORRELATED:
            return "correlated";
        case RSS_DDC_PROFILE_CONFIDENCE_SET_OBSERVED:
            return "validated";
        case RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED:
            return "hardware_validated";
        case RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN:
        default:
            return NULL;
    }
}

static RSSDDCProfileConfidence confidence_from_json(const char *name) {
    if (name == NULL) {
        return RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN;
    }
    if (strcmp(name, "candidate") == 0) {
        return RSS_DDC_PROFILE_CONFIDENCE_CANDIDATE;
    }
    if (strcmp(name, "observed") == 0) {
        return RSS_DDC_PROFILE_CONFIDENCE_OBSERVED;
    }
    if (strcmp(name, "correlated") == 0) {
        return RSS_DDC_PROFILE_CONFIDENCE_CORRELATED;
    }
    if (strcmp(name, "validated") == 0) {
        return RSS_DDC_PROFILE_CONFIDENCE_SET_OBSERVED;
    }
    if (strcmp(name, "hardware_validated") == 0) {
        return RSS_DDC_PROFILE_CONFIDENCE_HARDWARE_VALIDATED;
    }
    return RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN;
}

static const char *method_type_json(RSSDDCKnowledgeRouteKind kind) {
    if (kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) {
        return "vendor_protocol";
    }
    return "mccs_vcp";
}

static const char *risk_json(const RSSDDCKnowledgeRoute *route) {
    if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED &&
        (unknown_semantic(route->semantic_id) || !quick_vcp(route->address))) {
        return "read_extended";
    }
    return "read_standard";
}

static const char *evidence_type_json(const RSSDDCKnowledgeRoute *route) {
    if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_DECLARED) {
        return "mccs_advertised";
    }
    if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_PROFILE) {
        return "profile_known";
    }
    if (strcmp(route->provenance.evidence_id, "variable-get") == 0 ||
        (!quick_vcp(route->address) && route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED)) {
        return "extended_discovery";
    }
    return "stable_get";
}

static const char *evidence_reference_json(const RSSDDCKnowledgeRoute *route) {
    if (route->provenance.fact_kind != RSS_DDC_KNOWLEDGE_FACT_OBSERVED) {
        return NULL;
    }
    if (strcmp(route->provenance.evidence_id, "variable-get") == 0) {
        return "variable";
    }
    return "stable";
}

static bool include_route(const RSSDDCKnowledgeRoute *route, bool omit_profile) {
    return route != NULL && (!omit_profile || route->provenance.fact_kind != RSS_DDC_KNOWLEDGE_FACT_PROFILE);
}

static int compare_routes(const void *left, const void *right) {
    const RSSDDCKnowledgeRoute *a = *(const RSSDDCKnowledgeRoute *const *)left;
    const RSSDDCKnowledgeRoute *b = *(const RSSDDCKnowledgeRoute *const *)right;
    int by_semantic = strcmp(a->semantic_id, b->semantic_id);
    return by_semantic != 0 ? by_semantic : strcmp(a->route_id, b->route_id);
}

static void emit_evidence(Writer *w, const RSSDDCKnowledgeRoute *route) {
    const char *reference = evidence_reference_json(route);
    put(w, ",\"evidence\":[{\"type\":\"");
    put(w, evidence_type_json(route));
    put(w, "\"");
    if (route->provenance.source_id[0] != '\0' && json_safe(route->provenance.source_id)) {
        put(w, ",\"sourceId\":\"");
        put(w, route->provenance.source_id);
        put(w, "\"");
    }
    if (reference != NULL) {
        put(w, ",\"reference\":\"");
        put(w, reference);
        put(w, "\"");
    }
    put(w, "}]");
}

static void emit_identity(Writer *w, const RSSDDCMonitorKnowledgeIdentity *identity) {
    put(w, "\"identity\":{");
    bool comma = false;
    if (identity != NULL) {
        put_quoted(w, "manufacturer", identity->manufacturer, &comma);
        put_quoted(w, "model", identity->model, &comma);
        put_quoted(w, "edidManufacturer", identity->edid_manufacturer, &comma);
        put_quoted(w, "serial", identity->serial, &comma);
        put_quoted(w, "provider", identity->provider, &comma);
        put_quoted(w, "transport", identity->transport, &comma);
        put_quoted(w, "branch", identity->branch, &comma);
        if (identity->edid_product_code_present) {
            if (comma) {
                put(w, ",");
            }
            put(w, "\"edidProductCode\":");
            putn(w, identity->edid_product_code);
            comma = true;
        }
        if (identity->edid_manufacturer[0] != '\0' || identity->edid_product_code_present) {
            if (comma) {
                put(w, ",");
            }
            put(w, "\"evidence\":[{\"type\":\"edid_derived\"}]");
        }
    }
    put(w, "}");
}

static void emit_method(Writer *w, const RSSDDCKnowledgeRoute *route) {
    bool writable = route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_PROFILE && route->writable;
    const char *confidence = confidence_json(route->provenance.confidence);
    put(w, "{\"id\":\"");
    put(w, json_safe(route->route_id) ? route->route_id : "unknown");
    put(w, "\",\"type\":\"");
    put(w, method_type_json(route->kind));
    put(w, "\",\"readable\":");
    put(w, route->readable || route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED ? "true"
                                                                                            : "false");
    put(w, ",\"writable\":");
    put(w, writable ? "true" : "false");
    put(w, ",\"risk\":\"");
    put(w, risk_json(route));
    put(w, "\"");
    if (route->kind == RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP ||
        route->kind == RSS_DDC_KNOWLEDGE_ROUTE_PICTURE_MODE) {
        put(w, ",\"vcpCode\":");
        putn(w, route->address);
    }
    if (route->kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT) {
        put(w, ",\"protocolId\":\"lg-alt\",\"address\":\"");
        putn(w, route->address);
        put(w, "\"");
    }
    if (confidence != NULL) {
        put(w, ",\"confidence\":\"");
        put(w, confidence);
        put(w, "\"");
    }
    emit_evidence(w, route);
    put(w, "}");
}

static void emit_value(Writer *w, const RSSDDCKnowledgeRoute *route) {
    put(w, "{\"id\":\"");
    put(w, json_safe(route->route_id) ? route->route_id : "current");
    put(w, "\",\"raw\":{\"type\":");
    if (route->value.state == RSS_DDC_KNOWLEDGE_VALUE_STRING && json_safe(route->value.string_value)) {
        put(w, "\"string\",\"value\":\"");
        put(w, route->value.string_value);
        put(w, "\"}");
    } else {
        put(w, "\"unsigned\",\"value\":");
        putn(w, route->value.unsigned_value);
        put(w, "}");
    }
    put(w, ",\"readable\":true,\"writable\":false");
    if (route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED) {
        put(w, ",\"validation\":\"read_validated\"");
    }
    emit_evidence(w, route);
    put(w, "}");
}

static bool observed_value(const RSSDDCKnowledgeRoute *route) {
    return route->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED &&
           (route->value.state == RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED ||
            route->value.state == RSS_DDC_KNOWLEDGE_VALUE_STRING);
}

static void emit_capability(Writer *w, const RSSDDCKnowledgeRoute *const *routes, size_t count) {
    bool has_reported = false;
    bool reported_conflict = false;
    unsigned long reported = 0;
    const char *confidence = confidence_json(routes[0]->provenance.confidence);
    put(w, "{\"id\":\"");
    put(w, json_safe(routes[0]->semantic_id) ? routes[0]->semantic_id : "unknown");
    put(w, "\"");
    if (confidence != NULL) {
        put(w, ",\"confidence\":\"");
        put(w, confidence);
        put(w, "\"");
    }
    if (routes[0]->provenance.fact_kind == RSS_DDC_KNOWLEDGE_FACT_OBSERVED) {
        put(w, ",\"validation\":\"read_validated\"");
    }
    for (size_t index = 0; index < count; ++index) {
        const RSSDDCKnowledgeRoute *route = routes[index];
        if (!route->reported_maximum_present) {
            continue;
        }
        if (!has_reported) {
            reported = route->reported_maximum;
            has_reported = true;
        } else if (route->reported_maximum != reported) {
            reported_conflict = true;
        }
    }
    if (has_reported && !reported_conflict) {
        put(w, ",\"reportedMaximum\":{\"type\":\"unsigned\",\"value\":");
        putn(w, reported);
        put(w, "}");
    }
    put(w, ",\"methods\":[");
    for (size_t index = 0; index < count; ++index) {
        if (index > 0) {
            put(w, ",");
        }
        emit_method(w, routes[index]);
    }
    put(w, "],\"values\":[");
    bool value_comma = false;
    for (size_t index = 0; index < count; ++index) {
        if (!observed_value(routes[index])) {
            continue;
        }
        if (value_comma) {
            put(w, ",");
        }
        emit_value(w, routes[index]);
        value_comma = true;
    }
    put(w, "]}");
}

static RSSDDCError serialize(const RSSDDCMonitorKnowledge *knowledge,
                             const RSSDDCMonitorKnowledgeIdentity *identity, bool omit_profile,
                             char *buffer, size_t capacity, size_t *required) {
    if (knowledge == NULL || required == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    const RSSDDCKnowledgeRoute *ordered[RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_CAPABILITIES];
    size_t count = 0;
    size_t route_count = rss_ddc_monitor_knowledge_route_count(knowledge);
    for (size_t index = 0; index < route_count; ++index) {
        const RSSDDCKnowledgeRoute *route = rss_ddc_monitor_knowledge_route_at(knowledge, index);
        if (!include_route(route, omit_profile)) {
            continue;
        }
        if (!json_safe(route->semantic_id) || !json_safe(route->route_id)) {
            return RSS_DDC_ERROR_ARGUMENT;
        }
        if (count == RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_CAPABILITIES) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
        }
        ordered[count++] = route;
    }
    qsort(ordered, count, sizeof(*ordered), compare_routes);

    Writer w = {buffer, capacity, 0};
    put(&w, "{\"schemaVersion\":\"");
    put(&w, RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA);
    put(&w, "\",");
    emit_identity(&w, identity);
    put(&w, ",\"capabilities\":[");
    size_t emitted = 0;
    size_t index = 0;
    while (index < count) {
        size_t end = index + 1;
        while (end < count && strcmp(ordered[end]->semantic_id, ordered[index]->semantic_id) == 0) {
            ++end;
        }
        if (emitted > 0) {
            put(&w, ",");
        }
        emit_capability(&w, ordered + index, end - index);
        ++emitted;
        index = end;
    }
    put(&w, "],\"inputRoutes\":[],\"relationships\":[]}");
    *required = w.n + 1;
    if (*required > RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_BYTES) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
    }
    if (buffer != NULL && capacity < *required) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (buffer != NULL) {
        buffer[w.n] = '\0';
    }
    return RSS_DDC_OK;
}

RSSDDCError rss_ddc_monitor_knowledge_serialize_json(const RSSDDCMonitorKnowledge *knowledge,
                                                     const RSSDDCMonitorKnowledgeIdentity *identity,
                                                     char *buffer, size_t capacity, size_t *required) {
    return serialize(knowledge, identity, false, buffer, capacity, required);
}

static void ws(Cursor *c) {
    while (c->p < c->end && isspace((unsigned char)*c->p)) {
        ++c->p;
    }
}

static bool take(Cursor *c, char x) {
    ws(c);
    if (c->p == c->end || *c->p != x) {
        return false;
    }
    ++c->p;
    return true;
}

static bool parse_string(Cursor *c, char *out, size_t cap) {
    size_t n = 0;
    if (out == NULL || cap == 0 || !take(c, '"')) {
        return false;
    }
    while (c->p < c->end) {
        unsigned char x = (unsigned char)*c->p++;
        if (x == '"') {
            out[n] = '\0';
            return true;
        }
        if (x < 0x20 || x == '\\' || n + 1 >= cap) {
            return false;
        }
        out[n++] = (char)x;
    }
    return false;
}

static bool skip_string(Cursor *c) {
    if (!take(c, '"')) {
        return false;
    }
    while (c->p < c->end) {
        unsigned char x = (unsigned char)*c->p++;
        if (x == '"') {
            return true;
        }
        if (x < 0x20 || x == '\\') {
            return false;
        }
    }
    return false;
}

static bool parse_number(Cursor *c, uint32_t *out) {
    uint64_t v = 0;
    ws(c);
    if (c->p == c->end || !isdigit((unsigned char)*c->p)) {
        return false;
    }
    do {
        v = v * 10u + (uint32_t)(*c->p++ - '0');
        if (v > UINT32_MAX) {
            return false;
        }
    } while (c->p < c->end && isdigit((unsigned char)*c->p));
    if (c->p < c->end && strchr(".eE", *c->p) != NULL) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static bool parse_boolean(Cursor *c, bool *out) {
    ws(c);
    if ((size_t)(c->end - c->p) >= 4 && memcmp(c->p, "true", 4) == 0) {
        c->p += 4;
        *out = true;
        return true;
    }
    if ((size_t)(c->end - c->p) >= 5 && memcmp(c->p, "false", 5) == 0) {
        c->p += 5;
        *out = false;
        return true;
    }
    return false;
}

static bool skip_value(Cursor *c, unsigned depth) {
    ws(c);
    if (c->p == c->end || depth > MK_JSON_NESTING) {
        return false;
    }
    if (*c->p == '"') {
        return skip_string(c);
    }
    if (*c->p == '{' || *c->p == '[') {
        char open = *c->p++;
        char close = open == '{' ? '}' : ']';
        ws(c);
        if (c->p < c->end && *c->p == close) {
            ++c->p;
            return true;
        }
        for (;;) {
            if (open == '{' && (!skip_value(c, depth + 1) || !take(c, ':'))) {
                return false;
            }
            if (!skip_value(c, depth + 1)) {
                return false;
            }
            ws(c);
            if (c->p < c->end && *c->p == close) {
                ++c->p;
                return true;
            }
            if (!take(c, ',')) {
                return false;
            }
        }
    }
    if ((size_t)(c->end - c->p) >= 4 && memcmp(c->p, "true", 4) == 0) {
        c->p += 4;
        return true;
    }
    if ((size_t)(c->end - c->p) >= 5 && memcmp(c->p, "false", 5) == 0) {
        c->p += 5;
        return true;
    }
    if ((size_t)(c->end - c->p) >= 4 && memcmp(c->p, "null", 4) == 0) {
        c->p += 4;
        return true;
    }
    if (c->p < c->end && *c->p == '-') {
        ++c->p;
    }
    uint32_t ignored = 0;
    return parse_number(c, &ignored);
}

static RSSDDCError parse_evidence_item(Cursor *c, ParsedEvidence *out) {
    *out = (ParsedEvidence){0};
    if (!take(c, '{')) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    if (c->p < c->end && *c->p == '}') {
        ++c->p;
        return RSS_DDC_OK;
    }
    for (;;) {
        char key[32] = {0};
        if (!parse_string(c, key, sizeof(key)) || !take(c, ':')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        if (strcmp(key, "type") == 0) {
            if (!parse_string(c, out->type, sizeof(out->type))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "sourceId") == 0) {
            if (!parse_string(c, out->source_id, sizeof(out->source_id))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "reference") == 0) {
            if (!parse_string(c, out->reference, sizeof(out->reference))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (!skip_value(c, 1)) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        ws(c);
        if (c->p < c->end && *c->p == '}') {
            ++c->p;
            return RSS_DDC_OK;
        }
        if (!take(c, ',')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
}

static RSSDDCError parse_evidence_array(Cursor *c, ParsedEvidence *out, bool *present) {
    size_t count = 0;
    if (!take(c, '[')) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    ws(c);
    if (c->p < c->end && *c->p == ']') {
        ++c->p;
        return RSS_DDC_OK;
    }
    for (;;) {
        ParsedEvidence item = {0};
        RSSDDCError error = parse_evidence_item(c, &item);
        if (error != RSS_DDC_OK) {
            return error;
        }
        if (count == 0) {
            *out = item;
            *present = true;
        }
        ++count;
        if (count > RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_EVIDENCE) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
        }
        ws(c);
        if (c->p < c->end && *c->p == ']') {
            ++c->p;
            return RSS_DDC_OK;
        }
        if (!take(c, ',')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
}

static RSSDDCError parse_raw(Cursor *c, ParsedValue *value) {
    if (!take(c, '{')) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    for (;;) {
        char key[16] = {0};
        if (!parse_string(c, key, sizeof(key)) || !take(c, ':')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        if (strcmp(key, "type") == 0) {
            if (!parse_string(c, value->raw_type, sizeof(value->raw_type))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "value") == 0) {
            ws(c);
            if (c->p < c->end && *c->p == '"') {
                if (!parse_string(c, value->string_value, sizeof(value->string_value))) {
                    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
                }
                value->has_string = true;
            } else {
                uint32_t number = 0;
                if (!parse_number(c, &number) || number > UINT16_MAX) {
                    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
                }
                value->unsigned_value = (uint16_t)number;
                value->has_unsigned = true;
            }
        } else if (!skip_value(c, 1)) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        ws(c);
        if (c->p < c->end && *c->p == '}') {
            ++c->p;
            return RSS_DDC_OK;
        }
        if (!take(c, ',')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
}

static RSSDDCError parse_method(Cursor *c, ParsedMethod *method) {
    *method = (ParsedMethod){0};
    if (!take(c, '{')) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    for (;;) {
        char key[32] = {0};
        if (!parse_string(c, key, sizeof(key)) || !take(c, ':')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        if (strcmp(key, "id") == 0) {
            if (!parse_string(c, method->id, sizeof(method->id))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "type") == 0) {
            if (!parse_string(c, method->type, sizeof(method->type))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "readable") == 0) {
            if (!parse_boolean(c, &method->readable)) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "writable") == 0) {
            if (!parse_boolean(c, &method->writable)) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "risk") == 0) {
            if (!parse_string(c, method->risk, sizeof(method->risk))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "confidence") == 0) {
            if (!parse_string(c, method->confidence, sizeof(method->confidence))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "protocolId") == 0) {
            if (!parse_string(c, method->protocol_id, sizeof(method->protocol_id))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "vcpCode") == 0) {
            uint32_t code = 0;
            if (!parse_number(c, &code) || code > UINT16_MAX) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
            method->vcp_code = (uint16_t)code;
            method->has_vcp_code = true;
        } else if (strcmp(key, "evidence") == 0) {
            RSSDDCError error = parse_evidence_array(c, &method->evidence, &method->has_evidence);
            if (error != RSS_DDC_OK) {
                return error;
            }
        } else if (!skip_value(c, 1)) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        ws(c);
        if (c->p < c->end && *c->p == '}') {
            ++c->p;
            return method->id[0] != '\0' ? RSS_DDC_OK : RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        if (!take(c, ',')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
}

static RSSDDCError parse_value(Cursor *c, ParsedValue *value) {
    *value = (ParsedValue){0};
    if (!take(c, '{')) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    for (;;) {
        char key[32] = {0};
        if (!parse_string(c, key, sizeof(key)) || !take(c, ':')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        if (strcmp(key, "id") == 0) {
            if (!parse_string(c, value->id, sizeof(value->id))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "raw") == 0) {
            RSSDDCError error = parse_raw(c, value);
            if (error != RSS_DDC_OK) {
                return error;
            }
        } else if (!skip_value(c, 1)) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        ws(c);
        if (c->p < c->end && *c->p == '}') {
            ++c->p;
            return value->id[0] != '\0' ? RSS_DDC_OK : RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        if (!take(c, ',')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
}

static RSSDDCKnowledgeFactKind fact_from_evidence(const ParsedEvidence *evidence, bool present) {
    if (!present) {
        return RSS_DDC_KNOWLEDGE_FACT_OBSERVED;
    }
    if (strcmp(evidence->type, "mccs_advertised") == 0) {
        return RSS_DDC_KNOWLEDGE_FACT_DECLARED;
    }
    if (strcmp(evidence->type, "profile_known") == 0 ||
        strcmp(evidence->type, "rogue_validated_profile") == 0 ||
        strcmp(evidence->type, "local_validated") == 0) {
        return RSS_DDC_KNOWLEDGE_FACT_PROFILE;
    }
    return RSS_DDC_KNOWLEDGE_FACT_OBSERVED;
}

static RSSDDCError add_parsed_route(RSSDDCMonitorKnowledge *knowledge, const char *semantic_id,
                                    const ParsedMethod *method, const ParsedValue *values,
                                    size_t value_count, bool has_max, uint16_t reported_max) {
    RSSDDCKnowledgeRoute route = {0};
    RSSDDCKnowledgeFactKind fact = fact_from_evidence(&method->evidence, method->has_evidence);
    (void)method->writable;
    (void)snprintf(route.semantic_id, sizeof(route.semantic_id), "%s", semantic_id);
    (void)snprintf(route.route_id, sizeof(route.route_id), "%s", method->id);
    if (strcmp(method->type, "vendor_protocol") == 0 && strcmp(method->protocol_id, "lg-alt") == 0) {
        route.kind = RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT;
    } else {
        route.kind = RSS_DDC_KNOWLEDGE_ROUTE_STANDARD_VCP;
    }
    route.address = method->vcp_code;
    route.readable = method->readable;
    route.writable = false;
    route.write_authorized = false;
    (void)snprintf(route.transport_family, sizeof(route.transport_family), "%s",
                   route.kind == RSS_DDC_KNOWLEDGE_ROUTE_LG_ALT_INPUT ? "lg-alt" : "mccs-vcp");
    route.provenance.fact_kind = fact;
    route.provenance.confidence = confidence_from_json(method->confidence);
    if (route.provenance.confidence == RSS_DDC_PROFILE_CONFIDENCE_UNKNOWN) {
        route.provenance.confidence = RSS_DDC_PROFILE_CONFIDENCE_OBSERVED;
    }
    if (method->has_evidence && method->evidence.source_id[0] != '\0') {
        (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s",
                       method->evidence.source_id);
    } else {
        (void)snprintf(route.provenance.source_id, sizeof(route.provenance.source_id), "%s",
                       fact == RSS_DDC_KNOWLEDGE_FACT_DECLARED ? "mccs-capabilities"
                                                              : "alien-probe-live-read");
    }
    if (method->has_evidence && strcmp(method->evidence.reference, "variable") == 0) {
        (void)snprintf(route.provenance.evidence_id, sizeof(route.provenance.evidence_id), "%s",
                       "variable-get");
    } else if (fact == RSS_DDC_KNOWLEDGE_FACT_DECLARED) {
        (void)snprintf(route.provenance.evidence_id, sizeof(route.provenance.evidence_id), "%s",
                       "mccs-advertised");
    } else {
        (void)snprintf(route.provenance.evidence_id, sizeof(route.provenance.evidence_id), "%s",
                       "stable-get");
    }
    if (fact == RSS_DDC_KNOWLEDGE_FACT_PROFILE) {
        route.provenance.source = RSS_DDC_PROFILE_SOURCE_BUILTIN;
    } else {
        route.provenance.source = RSS_DDC_PROFILE_SOURCE_RESEARCH;
    }
    if (has_max) {
        route.reported_maximum_present = true;
        route.reported_maximum = reported_max;
    }
    for (size_t index = 0; index < value_count; ++index) {
        if (strcmp(values[index].id, method->id) == 0 || strcmp(values[index].id, "current") == 0) {
            if (values[index].has_string) {
                route.value.state = RSS_DDC_KNOWLEDGE_VALUE_STRING;
                (void)snprintf(route.value.string_value, sizeof(route.value.string_value), "%s",
                               values[index].string_value);
            } else if (values[index].has_unsigned) {
                route.value.state = RSS_DDC_KNOWLEDGE_VALUE_UNSIGNED;
                route.value.unsigned_value = values[index].unsigned_value;
            }
            break;
        }
    }
    return rss_ddc_monitor_knowledge_add_route(knowledge, &route);
}

static RSSDDCError parse_object_array(Cursor *c, RSSDDCError (*item)(Cursor *, void *, size_t *),
                                     void *items, size_t item_size, size_t max_items, size_t *count) {
    if (!take(c, '[')) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    ws(c);
    if (c->p < c->end && *c->p == ']') {
        ++c->p;
        return RSS_DDC_OK;
    }
    for (;;) {
        if (*count == max_items) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
        }
        RSSDDCError error = item(c, (char *)items + (*count) * item_size, count);
        if (error != RSS_DDC_OK) {
            return error;
        }
        ws(c);
        if (c->p < c->end && *c->p == ']') {
            ++c->p;
            return RSS_DDC_OK;
        }
        if (!take(c, ',')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
}

static RSSDDCError parse_method_item(Cursor *c, void *slot, size_t *count) {
    RSSDDCError error = parse_method(c, slot);
    if (error == RSS_DDC_OK) {
        ++*count;
    }
    return error;
}

static RSSDDCError parse_value_item(Cursor *c, void *slot, size_t *count) {
    RSSDDCError error = parse_value(c, slot);
    if (error == RSS_DDC_OK) {
        ++*count;
    }
    return error;
}

static RSSDDCError parse_capability(Cursor *c, RSSDDCMonitorKnowledge *knowledge) {
    char id[RSS_DDC_TEXT_MAX] = {0};
    ParsedMethod methods[RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_METHODS];
    ParsedValue values[RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_VALUES];
    size_t method_count = 0;
    size_t value_count = 0;
    bool has_max = false;
    uint16_t reported_max = 0;
    if (!take(c, '{')) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    for (;;) {
        char key[32] = {0};
        if (!parse_string(c, key, sizeof(key)) || !take(c, ':')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        if (strcmp(key, "id") == 0) {
            if (id[0] != '\0' || !parse_string(c, id, sizeof(id))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (strcmp(key, "methods") == 0) {
            RSSDDCError error = parse_object_array(c, parse_method_item, methods, sizeof(methods[0]),
                                                   RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_METHODS,
                                                   &method_count);
            if (error != RSS_DDC_OK) {
                return error;
            }
        } else if (strcmp(key, "values") == 0) {
            RSSDDCError error = parse_object_array(c, parse_value_item, values, sizeof(values[0]),
                                                   RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_VALUES,
                                                   &value_count);
            if (error != RSS_DDC_OK) {
                return error;
            }
        } else if (strcmp(key, "reportedMaximum") == 0) {
            ParsedValue raw = {0};
            RSSDDCError error = parse_raw(c, &raw);
            if (error != RSS_DDC_OK) {
                return error;
            }
            if (raw.has_unsigned) {
                has_max = true;
                reported_max = raw.unsigned_value;
            }
        } else if (strcmp(key, "observedRange") == 0) {
            if (!skip_value(c, 1)) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (!skip_value(c, 1)) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        ws(c);
        if (c->p < c->end && *c->p == '}') {
            ++c->p;
            break;
        }
        if (!take(c, ',')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
    if (id[0] == '\0') {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    for (size_t index = 0; index < method_count; ++index) {
        RSSDDCError error =
            add_parsed_route(knowledge, id, &methods[index], values, value_count, has_max, reported_max);
        if (error == RSS_DDC_ERROR_PROFILE_CONFLICT) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
        }
        if (error != RSS_DDC_OK) {
            return error;
        }
    }
    return RSS_DDC_OK;
}

static RSSDDCError parse_identity(Cursor *c, RSSDDCMonitorKnowledgeIdentity *identity) {
    if (!take(c, '{')) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    ws(c);
    if (c->p < c->end && *c->p == '}') {
        ++c->p;
        return RSS_DDC_OK;
    }
    for (;;) {
        char key[32] = {0};
        if (!parse_string(c, key, sizeof(key)) || !take(c, ':')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        if (identity != NULL && strcmp(key, "manufacturer") == 0) {
            if (!parse_string(c, identity->manufacturer, sizeof(identity->manufacturer))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (identity != NULL && strcmp(key, "model") == 0) {
            if (!parse_string(c, identity->model, sizeof(identity->model))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (identity != NULL && strcmp(key, "edidManufacturer") == 0) {
            if (!parse_string(c, identity->edid_manufacturer, sizeof(identity->edid_manufacturer))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (identity != NULL && strcmp(key, "serial") == 0) {
            if (!parse_string(c, identity->serial, sizeof(identity->serial))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (identity != NULL && strcmp(key, "provider") == 0) {
            if (!parse_string(c, identity->provider, sizeof(identity->provider))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (identity != NULL && strcmp(key, "transport") == 0) {
            if (!parse_string(c, identity->transport, sizeof(identity->transport))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (identity != NULL && strcmp(key, "branch") == 0) {
            if (!parse_string(c, identity->branch, sizeof(identity->branch))) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
        } else if (identity != NULL && strcmp(key, "edidProductCode") == 0) {
            uint32_t code = 0;
            if (!parse_number(c, &code) || code > UINT16_MAX) {
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
            identity->edid_product_code = (uint16_t)code;
            identity->edid_product_code_present = true;
        } else if (!skip_value(c, 1)) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        ws(c);
        if (c->p < c->end && *c->p == '}') {
            ++c->p;
            return RSS_DDC_OK;
        }
        if (!take(c, ',')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
}

static RSSDDCError parse_capabilities(Cursor *c, RSSDDCMonitorKnowledge *knowledge) {
    size_t count = 0;
    if (!take(c, '[')) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    ws(c);
    if (c->p < c->end && *c->p == ']') {
        ++c->p;
        return RSS_DDC_OK;
    }
    for (;;) {
        if (count == RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_CAPABILITIES) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
        }
        RSSDDCError error = parse_capability(c, knowledge);
        if (error != RSS_DDC_OK) {
            return error;
        }
        ++count;
        ws(c);
        if (c->p < c->end && *c->p == ']') {
            ++c->p;
            return RSS_DDC_OK;
        }
        if (!take(c, ',')) {
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
}

RSSDDCError rss_ddc_monitor_knowledge_parse_json(const char *data, size_t length,
                                                 RSSDDCMonitorKnowledge **knowledge,
                                                 RSSDDCMonitorKnowledgeIdentity *identity) {
    if (knowledge == NULL) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    *knowledge = NULL;
    if (data == NULL || length == 0) {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    if (length > RSS_DDC_MONITOR_KNOWLEDGE_JSON_MAX_BYTES) {
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
    }
    RSSDDCMonitorKnowledge *parsed = rss_ddc_monitor_knowledge_create();
    RSSDDCMonitorKnowledgeIdentity parsed_identity = {0};
    if (parsed == NULL) {
        return RSS_DDC_ERROR_SYSTEM;
    }
    Cursor c = {data, data + length};
    bool saw_schema = false;
    bool saw_identity = false;
    bool saw_capabilities = false;
    char schema[64] = {0};
    if (!take(&c, '{')) {
        rss_ddc_monitor_knowledge_destroy(parsed);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    for (;;) {
        char key[32] = {0};
        if (!parse_string(&c, key, sizeof(key)) || !take(&c, ':')) {
            rss_ddc_monitor_knowledge_destroy(parsed);
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        if (strcmp(key, "schemaVersion") == 0) {
            if (saw_schema || !parse_string(&c, schema, sizeof(schema))) {
                rss_ddc_monitor_knowledge_destroy(parsed);
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
            saw_schema = true;
            if (strcmp(schema, RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA) != 0) {
                rss_ddc_monitor_knowledge_destroy(parsed);
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_SCHEMA;
            }
        } else if (strcmp(key, "identity") == 0) {
            if (saw_identity) {
                rss_ddc_monitor_knowledge_destroy(parsed);
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
            RSSDDCError error = parse_identity(&c, &parsed_identity);
            if (error != RSS_DDC_OK) {
                rss_ddc_monitor_knowledge_destroy(parsed);
                return error;
            }
            saw_identity = true;
        } else if (strcmp(key, "capabilities") == 0) {
            if (saw_capabilities) {
                rss_ddc_monitor_knowledge_destroy(parsed);
                return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
            }
            RSSDDCError error = parse_capabilities(&c, parsed);
            if (error != RSS_DDC_OK) {
                rss_ddc_monitor_knowledge_destroy(parsed);
                return error;
            }
            saw_capabilities = true;
        } else if (!skip_value(&c, 1)) {
            rss_ddc_monitor_knowledge_destroy(parsed);
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
        ws(&c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            break;
        }
        if (!take(&c, ',')) {
            rss_ddc_monitor_knowledge_destroy(parsed);
            return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
        }
    }
    ws(&c);
    if (c.p != c.end || !saw_schema || !saw_identity || !saw_capabilities) {
        rss_ddc_monitor_knowledge_destroy(parsed);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    if (identity != NULL) {
        *identity = parsed_identity;
    }
    *knowledge = parsed;
    return RSS_DDC_OK;
}

static RSSDDCError write_json_file(const RSSDDCMonitorKnowledge *knowledge,
                                   const RSSDDCMonitorKnowledgeIdentity *identity, bool omit_profile,
                                   const char *path) {
    if (knowledge == NULL || path == NULL || path[0] == '\0') {
        return RSS_DDC_ERROR_ARGUMENT;
    }
    size_t required = 0;
    RSSDDCError error = serialize(knowledge, identity, omit_profile, NULL, 0, &required);
    if (error != RSS_DDC_OK) {
        return error;
    }
    char *data = calloc(1, required);
    char *tmp = calloc(1, strlen(path) + 12);
    if (data == NULL || tmp == NULL) {
        free(data);
        free(tmp);
        return RSS_DDC_ERROR_SYSTEM;
    }
    error = serialize(knowledge, identity, omit_profile, data, required, &required);
    if (error != RSS_DDC_OK) {
        free(data);
        free(tmp);
        return error;
    }
    (void)snprintf(tmp, strlen(path) + 12, "%s.tmp.XXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) {
        free(data);
        free(tmp);
        return RSS_DDC_ERROR_SYSTEM;
    }
    size_t off = 0;
    while (off < required - 1) {
        ssize_t wrote = write(fd, data + off, required - 1 - off);
        if (wrote <= 0) {
            error = RSS_DDC_ERROR_SYSTEM;
            break;
        }
        off += (size_t)wrote;
    }
    if (error == RSS_DDC_OK && fsync(fd) != 0) {
        error = RSS_DDC_ERROR_SYSTEM;
    }
    if (close(fd) != 0 && error == RSS_DDC_OK) {
        error = RSS_DDC_ERROR_SYSTEM;
    }
    if (error == RSS_DDC_OK && rename(tmp, path) != 0) {
        error = RSS_DDC_ERROR_SYSTEM;
    }
    if (error != RSS_DDC_OK) {
        (void)unlink(tmp);
    }
    free(data);
    free(tmp);
    return error;
}

RSSDDCError rss_ddc_monitor_knowledge_write_json_file(const RSSDDCMonitorKnowledge *knowledge,
                                                      const RSSDDCMonitorKnowledgeIdentity *identity,
                                                      const char *path) {
    return write_json_file(knowledge, identity, false, path);
}

